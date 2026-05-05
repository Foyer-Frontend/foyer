#pragma once

#include "scanner.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace foyer::library {

// Persisted snapshot of a scan_library() result. Lives at
// /foyer/data/library.cache.json by default. Format is JSON
// objects mirroring the in-memory Game / System structs plus
// per-system folder mtimes captured at write time, so the loader
// can detect "user added a rom" by re-stat'ing each folder
// against the stored mtime and falling back to a full scan when
// any mtime is fresher.
//
// The cache is purely a perf optimisation — users never need to
// know it exists, and Settings → Library → Rescan library wipes
// + rebuilds it.

bool save_library_cache(std::string_view path,
                        const std::vector<System>& systems,
                        std::string_view rom_root);

// Returns the cached systems vector iff every recorded folder
// still has its captured mtime AND no new top-level system
// folder appeared under rom_root since the cache was written.
// nullopt on missing file, parse error, or any staleness signal.
std::optional<std::vector<System>>
load_library_cache(std::string_view path,
                   std::string_view rom_root);

} // namespace foyer::library
