#pragma once

#include <borealis.hpp>

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
    EmulatorView*        m_view      = nullptr;
    brls::RepeatingTask* m_ticker    = nullptr;
    bool                 m_game_ok   = false;
};

}  // namespace foyer::player
