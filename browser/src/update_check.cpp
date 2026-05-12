#include "update_check.hpp"

#include "manifest_cache.hpp"
#include "self_update.hpp"

#include "library/config.hpp"
#include "library/foyer_update_job.hpp"
#include "library/foyer_updater.hpp"
#include "library/updates.hpp"
#include "library/worker.hpp"
#include "platform/log.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <memory>
#include <string>

namespace foyer::browser::update_check {

namespace {

std::unique_ptr<::foyer::library::FoyerUpdateJob> g_job;
std::unique_ptr<::foyer::library::Worker>         g_aux_job;
brls::RepeatingTimer*                             g_poll = nullptr;
brls::RepeatingTimer*                             g_aux_poll = nullptr;
bool                                              g_verbose = false;
Section                                           g_aux_section = Section::Cores;

// brls::RepeatingTimer::onUpdate writes `this->progress = 0` AFTER
// invoking the callback. Deleting the timer from inside its own
// callback frees `this` mid-execution and that trailing write
// faults. We defer the delete via brls::sync so onUpdate completes
// on a still-live object.
void cleanup_poll() {
    if (!g_poll) return;
    auto* dead = g_poll;
    g_poll = nullptr;
    dead->stop();
    brls::sync([dead]() { delete dead; });
}

void cleanup_aux_poll() {
    if (!g_aux_poll) return;
    auto* dead = g_aux_poll;
    g_aux_poll = nullptr;
    dead->stop();
    brls::sync([dead]() { delete dead; });
}

void prompt_restart() {
    auto* dlg = new brls::Dialog(
        "Update downloaded. Restart foyer now to install it?");
    dlg->addButton("Later", []() {});
    dlg->addButton("Restart", []() {
        // Chain-launch the staged .new file directly. FAT/exFAT
        // rename of a still-mapped running .nro intermittently
        // hits EEXIST even after unlink+rename (filesystem
        // cache lag), so the prior strategy of rename-then-
        // chain-launch silently fell back to the OLD canonical
        // nro on those failures.
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

void watch_download() {
    if (g_poll) cleanup_poll();
    g_poll = new brls::RepeatingTimer();
    g_poll->setPeriod(500);
    g_poll->setCallback([]() {
        if (!g_job || !g_job->done()) return;
        g_job->finish();
        const std::string ver = g_job->downloaded_version();
        cleanup_poll();
        if (!ver.empty()) {
            brls::Application::notify("Update v" + ver + " ready");
            prompt_restart();
        } else {
            brls::Application::notify("Update download failed");
        }
        g_job.reset();
    });
    g_poll->start();
}

void prompt_download(const std::string& version) {
    auto* dlg = new brls::Dialog(
        "Update available — v" + version
        + ".\n\nDownload now? Foyer will install it on next launch.");
    dlg->addButton("Cancel", []() {});
    dlg->addButton("Download", [version]() {
        g_job = std::make_unique<::foyer::library::FoyerUpdateJob>();
        g_job->start_check_and_download(
            ::foyer::library::config().foyer_manifest_url,
            FOYER_VERSION,
            ::foyer::browser::self_update::nro_path());
        brls::Application::notify("Downloading update v" + version + "…");
        watch_download();
    });
    dlg->open();
}

const char* section_label(Section s) {
    switch (s) {
        case Section::Cores:  return "cores";
        case Section::Bezels: return "bezels";
        case Section::Cheats: return "cheats";
    }
    return "?";
}

}  // namespace

bool kick(bool verbose) {
    if (g_job && g_job->active() && !g_job->done()) return false;
    if (g_job) {
        if (g_job->done()) g_job->finish();
        g_job.reset();
    }

    g_verbose = verbose;
    g_job = std::make_unique<::foyer::library::FoyerUpdateJob>();
    if (!g_job->start_check(::foyer::library::config().foyer_manifest_url)) {
        if (verbose) brls::Application::notify("Update check failed to start");
        g_job.reset();
        return false;
    }
    if (verbose) brls::Application::notify("Checking for foyer updates…");
    foyer::log::write("[update] foyer check kicked (verbose=%d)\n", (int)verbose);

    if (g_poll) cleanup_poll();
    g_poll = new brls::RepeatingTimer();
    g_poll->setPeriod(500);
    g_poll->setCallback([]() {
        if (!g_job || !g_job->done()) return;
        g_job->finish();
        cleanup_poll();
        if (!g_job->manifest_ok()) {
            if (g_verbose) brls::Application::notify("Update check failed");
            g_job.reset();
            return;
        }
        const auto& mf = g_job->manifest();
        const bool newer =
            ::foyer::library::is_newer_version(FOYER_VERSION, mf.version);
        const std::string ver = mf.version;
        g_job.reset();
        if (newer) {
            prompt_download(ver);
        } else if (g_verbose) {
            brls::Application::notify(
                "Foyer up to date (v" + std::string(FOYER_VERSION) + ")");
        }
    });
    g_poll->start();
    return true;
}

bool kick_content(Section s) {
    if (g_aux_job && g_aux_job->active() && !g_aux_job->done()) return false;
    if (g_aux_job) {
        if (g_aux_job->done()) g_aux_job->finish();
        g_aux_job.reset();
    }

    g_aux_section = s;
    const char* lbl = section_label(s);

    g_aux_job = std::make_unique<::foyer::library::Worker>();
    g_aux_job->start([s](::foyer::library::Worker& w) {
        const auto& cfg = ::foyer::library::config();
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
        (void)cfg;
    });

    brls::Application::notify(std::string("Checking for ") + lbl + " updates…");
    foyer::log::write("[update] %s content check kicked\n", lbl);

    if (g_aux_poll) cleanup_aux_poll();
    g_aux_poll = new brls::RepeatingTimer();
    g_aux_poll->setPeriod(500);
    g_aux_poll->setCallback([]() {
        if (!g_aux_job || !g_aux_job->done()) return;
        g_aux_job->finish();
        cleanup_aux_poll();

        ::foyer::library::FoyerManifest empty_foyer{};
        const auto buckets = ::foyer::library::compute_pending_updates(
            empty_foyer,
            FOYER_VERSION,
            ::foyer::browser::manifest_cache::cores(),
            ::foyer::browser::manifest_cache::bezels(),
            ::foyer::browser::manifest_cache::cheats());
        g_aux_job.reset();

        std::size_t n = 0;
        switch (g_aux_section) {
            case Section::Cores:  n = buckets.cores.size();  break;
            case Section::Bezels: n = buckets.bezels.size(); break;
            case Section::Cheats: n = buckets.cheats.size(); break;
        }
        const char* lbl = section_label(g_aux_section);
        if (n == 0) {
            brls::Application::notify(std::string("All ") + lbl + " up to date");
        } else {
            brls::Application::notify(
                std::to_string(n) + " " + lbl + " update"
                + (n == 1 ? "" : "s") + " available");
        }
    });
    g_aux_poll->start();
    return true;
}

void stop() {
    cleanup_poll();
    cleanup_aux_poll();
    if (g_job)     (void)g_job.release();
    if (g_aux_job) (void)g_aux_job.release();
}

}  // namespace foyer::browser::update_check
