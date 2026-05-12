#include "update_check.hpp"

#include "install_queue.hpp"
#include "manifest_cache.hpp"
#include "self_update.hpp"

#include "library/config.hpp"
#include "library/foyer_updater.hpp"
#include "library/updates.hpp"
#include "library/worker.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <memory>
#include <string>

namespace foyer::browser::update_check {

namespace {

const char* section_label(Section s) {
    switch (s) {
        case Section::Cores:  return "cores";
        case Section::Bezels: return "bezels";
        case Section::Cheats: return "cheats";
    }
    return "?";
}

void prompt_restart() {
    auto* dlg = new brls::Dialog(
        "Update downloaded. Restart foyer now to install it?");
    dlg->addButton("Later", []() {});
    dlg->addButton("Restart", []() {
        // Chain-launch the staged .new file directly. FAT/exFAT
        // rename of a still-mapped running .nro intermittently
        // hits EEXIST even after unlink+rename, so we use the
        // .new path explicitly here.
        const std::string& staged =
            ::foyer::browser::self_update::nro_new_path();
        const std::string& canonical =
            ::foyer::browser::self_update::nro_path();
        const std::string& target = !staged.empty() ? staged : canonical;
        if (R_FAILED(envSetNextLoad(target.c_str(), target.c_str()))) {
            foyer::log::write(
                "[update] envSetNextLoad(%s) failed; quitting to "
                "homebrew menu — user must relaunch foyer manually\n",
                target.c_str());
        }
        brls::Application::quit();
    });
    dlg->open();
}

// Enqueue the actual nro download on install_queue. The job body
// streams the .nro to <path>.new and, on success, brls::sync's the
// "restart now?" prompt onto the UI thread.
void enqueue_download(const ::foyer::library::FoyerManifest mf) {
    const std::string version = mf.version;
    ::foyer::browser::install_queue::enqueue(
        "foyer " + version,
        [mf](::foyer::library::Worker& w) {
            w.set_status("Downloading foyer " + mf.version + "…");
            const std::string& nro_path =
                ::foyer::browser::self_update::nro_path();
            const bool ok = ::foyer::library::download_foyer_update(
                mf, nro_path,
                [&w]{ return w.cancelled(); });
            const std::string version = mf.version;
            brls::sync([ok, version]() {
                if (ok) {
                    brls::Application::notify("Update v" + version + " ready");
                    prompt_restart();
                } else {
                    brls::Application::notify("Update download failed");
                }
            });
        });
}

void prompt_download(const std::string& version,
                     const ::foyer::library::FoyerManifest& mf) {
    auto* dlg = new brls::Dialog(
        "Update available — v" + version
        + ".\n\nDownload now? Foyer will install it on next launch.");
    dlg->addButton("Cancel", []() {});
    dlg->addButton("Download", [mf]() {
        enqueue_download(mf);
    });
    dlg->open();
}

}  // namespace

bool kick(bool verbose) {
    const std::string url = ::foyer::library::config().foyer_manifest_url;
    foyer::log::write("[update] foyer check enqueued (verbose=%d)\n",
        (int)verbose);
    ::foyer::browser::install_queue::enqueue(
        "Check foyer update",
        [url, verbose](::foyer::library::Worker& w) {
            w.set_status("Fetching foyer manifest…");
            const auto mf = ::foyer::library::fetch_foyer_manifest(url);
            const bool ok = !mf.version.empty();
            const bool newer = ok && ::foyer::library::is_newer_version(
                FOYER_VERSION, mf.version);
            brls::sync([ok, newer, verbose, mf]() {
                if (!ok) {
                    if (verbose) brls::Application::notify("Update check failed");
                    return;
                }
                if (newer) {
                    prompt_download(mf.version, mf);
                } else if (verbose) {
                    brls::Application::notify(
                        "Foyer up to date (v" + std::string(FOYER_VERSION) + ")");
                }
            });
        });
    return true;
}

bool kick_content(Section s) {
    const char* lbl = section_label(s);
    foyer::log::write("[update] %s content check enqueued\n", lbl);
    ::foyer::browser::install_queue::enqueue(
        std::string("Check ") + lbl + " updates",
        [s, lbl](::foyer::library::Worker& w) {
            switch (s) {
                case Section::Cores:
                    w.set_status("Refreshing cores manifest…");
                    ::foyer::browser::manifest_cache::prefetch_cores();
                    break;
                case Section::Bezels:
                    w.set_status("Refreshing bezels manifest…");
                    ::foyer::browser::manifest_cache::prefetch_bezels();
                    break;
                case Section::Cheats:
                    w.set_status("Refreshing cheats manifest…");
                    ::foyer::browser::manifest_cache::prefetch_cheats();
                    break;
            }
            // Aggregate fresh manifest vs installed sidecars + toast.
            brls::sync([s, lbl]() {
                ::foyer::library::FoyerManifest empty_foyer{};
                const auto buckets = ::foyer::library::compute_pending_updates(
                    empty_foyer,
                    FOYER_VERSION,
                    ::foyer::browser::manifest_cache::cores(),
                    ::foyer::browser::manifest_cache::bezels(),
                    ::foyer::browser::manifest_cache::cheats());
                std::size_t n = 0;
                switch (s) {
                    case Section::Cores:  n = buckets.cores.size();  break;
                    case Section::Bezels: n = buckets.bezels.size(); break;
                    case Section::Cheats: n = buckets.cheats.size(); break;
                }
                if (n == 0) {
                    brls::Application::notify(
                        std::string("All ") + lbl + " up to date");
                } else {
                    brls::Application::notify(
                        std::to_string(n) + " " + lbl + " update"
                        + (n == 1 ? "" : "s") + " available");
                }
            });
        });
    return true;
}

void stop() {
    // install_queue::stop() is invoked by HomeActivity's quit drain
    // and clears every pending update_check job along with the rest
    // of the queue. Nothing else for this module to tear down.
}

}  // namespace foyer::browser::update_check
