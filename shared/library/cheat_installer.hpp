#pragma once

#include "net/http.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace foyer::library {

// One row out of foyer-cheats's release manifest.json. Mirrors the
// writer in foyer-cheats/.github/workflows/build-cheats.yml.
//
// The manifest itself records which `libretro-database` upstream
// release each pack was sliced from, so foyer can show "based on
// v1.22.1" alongside the pack list.
struct CheatManifestEntry {
    std::string name;        // foyer system folder, e.g. "nes"
    std::string version;     // first 7 chars of the zip's sha256
    std::string zip;         // filename, e.g. "nes.zip"
    std::string sha256;      // hex digest of the zip
    std::string url;         // direct download URL on the release
    std::size_t size = 0;    // bytes
};

struct CheatManifest {
    std::string                       version;   // foyer-cheats release tag
    std::string                       upstream;  // libretro-database tag string
    std::vector<CheatManifestEntry>   packs;
};

// Download + parse the manifest. Returns an empty manifest on any
// error (network / parse / schema). Caller checks `packs.empty()`.
CheatManifest fetch_cheat_manifest(const std::string& manifest_url);

enum class CheatInstallAction {
    Skipped,    // already at the manifest's version
    Installed,  // wasn't on disk, downloaded fresh
    Updated,    // version mismatch — replaced
    Failed,     // network / write / unzip error
};

struct CheatInstallProgress {
    int                  index = 0;
    int                  total = 0;
    std::string          name;
    CheatInstallAction   action = CheatInstallAction::Skipped;
};

struct CheatInstallTotals {
    int installed = 0;
    int updated   = 0;
    int skipped   = 0;
    int failed    = 0;
};

// Install / update every pack in `manifest` to /foyer/cheats/<system>/.
// Each .zip is downloaded to a temporary path, unzipped, and stamped
// with a `.version` sidecar so future runs can detect updates.
// `only_pack`, when non-empty, restricts work to that single system
// folder (the per-row Install action). `force` re-downloads even when
// the version sidecar matches.
CheatInstallTotals install_cheats(
    const CheatManifest& manifest,
    std::function<void(const CheatInstallProgress&)> progress = {},
    std::string_view only_pack = {},
    bool force = false,
    foyer::net::CancelHook cancel = {});

// Read the version sidecar foyer wrote at install time. Empty string
// = pack never installed via this code path (or sidecar deleted).
std::string installed_cheat_version(std::string_view pack_name);

} // namespace foyer::library
