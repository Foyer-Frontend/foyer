#include "audio_sdl.hpp"
#include "platform/log.hpp"

namespace foyer::libretro {

AudioSinkSdl& AudioSinkSdl::instance() {
    static AudioSinkSdl s;
    return s;
}

bool AudioSinkSdl::init(unsigned sample_rate) {
    if (m_dev) return true;

    SDL_AudioSpec want{}, have{};
    want.freq     = (int)sample_rate;
    want.format   = AUDIO_S16LSB;
    want.channels = 2;
    want.samples  = 1024;            // ~21 ms at 48 kHz
    want.callback = nullptr;         // we use SDL_QueueAudio

    m_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!m_dev) {
        foyer::log::write("[audio_sdl] SDL_OpenAudioDevice failed: %s\n",
            SDL_GetError());
        return false;
    }
    m_rate = (unsigned)have.freq;
    SDL_PauseAudioDevice(m_dev, 0);          // start playback
    Frontend::instance().set_audio_sink(&AudioSinkSdl::on_frame);
    foyer::log::write("[audio_sdl] init ok @ %u Hz\n", m_rate);
    return true;
}

void AudioSinkSdl::shutdown() {
    if (m_dev) {
        SDL_CloseAudioDevice(m_dev);
        m_dev = 0;
    }
    m_rate = 0;
}

void AudioSinkSdl::on_frame(const Frontend::AudioFrame& f) {
    instance().queue(f);
}

void AudioSinkSdl::queue(const Frontend::AudioFrame& f) {
    if (!m_dev || !f.samples || f.frames == 0) return;
    // Skip when the queue is more than ~100 ms behind — avoids
    // unbounded growth if the renderer falls behind the core's
    // wall-clock cadence.
    const std::size_t queued = SDL_GetQueuedAudioSize(m_dev);
    if (queued > (m_rate * 4 / 10)) {
        return;
    }
    SDL_QueueAudio(m_dev, f.samples,
        (std::uint32_t)(f.frames * 2 * sizeof(std::int16_t)));
}

}  // namespace foyer::libretro
