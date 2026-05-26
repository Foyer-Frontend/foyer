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
    { "snes9x2010",     "Snes9x 2010 (lighter)" },
};
constexpr CoreDef kCoresGambatte[]    = {
    { "gambatte", "Gambatte" },
    { "sameboy",  "SameBoy"  },
    { "tgbdual",  "TGB Dual (link cable)" },
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
    { "pcsx_rearmed",     "PCSX ReARMed" },
    { "swanstation",      "Swanstation"  },
    { "mednafen_psx_hw",  "Beetle PSX HW (16:9)" },
};
constexpr CoreDef kCoresPpsspp[]      = { { "ppsspp",        "PPSSPP"        } };
constexpr CoreDef kCoresRace[]        = {
    { "race",         "RACE"         },
    { "mednafen_ngp", "Beetle NGP"   },
};
constexpr CoreDef kCoresStella[]      = {
    { "stella",     "Stella"                  },
    { "stella2014", "Stella 2014 (lighter)"   },
};
constexpr CoreDef kCoresProsystem[]   = { { "prosystem",     "ProSystem"     } };
constexpr CoreDef kCoresHandy[]       = {
    { "handy",         "Handy"      },
    { "mednafen_lynx", "Beetle Lynx"},
};
// 32X and SegaCD currently ship via picodrive only.
constexpr CoreDef kCoresPicoOnly[]    = { { "picodrive",     "PicoDrive"     } };

// PC Engine / TurboGrafx-16 — Beetle PCE first, the older lighter
// pce_fast as a thermal-friendly alternative.
constexpr CoreDef kCoresPce[] = {
    { "beetle_pce",        "Beetle PCE"         },
    { "mednafen_pce_fast", "Beetle PCE (fast)"  },
};
// PC Engine SuperGrafx — Beetle SuperGrafx is its own libretro core,
// not a mode of the regular PCE core.
constexpr CoreDef kCoresSupergrafx[]  = { { "beetle_supergrafx", "Beetle SuperGrafx" } };
constexpr CoreDef kCoresPcfx[]        = { { "beetle_pcfx",       "Beetle PC-FX"      } };

// WonderSwan / WonderSwan Color share the same Beetle core.
constexpr CoreDef kCoresWswan[]       = { { "beetle_wswan",      "Beetle WonderSwan" } };

constexpr CoreDef kCoresVb[]          = { { "beetle_vb",         "Beetle Virtual Boy" } };
constexpr CoreDef kCoresJaguar[]      = { { "virtualjaguar",     "Virtual Jaguar"     } };
constexpr CoreDef kCoresPokemini[]    = { { "pokemini",          "PokeMini"           } };
constexpr CoreDef kCoresIntellivision[] = { { "freeintv",        "FreeIntv"           } };
constexpr CoreDef kCoresGw[]          = { { "gw",                "Game and Watch"     } };

// Engine cores — one engine reimplementation per slot.
// Note: reminiscence (Flashback) ships as a core but has no SystemDef
// entry yet because the EmulationStation theme we use for system art
// doesn't have a "flashback" tile. Add the SystemDef once a tile lands.
constexpr CoreDef kCoresDoom[]        = { { "prboom",            "PrBoom"             } };
constexpr CoreDef kCoresQuake[]       = { { "tyrquake",          "TyrQuake"           } };
constexpr CoreDef kCoresPico8[]       = { { "retro8",            "retro8"             } };

// DOS — DOSBox-pure handles the full single-file zip workflow.
constexpr CoreDef kCoresDos[]         = { { "dosbox_pure",       "DOSBox-pure"        } };

// Amstrad CPC.
constexpr CoreDef kCoresCpc[]         = { { "caprice32",         "Caprice32"          } };

// Commodore 64 — Frodo (lightweight); vice will join when its
// ExternalProject recipe lands.
constexpr CoreDef kCoresC64[]         = { { "frodo",             "Frodo"              } };

// Atari 8-bit family (atari800 covers both Atari 800 + 5200).
constexpr CoreDef kCoresAtari800[]    = { { "atari800",          "Atari800"           } };

// MSX / MSX2 / MSX2+ — fMSX is light and stable; bluemsx will join
// when its recipe lands.
constexpr CoreDef kCoresMsx[]         = { { "fmsx",              "fMSX"               } };

// 3DO Interactive Multiplayer.
constexpr CoreDef kCores3do[]         = { { "opera",             "Opera"              } };

// MAME 2003-Plus — covers most arcade titles up to ~2003.
constexpr CoreDef kCoresMame[]        = { { "mame2003_plus",     "MAME 2003-Plus"     } };

// ScummVM — point-and-click adventures.
constexpr CoreDef kCoresScummvm[]     = { { "scummvm",           "ScummVM"            } };

// Amiga family (PUAE — Amiga 500/1200/2000/3000/4000/CD32/CDTV).
constexpr CoreDef kCoresAmiga[]       = { { "puae",              "PUAE"               } };

// nxengine (Cave Story) ships as a core but has no SystemDef entry —
// the theme has no cavestory tile. Add the SystemDef once a tile lands.

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

    // ----- 0.3.0 additions: extra-system cores -----

    { "pcengine",     "PC Engine",                    "PCE",
      "NEC - PC Engine - TurboGrafx 16",
      "pce|sgx|cue|chd|m3u", kCoresPce },

    { "pcenginecd",   "PC Engine CD",                 "PCE-CD",
      "NEC - PC Engine CD - TurboGrafx-CD",
      "cue|chd|m3u|iso",   kCoresPce },

    { "supergrafx",   "PC Engine SuperGrafx",         "SGX",
      "NEC - PC Engine SuperGrafx",
      "pce|sgx",           kCoresSupergrafx },

    { "pcfx",         "PC-FX",                        "PCFX",
      "NEC - PC-FX",
      "cue|chd|m3u|iso",   kCoresPcfx },

    { "wonderswan",   "WonderSwan",                   "WS",
      "Bandai - WonderSwan",
      "ws",                kCoresWswan },

    { "wonderswancolor", "WonderSwan Color",          "WSC",
      "Bandai - WonderSwan Color",
      "wsc",               kCoresWswan },

    { "virtualboy",   "Virtual Boy",                  "VB",
      "Nintendo - Virtual Boy",
      "vb|vboy|bin",       kCoresVb },

    { "atarijaguar",  "Atari Jaguar",                 "Jag",
      "Atari - Jaguar",
      "j64|jag|rom",       kCoresJaguar },

    { "pokemini",     "Pokemon Mini",                 "PM",
      "Nintendo - Pokemon Mini",
      "min",               kCoresPokemini },

    { "intellivision", "Intellivision",               "Intv",
      "Mattel - Intellivision",
      "int|bin|rom",       kCoresIntellivision },

    { "gameandwatch", "Game & Watch",                 "G&W",
      "Handheld Electronic Game",
      "mgw",               kCoresGw },

    // Engine-game systems — each rom is the data for a particular
    // engine reimplementation; folder = engine identity.
    { "doom",         "Doom (PrBoom)",                "DOOM",
      "DOOM",
      "wad|iwad|pwad",     kCoresDoom },

    { "quake",        "Quake (TyrQuake)",             "Q",
      "Quake",
      "pak",               kCoresQuake },

    { "pico8",        "Pico-8",                       "P8",
      "PICO-8",
      "p8|p8.png",         kCoresPico8 },

    { "dos",          "DOS",                          "DOS",
      "DOS",
      "exe|com|bat|conf|zip|m3u", kCoresDos },

    { "amstradcpc",   "Amstrad CPC",                  "CPC",
      "Amstrad - CPC",
      "dsk|cpr|cdt|sna|kcr", kCoresCpc },

    { "c64",          "Commodore 64",                 "C64",
      "Commodore - 64",
      "d64|t64|prg|crt|p00|tap|nib|m3u", kCoresC64 },

    { "atari800",     "Atari 800",                    "A800",
      "Atari - 8-bit",
      "atr|xfd|atx|cas|cdm|car|rom|com|xex", kCoresAtari800 },

    { "atari5200",    "Atari 5200",                   "A5200",
      "Atari - 5200",
      "a52|car|bin|rom",   kCoresAtari800 },

    { "msx",          "MSX",                          "MSX",
      "Microsoft - MSX",
      "rom|ri|mx1|mx2|col|dsk|cas|sg|sc|m3u", kCoresMsx },

    { "msx2",         "MSX2",                         "MSX2",
      "Microsoft - MSX2",
      "rom|ri|mx1|mx2|col|dsk|cas|sg|sc|m3u", kCoresMsx },

    // MSX2+ and TurboR — fMSX handles both transparently. Folders
    // kept distinct so users with separate ROM collections can sort
    // by hardware revision; the libretro core picks the right
    // machine from rom-format heuristics.
    { "msx2plus",     "MSX2+",                        "MSX2+",
      "Microsoft - MSX2+",
      "rom|ri|mx1|mx2|col|dsk|cas|sg|sc|m3u", kCoresMsx },

    { "msxturbor",    "MSX TurboR",                   "TurboR",
      "Microsoft - MSX TurboR",
      "rom|ri|mx1|mx2|col|dsk|cas|sg|sc|m3u", kCoresMsx },

    { "3do",          "3DO",                          "3DO",
      "The 3DO Company - 3DO",
      "iso|chd|cue|m3u",   kCores3do },

    { "arcade",       "Arcade",                       "ARC",
      "MAME 2003-Plus",
      "zip|7z|chd",        kCoresMame },

    { "scummvm",      "ScummVM",                      "SVM",
      "ScummVM",
      "scummvm|svm|exe|com|bat|pkg|m3u", kCoresScummvm },

    { "amiga",        "Amiga",                        "Amiga",
      "Commodore - Amiga",
      "adf|adz|ipf|dms|fdi|hdf|hdz|lha|slave|info|cue|ccd|nrg|mds|iso|chd|uae|m3u|zip|7z", kCoresAmiga },

    { "amiga600",     "Amiga 600",                    "A600",
      "Commodore - Amiga",
      "adf|adz|ipf|dms|fdi|hdf|hdz|lha|m3u",  kCoresAmiga },

    { "amiga1200",    "Amiga 1200",                   "A1200",
      "Commodore - Amiga",
      "adf|adz|ipf|dms|fdi|hdf|hdz|lha|m3u",  kCoresAmiga },

    { "amigacd32",    "Amiga CD32",                   "CD32",
      "Commodore - Amiga CD32",
      "cue|ccd|nrg|mds|iso|chd|m3u",          kCoresAmiga },

    { "cdtv",         "Commodore CDTV",               "CDTV",
      "Commodore - CDTV",
      "cue|ccd|nrg|mds|iso|chd|m3u",          kCoresAmiga },
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

const SystemDef kVirtualRecentDef = {
    "__recent", "Recently Played", "RECENT", "", "", {},
};
const SystemDef kVirtualFavoritesDef = {
    "__favorites", "Favorites", "FAVS", "", "", {},
};
const SystemDef kVirtualAllGamesDef = {
    "__allgames", "All Games", "ALL", "", "", {},
};
const SystemDef kVirtualUnknownDef = {
    "__unknown", "Unknown",  "UNK", "", "", {},
};
// 0.5.5: Switch-title launcher virtual system. Games inside this
// system carry path strings of the form "switch://<application_id>"
// which launch.cpp detects and routes through
// appletRequestLaunchApplication instead of envSetNextLoad.
const SystemDef kVirtualSwitchDef = {
    "__switch", "Nintendo Switch", "SWITCH", "", "", {},
};

bool is_virtual_system(const SystemDef& sys) {
    return !sys.folder_name.empty() && sys.folder_name.starts_with("__");
}

// Hardware-family aliases. Folders not listed here have their own
// family (folder == family). Resolvers fall back to the family slug
// when the folder-specific asset is missing — so a user who installs
// `megadrive-bezelproject` sees it on a genesis rom too, scrapes
// done under `genesis/` populate cheats lookups for `megadrive/`,
// etc.
//
// Covers every retro-system regional-naming pair that shows up
// either as a foyer kSystems folder slug, as a foyer-bezels pack
// name, or as a common-overlays / libretro slug an external user
// might rom-organise by. The family slug is the one foyer's
// kSystems uses; the alternates are the ones users (or upstream
// bezel/thumbnail packs) might call them. Resolvers walk both
// directions of the table so a rom under genesis/ finds
// megadrive-* assets and vice versa.
namespace {
struct SystemAlias { std::string_view folder; std::string_view family; };
constexpr SystemAlias kAliases[] = {
    // Nintendo Entertainment System / Famicom (Japan, "headered" variant).
    { "nes",            "nes" },
    { "famicom",        "nes" },
    { "nesh",           "nes" },
    // Super Nintendo / Super Famicom (Japan + assorted region tags).
    { "snes",           "snes" },
    { "sfc",            "snes" },
    { "snesh",          "snes" },
    { "snesna",         "snes" },
    { "sgb",            "snes" },
    { "sufami",         "snes" },
    // Game Boy (Japan-region "h" tags from common-overlays).
    { "gb",             "gb" },
    { "gbh",            "gb" },
    // Game Boy Color.
    { "gbc",            "gbc" },
    { "gbch",           "gbc" },
    // Game Boy Advance.
    { "gba",            "gba" },
    { "gbah",           "gba" },
    // Sega Mega Drive / Genesis (Japan / Europe / Australia vs.
    // North America). foyer kSystems carries both folders; family
    // slug = "megadrive" so the JP/EU canonical name wins.
    { "megadrive",      "megadrive" },
    { "genesis",        "megadrive" },
    // Sega Master System / Mark III (Japan).
    { "mastersystem",   "mastersystem" },
    { "markiii",        "mastersystem" },
    // Sega CD / Mega-CD (Japan / Europe).
    { "segacd",         "segacd" },
    { "megacd",         "segacd" },
    // NEC PC Engine / TurboGrafx-16 (NA).
    { "pcengine",       "pcengine" },
    { "turbografx",     "pcengine" },
    { "tg16",           "pcengine" },
    // NEC PC Engine CD / TurboGrafx-CD (NA).
    { "pcenginecd",     "pcenginecd" },
    { "turbografxcd",   "pcenginecd" },
    { "tg16cd",         "pcenginecd" },
    { "pcecd",          "pcenginecd" },
    // 3DO Interactive Multiplayer (foyer slug "opera" matches the
    // libretro core; Panasonic 3DO / Goldstar 3DO etc. are the
    // user-facing names).
    { "opera",          "opera" },
    { "3do",            "opera" },
    { "panasonic3do",   "opera" },
    // Sony PlayStation — slug "psx" everywhere in foyer.
    { "psx",            "psx" },
    { "playstation",    "psx" },
    { "ps1",            "psx" },
};
}  // namespace

std::string_view family_for_folder(std::string_view folder) {
    for (const auto& a : kAliases) {
        if (folder == a.folder) return a.family;
    }
    return folder;
}

} // namespace foyer::library
