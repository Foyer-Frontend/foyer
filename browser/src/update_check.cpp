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
#include <memory>
#include <string>

namespace foyer::browser::update_check {

namespace {

std::unique_ptr<::foyer::library::FoyerUpdateJob> g_job;
std::unique_ptr<::foyer::library::Worker>         g_aux_job;
brls::RepeatingTimer*                             g_poll = nullptr;
brls::RepeatingTimer*                             g_aux_poll = nullptr;
bool                                              g_verbose = false;

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

// Verbose-only second pass: refresh the cores/bezels/shaders/cheats
// manifests in case they advanced since boot, then run the pending-
// updates aggregator over installed sidecars. Toasts a summary —
// boot path stays silent and never reaches this.
void kick_aux_check() {
    if (g_aux_job && g_aux_job->active() && !g_aux_job->done()) return;
    if (g_aux_job) {
        if (g_aux_job->done()) g_aux_job->finish();
        g_aux_job.reset();
    }

    g_aux_job = std::make_unique<::foyer::library::Worker>();
    g_aux_job->start([](::foyer::library::Worker& w) {
        w.set_status("Refreshing manifests…");
        ::foyer::browser::manifest_cache::prefetch();
    });

    if (g_aux_poll) cleanup_aux_poll();
    g_aux_poll = new brls::RepeatingTimer();
    g_aux_poll->setPeriod(500);
    g_aux_poll->setCallback([]() {
        if (!g_aux_job || !g_aux_job->done()) return;
        g_aux_job->finish();
        cleanup_aux_poll();

        // Aggregate over freshly-fetched manifests + on-disk sidecars.
        ::foyer::library::FoyerManifest empty_foyer{};
        const auto buckets = ::foyer::library::compute_pending_updates(
            empty_foyer,
            FOYER_VERSION,
            ::foyer::browser::manifest_cache::cores(),
            ::foyer::browser::manifest_cache::bezels(),
            ::foyer::browser::manifest_cache::cheats());
        g_aux_job.reset();

        const std::size_t c = buckets.cores.size();
        const std::size_t b = buckets.bezels.size();
        const std::size_t k = buckets.cheats.size();
        if (c == 0 && b == 0 && k == 0) {
            brls::Application::notify("Content up to date");
        } else {
            brls::Application::notify(
                "Updates — cores " + std::to_string(c)
                + ", bezels " + std::to_string(b)
                + ", cheats " + std::to_string(k));
        }
    });
    g_aux_poll->start();
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
    if (verbose) brls::Application::notify("Checking for updates…");
    foyer::log::write("[update] check kicked (verbose=%d)\n", (int)verbose);

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
        // Verbose path also surveys aux content (cores/bezels/cheats)
        // so the user gets a full picture from one button. Boot path
        // stays silent — content updates aren't urgent enough to
        // warrant a launch-time prompt.
        if (g_verbose) kick_aux_check();
    });
    g_poll->start();
    return true;
}

void stop() {
    cleanup_poll();
    cleanup_aux_poll();
    if (g_job)     (void)g_job.release();
    if (g_aux_job) (void)g_aux_job.release();
}

}  // namespace foyer::browser::update_check
