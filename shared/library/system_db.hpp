#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace foyer::library {

// Static description of one supported emulation system. Both the browser and
// the per-core players pull from this table so the rom scanner, the launcher,
// and the libretro buildbot URLs stay in sync.
struct SystemDef {
    // Folder name on SD relative to the rom root (e.g. "nes"). Lowercase.
    std::string_view folder_name;
    // Pretty name shown in the carousel + system view header.
    std::string_view display_name;
    // 3-5 letter short name shown on the per-system tile in the carousel.
    std::string_view short_name;
    // Core suffix used to locate /foyer/cores/foyer-<core>.nro at launch.
    std::string_view core_name;
    // libretro-thumbnails repository folder for box art (URL-encoded later).
    std::string_view thumbnails_db;
    // Pipe-separated list of raw rom extensions accepted (lowercase).
    std::string_view extensions;
};

// Returns the master table. Order matters: the carousel renders systems in
// this order whenever a user has the "All" collection sorted by db default.
std::span<const SystemDef> all_systems();

// Lookup by folder name (case-insensitive). Returns nullptr on miss.
const SystemDef* find_system_by_folder(std::string_view folder);

// Lookup by core_name. Returns nullptr on miss. Used by the player to figure
// out which rom subdir it owns when no argv is supplied (dev fallback path).
const SystemDef* find_system_by_core(std::string_view core);

} // namespace foyer::library
