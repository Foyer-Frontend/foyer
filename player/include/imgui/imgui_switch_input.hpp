#pragma once
//
// player/imgui/imgui_switch_input — libnx pad + touch -> ImGuiIO.
// Phase ImGui-1 wires only enough to navigate ImGui's stock widgets
// (gamepad face buttons + dpad + L/R + Start/Select + touchscreen).
// Phase ImGui-2 reuses the same pad poll for libretro input by
// reading the *raw* libnx state separately — see EmulatorLoop.

#include <switch.h>

namespace foyer::player::imgui_shell {

// Initialise once after ImGui::CreateContext. Wires ConfigFlag_NavEnable
// + IsGamepadNavEnabled.
void input_init();

// Per-frame: poll libnx HID, translate to ImGuiIO events. Call
// between Application::frame() / equivalent and ImGui::NewFrame.
void input_new_frame();

// True if the user pressed Plus (KEY_PLUS) this frame. Phase 1
// uses it as the exit gesture before libretro is wired.
bool input_pressed_plus();

// Hand the underlying PadState to libretro's poll_input so the
// game and the ImGui nav share one HID poll. Returns nullptr until
// input_init() has run.
::PadState* input_pad();

}  // namespace foyer::player::imgui_shell
