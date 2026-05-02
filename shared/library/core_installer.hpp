#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace foyer::library {

// One row out of foyer-cores's release manifest.json. Mirror the writer in
// foyer-cores/.github/workflows/build-cores.yml.
struct CoreManifestEntry {
    std::string name;        // e.g. "fceumm"
    std::string version;     // e.g. "0.2.0"
    std::string nro;         // filename, e.g. "foyer-fceumm.nro"
    std::string sha256;      // hex digest reported by the workflow
    std::string url;         // direct download URL on the release
    std::size_t size = 0;    // bytes
};

struct CoreManifest {
    std::string version;     // overall release version
    std::vector<CoreManifestEntry> cores;
};

// Download + parse the manifest. Returns an empty manifest on any error
// (network, parse, schema). Callers check `cores.empty()`.
CoreManifest fetch_manifest(const std::string& manifest_url);

// Per-core install outcome reported to the progress callback.
enum class InstallAction {
    Skipped,    // already present at expected size
    Installed,  // wasn't on disk, downloaded fresh
    Updated,    // size mismatch — replaced
    Failed,     // network or write error
};

struct InstallProgress {
    int           index = 0;     // 1-based, including skips
    int           total = 0;
    std::string   name;          // core name, e.g. "fceumm"
    InstallAction action = InstallAction::Skipped;
};

struct InstallTotals {
    int installed = 0;
    int updated   = 0;
    int skipped   = 0;
    int failed    = 0;
};

// Install/update every core in `manifest` to /foyer/cores/<nro>. Existing
// files that already match the manifest size are skipped. `progress` (if
// set) is invoked once per core after each one resolves.
//
// Network IO is synchronous; expect this to take many seconds per core.
// Caller is responsible for pumping the UI loop between callbacks if it
// wants live progress.
InstallTotals install_cores(
    const CoreManifest& manifest,
    std::function<void(const InstallProgress&)> progress = {});

} // namespace foyer::library
