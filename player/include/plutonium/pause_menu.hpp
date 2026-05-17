#pragma once
//
// Plutonium-native pause overlay + 6 picker modes. Drawn ON TOP of
// the EmulatorElement (which keeps rendering the last frozen frame +
// bezel underneath) so the user sees the game behind the menu.
//
// Mode machine:
//   Pause -> root menu (9 items)
//   SaveSlot / LoadSlot -> 10 slot picker
//   Shaders / Display / CoreOptions / Cheats -> sub-pickers
// B returns to Pause root from sub; B on Pause root closes the
// overlay (unpause). Quit cell sets quit_requested so main_imgui's
// equivalent break-out fires from the SetOnInput hook.

#include <pu/Plutonium>
#include <switch.h>
#include <functional>
#include <string>
#include <vector>

namespace foyer::player::plut {

enum class PauseMode {
    Pause,
    SaveSlot,
    LoadSlot,
    Shaders,
    Display,           // Display root — Aspect / Bezel toggle / Bezel pick
    DisplayAspect,     // Aspect mode picker (7 rows)
    DisplayBezel,      // Bezel picker (list of available bezel PNGs)
    CoreOptions,
    Cheats,
    Achievements,
};

class PauseMenu {
public:
    using QuitFn = std::function<void()>;

    explicit PauseMenu(const std::string& rom_path,
                       const std::string& system_folder,
                       const std::string& back_nro);

    // Build the Menu element for the current mode.
    pu::ui::elm::Menu::Ref BuildMenu();

    // Refill the items on an existing Menu instance for the current
    // mode. Used by MainApplication so the same Menu element stays
    // in the Layout's elems vector across mode swaps — no Clear /
    // Add on the Layout, no chance of mid-iteration corruption.
    void Populate(pu::ui::elm::Menu::Ref& menu);

    // Default geometry used by both the standalone BuildMenu (legacy)
    // and the externally-allocated Menu instance Populate fills.
    static constexpr pu::i32 MenuX     = 360;
    static constexpr pu::i32 MenuY     = 140;
    static constexpr pu::i32 MenuW     = 1200;
    static constexpr pu::i32 RowH      = 80;
    static constexpr pu::i32 RowsToShow = 10;

    // Set / get currently visible mode.
    void     SetMode(PauseMode m) { mode = m; }
    PauseMode GetMode() const     { return mode; }

    // Called by MainApplication when user picks Quit. The lambda
    // should perform flush_sram + envSetNextLoad + Close on the
    // outer Application.
    void SetOnQuit(QuitFn fn) { on_quit = std::move(fn); }

    // Called when user picks Resume or the modal closes via B on
    // the pause root. Lets the MainApplication hide the dim/menu.
    void SetOnClose(std::function<void()> fn) { on_close = std::move(fn); }

    // Called whenever an item callback mutates `mode` (Save state
    // -> SaveSlot, etc.) so MainApplication can rebuild the Menu
    // element with the new mode's items.
    void SetOnModeChanged(std::function<void()> fn) { on_mode_changed = std::move(fn); }

    // Handle B on the current menu. Returns true if the modal should
    // stay up (back to a parent menu); false to close (unpause).
    bool OnBack();

    // Set mode + fire on_mode_changed so MainApplication rebuilds
    // the menu Element.
    void ChangeMode(PauseMode m);

private:
    pu::ui::elm::Menu::Ref BuildPauseRoot();
    pu::ui::elm::Menu::Ref BuildSlotPicker(bool save_mode);
    pu::ui::elm::Menu::Ref BuildShaders();
    pu::ui::elm::Menu::Ref BuildDisplay();
    pu::ui::elm::Menu::Ref BuildCoreOptions();
    pu::ui::elm::Menu::Ref BuildCheats();

    // Populate*() — refill items into the supplied shell Menu for
    // each mode. Same logic as the Build*() counterparts but never
    // allocates a new Menu element.
    void PopulatePauseRoot(pu::ui::elm::Menu::Ref& menu);
    void PopulateSlotPicker(pu::ui::elm::Menu::Ref& menu, bool save_mode);
    void PopulateShaders(pu::ui::elm::Menu::Ref& menu);
    void PopulateDisplay(pu::ui::elm::Menu::Ref& menu);
    void PopulateDisplayAspect(pu::ui::elm::Menu::Ref& menu);
    void PopulateDisplayBezel(pu::ui::elm::Menu::Ref& menu);
    void PopulateCoreOptions(pu::ui::elm::Menu::Ref& menu);
    void PopulateCheats(pu::ui::elm::Menu::Ref& menu);
    void PopulateAchievements(pu::ui::elm::Menu::Ref& menu);

    PauseMode   mode = PauseMode::Pause;
    QuitFn      on_quit;
    std::function<void()> on_close;
    std::function<void()> on_mode_changed;
    // Remembered selection so we can restore the highlight on the
    // current mode's menu after an on_mode_changed rebuild — without
    // this, every shader pick / cheat toggle / aspect change resets
    // the highlight to row 0 and the user has to navigate back to
    // where they were.
    int         last_selected_idx = 0;

    std::string rom_path;
    std::string system_folder;
    std::string rom_stem;
    std::string back_nro;
};

}  // namespace foyer::player::plut
