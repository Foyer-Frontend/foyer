#pragma once

#include "net/http.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace foyer::library {

// One row out of foyer-shaders's release manifest.json. Mirrors the
// writer in foyer-shaders/.github/workflows/build-shaders.yml.
struct ShaderManifestEntry {
    std::string name;        // e.g. "crt-aperture"
    std::string version;     // first 7 chars of the zip's sha256
    std::string zip;         // filename, e.g. "crt-aperture.zip"
    std::string sha256;      // hex digest of the zip
    std::string url;         // direct download URL on the release
    std::size_t size = 0;    // bytes
};

struct ShaderManifest {
    std::string                       version;  // overall release tag
    std::vector<ShaderManifestEntry>  presets;
};

// Download + parse the manifest. Returns an empty manifest on any
// error (network / parse / schema). Caller checks `presets.empty()`.
ShaderManifest fetch_shader_manifest(const std::string& manifest_url);

enum class ShaderInstallAction {
    Skipped,    // already at the manifest's version
    Installed,  // wasn't on disk, downloaded fresh
    Updated,    // version mismatch — replaced
    Failed,     // network / write / unzip error
};

struct ShaderInstallProgress {
    int                  index = 0;
    int                  total = 0;
    std::string          name;
    ShaderInstallAction  action = ShaderInstallAction::Skipped;
};

struct ShaderInstallTotals {
    int installed = 0;
    int updated   = 0;
    int skipped   = 0;
    int failed    = 0;
};

// Install / update every preset in `manifest` to /foyer/shaders/<name>/.
// Each .zip is downloaded to a temporary path, unzipped onto disk, and
// stamped with a `.version` sidecar so future runs can detect updates.
// `force` re-downloads even when the version sidecar matches (Re-install
// path, like core_installer).
ShaderInstallTotals install_shaders(
    const ShaderManifest& manifest,
    std::function<void(const ShaderInstallProgress&)> progress = {},
    bool force = false,
    foyer::net::CancelHook cancel = {});

// Read the version sidecar foyer wrote at install time. Empty string
// = preset never installed via this code path (or sidecar deleted).
std::string installed_shader_version(std::string_view preset_name);

} // namespace foyer::library
