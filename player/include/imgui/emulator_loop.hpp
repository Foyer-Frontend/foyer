#pragma once
//
// player/imgui/emulator_loop — drives the libretro core through the
// ImGui shell. Counterpart to player/src/emulator_activity.cpp under
// brls.
//
// Lifetime: one instance per process. Owns:
//   * Frontend (init / load_game / shutdown / unload)
//   * VideoSinkGl    — receives RGBA frames, draws aspect-fit quad
//   * AudioSink       — libnx audren via shared/libretro/audio
//   * bezel_gl       — overlay PNG
// Does NOT own EGL/GLES context (that's the player shell's job — we
// just borrow whatever is current).
//
// Phase 4 will plumb a pause-modal callback so the loop can keep
// ticking under the modal.

#include <string>

namespace foyer::player::emulator {

bool start(const std::string& rom_path,
           const std::string& back_nro,
           const std::string& system_folder);

// Per-frame. Call between input_new_frame() and ImGui::NewFrame().
// Polls libnx pad into the libretro input layer + advances retro_run.
// Returns true when a "request exit" gesture (L3+R3 or Plus, TBD)
// has been seen so the shell can break out of the main loop.
bool tick();

// Draws the most recent frame + bezel into the current EGL surface.
// Caller is expected to glClear before this if a black background is
// wanted; we do it inside to keep the call site small.
void draw(float screen_w, float screen_h);

void shutdown();

}  // namespace foyer::player::emulator
