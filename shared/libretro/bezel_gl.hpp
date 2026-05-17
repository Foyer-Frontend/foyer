#pragma once
//
// shared/libretro/bezel_gl — GLES3 sibling of bezel.hpp. Same lookup
// rules (per-rom -> bundle -> per-system); the difference is the
// upload path uses stb_image + glTexImage2D instead of
// nvgCreateImage, and the blit is a textured fullscreen quad on the
// player's GL context. Used by the ImGui player shell.
//
// set_bezel_rom_id() is shared with bezel.cpp via foyer::libretro::
// scope — both files use the same g_folder/g_stem globals via the
// public setter so the brls and ImGui players agree on what's
// loaded.

#include <string>

namespace foyer::libretro {

// Tells the bezel system which rom is loading; same prototype as
// bezel.cpp. ImGui player calls this; the setter clears any cached
// GL handle.
void bezel_gl_set_rom_id(const std::string& system_folder,
                         const std::string& rom_stem);

// Bring up GL resources (shader + VAO). Idempotent. Returns false
// only if shader compile/link failed.
bool bezel_gl_init();

// Draw the current bezel (if any) at full screen size. No-op when
// no bezel resolves. Caller must have a valid EGL context current.
void bezel_gl_draw(float screen_w, float screen_h);

// Drop the cached texture; call when the rom is unloaded so the
// next set_rom_id picks up a fresh png decode.
void bezel_gl_invalidate();

// Release GL resources (shader + VAO + texture). Pairs with
// bezel_gl_init() at player shutdown.
void bezel_gl_shutdown();

}  // namespace foyer::libretro
