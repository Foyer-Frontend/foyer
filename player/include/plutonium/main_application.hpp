#pragma once
//
// Plutonium MainApplication for the player. Phase P4: hosts the
// EmulatorElement plus a togglable pause overlay (dim Rectangle +
// PauseMenu's dynamic Menu). L3+R3 toggles the overlay.

#include "plutonium/emulator_element.hpp"
#include "plutonium/pause_menu.hpp"

#include <pu/Plutonium>
#include <memory>
#include <string>

namespace foyer::player::plut {

class EmulatorLayout : public pu::ui::Layout {
public:
    EmulatorLayout();
    PU_SMART_CTOR(EmulatorLayout)

    void Rebuild(EmulatorElement::Ref emu,
                 pu::ui::elm::Rectangle::Ref dim,
                 pu::ui::elm::Menu::Ref menu,
                 bool paused);
};

class MainApplication : public pu::ui::Application {
public:
    using pu::ui::Application::Application;
    PU_SMART_CTOR(MainApplication)

    void SetBootArgs(std::string rom_path,
                     std::string back_nro,
                     std::string system_folder);

    void OnLoad() override;

private:
    void TogglePause();
    void RefreshLayout();           // rebuild emu_layout for current state

    EmulatorElement::Ref         emu_element;
    EmulatorLayout::Ref          emu_layout;
    pu::ui::elm::Rectangle::Ref  dim_rect;
    std::unique_ptr<PauseMenu>   pause;
    bool                         paused = false;

    std::string rom_path;
    std::string back_nro;
    std::string system_folder;

    bool last_l3 = false;
    bool last_r3 = false;

    // Layout-mutating callbacks (Menu item A-press, Resume / B-back,
    // mode-changed) run from inside Plutonium's frame iteration over
    // emu_layout's elements. Calling Clear() + Add() on that same
    // container mid-iteration invalidates the iterator and freezes
    // rendering. Defer the rebuild by 1 frame via this flag.
    bool layout_dirty = false;
};

}  // namespace foyer::player::plut
