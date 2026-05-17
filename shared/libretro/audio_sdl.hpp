#pragma once
//
// shared/libretro/audio_sdl — SDL2 audio sink. Uses SDL_OpenAudioDevice
// + SDL_QueueAudio (simpler than SDL_mixer for raw S16-interleaved
// PCM out of libretro cores). Replaces libretro/audio.cpp's AudioSink
// (libnx audrv) when PLAYER_PLUTONIUM is on.

#include "frontend.hpp"

#include <SDL2/SDL.h>

namespace foyer::libretro {

struct AudioSinkSdl {
    static AudioSinkSdl& instance();

    bool init(unsigned sample_rate);
    void shutdown();

    static void on_frame(const Frontend::AudioFrame& f);

private:
    void queue(const Frontend::AudioFrame& f);

    SDL_AudioDeviceID m_dev   = 0;
    unsigned          m_rate  = 0;
};

}  // namespace foyer::libretro
