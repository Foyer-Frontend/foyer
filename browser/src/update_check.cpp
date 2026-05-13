#include "update_check.hpp"

#include "install_queue.hpp"
#include "mtp.hpp"
#include "manifest_cache.hpp"
#include "self_update.hpp"
#include "theme_watcher.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

// Copy bytes from one file to another. fopen("wb") truncates the
// destination in place, which is what we want even when the
// destination is the running .nro on FAT/exFAT (rename hits EEXIST
// and the unlink-then-rename fallback was destructive — the
// running process keeps a mapping to the old bytes until exit, but
// the file ON DISK gets the new contents and next boot reads
// those).
[[maybe_unused]] bool copy_file(const char* from, const char* to) {
    FILE* src = std::fopen(from, "rb");
    if (!src) {
        foyer::log::write(
            "[update] copy: fopen(%s, rb) failed errno=%d\n", from, errno);
        return false;
    }
    FILE* dst = std::fopen(to, "wb");
    if (!dst) {
        foyer::log::write(
            "[update] copy: fopen(%s, wb) failed errno=%d\n", to, errno);
        std::fclose(src);
        return false;
    }
    std::vector<unsigned char> buf(64 * 1024);
    bool ok = true;
    for (;;) {
        const std::size_t n = std::fread(buf.data(), 1, buf.size(), src);
        if (n == 0) break;
        if (std::fwrite(buf.data(), 1, n, dst) != n) {
            foyer::log::write(
                "[update] copy: fwrite short on %s\n", to);
            ok = false;
            break;
        }
    }
    std::fclose(dst);
    std::fclose(src);
    return ok;
}

void prompt_restart() {
    auto* dlg = new brls::Dialog(
        "Update downloaded. Restart foyer now?");
    dlg->addButton("Later", []() {});
    dlg->addButton("Restart", []() {
        // Tear down every long-lived RepeatingTask/Timer BEFORE
        // we touch the filesystem or chain-launch. brls's quit
        // drain otherwise lets these tick into already-freed
        // Theme / Application state.
        ::foyer::browser::install_queue::stop();
        ::foyer::browser::theme_watcher::stop();
        // libhaze (MTP) holds USB DMA buffers in the heap region
        // that hbloader needs to unmap on chain-launch; without
        // this stop, the unmap fails with MAKERESULT(347, 26).
        // Same bisection result as launch_game in launch.cpp.
        if (::foyer::browser::mtp_running()) {
            ::foyer::browser::mtp_stop();
        }

        // Promotion pattern lifted from switchfin's updater
        // (dragonflylee/switchfin@app/src/utils/version.cpp).
        // HOS won't let us unlink/rename foyer.nro while it's
        // mapped — the open romfs fd inside our own process
        // keeps the file locked. romfsExit() releases that fd;
        // the running code stays valid in memory but the file
        // on disk becomes unlink-able. THEN we
        // remove(canonical) + rename(.new -> canonical).
        // After that, envSetNextLoad to the canonical path so
        // hbloader chain-launches the same path it was already
        // running from (same-path chain-launch avoids any
        // hbloader unmap/remap transition for the next NRO load).
        ::foyer::browser::self_update::apply_staged_if_present();

        // Chain-launch the freshly-renamed foyer.nro. The v0.6.62/63
        // crash that forced us to drop envSetNextLoad was the
        // hbloader unmap rejection for 54 MB foyer.nro — that's
        // gone in v0.6.66 (asset pack moved out → nro is 8 MB,
        // well under any unmap threshold), so the chain-launch
        // path is safe again. argv[0] is the canonical sdmc:
        // path so hbloader loads exactly the file we just renamed.
        const std::string sd_self =
            std::string{"sdmc:"} + ::foyer::browser::self_update::nro_path();
        char argv[512];
        std::snprintf(argv, sizeof(argv), "\"%s\"", sd_self.c_str());
        if (R_FAILED(envSetNextLoad(sd_self.c_str(), argv))) {
            foyer::log::write(
                "[update] envSetNextLoad failed — quitting to "
                "the forwarder; relaunch manually\n");
        } else {
            foyer::log::write(
                "[update] chain-launching new foyer at %s\n",
                sd_self.c_str());
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
