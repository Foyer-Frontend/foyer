#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <mutex>

#include <switch.h>

#include "frontend.hpp"

namespace foyer::libretro {

// Audio sink for libretro cores. Drives a single audrv voice at the core's
// requested sample rate (no resampling — audrv handles it on the GPU/DSP).
//
// Usage:
//   audio.init(sample_rate);
//   ... per frame:
//       audio.pump();          // drives audrvUpdate
//       Frontend's audio_sample_batch_cb stuffs samples into the ring
//   audio.shutdown();
struct AudioSink {
    static AudioSink& instance();

    bool init(unsigned sample_rate);
    void shutdown();

    // Callback installed on Frontend::set_audio_sink().
    static void on_frame(const Frontend::AudioFrame& f);

    // Called once per app tick to advance the audrv timeline.
    void pump();

private:
    void enqueue(const std::int16_t* interleaved_stereo, std::size_t frames);
    static void thread_trampoline(void* arg);
    void thread_loop();

    bool          m_initialised   = false;
    // True when this AudioSink called audrenInitialize itself; false
    // when an earlier brls SwitchAudioPlayer / libpulsar got there
    // first. Drives whether shutdown() calls audrenExit (only the
    // owner is allowed to tear it down).
    bool          m_owns_audren   = false;
    unsigned      m_rate          = 0;

    Thread             m_thread{};
    std::atomic<bool>  m_thread_run{false};
    // audrv is not thread-safe across voice mutations + audrvUpdate. The
    // libretro core stuffs samples on the game thread while the audio
    // thread drives audrvUpdate; serialise both with a mutex.
    std::mutex         m_audrv_mutex{};

    // audrv handles, kept opaque so the header doesn't drag in <switch.h>.
    void*         m_driver        = nullptr;
    int           m_voice_id      = -1;
    int           m_mempool_id    = -1;
    void*         m_pool          = nullptr;
    std::size_t   m_pool_size     = 0;

    // 4 wave buffers cycled in order. Each holds ~21 ms of stereo @ 48k.
    static constexpr int          kBufCount    = 4;
    static constexpr std::size_t  kBufFrames   = 1024;        // sample-frames
    static constexpr std::size_t  kBytesPerBuf = kBufFrames * 2 * sizeof(std::int16_t);

    void*         m_wavebufs      = nullptr; // AudioDriverWaveBuf[kBufCount]
    int           m_next_buf      = 0;

    // Free-running write cursor into the active buffer's bytes.
    std::size_t   m_cursor_frames = 0;
};

} // namespace foyer::libretro
