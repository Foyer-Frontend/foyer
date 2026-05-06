// foyer browser entry.
//
// Scans /foyer/roms for known systems, drives the carousel + game list +
// detail + search + settings views, manages the background workers
// (manifest fetch, core install, scrape, foyer self-update, shader
// install), and chain-launches the matching foyer-<core>.nro player.

#include <switch.h>

#include "platform/app.hpp"
#include "platform/log.hpp"
#include "boot_splash.hpp"

#include <chrono>
#include <thread>
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
#include "session.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {

// Filesystem path to the running foyer.nro, derived from libnx's argv.
// hbloader / nxlink / atmosphère all surface argv[0] as the nro path
// under sdmc:/, but users install foyer wherever they like —
// /switch/foyer.nro, /switch/foyer/foyer.nro, /switch/applets/foyer/foyer.nro,
// etc. The self-update flow has to write the staged .new sibling next
// to the real running nro, so we derive the path at boot rather than
// hard-coding one. Falls back to /switch/foyer/foyer.nro only if
// libnx didn't expose argv (very old loaders).
std::string detect_foyer_nro_path() {
    std::string p;
    if (envHasArgv()) {
        const char* a = static_cast<const char*>(envGetArgv());
        if (a) {
            while (*a == ' ') a++;
            const char* end = nullptr;
            if (*a == '"') { a++; end = std::strchr(a, '"'); }
            else           { end = std::strchr(a, ' '); }
            p.assign(a, end ? static_cast<std::size_t>(end - a) : std::strlen(a));
        }
    }
    if (p.compare(0, 5, "sdmc:") == 0) p.erase(0, 5);
    if (p.empty()) p = "/switch/foyer/foyer.nro";
    return p;
}

// Two paths the rest of main() needs: the running nro itself (for the
// self-update download destination) and its .new sibling (which the
// boot path swaps in if the previous run staged an update).
std::string g_foyer_nro_path;
std::string g_foyer_nro_new_path;

// Idempotent scrub of the bundled CRT-TV bezel that v0.2.x shipped as
// `/foyer/bezels/default.png`. The new resolve_path() in bezel.cpp
// stopped consulting it in v0.2.52 — but player nros built before
// that still ride the old path, and their fallback chain happily
// finds default.png on disk. Nuking the file makes the OLD players'
// fallback chain return "" too, which short-circuits draw_bezel to
// a no-op for systems the user hasn't picked a bezel for.
//
// Used to be marker-gated (one-shot on first boot of v0.2.53+) but
// the marker meant a pack install that re-introduced default.png
// got stuck. Now unconditional: every boot deletes /foyer/bezels/
// default.png if present. A user who genuinely wants the bundled
// CRT-TV image can re-pick it via the bezel picker (which copies it
// to a per-system slot, not the catch-all default.png that the OLD
// players still treat as a wildcard).
void scrub_legacy_default_bezel_once() {
    if (::unlink("/foyer/bezels/default.png") == 0) {
        foyer::log::write(
            "[bezel] scrubbed legacy /foyer/bezels/default.png\n");
    }
}

// If the previous run staged an update, swap it in before we boot the
// rest of the app. We're not the loaded-into-memory image; renaming the
// SD-side file is safe — the next launch picks up the new bytes.
//
// devkitA64's FAT/exFAT-backed rename(2) doesn't honour the POSIX
// "atomic replace" rule — when the destination exists it returns
// EEXIST instead of overwriting. Same workaround as
// shared/net/http.cpp's stream_to_file: try direct rename first, then
// unlink-then-rename on failure. Brief window where dest doesn't
// exist between the two syscalls but the staged file is fully on
// disk by this point so a power-cut won't lose data.
void apply_staged_update_if_present() {
    struct stat st{};
    if (::stat(g_foyer_nro_new_path.c_str(), &st) != 0) return;
    if (::rename(g_foyer_nro_new_path.c_str(),
                 g_foyer_nro_path.c_str()) != 0) {
        ::unlink(g_foyer_nro_path.c_str());
        if (::rename(g_foyer_nro_new_path.c_str(),
                     g_foyer_nro_path.c_str()) != 0) {
            foyer::log::write(
                "[foyer_update] rename of staged nro failed errno=%d\n",
                errno);
            ::unlink(g_foyer_nro_new_path.c_str());
            return;
        }
    }
    foyer::log::write("[foyer_update] applied staged nro -> %s\n",
        g_foyer_nro_path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    g_foyer_nro_path     = detect_foyer_nro_path();
    g_foyer_nro_new_path = g_foyer_nro_path + ".new";
    foyer::log::write("[foyer_update] running nro path = %s\n",
        g_foyer_nro_path.c_str());

    apply_staged_update_if_present();
    scrub_legacy_default_bezel_once();

    foyer::platform::App app;

    // Loading screen — paint immediately so the user sees something
    // while the (potentially multi-second) scan/init sequence runs.
    // The closure captures a pointer to a status string we update
    // between init phases; tick() between updates flushes a fresh
    // frame so the user gets feedback rather than a frozen logo.
    //
    // BootSplash is short-lived — its image handles get dropped on
    // the floor when we replace draw_fn below. The App tears down the
    // NVG context on quit so we don't leak across runs.
    std::string boot_status = "Starting...";
    foyer::browser::BootSplash splash{app.vg()};
    app.set_draw_fn([&boot_status, &splash](NVGcontext* vg, float w, float h) {
        splash.draw(vg, w, h, boot_status, FOYER_DISPLAY_VERSION);
    });
    // Wall-clock helper for boot phase timing. Logs a "[boot]" line
    // for each phase so a slow load complaint can point at the
    // offending step (cache miss vs. seed walking the whole bezel
    // tree vs. theme JSONC stall, etc.) without us having to rebuild.
    auto tick_phase = [&](const char* label) {
        static auto t0 = std::chrono::steady_clock::now();
        static auto prev = t0;
        const auto now  = std::chrono::steady_clock::now();
        const auto step =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - prev).count();
        const auto total =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count();
        prev = now;
        foyer::log::write("[boot] %s (+%lldms / %lldms total)\n",
            label, (long long)step, (long long)total);
    };
    tick_phase("app constructed");

    boot_status = "Seeding assets...";
    app.tick();

    // Seed bundled bezels + cheats from romfs into the SD tree on
    // first boot (and every boot a NEW system / cheat ships in the
    // browser's romfs). User-installed files are never overwritten.
    foyer::browser::seed_assets_if_missing();
    tick_phase("seed_assets done");
    boot_status = "Initialising network...";
    app.tick();

    // Initialise curl on the main thread BEFORE any worker spawns
    // one. curl_global_init must run single-threaded; if a worker
    // racing in start_check() does it first, Switch's socket layer
    // can leave curl in a state where every subsequent perform
    // hangs at "Fetching manifest...". Boot-time foyer manifest
    // check is the trigger.
    foyer::net::init();
    // Wire the per-byte UI pump. xferinfo (called from inside
    // curl_easy_perform) hits this every ~200ms; we throttle to ~30
    // fps so the progress bar redraws without blowing the frame
    // budget on every curl tick.
    //
    // Crucial: app.tick() drives deko3d's swap chain, which is bound
    // to the thread that constructed the App (this thread = main).
    // Some downloaders run inside a Worker thread (foyer self-update,
    // cores install when invoked async, scrape jobs that fetch
    // assets) — when xferinfo fires from there, calling app.tick()
    // crashes the GPU. Compare std::this_thread::get_id() against
    // the main-thread id captured here and fall through to a
    // no-op pump on worker threads. The main-thread loop polling
    // worker state already pumps app.tick() at the right cadence;
    // worker-side downloads still update g_download progress
    // counters atomically (set inside stream_to_file) so the UI
    // sees fresh numbers even without the per-byte tick.
    {
        static const auto main_tid = std::this_thread::get_id();
        static auto last_pump = std::chrono::steady_clock::now()
            - std::chrono::milliseconds(100);
        foyer::net::set_pump_callback([&app]() {
            if (std::this_thread::get_id() != main_tid) return;
            const auto now = std::chrono::steady_clock::now();
            if (now - last_pump >=
                std::chrono::milliseconds(33)) {
                last_pump = now;
                app.tick();
            }
        });
    }
    tick_phase("net::init done");
    boot_status = "Loading theme...";
    app.tick();

    foyer::browser::load_theme(foyer::library::config().theme_name);
    tick_phase("theme loaded");

    // NOTE: temporarily skipping MTP autostart — libhaze appears to break
    // libnx's romfs devoptab so subsequent fopen("romfs:/...") returns
    // ENOSYS. Re-enable once we understand the interaction.
    // if (foyer::library::config().mtp_autostart) {
    //     foyer::browser::mtp_start();
    // }

    boot_status = "Scanning library...";
    app.tick();

    foyer::library::ScanOptions opts;
    opts.rom_root = foyer::library::config().rom_root;
    opts.recurse  = foyer::library::config().scan_subfolders;
    foyer::browser::Library lib;
    lib.systems = foyer::library::scan_library(opts);
    tick_phase("scan_library done");
    boot_status = "Ready";
    app.tick();

    foyer::browser::State state;

    // Session restore — only when we got chained back from a player
    // nro (recognised by the "foyer-resume" argv token the player's
    // chain-back inserts). Cold launch from hbmenu has no marker and
    // lands on Home view; this also cleans up any stale session
    // file from a prior run.
    foyer::browser::load_and_consume_session(state,
        foyer::browser::argv_has_resume_marker(argc, argv));

    // One-shot self-update check on boot. Off-thread so the UI is up
    // immediately even on a slow CDN; the result is processed by the
    // foyer_job poll block in the main loop below.
    state.foyer_job.start_check(foyer::library::config().foyer_manifest_url);

    // Boot-time manifest scrape: fire the same request flags that
    // Settings → Updates → "Refresh manifest" raises, so the user
    // arrives at a UI that already knows what's installable / out of
    // date without having to enter any settings page first. Cores
    // refresh runs on a Worker (async) so the UI stays responsive; the
    // cheats / bezels paths are sync today but cheap (small JSON pulls)
    // and only block the first frames of the main loop.
    state.request_refresh_manifest         = true;
    state.request_refresh_cheats_manifest  = true;
    state.request_refresh_bezels_manifest  = true;

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

        // "Restart now" from the post-download confirm modal. Apply
        // the staged .new -> .nro swap right here, then chain-launch
        // the freshly-renamed binary via libnx's envSetNextLoad — the
        // hbloader picks it up on our exit and runs the new bytes.
        // No-op fallback if the rename fails (rare; we still apply
        // on exit via apply_staged_update_if_present below).
        if (state.request_restart_now) {
            state.request_restart_now = false;
            foyer::browser::mtp_stop();
            apply_staged_update_if_present();
            const std::string load_path = std::string{"sdmc:"} + g_foyer_nro_path;
            envSetNextLoad(load_path.c_str(), "\"foyer-restart\"");
            foyer::log::write("[foyer_update] chain-launching %s\n",
                load_path.c_str());
            app.quit();
            continue;
        }

        if (state.request_rescan) {
            state.request_rescan = false;
            foyer::library::reload_config();
            opts.rom_root = foyer::library::config().rom_root;
            opts.recurse  = foyer::library::config().scan_subfolders;
            // Explicit user-triggered rescan — bypass the cache and
            // walk the SD again. The freshly-built snapshot replaces
            // /foyer/data/library.cache.json so future cold boots
            // load the new content without re-scanning.
            opts.force_rescan = true;
            lib.systems = foyer::library::scan_library(opts);
            opts.force_rescan = false;
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
                g_foyer_nro_path);
        } else if (state.request_install_foyer_update) {
            state.request_install_foyer_update = false;
        }

        // Mirror foyer_job progress + finalise. The polling block has
        // to be careful to distinguish three end states:
        //   - boot check that found a newer version → "available"
        //     banner pointing the user at the Updates page
        //   - user-driven install that succeeded → "ready, restart"
        //   - user-driven install that failed → an explicit failure
        //     banner, *not* a silent fall-through to "available"
        //     (which previously made a failed download look like "no
        //     update happened" since the banner just said "available"
        //     again instead of "download failed")
        if (state.foyer_job.active()) {
            const auto status = state.foyer_job.status_snapshot();
            if (!status.empty()) {
                state.banner_text = status;
                state.banner_ttl  = 60;
            }
            if (state.foyer_job.done()) {
                const auto& m          = state.foyer_job.manifest();
                const auto  downloaded = state.foyer_job.downloaded_version();
                const bool  was_install = state.foyer_job.download_mode();
                state.foyer_job.finish();
                const bool newer = !m.version.empty() &&
                    foyer::library::is_newer_version(FOYER_VERSION, m.version);
                if (newer) {
                    state.foyer_update_available = true;
                    state.foyer_update_version   = m.version;
                }
                if (was_install && newer && downloaded.empty()) {
                    // User asked for an install but the staged .new
                    // never landed. Surface the failure verbatim
                    // instead of looping back to "available".
                    state.banner_text =
                        "Foyer update download failed - check Wi-Fi / log";
                    state.banner_ttl = 360;
                } else if (was_install && !downloaded.empty()) {
                    // Staged .new is ready — surface the restart-confirm
                    // modal so the user can apply + relaunch in one
                    // tap instead of having to manually exit and re-
                    // enter foyer. Banner clears; the modal is the
                    // primary surface now.
                    state.foyer_update_version = downloaded;
                    state.restart_confirm_open  = true;
                    state.restart_confirm_index = 0; // default Yes
                    state.banner_text.clear();
                    state.banner_ttl = 0;
                } else if (newer) {
                    // Silent boot check found something — point the
                    // user at the actionable surface.
                    state.banner_text =
                        "Foyer update available: v" + m.version
                        + " - open Settings -> Updates";
                    state.banner_ttl = 360;
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

        // Drain a finished-but-not-yet-finalised foyer self-update
        // job so the next click doesn't see a stale "active" job.
        if (state.foyer_job.active() && state.foyer_job.done()) {
            state.foyer_job.finish();
        }

        // "Update everything" footer from the Updates page. Fans
        // out to the existing per-kind request flags so the install
        // handlers below pick them up sequentially. We don't try to
        // run them in parallel — libcurl on Switch is synchronous on
        // this thread and the install handlers all pump app.tick()
        // between rows.
        if (state.request_update_all) {
            state.request_update_all = false;
            state.install_only_core.clear();
            state.install_only_bezel.clear();
            state.install_only_cheat.clear();
            state.install_force = false;
            state.request_install_cores  = true;
            state.request_install_bezels = true;
            state.request_install_cheats = true;
            // Chain in foyer self-update if a check already flagged
            // a newer version. Skipped versions are honoured by the
            // install path itself.
            if (state.foyer_update_available) {
                state.request_install_foyer_update = true;
            }
        }

        // Cores install. Synchronous on the main thread — the
        // earlier worker-thread path didn't reliably finish on Switch
        // (libcurl-on-Switch quirks + Worker lifecycle races). Same
        // shape as the shader / cheat / bezel install handlers below:
        // banner-driven progress, app.tick() pumped between rows so
        // the user sees a live status without needing a second
        // worker thread.
        if (state.request_install_cores) {
            state.request_install_cores = false;
            const std::string only  = std::move(state.install_only_core);
            state.install_only_core.clear();
            const bool        force = state.install_force;
            state.install_force = false;

            // Manifest fetch is fast (small JSON, sub-second) — skip
            // the "Fetching..." banner; the byte-progress overlay
            // takes over the moment the first core download starts.
            // Failures still surface explicitly.
            app.tick();
            auto manifest = foyer::library::fetch_manifest(
                foyer::library::config().cores_manifest_url);
            foyer::log::write(
                "[install_cores] manifest=%zu cores; only=%s; force=%d\n",
                manifest.cores.size(), only.c_str(), (int)force);
            if (manifest.cores.empty()) {
                state.banner_text = "Cores manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                if (!only.empty()) {
                    std::erase_if(manifest.cores,
                        [&](const auto& c) { return c.name != only; });
                    if (manifest.cores.empty()) {
                        char b[160];
                        std::snprintf(b, sizeof(b),
                            "Core not in manifest: %s", only.c_str());
                        state.banner_text = b;
                        state.banner_ttl  = 240;
                    }
                }
                if (!manifest.cores.empty()) {
                    foyer::browser::set_manifest_cache(manifest);
                    // No per-row banner. The byte progress overlay
                    // already carries everything visible the user
                    // needs (live bar, MB counter, % complete) for
                    // the active transfer; skipped/installed/updated
                    // rows go to the log only. The callback still
                    // pumps app.tick() between rows so the UI stays
                    // responsive between back-to-back downloads.
                    const auto totals = foyer::library::install_cores(manifest,
                        [&](const foyer::library::InstallProgress&) {
                            app.tick();
                        }, force);
                    if (totals.failed > 0) {
                        char b[120];
                        std::snprintf(b, sizeof(b),
                            "%d core%s failed - check log",
                            totals.failed, totals.failed == 1 ? "" : "s");
                        state.banner_text = b;
                        state.banner_ttl = 360;
                    } else {
                        // Success or all-skipped: nothing to surface.
                        state.banner_text.clear();
                        state.banner_ttl = 0;
                    }
                    // Refresh manifest cache so per-row state
                    // reflects the new "up to date" status.
                    foyer::browser::set_manifest_cache(
                        foyer::library::fetch_manifest(
                            foyer::library::config().cores_manifest_url));
                }
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

        // Cheat-pack manifest refresh. Pull only — per-row install
        // happens via OpInstallSingleCheatPack below.
        if (state.request_refresh_cheats_manifest) {
            state.request_refresh_cheats_manifest = false;
            auto cm = foyer::library::fetch_cheat_manifest(
                foyer::library::config().cheats_manifest_url);
            if (cm.packs.empty()) {
                state.banner_text = "Cheats manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                char b[160];
                std::snprintf(b, sizeof(b),
                    "Cheats manifest loaded (%zu packs, %s)",
                    cm.packs.size(), cm.version.c_str());
                state.banner_text = b;
                state.banner_ttl  = 240;
                foyer::browser::set_cheats_manifest_cache(std::move(cm));
            }
        }

        // Bezel-pack manifest refresh — same shape as cheats above.
        if (state.request_refresh_bezels_manifest) {
            state.request_refresh_bezels_manifest = false;
            auto bm = foyer::library::fetch_bezel_manifest(
                foyer::library::config().bezels_manifest_url);
            if (bm.packs.empty()) {
                state.banner_text = "Bezels manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                char b[160];
                std::snprintf(b, sizeof(b),
                    "Bezels manifest loaded (%zu packs, %s)",
                    bm.packs.size(), bm.version.c_str());
                state.banner_text = b;
                state.banner_ttl  = 240;
                foyer::browser::set_bezels_manifest_cache(std::move(bm));
            }
        }

        // Cheat-pack install. Honors install_only_cheat for the
        // per-row picker; otherwise installs every pack the manifest
        // lists. Manifest is fetched fresh each time so the user
        // doesn't need to refresh first for the install-all path.
        if (state.request_install_cheats) {
            state.request_install_cheats = false;
            const auto only = std::move(state.install_only_cheat);
            state.install_only_cheat.clear();
            state.banner_text = "Fetching cheats manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto cm = foyer::library::fetch_cheat_manifest(
                foyer::library::config().cheats_manifest_url);
            if (cm.packs.empty()) {
                state.banner_text = "Cheats manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                // Refresh the cache so per-pack rows reflect the
                // post-install state on the next Settings draw.
                foyer::browser::set_cheats_manifest_cache(cm);
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
                    },
                    only,
                    /*force=*/false);
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

        // Bezel-pack install. Mirrors cheats; honors
        // install_only_bezel for per-row picks.
        if (state.request_install_bezels) {
            state.request_install_bezels = false;
            const auto only = std::move(state.install_only_bezel);
            state.install_only_bezel.clear();
            state.banner_text = "Fetching bezels manifest...";
            state.banner_ttl  = 60;
            app.tick();
            auto bm = foyer::library::fetch_bezel_manifest(
                foyer::library::config().bezels_manifest_url);
            if (bm.packs.empty()) {
                state.banner_text = "Bezels manifest fetch failed";
                state.banner_ttl  = 240;
            } else {
                foyer::browser::set_bezels_manifest_cache(bm);
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
                    },
                    only,
                    /*force=*/false);
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
            // Stash the current view + cursor so foyer's next boot
            // (after the core exits and chains back) lands the user
            // exactly where they were. One-shot file with a 1h TTL,
            // consumed + deleted on the next load_and_consume_session
            // call. Cold launch still defaults to Home.
            foyer::browser::save_session(state);
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

        if (state.request_pick_cover) {
            state.request_pick_cover = false;
            // Resolve focused game + system, then pull up to N
            // SteamGridDB grid candidates and stash them on SD where
            // the OptionPicker can render them as thumbnails.
            if (state.system_index < lib.systems.size()) {
                const auto& sys2 = lib.systems[state.system_index];
                if (state.game_index < sys2.games.size()) {
                    const auto& g = sys2.games[state.game_index];
                    const auto* def = foyer::library::is_virtual_system(*sys2.def)
                        ? foyer::library::origin_system_for_rom(g.path)
                        : sys2.def;
                    if (def) {
                        constexpr const char* kCacheDir = "/foyer/data/cover_picks";
                        ::mkdir("/foyer/data", 0777);
                        ::mkdir(kCacheDir, 0777);
                        // Wipe stale candidates from a previous run so
                        // the picker doesn't show the previous game's
                        // thumbnails when SteamGridDB returns fewer
                        // images this time around.
                        if (auto* d = ::opendir(kCacheDir)) {
                            while (auto* e = ::readdir(d)) {
                                if (!e->d_name[0] || e->d_name[0] == '.') continue;
                                std::string p = std::string{kCacheDir}
                                              + "/" + e->d_name;
                                ::unlink(p.c_str());
                            }
                            ::closedir(d);
                        }
                        app.tick();   // keep UI responsive while we fetch
                        auto cands = foyer::scrapers::steamgriddb
                            ::fetch_cover_candidates(g.stem, kCacheDir, 8);
                        if (cands.empty()) {
                            state.banner_text = "No cover candidates found";
                            state.banner_ttl  = 240;
                        } else {
                            // views.cpp owns the OpPickCover enum
                            // value — expose via open_cover_picker so
                            // main.cpp doesn't reach into the
                            // anonymous-namespace settings:: ops.
                            foyer::browser::open_cover_picker(state,
                                std::string{"Pick cover for "} + g.stem,
                                std::string{def->folder_name},
                                g.stem,
                                std::move(cands));
                            state.banner_text = "";
                            state.banner_ttl  = 0;
                        }
                    }
                }
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

    // Apply any staged self-update before we exit. Without this the
    // user who picked "Later" on the restart-confirm modal would have
    // to launch foyer TWICE after a download (once to swap .new ->
    // .nro at boot, again to actually run the new bytes). Doing the
    // rename on the way out closes that gap — the next launch is
    // already the new version.
    apply_staged_update_if_present();
    return 0;
}
