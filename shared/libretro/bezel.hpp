#pragma once

#include <string>

#include <nanovg.h>

namespace foyer::libretro {

// Draws a per-rom or per-system bezel PNG over the framebuffer.
//
// Lookup order (first match wins):
//   1. /foyer/bezels/<system_folder>/<rom_stem>.png   (per-rom)
//   2. /foyer/bezels/<system_folder>.png              (per-system)
//   3. /foyer/bezels/default.png                      (catch-all CRT TV)
//
// The PNG is expected to be the full screen size with a transparent
// hole where the emulator output should show through. We composite it
// AFTER the video sink and BEFORE the pause overlay so the overlay
// stays on top of bezel art.
//
// (3) is seeded by the browser on first boot from its bundled romfs,
// so every game gets at least the generic CRT TV frame out of the box.
// No-op only if even the default has been deleted from SD.
void set_bezel_rom_id(const std::string& system_folder,
                      const std::string& rom_stem);

void draw_bezel(NVGcontext* vg, float w, float h);

// Drop the cached image — call when the rom is unloaded so the
// nanovg context doesn't hold onto a stale handle.
void invalidate_bezel(NVGcontext* vg);

} // namespace foyer::libretro
