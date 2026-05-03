#include "core_install_job.hpp"

#include "platform/log.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace foyer::library {

bool CoreInstallJob::start(CoreManifest manifest, std::string only_core,
                           bool force) {
    return m_worker.start(
        [this, manifest = std::move(manifest),
               only = std::move(only_core), force](Worker& w) mutable {
            foyer::log::write("[install_job] starting; %zu manifest cores; only=%s force=%d\n",
                manifest.cores.size(), only.c_str(), (int)force);
            if (manifest.cores.empty()) {
                w.set_status("Manifest is empty");
                return;
            }
            if (w.cancelled()) { w.set_status("Cancelled"); return; }
            if (!only.empty()) {
                std::erase_if(manifest.cores,
                    [&](const auto& c) { return c.name != only; });
                if (manifest.cores.empty()) {
                    w.set_status("Core not in manifest: " + only);
                    return;
                }
            }
            m_totals = install_cores(manifest,
                [&w](const InstallProgress& p) {
                    const char* verb =
                        p.action == InstallAction::Skipped   ? "skipped" :
                        p.action == InstallAction::Updated   ? "updated" :
                        p.action == InstallAction::Installed ? "installed"
                                                             : "FAILED";
                    char b[160];
                    std::snprintf(b, sizeof(b), "[%d/%d] %s - %s",
                        p.index, p.total, p.name.c_str(), verb);
                    w.set_status(b);
                }, force,
                /*cancel=*/[&w]{ return w.cancelled(); });
        });
}

InstallTotals CoreInstallJob::finish() {
    m_worker.finish();
    return m_totals;
}

} // namespace foyer::library
