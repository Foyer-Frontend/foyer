#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <string_view>

namespace foyer::libretro {

// 0 = "Quick" slot (used by Save/Load shortcuts), 1..9 = manual slots.
constexpr int kStateSlotCount = 10;

// Returns "/foyer/states/<system>/<rom-stem>.<slot>.state" given a rom path
// on SD, the short system folder (e.g. "nes") and a slot index 0..9.
// Creates the destination dir on first use. Pass an empty `system_folder`
// to fall back to the rom's parent dir name.
std::string state_path_for(std::string_view rom_path,
                           std::string_view system_folder,
                           int               slot);

// Snapshot of one slot's status, used by the load/save list UI.
struct StateSlot {
    int          slot       = 0;
    bool         exists     = false;
    std::time_t  mtime      = 0;     // last modified time (epoch)
    std::uint64_t size_bytes = 0;
};

// Probe all slots. Always returns kStateSlotCount entries.
void inspect_slots(std::string_view rom_path,
                   std::string_view system_folder,
                   StateSlot       out[kStateSlotCount]);

// retro_serialize → write to disk. Returns true on success.
bool save_state(const std::string& path);

// retro_unserialize ← read from disk. Returns true on success.
bool load_state(const std::string& path);

} // namespace foyer::libretro
