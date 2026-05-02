#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace foyer::library {

// One row out of foyer's release manifest.json. Mirrors the writer in
// foyer/.github/workflows/release.yml.
struct FoyerManifest {
    std::string version;     // semver, e.g. "0.2.2"
    std::string url;         // direct download for foyer.nro
    std::string sha256;      // expected hex digest
    std::size_t size = 0;    // bytes
};

// Download + parse the manifest. Returns an empty manifest on any error;
// caller checks `version.empty()`.
FoyerManifest fetch_foyer_manifest(const std::string& url);

// Numeric semver comparison. Returns true iff `candidate` (e.g. "0.2.2")
// is strictly newer than `current` (e.g. "0.2.1").
bool is_newer_version(std::string_view current, std::string_view candidate);

// Streams the new nro to <nro_path>.new (atomic via foyer::net::get_to_file).
// Caller is responsible for prompting the user to relaunch foyer afterwards;
// the boot-time check in main() does the actual rename.
bool download_foyer_update(const FoyerManifest& m, const std::string& nro_path);

} // namespace foyer::library
