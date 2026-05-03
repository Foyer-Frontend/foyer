#pragma once

#include "core_installer.hpp"
#include "worker.hpp"

#include <string>

namespace foyer::library {

// Background install. The worker (a) fetches the manifest via curl,
// (b) optionally narrows it to a single core, (c) downloads each
// missing/outdated nro into /foyer/cores. UI keeps drawing throughout.
//
// Lifecycle is the same as plain Worker (start/active/done/finish),
// plus cancel() to abort an in-flight curl transfer cleanly.
class CoreInstallJob {
public:
    bool active()    const { return m_worker.active();    }
    bool done()      const { return m_worker.done();      }
    bool cancelled() const { return m_worker.cancelled(); }

    // Spawn the worker. The caller hands in a pre-fetched manifest
    // (do the manifest fetch on the main thread — a 7 KB JSON pull
    // takes ~1 s and worker-side fetches hit a libcurl-on-Switch
    // quirk where the 3rd consecutive worker's curl_easy_perform
    // can hang for ~90 s). If `only_core` is non-empty, the
    // manifest is narrowed to that single entry. `force`
    // re-downloads even when the version sidecar matches the
    // manifest's version (Re-install path).
    bool start(CoreManifest manifest, std::string only_core, bool force);

    // UI cancel signal. Curl aborts at the next progress callback
    // (~200ms granularity).
    void cancel() { m_worker.cancel(); }

    // Latest progress text the worker has published.
    std::string status_snapshot() const { return m_worker.status_snapshot(); }

    // Join the thread; clears the slot. Returns the totals the worker
    // accumulated (zeros if cancelled before any core finished).
    InstallTotals finish();

private:
    Worker         m_worker;
    InstallTotals  m_totals;
};

} // namespace foyer::library
