#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <nanovg.h>
#include <switch.h>

#include "aspect.hpp"
#include "savestate.hpp"

namespace foyer::libretro {

// In-game pause overlay. Toggles on Minus + Plus held together. While the
// overlay is open the player should NOT call retro_run() and should NOT
// forward Switch input to the libretro core.
struct Overlay {
    enum class Action {
        None,
        SaveStateSlot,    // result_slot holds the chosen slot (0..9)
        LoadStateSlot,    // result_slot holds the chosen slot (0..9)
        Reset,
        Quit,
    };

    struct Hooks {
        // Aspect ratio getter/setter.
        std::function<AspectMode()>            get_aspect;
        std::function<void(AspectMode)>        set_aspect;
        // Returns a fresh snapshot of all 10 slots' on-disk status. Called
        // on each save/load menu open so the times stay current.
        std::function<void(StateSlot[kStateSlotCount])> probe_slots;
    };

    void set_hooks(Hooks h) { m_hooks = std::move(h); }

    bool is_open() const { return m_state != State::Hidden; }

    // Per-frame update. `held`/`down` are libnx pad bitmasks for this frame.
    Action update(std::uint64_t held, std::uint64_t down);

    // After update() returns SaveStateSlot/LoadStateSlot, this holds the
    // chosen slot index 0..9.
    int last_slot() const { return m_result_slot; }

    void draw(NVGcontext* vg, float screen_w, float screen_h);

    // Bottom-of-screen toast for ~1.5 s.
    void toast(std::string msg);

private:
    enum class State {
        Hidden,
        Main,
        SaveSlots,
        LoadSlots,
        Settings,
        CoreOptions,
    };

    void refresh_slots();
    void draw_panel(NVGcontext* vg, float w, float h, const char* title);

    State        m_state = State::Hidden;
    int          m_main_index     = 0;
    int          m_settings_index = 0;
    int          m_slot_index     = 0;
    int          m_core_opt_index = 0;
    int          m_core_opt_scroll = 0;

    int          m_result_slot    = 0;

    StateSlot    m_slots[kStateSlotCount]{};
    bool         m_slots_dirty    = true;

    Hooks        m_hooks{};

    std::string  m_toast{};
    int          m_toast_ttl = 0;

    bool         m_combo_was_held = false;
};

} // namespace foyer::libretro
