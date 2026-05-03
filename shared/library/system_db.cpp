#include "system_db.hpp"

#include <cstring>

namespace foyer::library {
namespace {

// Per-system core lists. Order = priority. cores[0] is the system default
// when no per-game / general override is set.
//
// Keep these arrays in sync with the cores/<name>.cmake recipes and with
// the README's "Supported systems / cores" table.

constexpr CoreDef kCoresNes[]         = {
    { "fceumm",   "FCEUmm"   },
    { "nestopia", "Nestopia UE" },
    { "mesen",    "Mesen"    },
};
constexpr CoreDef kCoresSnes9x[]      = {
    { "snes9x",         "Snes9x"          },
    { "bsnes_hd_beta",  "bsnes-hd (16:9)" },
};
constexpr CoreDef kCoresGambatte[]    = {
    { "gambatte", "Gambatte" },
    { "sameboy",  "SameBoy"  },
};
constexpr CoreDef kCoresMgba[]        = {
    { "mgba",     "mGBA"     },
    { "vba_next", "VBA Next" },
    { "gpsp",     "gpSP (arm64 dynarec)" },
};
constexpr CoreDef kCoresMupen64[]     = {
    { "mupen64plus", "Mupen64Plus" },
    { "parallel_n64", "parallel-n64 (16:9 hacks DB)" },
};
constexpr CoreDef kCoresMelonds[]     = { { "melonds",       "melonDS"       } };
constexpr CoreDef kCoresDolphin[]     = { { "dolphin",       "Dolphin"       } };
// Genesis / Mega Drive / Master System / Game Gear share the same core
// list. Order = priority — genesisplusgx is the safe default,
// genesis_plus_gx_wide gives the H40 widescreen patch (16:9 fit on
// supported titles), picodrive is a lighter alternative.
constexpr CoreDef kCoresGenesisGx[]   = {
    { "genesisplusgx",       "Genesis Plus GX"      },
    { "genesis_plus_gx_wide","Genesis Plus GX Wide" },
    { "picodrive",           "PicoDrive"            },
};
constexpr CoreDef kCoresYabaSan[]     = { { "yabasanshiro",  "YabaSanshiro"  } };
constexpr CoreDef kCoresFlycast[]     = { { "flycast",       "Flycast"       } };
constexpr CoreDef kCoresPsx[]         = {
    { "pcsx_rearmed", "PCSX ReARMed" },
    { "swanstation",  "Swanstation"  },
};
constexpr CoreDef kCoresPpsspp[]      = { { "ppsspp",        "PPSSPP"        } };
constexpr CoreDef kCoresRace[]        = {
    { "race",         "RACE"         },
    { "mednafen_ngp", "Beetle NGP"   },
};
constexpr CoreDef kCoresStella[]      = { { "stella",        "Stella"        } };
constexpr CoreDef kCoresProsystem[]   = { { "prosystem",     "ProSystem"     } };
constexpr CoreDef kCoresHandy[]       = {
    { "handy",         "Handy"      },
    { "mednafen_lynx", "Beetle Lynx"},
};
// 32X and SegaCD currently ship via picodrive only.
constexpr CoreDef kCoresPicoOnly[]    = { { "picodrive",     "PicoDrive"     } };

constexpr SystemDef kSystems[] = {
    { "nes",          "Nintendo Entertainment System", "NES",
      "Nintendo - Nintendo Entertainment System",
      "nes|fds|unif|unf",  kCoresNes },

    { "snes",         "Super Nintendo",               "SNES",
      "Nintendo - Super Nintendo Entertainment System",
      "smc|sfc|swc|fig|bs|st", kCoresSnes9x },

    { "gb",           "Game Boy",                     "GB",
      "Nintendo - Game Boy",
      "gb",                kCoresGambatte },

    { "gbc",          "Game Boy Color",               "GBC",
      "Nintendo - Game Boy Color",
      "gbc",               kCoresGambatte },

    { "gba",          "Game Boy Advance",             "GBA",
      "Nintendo - Game Boy Advance",
      "gba",               kCoresMgba },

    { "n64",          "Nintendo 64",                  "N64",
      "Nintendo - Nintendo 64",
      "n64|z64|v64",       kCoresMupen64 },

    { "nds",          "Nintendo DS",                  "NDS",
      "Nintendo - Nintendo DS",
      "nds",               kCoresMelonds },

    { "gc",           "GameCube",                     "GC",
      "Nintendo - GameCube",
      "iso|gcm|rvz",       kCoresDolphin },

    { "genesis",      "Sega Genesis",                 "MD",
      "Sega - Mega Drive - Genesis",
      "md|gen|smd|bin",    kCoresGenesisGx },

    { "megadrive",    "Sega Mega Drive",              "MD",
      "Sega - Mega Drive - Genesis",
      "md|gen|smd|bin",    kCoresGenesisGx },

    { "mastersystem", "Sega Master System",           "SMS",
      "Sega - Master System - Mark III",
      "sms",               kCoresGenesisGx },

    { "gamegear",     "Game Gear",                    "GG",
      "Sega - Game Gear",
      "gg",                kCoresGenesisGx },

    { "saturn",       "Sega Saturn",                  "SAT",
      "Sega - Saturn",
      "cue|chd|iso",       kCoresYabaSan },

    { "dc",           "Dreamcast",                    "DC",
      "Sega - Dreamcast",
      "cdi|chd|gdi|m3u",   kCoresFlycast },

    { "psx",          "PlayStation",                  "PSX",
      "Sony - PlayStation",
      "cue|chd|pbp|m3u",   kCoresPsx },

    { "psp",          "PlayStation Portable",         "PSP",
      "Sony - PlayStation Portable",
      "iso|cso|pbp",       kCoresPpsspp },

    { "ngp",          "Neo Geo Pocket",               "NGP",
      "SNK - Neo Geo Pocket",
      "ngp",               kCoresRace },

    { "ngpc",         "Neo Geo Pocket Color",         "NGPC",
      "SNK - Neo Geo Pocket Color",
      "ngc",               kCoresRace },

    { "atari2600",    "Atari 2600",                   "A2600",
      "Atari - 2600",
      "a26|bin",           kCoresStella },

    { "atari7800",    "Atari 7800",                   "A7800",
      "Atari - 7800",
      "a78|bin",           kCoresProsystem },

    { "atarilynx",    "Atari Lynx",                   "Lynx",
      "Atari - Lynx",
      "lnx|lyx",           kCoresHandy },

    { "32x",          "Sega 32X",                     "32X",
      "Sega - 32X",
      "32x|bin|md",        kCoresPicoOnly },

    { "segacd",       "Sega CD",                      "SegaCD",
      "Sega - Mega-CD - Sega CD",
      "cue|iso|chd|m3u",   kCoresPicoOnly },
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

CoreLookup find_core(std::string_view core_name) {
    for (const auto& s : kSystems) {
        for (const auto& c : s.cores) {
            if (iequal(c.name, core_name)) return { &s, &c };
        }
    }
    return { nullptr, nullptr };
}

const CoreDef* default_core(const SystemDef& sys) {
    if (sys.cores.empty()) return nullptr;
    return &sys.cores.front();
}

const CoreDef* find_core_in_system(const SystemDef& sys, std::string_view name) {
    for (const auto& c : sys.cores) {
        if (iequal(c.name, name)) return &c;
    }
    return nullptr;
}

} // namespace foyer::library
