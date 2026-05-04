#pragma once

#include <string>
#include <vector>

namespace foyer::libretro {

// One cheat entry parsed out of a RetroArch-format .cht file. We only
// keep the three fields the libretro API actually needs:
//
//   * `desc`   — human-readable label shown in the overlay
//   * `code`   — the cheat code string (format is core-dependent;
//                 we pass it through verbatim to retro_cheat_set)
//   * `enabled` — runtime toggle persisted back into the .cht file
struct Cheat {
    std::string desc;
    std::string code;
    bool        enabled = false;
};

// Loads cheats for the currently-running rom. Path resolution:
//   /foyer/cheats/<system_folder>/<rom_stem>.cht
//
// Returns an empty vector if the file is missing or unparseable.
// Existing cheat<N>_enable values seed the runtime `enabled` field
// so user toggles persist across game launches.
std::vector<Cheat> load_cheats_for(std::string_view system_folder,
                                   std::string_view rom_stem);

// Writes the current toggle state back to the .cht file (only the
// _enable fields; everything else is preserved). Called after the
// user changes a cheat in the pause overlay so the state survives
// the next launch.
void save_cheats_for(std::string_view system_folder,
                     std::string_view rom_stem,
                     const std::vector<Cheat>& cheats);

// Push the entire cheat list to the running core. retro_cheat_reset
// first to clear any prior state, then retro_cheat_set(idx, enabled,
// code) for each one. Should be called after the rom is loaded
// (cheats apply to RAM, which doesn't exist before retro_load_game)
// and again whenever the user toggles something in the overlay.
void apply_cheats_to_core(const std::vector<Cheat>& cheats);

} // namespace foyer::libretro
