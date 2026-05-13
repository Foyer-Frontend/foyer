#include "update_check.hpp"

#include "install_queue.hpp"
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
bool copy_file(const char* from, const char* to) {
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
        "Update downloaded. Restart foyer now to install it?");
    dlg->addButton("Later", []() {});
    dlg->addButton("Restart", []() {
        // Tear down every long-lived RepeatingTask/Timer BEFORE
        // envSetNextLoad + Application::quit. brls's quit drain
        // otherwise lets these tick into already-freed Theme /
        // Application state.
        ::foyer::browser::install_queue::stop();
        ::foyer::browser::theme_watcher::stop();

        const std::string& staged =
            ::foyer::browser::self_update::nro_new_path();
        const std::string& canonical =
            ::foyer::browser::self_update::nro_path();

        // Promote the staged .new into the canonical .nro IN
        // PROCESS, then chain-launch the canonical path. Avoids the
        // prior approach of chain-launching the .new file directly,
        // which left the .new sentinel around — when the .new boot
        // didn't make it through apply_staged_if_present (crash,
        // power loss, hbloader path quirk), the user kept booting
        // the OLD .nro because the canonical file was never
        // promoted. Doing the byte-copy here means the next boot
        // reads the new build from the canonical path even if
        // anything later in this restart chain fails.
        bool promoted = false;
        if (!staged.empty() && !canonical.empty()) {
            struct stat st{};
            if (::stat(staged.c_str(), &st) == 0 && st.st_size > 1024 * 1024) {
                // Try fopen("wb") on the canonical path directly.
                // HOS may report the running NRO as in-use, in which
                // case the first fopen returns NULL — that path
                // matched the user-reported "manual rename still
                // needed" symptom on v0.6.53. Strategy now:
                //   1. unlink canonical first (FAT allows even on
                //      mapped files since hbloader closed the fd
                //      after reading; only the directory entry is
                //      removed, the in-memory pages stay valid).
                //   2. fopen("wb") creates a fresh canonical file
                //      with the .new bytes. The running process's
                //      mapping is unaffected; it lives until quit.
                // If unlink fails we still try copy_file as before
                // for a partial chance at success on filesystems
                // that allow truncate-in-place.
                if (::unlink(canonical.c_str()) != 0) {
                    foyer::log::write(
                        "[update] pre-promote unlink(%s) failed errno=%d "
                        "— attempting truncate-in-place\n",
                        canonical.c_str(), errno);
                }
                promoted = copy_file(staged.c_str(), canonical.c_str());
                if (promoted) {
                    foyer::log::write(
                        "[update] promoted staged nro -> %s (%lld bytes)\n",
                        canonical.c_str(), (long long)st.st_size);
                    // Best-effort sentinel removal; FAT may
                    // refuse if the running process still has
                    // the .new mapped, in which case the next
                    // boot's apply_staged_if_present hits the
                    // size-floor check and unlinks it.
                    if (::unlink(staged.c_str()) != 0) {
                        foyer::log::write(
                            "[update] unlink(%s) failed errno=%d "
                            "(will retry on next boot)\n",
                            staged.c_str(), errno);
                    }
                }
            }
        }

        const std::string& target =
            promoted ? canonical
                     : (!staged.empty() ? staged : canonical);
        foyer::log::write(
            "[update] restart accepted — envSetNextLoad(%s) promoted=%d\n",
            target.c_str(), (int)promoted);
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
