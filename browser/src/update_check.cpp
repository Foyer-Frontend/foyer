#include "update_check.hpp"

#include "self_update.hpp"

#include "library/config.hpp"
#include "library/foyer_update_job.hpp"
#include "library/foyer_updater.hpp"
#include "platform/log.hpp"

#include <borealis.hpp>
#include <memory>

namespace foyer::browser::update_check {

namespace {

std::unique_ptr<::foyer::library::FoyerUpdateJob> g_job;
brls::RepeatingTimer*                             g_poll = nullptr;
bool                                              g_verbose = false;

void cleanup_poll() {
    if (g_poll) {
        g_poll->stop();
        delete g_poll;
        g_poll = nullptr;
    }
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

// Second poll cycle — watch the download job, prompt restart on
// success.
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
                "Up to date (v" + std::string(FOYER_VERSION) + ")");
        }
    });
    g_poll->start();
    return true;
}

void stop() {
    cleanup_poll();
    // Don't join the worker thread — see SystemActivity::cancel_pending_scrape
    // for why blocking on shutdown looks like a HOS crash.
    if (g_job) (void)g_job.release();
}

}  // namespace foyer::browser::update_check
