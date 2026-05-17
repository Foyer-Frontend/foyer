#pragma once
//
// shared/libretro/input_sdl — SDL_GameController -> libretro
// InputState. Per-frame poll. Replaces libretro/input.cpp's libnx
// pad path when PLAYER_PLUTONIUM is on.

namespace foyer::libretro {

void input_sdl_init();
void input_sdl_shutdown();

// Per-frame: read SDL_GameController state, populate Frontend
// InputState.
void input_sdl_poll();

}  // namespace foyer::libretro
