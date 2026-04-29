#include "system_db.hpp"

#include <cstring>

namespace foyer::library {
namespace {

// Master system table. Add new rows here when expanding cores. The carousel
// + scanner + launcher all key off this single source.
//
// Notes:
//   - `core_name` must match an upstream libretro buildbot artifact AND the
//     directory name we ship as cores/<core>.cmake.
//   - `thumbnails_db` matches the folder name in the libretro-thumbnails
//     repo (https://github.com/libretro-thumbnails/libretro-thumbnails).
constexpr SystemDef kSystems[] = {
    { "nes",          "Nintendo Entertainment System",
      "NES",          "fceumm",
      "Nintendo - Nintendo Entertainment System",
      "nes|fds|unif|unf" },
    { "snes",         "Super Nintendo",
      "SNES",         "snes9x",
      "Nintendo - Super Nintendo Entertainment System",
      "smc|sfc|swc|fig|bs|st" },
    { "gb",           "Game Boy",
      "GB",           "gambatte",
      "Nintendo - Game Boy",
      "gb" },
    { "gbc",          "Game Boy Color",
      "GBC",          "gambatte",
      "Nintendo - Game Boy Color",
      "gbc" },
    { "gba",          "Game Boy Advance",
      "GBA",          "mgba",
      "Nintendo - Game Boy Advance",
      "gba" },
    { "n64",          "Nintendo 64",
      "N64",          "mupen64plus",
      "Nintendo - Nintendo 64",
      "n64|z64|v64" },
    { "nds",          "Nintendo DS",
      "NDS",          "melonds",
      "Nintendo - Nintendo DS",
      "nds" },
    { "gc",           "GameCube",
      "GC",           "dolphin",
      "Nintendo - GameCube",
      "iso|gcm|rvz" },
    { "genesis",      "Sega Genesis",
      "MD",           "genesisplusgx",
      "Sega - Mega Drive - Genesis",
      "md|gen|smd|bin" },
    { "megadrive",    "Sega Mega Drive",
      "MD",           "genesisplusgx",
      "Sega - Mega Drive - Genesis",
      "md|gen|smd|bin" },
    { "mastersystem", "Sega Master System",
      "SMS",          "genesisplusgx",
      "Sega - Master System - Mark III",
      "sms" },
    { "gamegear",     "Game Gear",
      "GG",           "genesisplusgx",
      "Sega - Game Gear",
      "gg" },
    { "saturn",       "Sega Saturn",
      "SAT",          "yabasanshiro",
      "Sega - Saturn",
      "cue|chd|iso" },
    { "dc",           "Dreamcast",
      "DC",           "flycast",
      "Sega - Dreamcast",
      "cdi|chd|gdi|m3u" },
    { "psx",          "PlayStation",
      "PSX",          "swanstation",
      "Sony - PlayStation",
      "cue|chd|pbp|m3u" },
    { "psp",          "PlayStation Portable",
      "PSP",          "ppsspp",
      "Sony - PlayStation Portable",
      "iso|cso|pbp" },
    { "ngp",          "Neo Geo Pocket",
      "NGP",          "race",
      "SNK - Neo Geo Pocket",
      "ngp" },
    { "ngpc",         "Neo Geo Pocket Color",
      "NGPC",         "race",
      "SNK - Neo Geo Pocket Color",
      "ngc" },
};

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        char ca = a[i]; if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        char cb = b[i]; if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return false;
    }
    return true;
}

} // namespace

std::span<const SystemDef> all_systems() {
    return { kSystems, std::size(kSystems) };
}

const SystemDef* find_system_by_folder(std::string_view folder) {
    for (const auto& s : kSystems) {
        if (iequal(s.folder_name, folder)) return &s;
    }
    return nullptr;
}

const SystemDef* find_system_by_core(std::string_view core) {
    for (const auto& s : kSystems) {
        if (iequal(s.core_name, core)) return &s;
    }
    return nullptr;
}

} // namespace foyer::library
