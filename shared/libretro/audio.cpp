#include "audio.hpp"
#include "frontend.hpp"
#include "platform/log.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

#include <switch.h>

namespace foyer::libretro {
namespace {

constexpr AudioRendererConfig kAudrenConfig = {
    .output_rate     = AudioRendererOutputRate_48kHz,
    .num_voices      = 4,
    .num_effects     = 0,
    .num_sinks       = 1,
    .num_mix_objs    = 1,
    .num_mix_buffers = 2,
};

inline AudioDriver& drv(void* p) {
    return *static_cast<AudioDriver*>(p);
}

inline AudioDriverWaveBuf* wavebufs(void* p) {
    return static_cast<AudioDriverWaveBuf*>(p);
}

} // namespace

AudioSink& AudioSink::instance() {
    static AudioSink g;
    return g;
}

bool AudioSink::init(unsigned sample_rate) {
    if (m_initialised) return true;
    if (sample_rate == 0) sample_rate = 48000;
    m_rate = sample_rate;

    // brls's libpulsar may have called audrenInitialize first in
    // SwitchAudioPlayer's constructor (PLAYER_BRLS=ON path). If so,
    // libnx returns "already initialized" — accept that and share
    // the existing renderer instance. We track whether we own the
    // init so we don't yank it out from under pulsar on shutdown.
    Result rc = audrenInitialize(&kAudrenConfig);
    m_owns_audren = R_SUCCEEDED(rc);
    if (R_FAILED(rc)) {
        // 0x4D215 / 0xF601 = "service already initialized" in libnx —
        // any failure here we treat as "someone else got there first";
        // try audrvCreate against the existing context.
        foyer::log::write(
            "[audio] audrenInitialize rc=0x%X — assuming someone "
            "else (likely brls pulsar) already initialized; sharing\n",
            (unsigned)rc);
    } else {
        foyer::log::write("[audio] audrenInitialize ok (we own it)\n");
    }

    m_driver = std::malloc(sizeof(AudioDriver));
    if (!m_driver) {
        if (m_owns_audren) audrenExit();
        return false;
    }
    new (m_driver) AudioDriver{};

    rc = audrvCreate(&drv(m_driver), &kAudrenConfig, /*final_mix_channels=*/2);
    if (R_FAILED(rc)) {
        foyer::log::write("[audio] audrvCreate failed: rc=0x%X\n", rc);
        std::free(m_driver); m_driver = nullptr;
        if (m_owns_audren) audrenExit();
        return false;
    }
    foyer::log::write("[audio] audrvCreate ok\n");

    // Final mix output device (speakers).
    static const u8 sink_channels[] = { 0, 1 };
    audrvDeviceSinkAdd(&drv(m_driver), "MainAudioOut",
        sizeof(sink_channels), sink_channels);

    // Aligned memory pool sized for kBufCount × stereo S16 × kBufFrames.
    m_pool_size = (kBytesPerBuf * kBufCount + AUDREN_MEMPOOL_ALIGNMENT - 1)
                & ~(AUDREN_MEMPOOL_ALIGNMENT - 1);
    m_pool = aligned_alloc(AUDREN_MEMPOOL_ALIGNMENT, m_pool_size);
    if (!m_pool) {
        audrvClose(&drv(m_driver));
        std::free(m_driver); m_driver = nullptr;
        if (m_owns_audren) audrenExit();
        return false;
    }
    std::memset(m_pool, 0, m_pool_size);

    m_mempool_id = audrvMemPoolAdd(&drv(m_driver), m_pool, m_pool_size);
    audrvMemPoolAttach(&drv(m_driver), m_mempool_id);

    // Voice 0: stereo S16 @ core's native rate.
    m_voice_id = 0;
    if (!audrvVoiceInit(&drv(m_driver), m_voice_id, 2, PcmFormat_Int16, m_rate)) {
        foyer::log::write("[audio] audrvVoiceInit failed\n");
        std::free(m_pool);   m_pool = nullptr;
        audrvClose(&drv(m_driver));
        std::free(m_driver); m_driver = nullptr;
        if (m_owns_audren) audrenExit();
        return false;
    }
    audrvVoiceSetDestinationMix(&drv(m_driver), m_voice_id, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&drv(m_driver), m_voice_id, 1.0f, 0, 0); // L → L
    audrvVoiceSetMixFactor(&drv(m_driver), m_voice_id, 1.0f, 1, 1); // R → R
    audrvVoiceStart(&drv(m_driver), m_voice_id);

    // Wave buffers — one per slice of the pool.
    m_wavebufs = std::malloc(sizeof(AudioDriverWaveBuf) * kBufCount);
    auto* wbs  = wavebufs(m_wavebufs);
    for (int i = 0; i < kBufCount; i++) {
        wbs[i] = {};
        wbs[i].data_raw            = (u8*)m_pool + (std::size_t)i * kBytesPerBuf;
        wbs[i].size                = kBytesPerBuf;
        wbs[i].start_sample_offset = 0;
        wbs[i].end_sample_offset   = kBufFrames;
        wbs[i].state               = AudioDriverWaveBufState_Done;
    }
    m_next_buf      = 0;
    m_cursor_frames = 0;

    // Kick the renderer. Without this audrv frames never advance.
    rc = audrenStartAudioRenderer();
    if (R_FAILED(rc)) {
        foyer::log::write("[audio] audrenStartAudioRenderer failed: rc=0x%X\n", rc);
    } else {
        foyer::log::write("[audio] audrenStartAudioRenderer ok\n");
    }

    // Audio thread — pumps audrvUpdate every audren frame (~5ms) so the
    // GPU/DSP keeps consuming our wave buffers regardless of how slow the
    // game-loop happens to tick.
    m_thread_run = true;
    threadCreate(&m_thread, &AudioSink::thread_trampoline,
                 this, nullptr, 0x4000, 0x20, -2);
    threadStart(&m_thread);

    Frontend::instance().set_audio_sink(&AudioSink::on_frame);

    m_initialised = true;
    foyer::log::write("[audio] init ok @ %u Hz\n", m_rate);
    return true;
}

void AudioSink::shutdown() {
    if (!m_initialised) return;

    Frontend::instance().set_audio_sink(nullptr);

    m_thread_run = false;
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);

    audrenStopAudioRenderer();

    if (m_voice_id >= 0) {
        audrvVoiceStop(&drv(m_driver), m_voice_id);
        audrvVoiceDrop(&drv(m_driver), m_voice_id);
    }
    if (m_mempool_id >= 0) {
        audrvMemPoolDetach(&drv(m_driver), m_mempool_id);
        audrvMemPoolRemove(&drv(m_driver), m_mempool_id);
    }
    if (m_driver) {
        audrvClose(&drv(m_driver));
        std::free(m_driver);
        m_driver = nullptr;
    }
    if (m_owns_audren) {
        audrenExit();
        m_owns_audren = false;
    }

    std::free(m_pool);     m_pool     = nullptr;
    std::free(m_wavebufs); m_wavebufs = nullptr;
    m_initialised = false;
}

void AudioSink::thread_trampoline(void* arg) {
    static_cast<AudioSink*>(arg)->thread_loop();
}

void AudioSink::thread_loop() {
    while (m_thread_run) {
        audrenWaitFrame();
        std::scoped_lock lk{m_audrv_mutex};
        audrvUpdate(&drv(m_driver));
    }
}

void AudioSink::on_frame(const Frontend::AudioFrame& f) {
    instance().enqueue(f.samples, f.frames);
}

void AudioSink::enqueue(const std::int16_t* in, std::size_t frames) {
    if (!m_initialised || !in || frames == 0) return;

    std::scoped_lock lk{m_audrv_mutex};
    auto* wbs = wavebufs(m_wavebufs);

    while (frames > 0) {
        auto& wb = wbs[m_next_buf];

        // Wait for the slot to be done (either completed playing or never used).
        if (wb.state != AudioDriverWaveBufState_Done) {
            // Slot still in flight; drop incoming frames rather than block.
            // (Cores produce audio faster than realtime sometimes; tolerable.)
            return;
        }

        const std::size_t remaining_in_buf = kBufFrames - m_cursor_frames;
        const std::size_t take = (frames < remaining_in_buf) ? frames : remaining_in_buf;

        auto* dst = reinterpret_cast<std::int16_t*>(
                        const_cast<void*>(wb.data_raw))
                  + m_cursor_frames * 2;
        std::memcpy(dst, in, take * 2 * sizeof(std::int16_t));

        in              += take * 2;
        frames          -= take;
        m_cursor_frames += take;

        if (m_cursor_frames >= kBufFrames) {
            // Slot is full; submit it and move to the next one.
            armDCacheFlush(const_cast<void*>(wb.data_raw), kBytesPerBuf);
            audrvVoiceAddWaveBuf(&drv(m_driver), m_voice_id, &wb);
            m_cursor_frames = 0;
            m_next_buf = (m_next_buf + 1) % kBufCount;
        }
    }
}

void AudioSink::pump() {
    // audrvUpdate is owned by the dedicated audio thread; no-op here.
}

} // namespace foyer::libretro
