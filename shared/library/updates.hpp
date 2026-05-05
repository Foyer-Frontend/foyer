#pragma once

#include "bezel_installer.hpp"
#include "cheat_installer.hpp"
#include "core_installer.hpp"
#include "foyer_updater.hpp"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace foyer::library {

enum class UpdateKind { Foyer, Core, Bezel, Cheat, Shader };

// One pending change. Items the user has never installed do NOT show
// up here — those belong in the per-kind Catalog views. An item only
// surfaces as an UpdateItem if it's installed on disk *and* the
// available manifest version differs.
struct UpdateItem {
    UpdateKind   kind;
    std::string  id;             // "pcsx_rearmed", "snes" (bezel slug), …
    std::string  display;        // shown in UI; usually == id
    std::string  installed_ver;  // sidecar value; "" if Foyer (we know FOYER_VERSION)
    std::string  available_ver;
    std::uint64_t download_size = 0;
};

// Result of one aggregation pass. The top-level UI iterates these
// vectors to render bucketed sections; main.cpp's "Update everything"
// handler runs each bucket in order.
struct UpdateBuckets {
    std::vector<UpdateItem> foyer;     // 0 or 1 entry
    std::vector<UpdateItem> cores;
    std::vector<UpdateItem> bezels;
    std::vector<UpdateItem> cheats;
    // Shader presets join the rotation later; manifest isn't cached yet.

    // Wall-clock when the manifests these were derived from were
    // last fetched. Drives the "Last: 2 min ago" footer + the
    // 5-minute auto-rescrape on Updates entry.
    std::time_t scraped_at = 0;

    std::size_t total() const {
        return foyer.size() + cores.size() + bezels.size() + cheats.size();
    }
};

// Aggregate. `foyer_current` is the running build's version (compile
// constant) — when the manifest's version is strictly newer, foyer
// gets a row. `cores`/`bezels`/`cheats` come from the manifest caches
// already maintained by the browser; we read each entry's installed
// sidecar and compare.
//
// Items the user has explicitly skipped (skipped_versions.json) are
// filtered out — adding "Skip this version" to the Updates picker is
// what feeds that store. A newer release re-enables the row.
UpdateBuckets compute_pending_updates(
    const FoyerManifest&  foyer_m,
    const std::string&    foyer_current,
    const CoreManifest&   cores_m,
    const BezelManifest&  bezels_m,
    const CheatManifest&  cheats_m);

} // namespace foyer::library
