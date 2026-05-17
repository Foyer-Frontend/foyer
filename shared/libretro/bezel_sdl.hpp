#pragma once
//
// shared/libretro/bezel_sdl — SDL2 sibling of bezel.hpp.
// Resolves the same per-rom -> per-bundle -> per-system PNG paths,
// loads via SDL2_image (IMG_Load), draws a fullscreen quad on top
// of the game frame.

#include <SDL2/SDL.h>
#include <string>

namespace foyer::libretro {

void bezel_sdl_set_rom_id(const std::string& system_folder,
                          const std::string& rom_stem);

bool bezel_sdl_init(SDL_Renderer* renderer);

void bezel_sdl_draw(int screen_w, int screen_h);

void bezel_sdl_invalidate();

void bezel_sdl_shutdown();

}  // namespace foyer::libretro
