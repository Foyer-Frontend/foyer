#pragma once

#include "net/http.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace foyer::library {

// One row out of foyer-bezels's release manifest.json. Mirrors the
// writer in foyer-bezels/.github/workflows/build-bezels.yml. The
// manifest also records which libretro/common-overlays commit each
// pack was sliced from.
struct BezelManifestEntry {
    std::string name;        // foyer system folder, e.g. "nes"
    std::string version;     // first 7 chars of the zip's sha256
    std::string zip;         // filename, e.g. "nes.zip"
    std::string sha256;      // hex digest of the zip
    std::string url;         // direct download URL on the release
    std::size_t size = 0;    // bytes
};

struct BezelManifest {
    std::string                       version;   // foyer-bezels release tag
    std::string                       upstream;  // libretro/common-overlays@<sha>
    std::vector<BezelManifestEntry>   packs;
};

BezelManifest fetch_bezel_manifest(const std::string& manifest_url);

enum class BezelInstallAction {
    Skipped,
    Installed,
    Updated,
    Failed,
};

struct BezelInstallProgress {
    int                  index = 0;
    int                  total = 0;
    std::string          name;
    BezelInstallAction   action = BezelInstallAction::Skipped;
};

struct BezelInstallTotals {
    int installed = 0;
    int updated   = 0;
    int skipped   = 0;
    int failed    = 0;
};

// Install / update every pack in `manifest`. Each zip ships with a
// flat `<system>.png` entry that lands at `/foyer/bezels/<system>.png`.
// Version sidecars are stamped at `/foyer/bezels/.<system>.version`
// — tucked under the bezels root rather than alongside the PNG so
// users browsing /foyer/bezels/ don't see the bookkeeping files.
BezelInstallTotals install_bezels(
    const BezelManifest& manifest,
    std::function<void(const BezelInstallProgress&)> progress = {},
    std::string_view only_pack = {},
    bool force = false,
    foyer::net::CancelHook cancel = {});

std::string installed_bezel_version(std::string_view pack_name);

} // namespace foyer::library
