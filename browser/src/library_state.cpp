#include "library_state.hpp"

#include "library/config.hpp"
#include "library/system_db.hpp"

namespace foyer::browser::library_state {
namespace {

std::vector<::foyer::library::System> g_systems;

// Monotonic counter bumped on every rescan. Activities snapshot the
// counter when they build their carousel and re-check on willReappear
// — if it has moved, they rebuild. Lets the Settings → Rescan button
// surface freshly-discovered roms back on Home without a foyer reboot.
std::uint32_t g_generation = 0;

}  // namespace

void rescan() {
    ::foyer::library::ScanOptions opts{};
    opts.rom_root     = ::foyer::library::config().rom_root;
    opts.recurse      = false;
    opts.force_rescan = false;
    g_systems = ::foyer::library::scan_library(opts);
    g_generation++;
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
    g_generation++;
}

std::uint32_t generation() { return g_generation; }

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
