#pragma once
//
// player/imgui/imgui_theme — dark/light style follows foyer config's
// theme_override (same field the browser brls theme_watcher reads).
// "light" → ImGui::StyleColorsLight, "dark" → ImGui::StyleColorsDark,
// empty → setsysGetColorSetId (HOS system theme).

namespace foyer::player::imgui_shell {

// Read config + system, return true if dark theme should be active.
bool theme_want_dark();

// Apply Dark/Light ImGui style based on theme_want_dark(). Cheap to
// call every frame, but typically polled at 1 Hz from the main loop.
void theme_apply();

// Returns true and applies the style if theme_want_dark() flipped
// since the last call. Use as the 1 Hz live-refresh hook.
bool theme_apply_if_changed();

}  // namespace foyer::player::imgui_shell
