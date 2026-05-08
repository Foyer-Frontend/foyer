#pragma once

#include "net/http.hpp"

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
    Started,    // about to download (skip-or-fetch decision still upcoming)
    Skipped,    // already present at the manifest's version
    Installed,  // wasn't on disk, downloaded fresh
    Updated,    // version mismatch — replaced
    Failed,     // network or write error
};

// Read the version sidecar (`<nro>.version`) we write next to each
// installed core. Empty string means: never installed via this code
// path, or the sidecar was deleted.
std::string installed_core_version(std::string_view core_nro);

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
// files at the manifest's version are skipped unless `force` is true
// (re-install path: redownload regardless of recorded version).
// `progress` (if set) is invoked once per core after each one resolves.
// `cancel`, if set, is polled by curl during each download — return true
// to abort the in-flight transfer; the loop also bails out between
// cores. The returned totals reflect what completed before the abort.
//
// Network IO is synchronous; this is intended to be invoked from a
// background worker (see CoreInstallJob).
InstallTotals install_cores(
    const CoreManifest& manifest,
    std::function<void(const InstallProgress&)> progress = {},
    bool force = false,
    foyer::net::CancelHook cancel = {});

} // namespace foyer::library
