#include "foyer_update_job.hpp"

#include <utility>

namespace foyer::library {

bool FoyerUpdateJob::start_check(std::string manifest_url) {
    return m_worker.start(
        [this, url = std::move(manifest_url)](Worker& w) {
            w.set_status("Checking for updates...");
            m_manifest    = fetch_foyer_manifest(url);
            m_manifest_ok = !m_manifest.version.empty();
            m_downloaded.clear();
            w.set_status(m_manifest_ok ? "Update check done"
                                       : "Manifest fetch failed");
        });
}

bool FoyerUpdateJob::start_check_and_download(std::string manifest_url,
                                              std::string current_version,
                                              std::string nro_path) {
    return m_worker.start(
        [this, url = std::move(manifest_url),
               cur = std::move(current_version),
               path = std::move(nro_path)](Worker& w) {
            w.set_status("Checking for updates...");
            m_manifest    = fetch_foyer_manifest(url);
            m_manifest_ok = !m_manifest.version.empty();
            m_downloaded.clear();
            if (!m_manifest_ok) {
                w.set_status("Manifest fetch failed");
                return;
            }
            if (w.cancelled())                          { w.set_status("Cancelled"); return; }
            if (!is_newer_version(cur, m_manifest.version)) {
                w.set_status("Foyer is up to date");
                return;
            }
            w.set_status("Downloading foyer v" + m_manifest.version + "...");
            const bool ok = download_foyer_update(m_manifest, path,
                /*cancel=*/[&w]{ return w.cancelled(); });
            if (ok) {
                m_downloaded = m_manifest.version;
                w.set_status("Update v" + m_manifest.version
                           + " ready - restart foyer");
            } else {
                w.set_status(w.cancelled() ? "Cancelled"
                                           : "Update download failed");
            }
        });
}

void FoyerUpdateJob::finish() {
    m_worker.finish();
}

} // namespace foyer::library
