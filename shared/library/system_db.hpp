#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace foyer::library {

// One libretro core that's been ported / packaged for foyer. The order
// inside SystemDef::cores matters: index 0 is the system's default core.
struct CoreDef {
    std::string_view name;          // matches cores/<name>.cmake + foyer-<name>.nro
    std::string_view display_name;  // shown in core picker UIs
};

// Static description of one supported emulation system. Both the browser and
// the per-core players pull from this table so the rom scanner, the launcher,
// and the libretro buildbot URLs stay in sync.
struct SystemDef {
    std::string_view folder_name;     // SD subdir under rom root (e.g. "nes")
    std::string_view display_name;    // pretty name for UIs
    std::string_view short_name;      // 3-5 letter abbreviation for tiles
    std::string_view thumbnails_db;   // libretro-thumbnails repo folder
    std::string_view extensions;      // pipe-separated raw rom exts
    std::span<const CoreDef> cores;   // ordered cores; cores[0] = default
};

std::span<const SystemDef> all_systems();

// Lookup by folder name (case-insensitive). Returns nullptr on miss.
const SystemDef* find_system_by_folder(std::string_view folder);

// Lookup by core name across every system. Returns the (sys, core) pair the
// match belongs to, or nullptrs on miss.
struct CoreLookup { const SystemDef* sys; const CoreDef* core; };
CoreLookup find_core(std::string_view core_name);

// Convenience — first valid core for a system (cores[0]).
const CoreDef* default_core(const SystemDef& sys);

// Locate a CoreDef by name within a given system. Used by the launcher when
// resolving per-game / per-system overrides; falls back to nullptr if the
// requested core isn't valid for that system.
const CoreDef* find_core_in_system(const SystemDef& sys, std::string_view core_name);

// Synthetic "virtual" systems that show up alongside real ones in the home
// carousel. They have no real folder on the SD; their game lists are
// computed in scan_library by filtering across every real system. The
// folder_name starts with "__" so callers can detect them and route core
// resolution / launch through the rom's origin system instead of these
// sentinels (which have an empty cores span).
extern const SystemDef kVirtualRecentDef;     // "__recent"
extern const SystemDef kVirtualFavoritesDef;  // "__favorites"

bool is_virtual_system(const SystemDef& sys);

} // namespace foyer::library
