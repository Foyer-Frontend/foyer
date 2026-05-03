#pragma once

#include "foyer_updater.hpp"
#include "worker.hpp"

#include <string>

namespace foyer::library {

// Background helper for the foyer self-update flow. Two modes:
//   * Check only — fetch foyer-manifest.json off-thread; result lands
//     in `manifest()`. Used by the boot-time and Settings-button
//     check paths so the UI stays responsive even on a slow CDN.
//   * Check + download — fetch the manifest, and if it advertises a
//     newer version than `current_version`, stream the .nro to
//     `<nro_path>.new` so the next boot atomically swaps it in.
//
// Both modes set the worker's status string ("Checking...",
// "Downloading 17%...", etc.) so the UI banner can mirror it.
class FoyerUpdateJob {
public:
    bool active()    const { return m_worker.active();    }
    bool done()      const { return m_worker.done();      }
    bool cancelled() const { return m_worker.cancelled(); }

    // Manifest-fetch only. Sets manifest() on success.
    bool start_check(std::string manifest_url);

    // Manifest fetch + conditional download. Sets manifest() on
    // success; sets downloaded_version() on a successful staged
    // download.
    bool start_check_and_download(std::string manifest_url,
                                  std::string current_version,
                                  std::string nro_path);

    void cancel() { m_worker.cancel(); }
    std::string status_snapshot() const { return m_worker.status_snapshot(); }

    // Read after done() flips and finish() joins.
    const FoyerManifest& manifest()           const { return m_manifest; }
    const std::string&   downloaded_version() const { return m_downloaded; }
    bool                 manifest_ok()        const { return m_manifest_ok; }

    void finish();

private:
    Worker         m_worker;
    FoyerManifest  m_manifest;
    std::string    m_downloaded;
    bool           m_manifest_ok = false;
};

} // namespace foyer::library
