#pragma once

#include <borealis.hpp>
#include <switch.h>

#include <memory>
#include <string>

namespace foyer::libretro { struct Frontend; }

namespace foyer::player {

class EmulatorView;

// Top-level Activity for the player nro. Owns the libretro Frontend
// + a single EmulatorView; drives retro_run once per brls tick.
//
// Lifecycle:
//   - Constructor receives the resolved rom path (post argv parse +
//     archive extract).
//   - onContentAvailable wires the Frontend's video sink to brls's
//     NVGcontext, kicks the per-frame ticker, and routes B (or
//     Settings) to the pause overlay (TBD: a second Activity).
//   - Destructor stops the ticker, unloads the game, shuts the
//     Frontend down. SRAM persistence happens via the Frontend's
//     own unload path.
class EmulatorActivity : public brls::Activity {
public:
    explicit EmulatorActivity(std::string rom_path);
    ~EmulatorActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void tick_frame();

    std::string          m_rom_path;
    // Path the user actually picked (e.g. /foyer/roms/nes/Foo.zip).
    // Held alongside m_rom_path so the pause menu's Save/Load
    // state cells can derive the .state file's slot path off the
    // original location — extraction rewrites m_rom_path to
    // /foyer/data/extract/Foo.nes, which would otherwise mis-route
    // both state and SRAM files.
    std::string          m_original_rom_path;
    std::string          m_system_folder;
    EmulatorView*        m_view      = nullptr;
    brls::RepeatingTask* m_ticker    = nullptr;
    bool                 m_game_ok   = false;
    // Edge-trigger gate for the L3+R3 pause combo so the menu
    // doesn't re-push every tick while the user holds it.
    bool                 m_pause_pushed = false;
    // Wall-clock ms (brls::getCPUTimeUsec()/1000) until which the
    // L3+R3 trigger is muted after a pause overlay closes — gives
    // brls's deletion pool a couple of frames to flush so the
    // next push doesn't trip the second-pause crash.
    long long            m_pause_cooldown_until_ms = 0;
    PadState             m_pad{};
    bool                 m_pad_inited = false;
};

}  // namespace foyer::player
