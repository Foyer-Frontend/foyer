#include "library_state.hpp"

#include "library/config.hpp"
#include "library/system_db.hpp"

namespace foyer::browser::library_state {
namespace {

std::vector<::foyer::library::System> g_systems;

}  // namespace

void rescan() {
    ::foyer::library::ScanOptions opts{};
    opts.rom_root     = ::foyer::library::config().rom_root;
    opts.recurse      = false;
    opts.force_rescan = false;
    g_systems = ::foyer::library::scan_library(opts);
}

void rescan_forced() {
    // Settings → Library → Rescan calls this. force_rescan=true
    // makes scan_library bypass the on-disk cache and re-stat
    // every rom folder; the regular rescan() above shares the
    // cache fast-path used at boot for sub-second startup.
    ::foyer::library::ScanOptions opts{};
    opts.rom_root     = ::foyer::library::config().rom_root;
    opts.recurse      = false;
    opts.force_rescan = true;
    g_systems = ::foyer::library::scan_library(opts);
}

const std::vector<::foyer::library::System>& systems() {
    return g_systems;
}

const ::foyer::library::System* find_system(std::string_view folder) {
    for (const auto& s : g_systems) {
        if (s.def && s.def->folder_name == folder) return &s;
    }
    return nullptr;
}

}  // namespace foyer::browser::library_state
