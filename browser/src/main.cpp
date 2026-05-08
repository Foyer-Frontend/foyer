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
#include "i18n/i18n.hpp"
#include "library/scanner.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/core_installer.hpp"
#include "library/foyer_updater.hpp"
#include "library/shader_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/bezel_installer.hpp"
#include "library/skipped_versions.hpp"
#include "net/http.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/libretro_thumbnails.hpp"
#include "scrapers/screenscraper.hpp"
#include "scrapers/steamgriddb.hpp"
#include "scrapers/accounts.hpp"
#include "theme.hpp"
#include "views.hpp"
#include "hos_status.hpp"
#include "switch_titles.hpp"
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
    // Canonicalise: when the chain-launch path ended in ".new"
    // (which is what the foyer self-update Restart-now path
    // produces — see envSetNextLoad("sdmc:.../foyer.nro.new")),
    // strip the suffix so g_foyer_nro_path always names the
    // CANONICAL on-disk file. Otherwise apply_staged_update_if_-
    // present would compute g_foyer_nro_new_path = ".../foyer.nro.new.new"
    // and find no staged file to apply — the user's foyer.nro
    // would stay at the old version forever even after the new
    // bytes were running successfully.
    if (p.size() > 4 &&
        p.compare(p.size() - 4, 4, ".new") == 0) {
        p.erase(p.size() - 4);
    }
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
    // 0.5.21: drop suspiciously-small staged files BEFORE applying
    // them. A truncated download from a flaky connection can leave
    // a partial foyer.nro.new on disk; renaming it to foyer.nro
    // means the next boot tries to chain-launch a corrupt nro and
    // atmosphere fatals at PC=0 (error 2354-0001 reported in the
    // wild). Foyer's smallest plausible build is ~5 MB even before
    // any romfs content; the current build is ~38 MB. 1 MB is a
    // generous floor that catches partial writes without false-
    // flagging future minimal builds.
    if (st.st_size < 1024 * 1024) {
        foyer::log::write(
            "[foyer_update] staged file is %lld bytes — too small, "
            "deleting (probably a partial download)\n",
            (long long)st.st_size);
        ::unlink(g_foyer_nro_new_path.c_str());
        return;
    }
    if (::rename(g_foyer_nro_new_path.c_str(),
                 g_foyer_nro_path.c_str()) != 0) {
        ::unlink(g_foyer_nro_path.c_str());
        if (::rename(g_foyer_nro_new_path.c_str(),
                     g_foyer_nro_path.c_str()) != 0) {
            foyer::log::write(
                "[foyer_update] rename of staged nro failed errno=%d "
                "(leaving %s in place for the next attempt)\n",
                errno, g_foyer_nro_new_path.c_str());
            // Do NOT unlink foyer.nro.new on failure. Earlier
            // versions did, which then made the chain-launch in the
            // "Restart now" flow find no file to load — so foyer
            // bounced back into the OLD foyer.nro and the user
            // stayed stuck on the old version. Leaving the staged
            // file alone means a subsequent boot (when neither file
            // is held open by the running process) gets another
            // chance to apply it.
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
    // Pull the Switch system language right away so even the early
    // boot-status flashes ("Starting..." / "Seeding assets...") render
    // in the user's locale. init() is idempotent — the explicit
    // Settings → Language override below will replay it.
    foyer::i18n::init();

    std::string boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootStarting);
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

    boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootSeedingAssets);
    app.tick();

    // Seed bundled bezels + cheats from romfs into the SD tree on
    // first boot (and every boot a NEW system / cheat ships in the
    // browser's romfs). User-installed files are never overwritten.
    foyer::browser::seed_assets_if_missing();
    tick_phase("seed_assets done");
    boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootInitNetwork);
    app.tick();

    // Initialise curl on the main thread BEFORE any worker spawns
    // one. curl_global_init must run single-threaded; if a worker
    // racing in start_check() does it first, Switch's socket layer
    // can leave curl in a state where every subsequent perform
    // hangs at "Fetching manifest...". Boot-time foyer manifest
    // check is the trigger.
    foyer::net::init();
    // i18n locale detection runs before any UI string is read. Pulls
    // the Switch system language and picks one of the bundled
    // catalogues — falls through to English when the user's locale
    // doesn't have a translation yet. Cheap (one libnx Set service
    // call) so no async work needed.
    foyer::i18n::init();
    // Apply the user's explicit Settings → General → Language
    // override on top of the system-language autodetect. Empty
    // string = follow system; otherwise we set the language enum to
    // match the saved code. Map mirrors the picker in views.cpp.
    {
        const auto& saved = foyer::library::config().language;
        if (!saved.empty()) {
            using L = foyer::i18n::Language;
            if      (saved == "en")    foyer::i18n::set_language(L::English);
            else if (saved == "es")    foyer::i18n::set_language(L::Spanish);
            else if (saved == "pt-BR") foyer::i18n::set_language(L::PortugueseBrazil);
            // Unrecognised codes silently fall through to the
            // system-detected default — same shape as
            // map_switch_language()'s default case.
        }
    }
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
    boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootLoadingTheme);
    app.tick();

    foyer::browser::load_theme(foyer::library::config().theme_name);
    tick_phase("theme loaded");

    // NOTE: temporarily skipping MTP autostart — libhaze appears to break
    // libnx's romfs devoptab so subsequent fopen("romfs:/...") returns
    // ENOSYS. Re-enable once we understand the interaction.
    // if (foyer::library::config().mtp_autostart) {
    //     foyer::browser::mtp_start();
    // }

    boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootScanningLibrary);
    app.tick();

    foyer::library::ScanOptions opts;
    opts.rom_root = foyer::library::config().rom_root;
    opts.recurse  = foyer::library::config().scan_subfolders;
    foyer::browser::Library lib;
    lib.systems = foyer::library::scan_library(opts);
    tick_phase("scan_library done");
    boot_status = foyer::i18n::tr(foyer::i18n::StringId::BootReady);
    app.tick();

    // 0.5.0 HOS chrome — load the active user's avatar/nickname and
    // prime the battery + wifi cache. After this the per-frame poll()
    // is a cheap debounced refresh; the avatar JPEG decode (the only
    // expensive piece) only runs once.
    foyer::browser::hos_status::init(app.vg());

    // 0.5.5 Switch-title launcher cache. NACP icon decode is the slow
    // step (a JPEG per installed title — on a console with 197+
    // titles, the cold-boot scan can take 30+ seconds). Pass a
    // progress callback so the boot splash shows live progress —
    // tick EVERY record (was every 4th in 0.5.13, which produced
    // 5-10 second gaps between visible updates and looked frozen).
    boot_status = ::foyer::i18n::tr(::foyer::i18n::StringId::BootLoadingSwitchTitles);
    app.tick();
    foyer::browser::switch_titles::load(app.vg(),
        [&](int idx, int total) {
            char buf[120];
            std::snprintf(buf, sizeof(buf),
                ::foyer::i18n::tr(::foyer::i18n::StringId::BootLoadingSwitchTitlesProgress),
                idx, total);
            boot_status = buf;
            app.tick();
        });

    // Expose installed Switch titles as a virtual system at the front
    // of the carousel. Each title becomes a Game with path
    // "switch://<application_id>" so launch.cpp can detect the schema
    // and route to appletRequestLaunchApplication. Skipped when no
    // titles are installed (homebrew-only consoles).
    if (!foyer::browser::switch_titles::titles().empty()) {
        foyer::library::System sw;
        sw.def       = &foyer::library::kVirtualSwitchDef;
        sw.root_path = "(virtual)";
        for (const auto& t : foyer::browser::switch_titles::titles()) {
            foyer::library::Game g{};
            char buf[40];
            std::snprintf(buf, sizeof(buf),
                "switch://%016lx", (unsigned long)t.application_id);
            g.path     = buf;
            g.stem     = t.name.empty() ? std::string{"(unnamed)"} : t.name;
            g.display  = g.stem;
            g.ext      = "nca";
            sw.games.push_back(std::move(g));
        }
        lib.systems.insert(lib.systems.begin(), std::move(sw));
    }

    foyer::browser::State state;

    // Session restore — only when we got chained back from a player
    // nro (recognised by the "foyer-resume" argv token the player's
    // chain-back inserts). Cold launch from hbmenu has no marker and
    // lands on Home view; this also cleans up any stale session
    // file from a prior run.
    foyer::browser::load_and_consume_session(state,
        foyer::browser::argv_has_resume_marker(argc, argv));

    const bool resuming_from_player =
        foyer::browser::argv_has_resume_marker(argc, argv);

    // One-shot self-update check on boot. Off-thread so the UI is up
    // immediately even on a slow CDN; the result is processed by the
    // foyer_job poll block in the main loop below.
    //
    // Skip the manifest scrapes entirely when we're resuming from a
    // player nro (the "foyer-resume" argv marker the player passes
    // when chain-launching back to the browser). The user already
    // saw the boot-time check on the cold launch a few minutes ago;
    // re-running it on every game-exit costs them several seconds
    // of network latency for data that's almost certainly unchanged.
    // The Cores Catalog page still has an explicit "Refresh
    // manifest" action when the user actually wants fresh data.
    if (!resuming_from_player) {
        state.foyer_job.start_check(foyer::library::config().foyer_manifest_url);
        state.request_refresh_manifest         = true;
        state.request_refresh_cheats_manifest  = true;
        state.request_refresh_bezels_manifest  = true;
    } else {
        foyer::log::write(
            "[boot] resume-from-player: skipping manifest fetches\n");
    }

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

        // Refresh the chrome status reads (battery / wifi). Internally
        // debounced to once per second so this is cheap to call every
        // frame — keeps the top-bar values current without a separate
        // timer hook.
        foyer::browser::hos_status::poll();

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

        // 0.5.0 Home action-row: Sleep + Power. Sleep is reversible so
        // it just transitions to the OS sleep state; the user wakes the
        // console and lands back on Home. Power is a hard shutdown — we
        // commit the SD device handle first so any pending writes flush
        // before the firmware unmounts.
        if (state.request_sleep) {
            state.request_sleep = false;
            foyer::log::write("[home_action] sleep requested\n");
            appletStartSleepSequence(true);
        }
        if (state.pending_profile_switch >= 0) {
            const int idx = state.pending_profile_switch;
            state.pending_profile_switch = -1;
            foyer::log::write("[profile] switching to secondary idx=%d\n", idx);
            foyer::browser::hos_status::switch_active(idx, app.vg());
        }
        if (state.request_power_off) {
            state.request_power_off = false;
            foyer::log::write("[home_action] power off requested\n");
            if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
                fsFsCommit(fs);
            }
            appletRequestToShutdown();
        }
        if (state.request_restart) {
            state.request_restart = false;
            foyer::log::write("[home_action] restart requested\n");
            if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
                fsFsCommit(fs);
            }
            appletRequestToReboot();
        }
        if (state.request_reboot_hekate) {
            state.request_reboot_hekate = false;
            foyer::log::write("[home_action] reboot to hekate requested\n");
            // Convention: copy the user's hekate payload at
            // /bootloader/update.bin to /atmosphere/reboot_payload.bin
            // so a normal reboot (with atmosphere's reboot-to-payload
            // feature enabled in system_settings.ini) lands on hekate.
            // If either path is missing we fall back to a regular
            // reboot — the user's existing atmosphere config decides
            // what boots next.
            std::FILE* in = std::fopen("/bootloader/update.bin", "rb");
            if (in) {
                std::FILE* out = std::fopen(
                    "/atmosphere/reboot_payload.bin", "wb");
                if (out) {
                    char buf[64 * 1024];
                    std::size_t n;
                    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0)
                        std::fwrite(buf, 1, n, out);
                    std::fclose(out);
                    foyer::log::write(
                        "[home_action] copied hekate payload to "
                        "atmosphere/reboot_payload.bin\n");
                }
                std::fclose(in);
            } else {
                foyer::log::write(
                    "[home_action] /bootloader/update.bin missing — "
                    "falling back to normal reboot\n");
            }
            if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
                fsFsCommit(fs);
            }
            appletRequestToReboot();
        }

        // "Restart now" from the post-download confirm modal.
        //
        // Critical: do NOT try to swap foyer.nro.new -> foyer.nro
        // in-place from inside the running process. devkitA64's
        // FAT/exFAT backing of the SD doesn't let us unlink the
        // currently-running .nro (the file IS the directory entry on
        // FAT — there are no POSIX inodes that survive a deleted
        // directory entry). The swap silently fails with EEXIST and
        // we end up chain-launching the SAME old foyer.nro back into
        // itself — which on hardware fully crashes Atmosphère hard
        // enough to need a power-cycle (observed in v0.4.0 user
        // report: romfsInit + nvgCreateDk both fail on the second
        // session after the chain-launch).
        //
        // Correct flow: chain-launch foyer.nro.new directly. hbloader
        // copies its bytes into memory + closes the file before the
        // new process starts main(). The new binary's
        // apply_staged_update_if_present() at boot then renames
        // foyer.nro.new -> foyer.nro from a state where neither file
        // is open, which works fine on FAT.
        if (state.request_restart_now) {
            state.request_restart_now = false;
            foyer::browser::mtp_stop();

            struct stat st{};
            std::string target = g_foyer_nro_path;
            if (::stat(g_foyer_nro_new_path.c_str(), &st) == 0 &&
                st.st_size > 0) {
                target = g_foyer_nro_new_path;
                foyer::log::write(
                    "[foyer_update] staged update detected; chain-launching "
                    "%s directly (rename happens on its boot)\n",
                    target.c_str());
            }
            // argv MUST start with the NRO path itself — libnx's
            // romfsInit() parses argv[0] to locate the .nro on disk
            // and opens it to read the romfs section. Earlier
            // versions passed "\"foyer-restart\"" alone, which
            // matches no file: romfsInit failed, nvg/deko3d aborted
            // with no fonts/textures, and the whole NRO crashed
            // hard enough to take Atmosphère with it. Match the
            // shape launch.cpp already uses for player-NRO chain-
            // launches: quoted-NRO-path followed by the marker arg.
            const std::string load_path = std::string{"sdmc:"} + target;
            const std::string argv =
                std::string{"\""} + load_path + "\" \"foyer-restart\"";
            envSetNextLoad(load_path.c_str(), argv.c_str());
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
            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerLibraryRescanned);
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
                w.set_status(foyer::i18n::tr(foyer::i18n::StringId::BannerFetchingCoresManifest));
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
                    state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerManifestFetchFail);
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

        // "Update all cores" footer from the Updates page. Cores
        // only — bezels/cheats/foyer-self each have their own row.
        // Bundling them produced an opaque "updating everything"
        // banner with no per-item visibility, so the action was
        // narrowed to just the most-asked-for path.
        if (state.request_update_all) {
            state.request_update_all = false;
            state.install_only_core.clear();
            state.install_force = false;
            state.request_install_cores = true;
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
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerCoresManifestFetchFail);
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
                        [&](const foyer::library::InstallProgress& ip) {
                            // Banner update on Started so the user
                            // sees per-core progress during the loop
                            // instead of a stale "Updating all cores"
                            // string the entire time.
                            if (ip.action == foyer::library::InstallAction::Started) {
                                char bb[200];
                                std::snprintf(bb, sizeof(bb),
                                    foyer::i18n::tr(foyer::i18n::StringId::BannerInstallingItem),
                                    ip.name.c_str());
                                state.banner_text = bb;
                                state.banner_ttl  = 240;
                            }
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
                    // reflects the new "up to date" status, and drop
                    // the per-row sidecar cache so the next build_items
                    // re-reads disk truth instead of stale "not
                    // installed" markers.
                    foyer::browser::set_manifest_cache(
                        foyer::library::fetch_manifest(
                            foyer::library::config().cores_manifest_url));
                    foyer::browser::invalidate_install_caches();
                    // If the install was triggered by the pre-launch
                    // update prompt ("Update before playing?"), the
                    // pending launch is suspended on State. Re-fire
                    // it now so the user sees the game boot through
                    // immediately after the core finishes downloading.
                    if (state.launch_after_core_install) {
                        state.launch_after_core_install = false;
                        if (totals.failed == 0) {
                            state.request_launch = true;
                        } else {
                            // Install failed — keep them in foyer with
                            // the failure banner already on State.
                        }
                    }
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
            const std::string only = std::move(state.install_only_shader);
            state.install_only_shader.clear();
            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerFetchingShaderManifest);
            state.banner_ttl  = 60;
            app.tick();
            auto sm = foyer::library::fetch_shader_manifest(
                foyer::library::config().shaders_manifest_url);
            if (sm.presets.empty()) {
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerShadersManifestFetchFail);
                state.banner_ttl  = 240;
            } else {
                if (!only.empty()) {
                    std::erase_if(sm.presets,
                        [&](const auto& p) { return p.name != only; });
                }
                if (sm.presets.empty()) {
                    char b[200];
                    std::snprintf(b, sizeof(b),
                        "Shader preset not in manifest: %s", only.c_str());
                    state.banner_text = b;
                    state.banner_ttl  = 240;
                } else {
                    foyer::browser::set_shaders_manifest_cache(sm);
                    const auto totals = foyer::library::install_shaders(sm,
                        [&](const foyer::library::ShaderInstallProgress& p) {
                            // Banner-update on Started so the user sees
                            // per-preset progress during the loop;
                            // terminal action codes (Installed/Updated/
                            // Skipped/Failed) only get logged below.
                            if (p.action == foyer::library::ShaderInstallAction::Started) {
                                char b[200];
                                std::snprintf(b, sizeof(b),
                                    foyer::i18n::tr(foyer::i18n::StringId::BannerInstallingItem),
                                    p.name.c_str());
                                state.banner_text = b;
                                state.banner_ttl  = 240;
                            }
                            app.tick();
                        });
                    if (totals.failed > 0) {
                        char b[160];
                        std::snprintf(b, sizeof(b),
                            "%d shader preset%s failed - check log",
                            totals.failed, totals.failed == 1 ? "" : "s");
                        state.banner_text = b;
                        state.banner_ttl = 360;
                    } else {
                        // Success: nothing to surface — the per-preset
                        // banner during the loop already showed every
                        // step the user cares about.
                        state.banner_text.clear();
                        state.banner_ttl = 0;
                    }
                }
            }
            foyer::browser::invalidate_install_caches();
        }

        // Shader manifest refresh (pull-only — per-preset install
        // happens via OpInstallSingleShaderPreset above).
        if (state.request_refresh_shaders_manifest) {
            state.request_refresh_shaders_manifest = false;
            auto sm = foyer::library::fetch_shader_manifest(
                foyer::library::config().shaders_manifest_url);
            if (sm.presets.empty()) {
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerShadersManifestFetchFail);
                state.banner_ttl  = 240;
            } else {
                foyer::browser::set_shaders_manifest_cache(std::move(sm));
                state.banner_text.clear();
                state.banner_ttl = 0;
            }
        }

        // Cheat-pack manifest refresh. Pull only — per-row install
        // happens via OpInstallSingleCheatPack below.
        if (state.request_refresh_cheats_manifest) {
            state.request_refresh_cheats_manifest = false;
            auto cm = foyer::library::fetch_cheat_manifest(
                foyer::library::config().cheats_manifest_url);
            if (cm.packs.empty()) {
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerCheatsManifestFetchFail);
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
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerBezelsManifestFetchFail);
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
            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerFetchingCheatsManifest);
            state.banner_ttl  = 60;
            app.tick();
            auto cm = foyer::library::fetch_cheat_manifest(
                foyer::library::config().cheats_manifest_url);
            if (cm.packs.empty()) {
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerCheatsManifestFetchFail);
                state.banner_ttl  = 240;
            } else {
                // Refresh the cache so per-pack rows reflect the
                // post-install state on the next Settings draw.
                foyer::browser::set_cheats_manifest_cache(cm);
                const auto totals = foyer::library::install_cheats(cm,
                    [&](const foyer::library::CheatInstallProgress& p) {
                        char b[160];
                        const char* verb =
                            foyer::i18n::tr(
                                p.action == foyer::library::CheatInstallAction::Skipped   ? foyer::i18n::StringId::ActionPastSkipped :
                                p.action == foyer::library::CheatInstallAction::Updated   ? foyer::i18n::StringId::ActionPastUpdated :
                                p.action == foyer::library::CheatInstallAction::Installed ? foyer::i18n::StringId::ActionPastInstalled
                                                                                          : foyer::i18n::StringId::ActionPastFailed);
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
            foyer::browser::invalidate_install_caches();
        }

        // Bezel-pack install. Mirrors cheats; honors
        // install_only_bezel for per-row picks.
        if (state.request_install_bezels) {
            state.request_install_bezels = false;
            const auto only = std::move(state.install_only_bezel);
            state.install_only_bezel.clear();
            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerFetchingBezelsManifest);
            state.banner_ttl  = 60;
            app.tick();
            auto bm = foyer::library::fetch_bezel_manifest(
                foyer::library::config().bezels_manifest_url);
            if (bm.packs.empty()) {
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerBezelsManifestFetchFail);
                state.banner_ttl  = 240;
            } else {
                foyer::browser::set_bezels_manifest_cache(bm);
                const auto totals = foyer::library::install_bezels(bm,
                    [&](const foyer::library::BezelInstallProgress& p) {
                        char b[160];
                        const char* verb =
                            foyer::i18n::tr(
                                p.action == foyer::library::BezelInstallAction::Skipped   ? foyer::i18n::StringId::ActionPastSkipped :
                                p.action == foyer::library::BezelInstallAction::Updated   ? foyer::i18n::StringId::ActionPastUpdated :
                                p.action == foyer::library::BezelInstallAction::Installed ? foyer::i18n::StringId::ActionPastInstalled
                                                                                          : foyer::i18n::StringId::ActionPastFailed);
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
            foyer::browser::invalidate_install_caches();
        }

        if (state.request_launch) {
            const auto& sys  = lib.systems[state.system_index];
            const auto& game = sys.games[state.game_index];

            // Pre-launch core-update prompt. If the chosen core has
            // an update pending in the cached manifest AND the user
            // hasn't already pressed "Play anyway" for this exact
            // version, surface the prompt before consuming
            // request_launch — let them pick Update / Play / Cancel.
            // The prompt's input handler in views.cpp drives the
            // follow-up: Update raises request_install_cores +
            // launch_after_core_install, Play anyway records
            // skip_version + lets request_launch re-fire next frame,
            // Cancel just drops the launch.
            //
            // launch_after_core_install short-circuits the check
            // when we're resuming after the install Worker finished
            // — at that point the core IS up-to-date.
            bool defer_to_prompt = false;
            if (!state.update_prompt_open
                && !state.launch_after_core_install
                && sys.def) {
                const auto* core = foyer::library::resolve_core(*sys.def, game.path);
                const auto* mc   = foyer::browser::cached_core_manifest();
                if (core && mc) {
                    for (const auto& c : mc->cores) {
                        if (c.name != std::string{core->name}) continue;
                        const auto local =
                            foyer::library::installed_core_version(c.nro);
                        const bool has_update =
                            !local.empty() && local != c.version;
                        const bool skipped =
                            foyer::library::is_version_skipped(
                                foyer::library::SkipKind::Core,
                                c.name, c.version);
                        if (has_update && !skipped) {
                            state.update_prompt_open = true;
                            state.update_prompt_index = 1;
                            state.update_prompt_core_name    = c.name;
                            state.update_prompt_core_version = c.version;
                            // Leave request_launch set so the
                            // "Play anyway" path resumes seamlessly.
                            defer_to_prompt = true;
                        }
                        break;
                    }
                }
            }
            if (!defer_to_prompt) {
                state.request_launch = false;
                const int resume = state.request_resume_slot;
                state.request_resume_slot = -1;
                // Stamp last_played BEFORE launch_game (which
                // usually app.quit()s on success and doesn't
                // return). Powers the home view's Recent virtual
                // system + the Resume action.
                foyer::library::mark_per_game_played(game.path);
                // Stash the current view + cursor so foyer's next
                // boot (after the core exits and chains back)
                // lands the user exactly where they were. One-
                // shot file with a 1h TTL, consumed + deleted on
                // the next load_and_consume_session call. Cold
                // launch still defaults to Home.
                foyer::browser::save_session(state);
                if (foyer::browser::launch_game(sys, game, resume)) {
                    foyer::browser::mtp_stop();
                    app.quit();
                } else if (game.path.starts_with("switch://")) {
                    // Switch-title launch went through appletRequest-
                    // LaunchApplication and the firmware refused. Most
                    // common cause is hbloader-applet running without
                    // application-launch permission (some 17.x+
                    // setups). Surface a specific banner — the
                    // "Core not installed" wording is meaningless here.
                    state.banner_text = foyer::i18n::tr(
                        foyer::i18n::StringId::BannerSwitchLaunchDenied);
                    state.banner_ttl  = 300;
                } else {
                    const auto* core = foyer::library::resolve_core(*sys.def, game.path);
                    char bb[200];
                    std::snprintf(bb, sizeof(bb),
                        foyer::i18n::tr(foyer::i18n::StringId::BannerCoreNotInstalled),
                        core ? std::string{core->name}.c_str() : "?");
                    state.banner_text = bb;
                    state.banner_ttl  = 180;
                }
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
                            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerNoCoverCandidates);
                            state.banner_ttl  = 240;
                        } else {
                            // views.cpp owns the OpPickCover enum
                            // value — expose via open_cover_picker so
                            // main.cpp doesn't reach into the
                            // anonymous-namespace settings:: ops.
                            char tbuf[200];
                            std::snprintf(tbuf, sizeof(tbuf),
                                foyer::i18n::tr(foyer::i18n::StringId::PickCoverForGame),
                                std::string{g.stem}.c_str());
                            foyer::browser::open_cover_picker(state,
                                tbuf,
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
                state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerScrapeWorkerFailed);
                state.banner_ttl  = 240;
            }
        } else if (state.request_scrape_kind != foyer::browser::State::ScrapeKind::None) {
            state.request_scrape_kind = foyer::browser::State::ScrapeKind::None;
            state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerScrapeAlreadyRunning);
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
                    state.banner_text = foyer::i18n::tr(foyer::i18n::StringId::BannerScrapeNoCovers);
                    state.banner_ttl  = 240;
                }
            }
        }
    }
    foyer::browser::mtp_stop();

    // Don't try to apply the staged update on exit — the rename
    // never works while we're still running because devkitA64's
    // FAT/exFAT layer treats the running .nro as held open. The
    // earlier version's exit-time call would fail, and (worse) on
    // the v0.4.1 chain-launch path it would unlink foyer.nro.new
    // out from under hbloader, leaving the user stuck on the old
    // version. Boot-time apply_staged_update_if_present() at the
    // start of main() handles the swap on the next launch — by then
    // the previous foyer process is gone and FAT lets us replace
    // the file.
    return 0;
}
