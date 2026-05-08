#pragma once

#include "library/scanner.hpp"

#include <string_view>
#include <vector>

namespace foyer::browser::library_state {

// One-shot scan called at boot from main(). Replaces the legacy
// scan_library() result with the scan output; subsequent calls
// re-scan from disk (used by an explicit "Rescan" action later).
void rescan();

// Read-only access to the cached scan result. Activities pull
// games for a given system folder via find_system().
const std::vector<::foyer::library::System>& systems();

// Lookup a System by folder name. Returns nullptr if the folder
// has no scanned games (system was empty or absent on disk).
const ::foyer::library::System* find_system(std::string_view folder);

}  // namespace foyer::browser::library_state
