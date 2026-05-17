#pragma once
//
// player/imgui/modals — ImGui modal state machine for the in-game
// pause overlay. Phase 4 ports the 6 brls picker activities (Pause,
// SlotPicker, ShadersPicker, DisplayPicker, CoreOptionsPicker,
// CheatsPicker) to ImGui popups.
//
// Functional parity, NOT visual parity: stock ImGui dark/light
// styling (theme follows config().theme_override, see imgui_theme).
// Gamepad nav owns input while a modal is open; the emulator loop
// gates its poll_input on modals_input_blocked() so the core
// doesn't see B/A while the user is navigating menus.

#include <string>

namespace foyer::player::imgui_shell {

enum class Modal {
    None,
    Pause,
    SaveSlot,
    LoadSlot,
    Shaders,
    Display,
    CoreOptions,
    Cheats,
};

// L3+R3 edge handler — opens Pause from any non-modal state.
void modals_open_pause();

// Programmatic modal change (sub-modals from inside pause).
void modals_open(Modal m);

// Close every open modal (Quit cell, B-back beyond root, app exit).
void modals_close_all();

// Returns the currently active modal (None when no modal is up).
Modal modals_active();

// True while any modal is up: emulator_loop gates poll_input on
// this so the game doesn't eat the modal's A/B presses.
bool modals_input_blocked();

// Per-frame: render whichever modal is active. Must be called from
// inside an ImGui::NewFrame / ImGui::Render bracket on the main
// thread.
void modals_draw();

// Set / read the "Quit cell was activated" flag. main_imgui polls
// this each frame and breaks the loop on true (after the cell has
// already flushed SRAM + envSetNextLoad'd inside its click).
bool modals_quit_requested();

// Hook the emulator_loop installs so the Quit cell can flush SRAM
// + envSetNextLoad before brls::Application::quit equivalent.
// Mirrors PauseActivity's Quit cell exactly.
void modals_install_quit_handler(void (*on_quit)(),
                                 const std::string& back_nro);

// Hook used by the SaveSlot/LoadSlot modals to identify the slot
// directory. rom_path is the canonical path the launcher gave us
// (NOT the extract-cache copy); system_folder is the parent dir.
void modals_set_rom_id(const std::string& rom_path,
                       const std::string& system_folder);

}  // namespace foyer::player::imgui_shell
