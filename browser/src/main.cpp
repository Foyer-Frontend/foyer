// foyer browser entry.
//
// Scans /foyer/roms for known systems, drives the carousel + game list +
// detail + search + settings views, manages the background workers
// (manifest fetch, core install, scrape, foyer self-update, shader
// install), and chain-launches the matching foyer-<core>.nro player.

#include <switch.h>

#include "platform/app.hpp"
#include "platform/log.hpp"
#include "library/scanner.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/core_installer.hpp"
#include "library/foyer_updater.hpp"
#include "library/shader_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/bezel_installer.hpp"
#include "net/http.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/libretro_thumbnails.hpp"
#include "scrapers/screenscraper.hpp"
#include "scrapers/steamgriddb.hpp"
#include "scrapers/accounts.hpp"
#include "theme.hpp"
#include "views.hpp"
#include "launch.hpp"
#include "mtp.hpp"
#include "seed_assets.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace {

// Path to the running foyer.nro on the SD card. Hard-coded because nxlink
// builds and direct-launches both use this canonical install path.
constexpr const char* kFoyerNroPath    = "/switch/foyer/foyer.nro";
constexpr const char* kFoyerNroNewPath = "/switch/foyer/foyer.nro.new";

// If the previous run staged an update, swap it in before we boot the
// rest of the app. We're not the loaded-into-memory image; renaming the
// SD-side file is safe — the next launch picks up the new bytes.
void apply_staged_update_if_present() {
    struct stat st{};
    if (::stat(kFoyerNroNewPath, &st) != 0) return;
    if (::rename(kFoyerNroNewPath, kFoyerNroPath) != 0) {
        foyer::log::write("[foyer_update] rename of staged nro failed\n");
        return;
    }
    foyer::log::write("[foyer_update] applied staged nro -> %s\n", kFoyerNroPath);
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    apply_staged_update_if_present();

    foyer::platform::App app;

    // Seed bundled bezels + cheats from romfs into the SD tree on
    // first boot (and every boot a NEW system / cheat ships in the
    // browser's romfs). User-installed files are never overwritten.
    foyer::browser::seed_assets_if_missing();

    // Initialise curl on the main thread BEFORE any worker spawns
    // one. curl_global_init must run single-threaded; if a worker
    // racing in start_check() does it first, Switch's socket layer
    // can leave curl in a state where every subsequent perform
    // hangs at "Fetching manifest...". Boot-time foyer manifest
    // check is the trigger.
    foyer::net::init();

    foyer::browser::load_theme(foyer::library::config().theme_name);

    // NOTE: temporarily skipping MTP autostart — libhaze appears to break
    // libnx's romfs devoptab so subsequent fopen("romfs:/...") returns
    // ENOSYS. Re-enable once we understand the interaction.
    // if (foyer::library::config().mtp_autostart) {
    //     foyer::browser::mtp_start();
    // }

    foyer::library::ScanOptions opts;
    opts.rom_root = foyer::library::config().rom_root;
    opts.recurse  = foyer::library::config().scan_subfolders;
    foyer::browser::Library lib;
    lib.systems = foyer::library::scan_library(opts);

    foyer::browser::State state;

    // One-shot self-update check on boot. Off-thread so the UI is up
    // immediately even on a slow CDN; the result is processed by the
    // foyer_job poll block in the main loop below.
    state.foyer_job.start_check(foyer::library::config().foyer_manifest_url);

    app.set_draw_fn([&](NVGcontext* vg, float w, float h) {
        foyer::browser::draw(vg, w, h, state, lib);
    });

    // Cache the most recently rendered MTP status so we only refresh
    // the banner when the haze callback actually changed the string.
    // Without this we'd thrash banner_ttl every frame and stomp on
    // unrelated banners (rescan toast, scrape progress, etc.).
    std::string last_mtp_status;

    while (app.tick()) {
        const auto held = padGetButtons(&app.pad());
        const auto down = padGetButtonsDown(&app.pad());

        foyer::browser::update(state, lib, held, down,
            app.touch(), (float)app.width(), (float)app.height());

        if (foyer::browser::mtp_running()) {
            auto s = foyer::browser::mtp_status();
            if (s != last_mtp_status && !s.empty()) {
                state.banner_text = s;
                state.banner_ttl  = 90;
                last_mtp_status   = std::move(s);
            }
        } else if (!last_mtp_status.empty()) {
            last_mtp_status.clear();
        }

        if (state.request_quit) {
            foyer::browser::mtp_stop();
            app.quit();
            continue;
        }

        if (state.request_rescan) {
            state.request_rescan = false;
            foyer::library::reload_config();
            opts.rom_root = foyer::library::config().rom_root;
            opts.recurse  = foyer::library::config().scan_subfolders;
            lib.systems = foyer::library::scan_library(opts);
            // Cursors might point past the end of the rescanned library.
            if (state.system_index >= lib.systems.size()) state.system_index = 0;
            state.game_index = 0;
            foyer::browser::invalidate_cover_cache(app.vg());
            state.banner_text = "Library rescanned";
            state.banner_ttl  = 120;
        }
        if (state.request_invalidate_covers) {
            state.request_invalidate_covers = false;
            foyer::browser::invalidate_cover_cache(app.vg());
        }

        // Settings -> Updates -> Check: spawn a fresh check job (only
        // if no foyer job is currently running).
        if (state.request_check_foyer_update && !state.foyer_job.active()) {
            state.request_check_foyer_update = false;
            state.foyer_job.start_check(
                foyer::library::config().foyer_manifest_url);
        } else if (state.request_check_foyer_update) {
            state.request_check_foyer_update = false; // drop, busy
        }

        // Settings -> Updates -> Update foyer (Yes confirmed). Same
        // job: it does the manifest check + download in one pass.
        if (state.request_install_foyer_update && !state.foyer_job.active()) {
            state.request_install_foyer_update = false;
            state.foyer_job.start_check_and_download(
                foyer::library::config().foyer_manifest_url,
                FOYER_VERSION,
                "/switch/foyer/foyer.nro");
        } else if (state.request_install_foyer_update) {
            state.request_install_foyer_update = false;
        }

        // Mirror foyer_job progress + finalise.
        if (state.foyer_job.active()) {
            const auto status = state.foyer_job.status_snapshot();
            if (!status.empty()) {
                state.banner_text = status;
                state.banner_ttl  = 60;
            }
            if (state.foyer_job.done()) {
                const auto& m = state.foyer_job.manifest();
                const auto downloaded = state.foyer_job.downloaded_version();
                state.foyer_job.finish();
                if (!m.version.empty() &&
                    foyer::library::is_newer_version(FOYER_VERSION, m.version)) {
                    state.foyer_update_available = true;
                    state.foyer_update_version   = m.version;
                    if (!downloaded.empty()) {
                        state.banner_text =
                            "Update v" + downloaded +
                            " downloaded - restart foyer to apply";
                        state.banner_ttl = 600;
                    } else {
                        state.banner_text =
                            "Foyer update available: v" + m.version
                            + " - Settings -> Updates -> Update foyer";
                        state.banner_ttl = 360;
                    }
                } else if (!m.version.empty()) {
                    state.foyer_update_available = false;
                    state.foyer_update_version.clear();
                    // Don't clobber a more interesting banner with
                    // "up to date" on the silent boot check.
                }
            }
        }

        if (state.request_refresh_manifest && !state.refresh_job.active()) {
            state.request_refresh_manifest = false;
            const std::string url = foyer::library::config().cores_manifest_url;
            state.refresh_job.start([&state, url](foyer::library::Worker& w) {
                w.set_status("Fetching cores manifest...");
                state.refresh_result = foyer::library::fetch_manifest(url);
            });
        } else if (state.request_refresh_manifest) {
            state.request_refresh_manifest = false;
        }
        if (state.refresh_job.active()) {
            const auto status = state.refresh_job.status_snapshot();
            if (!status.empty()) {
                state.banner_text = status;
                state.banner_ttl  = 60;
            }
            if (state.refresh_job.done()) {
                state.refresh_job.finish();
                if (state.refresh_result.cores.empty()) {
                    state.banner_text = "Manifest fetch failed - check log";
                    state.banner_ttl  = 240;
                } else {
                    char b[160];
                    std::snprintf(b, sizeof(b),
                        "Manifest %s - %zu cores available",
                        state.refresh_result.version.c_str(),
                        state.refresh_result.cores.size());
                    state.banner_text = b;
                    state.banner_ttl  = 240;
                    foyer::browser::set_manifest_cache(
                        std::move(state.refresh_result));
                    state.refresh_result = {};
                }
            }
        }

        // Manifest fetch runs on the main thread (~1 s on a typical
        // connection, 7 KB JSON) — worker-side fetches hit a libcurl
        // -on-Switch quirk where the third-or-later worker thread's
        // perform call can hang for ~90 s. The big nro download still
        // happens on the worker, where blocking the UI matters.
        if (state.request_install_cores && !state.install_job.active()) {
            state.request_install_cores = false;
            std::string only = std::move(state.install_only_core);
            state.install_only_core.clear();
            const bool force = state.install_force;
            state.install_force = false;

            state.banner_text = "Fetching manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto manifest = foyer::library::fetch_manifest(
                foyer::library::config().cores_manifest_url);
            if (manifest.cores.empty()) {
                state.banner_text = "Manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                const bool started = state.install_job.start(
                    std::move(manifest), std::move(only), force);
                if (!started) {
                    state.banner_text = "Install worker failed to start";
                    state.banner_ttl  = 240;
                } else {
                    state.banner_text = "Starting...";
                    state.banner_ttl  = 60;
                }
            }
        } else if (state.request_install_cores) {
            state.request_install_cores = false;
            state.install_only_core.clear();
            state.install_force = false;
            state.banner_text = "Download already in progress";
            state.banner_ttl  = 180;
        }

        // Mirror live progress + finalise when worker exits.
        if (state.install_job.active()) {
            // Refresh banner from the worker's mutex-protected status
            // string. Cheap copy under lock.
            const auto status = state.install_job.status_snapshot();
            if (!status.empty()) {
                state.banner_text = status;
                state.banner_ttl  = 60;
            }
            if (state.install_job.done()) {
                const auto totals = state.install_job.finish();
                // Silent on success — the worker's last per-core status
                // line ("[N/M] foo - installed") fades naturally. Only
                // surface a banner when something actually failed.
                if (totals.failed > 0) {
                    char doneb[120];
                    std::snprintf(doneb, sizeof(doneb),
                        "%d core%s failed - check log",
                        totals.failed, totals.failed == 1 ? "" : "s");
                    state.banner_text = doneb;
                    state.banner_ttl  = 360;
                }
                // Refresh the cached manifest so the per-core rows
                // immediately reflect the new "up to date" state.
                foyer::browser::set_manifest_cache(
                    foyer::library::fetch_manifest(
                        foyer::library::config().cores_manifest_url));
            }
        }

        // Shader presets install. Synchronous on the main thread for
        // now (manifest is small; each preset zip is KB-sized; the
        // full catalogue is < 1 MB total). The user-facing flow:
        //   Settings -> Updates -> Install shader presets
        // fetches manifest, downloads + unzips every preset under
        // /foyer/shaders/<name>/.
        if (state.request_install_shaders) {
            state.request_install_shaders = false;
            state.banner_text = "Fetching shader manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto sm = foyer::library::fetch_shader_manifest(
                foyer::library::config().shaders_manifest_url);
            if (sm.presets.empty()) {
                state.banner_text = "Shader manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                const auto totals = foyer::library::install_shaders(sm,
                    [&](const foyer::library::ShaderInstallProgress& p) {
                        char b[160];
                        const char* verb =
                            p.action == foyer::library::ShaderInstallAction::Skipped   ? "skipped" :
                            p.action == foyer::library::ShaderInstallAction::Updated   ? "updated" :
                            p.action == foyer::library::ShaderInstallAction::Installed ? "installed"
                                                                                       : "FAILED";
                        std::snprintf(b, sizeof(b), "[%d/%d] %s - %s",
                            p.index, p.total, p.name.c_str(), verb);
                        state.banner_text = b;
                        state.banner_ttl  = 60;
                        app.tick();
                    });
                if (totals.failed > 0) {
                    char b[120];
                    std::snprintf(b, sizeof(b),
                        "%d shader preset%s failed - check log",
                        totals.failed, totals.failed == 1 ? "" : "s");
                    state.banner_text = b;
                } else {
                    char b[120];
                    std::snprintf(b, sizeof(b),
                        "Shader presets ready (%d new, %d updated, %d skipped)",
                        totals.installed, totals.updated, totals.skipped);
                    state.banner_text = b;
                }
                state.banner_ttl = 360;
            }
        }

        // Cheat-pack install. Same shape as shaders: blocking call on
        // the main thread, banner-driven progress, atomic per-pack
        // .tmp -> rename via libarchive.
        if (state.request_install_cheats) {
            state.request_install_cheats = false;
            state.banner_text = "Fetching cheats manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto cm = foyer::library::fetch_cheat_manifest(
                foyer::library::config().cheats_manifest_url);
            if (cm.packs.empty()) {
                state.banner_text = "Cheats manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                const auto totals = foyer::library::install_cheats(cm,
                    [&](const foyer::library::CheatInstallProgress& p) {
                        char b[160];
                        const char* verb =
                            p.action == foyer::library::CheatInstallAction::Skipped   ? "skipped" :
                            p.action == foyer::library::CheatInstallAction::Updated   ? "updated" :
                            p.action == foyer::library::CheatInstallAction::Installed ? "installed"
                                                                                       : "FAILED";
                        std::snprintf(b, sizeof(b), "[%d/%d] %s - %s",
                            p.index, p.total, p.name.c_str(), verb);
                        state.banner_text = b;
                        state.banner_ttl  = 60;
                        app.tick();
                    });
                if (totals.failed > 0) {
                    char b[120];
                    std::snprintf(b, sizeof(b),
                        "%d cheat pack%s failed - check log",
                        totals.failed, totals.failed == 1 ? "" : "s");
                    state.banner_text = b;
                } else {
                    char b[160];
                    std::snprintf(b, sizeof(b),
                        "Cheat packs ready (%d new, %d updated, %d skipped)",
                        totals.installed, totals.updated, totals.skipped);
                    state.banner_text = b;
                }
                state.banner_ttl = 360;
            }
        }

        // Bezel-pack install. Same blocking shape as the cheats /
        // shaders flows; per-system PNGs land at /foyer/bezels/<sys>.png
        // and the player's bezel.cpp picks them up automatically.
        if (state.request_install_bezels) {
            state.request_install_bezels = false;
            state.banner_text = "Fetching bezels manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto bm = foyer::library::fetch_bezel_manifest(
                foyer::library::config().bezels_manifest_url);
            if (bm.packs.empty()) {
                state.banner_text = "Bezels manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                const auto totals = foyer::library::install_bezels(bm,
                    [&](const foyer::library::BezelInstallProgress& p) {
                        char b[160];
                        const char* verb =
                            p.action == foyer::library::BezelInstallAction::Skipped   ? "skipped" :
                            p.action == foyer::library::BezelInstallAction::Updated   ? "updated" :
                            p.action == foyer::library::BezelInstallAction::Installed ? "installed"
                                                                                       : "FAILED";
                        std::snprintf(b, sizeof(b), "[%d/%d] %s - %s",
                            p.index, p.total, p.name.c_str(), verb);
                        state.banner_text = b;
                        state.banner_ttl  = 60;
                        app.tick();
                    });
                if (totals.failed > 0) {
                    char b[120];
                    std::snprintf(b, sizeof(b),
                        "%d bezel pack%s failed - check log",
                        totals.failed, totals.failed == 1 ? "" : "s");
                    state.banner_text = b;
                } else {
                    char b[160];
                    std::snprintf(b, sizeof(b),
                        "Bezel packs ready (%d new, %d updated, %d skipped)",
                        totals.installed, totals.updated, totals.skipped);
                    state.banner_text = b;
                }
                state.banner_ttl = 360;
            }
        }

        if (state.request_launch) {
            state.request_launch = false;
            const auto& sys  = lib.systems[state.system_index];
            const auto& game = sys.games[state.game_index];
            const int   resume = state.request_resume_slot;
            state.request_resume_slot = -1;
            // Stamp last_played BEFORE launch_game (which usually
            // app.quit()s on success and doesn't return). Powers the
            // home view's Recent virtual system + the Resume action.
            foyer::library::mark_per_game_played(game.path);
            if (foyer::browser::launch_game(sys, game, resume)) {
                foyer::browser::mtp_stop();
                app.quit();
            } else {
                const auto* core = foyer::library::resolve_core(*sys.def, game.path);
                state.banner_text = std::string{"Core not installed: foyer-"}
                    + (core ? std::string{core->name} : "?") + ".nro";
                state.banner_ttl  = 180;
            }
        }

        if (state.request_scrape_kind != foyer::browser::State::ScrapeKind::None
            && !state.scrape_job.active()) {
            const auto kind = state.request_scrape_kind;
            state.request_scrape_kind = foyer::browser::State::ScrapeKind::None;
            const auto& sys = lib.systems[state.system_index];
            const auto src =
                (kind == foyer::browser::State::ScrapeKind::ScreenScraper)
                    ? foyer::library::ScrapeJob::Source::ScreenScraper :
                (kind == foyer::browser::State::ScrapeKind::SteamGridDB)
                    ? foyer::library::ScrapeJob::Source::SteamGridDB :
                      foyer::library::ScrapeJob::Source::Libretro;
            if (!state.scrape_job.start(sys, src)) {
                state.banner_text = "Scrape worker failed to start";
                state.banner_ttl  = 240;
            }
        } else if (state.request_scrape_kind != foyer::browser::State::ScrapeKind::None) {
            state.request_scrape_kind = foyer::browser::State::ScrapeKind::None;
            state.banner_text = "Scrape already in progress";
            state.banner_ttl  = 180;
        }
        if (state.scrape_job.active()) {
            const auto status = state.scrape_job.status_snapshot();
            if (!status.empty()) {
                state.banner_text = status;
                state.banner_ttl  = 60;
            }
            if (state.scrape_job.done()) {
                const int hits  = state.scrape_job.hits();
                state.scrape_job.finish();
                // Drop cached nanovg handles so newly-downloaded files
                // show up immediately.
                foyer::browser::invalidate_cover_cache(app.vg());
                // Silent on success — the worker's per-game status line
                // fades naturally. Only banner-flag a no-hit run, which
                // usually means the chosen scraper has no data for this
                // system / wrong account credentials.
                if (hits == 0) {
                    state.banner_text = "Scrape found no covers - check log";
                    state.banner_ttl  = 240;
                }
            }
        }
    }
    foyer::browser::mtp_stop();
    return 0;
}
