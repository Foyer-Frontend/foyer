#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {

struct SystemDef;

// One rom file on the SD that's been recognised by a system.
struct Game {
    std::string path;        // absolute "/foyer/roms/nes/Game.nes"
    std::string filename;    // "Game.nes"
    std::string stem;        // "Game"
    std::string ext;         // "nes" (lowercase, post-archive-peek if applicable)
    std::string display;     // metadata-overridden name or stem
    std::string description; // optional, from gamelist.xml
    std::string box_art;     // path to box art if resolved locally; empty otherwise
    bool        favorite   = false;
    std::uint64_t last_played = 0;
};

// One enabled system + its game list. Only systems that have at least one
// matching rom on disk make it into the scan output.
struct System {
    const SystemDef* def = nullptr;     // points into system_db's static table
    std::vector<Game> games;
    std::string root_path;              // /foyer/roms/<folder>
};

// Scan options. Eventually populated from /foyer/config/general.jsonc.
struct ScanOptions {
    std::string rom_root = "/foyer/roms";
    bool        recurse  = false;
    // When true, ignore /foyer/data/library.cache.json and force a
    // full SD walk. Used by the explicit "Rescan library" action.
    // Default false → boot path takes the cache fast-path when the
    // cache is fresh.
    bool        force_rescan = false;
};

// Walks the rom root and returns all Systems whose folder matches a known
// SystemDef and whose folder contains at least one rom with a recognised
// extension (raw or peeked from inside .zip).
std::vector<System> scan_library(const ScanOptions& opts);

} // namespace foyer::library
