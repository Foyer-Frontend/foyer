/*
 * foyer 0.6.0 — borealis app shell.
 *
 * Phase A bootstrapped brls + a minimal HomeActivity. Phase B wires service
 * init back in (i18n, library config, self-update) so users can keep
 * upgrading from one alpha to the next. Settings / library scan / Switch
 * title browser come back in subsequent alphas.
 *
 * Reference: borealis_template/demo/src/main.cpp + Moonlight Switch.
 */

#include <borealis.hpp>
#include <cstring>
#include <string_view>

#include "activity/game_activity.hpp"
#include "activity/home_activity.hpp"
#include "activity/splash_activity.hpp"
#include "activity/system_activity.hpp"
#include "activity/wizard_activity.hpp"
#include "tab/settings_tab.hpp"
#include "first_run.hpp"
#include "hos_status.hpp"
#include "library_state.hpp"
#include "manifest_cache.hpp"
#include "mtp.hpp"
#include "self_update.hpp"
#include "theme_watcher.hpp"
#include "update_check.hpp"

#include "i18n/i18n.hpp"
#include "library/config.hpp"
#include "library/switch_titles.hpp"

#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#include "net/http.hpp"
#include "platform/log.hpp"

using namespace brls::literals;

namespace {

void apply_saved_language() {
    const auto& saved = foyer::library::config().language;
    if (saved.empty()) return;
    using L = foyer::i18n::Language;
    if      (saved == "en")    foyer::i18n::set_language(L::English);
    else if (saved == "es")    foyer::i18n::set_language(L::Spanish);
    else if (saved == "pt-BR") foyer::i18n::set_language(L::PortugueseBrazil);
    // Unrecognised codes silently fall through to system-detected default.
}

}  // namespace

int main(int argc, char* argv[])
{
    // Open the log file FIRST so every subsequent foyer::log::write
    // lands on disk. The legacy App::App() did this; we lost it
    // when ripping App out for the brls cutover, so logs went to
    // a never-opened FILE* and crashes printed nothing diagnostic.
    foyer::log::init_file();
    foyer::log::write("foyer %s starting\n", FOYER_DISPLAY_VERSION);

    // Read libnx argv[0] BEFORE brls touches anything — needed to compute
    // where to write self-update payloads. Stripping the ".new" suffix
    // here means apply_staged_if_present() (called below, after brls
    // owns the romfs fd) hits the right path.
    foyer::browser::self_update::detect_paths();

    // CLI flags borrowed from borealis_template — useful when running
    // on PC for view debugging; harmless on Switch (argv is just the
    // NRO path + our chain-launch markers).
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-d") == 0) {
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        } else if (std::strcmp(argv[i], "-v") == 0) {
            brls::Application::enableDebuggingView(true);
        }
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    foyer::log::write("[boot] brls::Application::init…\n");
    if (!brls::Application::init()) {
        foyer::log::write("[boot] brls::Application::init FAILED\n");
        brls::Logger::error("foyer: brls::Application::init failed");
        return EXIT_FAILURE;
    }
    foyer::log::write("[boot] brls init ok\n");

    // Re-enable HOS idle auto-sleep. Brls (via XITRIX's moonlight
    // build) defaults to media-playback mode + auto-sleep disabled
    // because that fork drives video; foyer's a UI shell, the
    // console should sleep when the user walks away.
    appletSetMediaPlaybackState(false);
    appletSetAutoSleepDisabled(false);
    foyer::log::write("[boot] idle auto-sleep enabled\n");

    // Register foyer-specific theme colors before any XML inflates.
    // Splash references these for the bg overlay + custom progress bar.
    // Dark overlay is heavier so the pixel-art bg dims toward the
    // brand panel; light overlay is gentle so colors still pop.
    brls::Theme::getDarkTheme().addColor("foyer/splash_overlay",
        nvgRGBA(15, 18, 30, 190));
    brls::Theme::getLightTheme().addColor("foyer/splash_overlay",
        nvgRGBA(245, 248, 255, 140));
    // Translucent top/bottom bar tint that lets the backdrop bleed
    // through while still keeping clock / wifi / battery / button
    // hints legible. Dark = ~70% black for dark backdrops, Light =
    // ~70% white for light themes.
    brls::Theme::getDarkTheme().addColor("foyer/bar_overlay",
        nvgRGBA(0, 0, 0, 180));
    brls::Theme::getLightTheme().addColor("foyer/bar_overlay",
        nvgRGBA(255, 255, 255, 180));
    brls::Theme::getDarkTheme().addColor("foyer/splash_bar_track",
        nvgRGBA(255, 255, 255, 50));
    brls::Theme::getLightTheme().addColor("foyer/splash_bar_track",
        nvgRGBA(0, 0, 0, 50));
    brls::Theme::getDarkTheme().addColor("foyer/splash_bar_fill",
        nvgRGBA(0, 200, 255, 255));
    brls::Theme::getLightTheme().addColor("foyer/splash_bar_fill",
        nvgRGBA(0, 130, 220, 255));

    // brls now owns the romfs fd. Safe to swap the staged-update file in.
    // Returns true when a stage was applied — in that case our own
    // romfs handle is gone (romfsExit inside) and continuing this
    // boot would crash the next XML inflate. exit(0) so the user's
    // next launch picks up the freshly-renamed nro cleanly.
    if (foyer::browser::self_update::apply_staged_if_present()) {
        foyer::log::write(
            "[boot] staged nro applied during boot — exiting; relaunch\n");
        std::exit(0);
    }
    foyer::browser::self_update::scrub_legacy_default_bezel();
    if (foyer::library::config().scrub_extracted_enabled) {
        foyer::browser::self_update::scrub_extract_lru(
            foyer::library::config().scrub_extracted_days);
    }
    foyer::log::write("[boot] self_update applied\n");

    // curl one-shot init must run on the main thread before any
    // worker spawns one (curl_global_init isn't reentrant on
    // libnx's socket layer). Wizard's manifest prefetch + future
    // download workers all rely on this.
    foyer::net::init();
    foyer::log::write("[boot] net::init done\n");

    // Pull foyer's language overrides + i18n catalogues. Independent of
    // brls's own i18n (which serves brls strings). Both live side-by-
    // side on romfs:/i18n.
    foyer::i18n::init();
    apply_saved_language();
    foyer::log::write("[boot] i18n init done\n");

    // Window must exist before getNVGContext() returns a usable
    // pointer — hos_status::init below caches an NVG image handle for
    // the avatar and would otherwise allocate against a null context.
    brls::Application::createWindow("foyer/title"_i18n);
    brls::Application::setGlobalQuit(false);

    // Pull the active user's avatar + nickname from libnx
    // accountsService so the profile circle on Home shows real
    // data. Cheap (libnx call + JPEG decode) so we keep it on the
    // boot path rather than hand it to the splash worker.
    foyer::browser::hos_status::init(brls::Application::getNVGContext());
    foyer::log::write("[boot] hos_status init done\n");

    // Custom XML views referenced from XML layouts must register
    // before the first activity push so the inflater finds them.
    using namespace ::foyer::browser;
    brls::Application::registerXMLView("FoyerGeneralTab",  FoyerGeneralTab::create);
    brls::Application::registerXMLView("FoyerAccountsTab", FoyerAccountsTab::create);
    brls::Application::registerXMLView("FoyerLibraryTab",  FoyerLibraryTab::create);
    brls::Application::registerXMLView("FoyerCoresTab",    FoyerCoresTab::create);
    brls::Application::registerXMLView("FoyerEmulatorsTab", FoyerEmulatorsTab::create);
    brls::Application::registerXMLView("FoyerBezelsTab",   FoyerBezelsTab::create);
    brls::Application::registerXMLView("FoyerShadersTab",  FoyerShadersTab::create);
    brls::Application::registerXMLView("FoyerCheatsTab",   FoyerCheatsTab::create);
    brls::Application::registerXMLView("FoyerDownloadsTab", FoyerDownloadsTab::create);
    brls::Application::registerXMLView("FoyerUpdatesTab",  FoyerUpdatesTab::create);
    brls::Application::registerXMLView("FoyerAboutTab",    FoyerAboutTab::create);

    // SplashActivity owns the rest of the boot work: kicks a Worker
    // for the library scan + (first-run only) manifest prefetch,
    // polls it from a RepeatingTask, then transitions to Home (and
    // optionally the wizard) when the worker reports done. Replaces
    // the prior blank-window-during-boot UX.
    // Live HOS Light/Dark tracking — polls setsysGetColorSetId once
    // per second so flipping the system theme takes effect without
    // relaunching foyer.
    foyer::browser::theme_watcher::start();

    // Library scan + Switch title enumeration moved into the splash
    // worker (see SplashActivity::onContentAvailable). Doing them
    // here blocks the main thread before mainLoop runs — with ~200
    // installed titles, that's seconds of black screen on first boot
    // because nsGetApplicationControlData is a slow IPC per title.
    // The splash now owns the wait; main thread renders the brand
    // immediately.

    // libhaze MTP — auto-spin on boot when either mount toggle is on.
    // The user can flip the toggles later in Settings → Library to
    // start/stop the server without rebooting.
    if (foyer::library::config().mtp_expose_roms) {
        foyer::browser::mtp_start();
    }

    // Return-from-core fast path. launch_game writes
    // /foyer/data/cache/last_session.txt with the system folder +
    // game path before chain-launching the core. If that marker
    // exists and is recent (<24 h) we infer "user just came back
    // from a game" and skip the network-heavy splash entirely:
    //   - library_state::rescan inline (cache fast-path, ~100 ms)
    //   - push Home → SystemActivity(folder) → GameActivity(path)
    //   - delete the marker so the next plain boot doesn't repeat
    // Manifest fetches + update check + asset pack download all
    // wait until the next fresh boot.
    bool fast_returned = false;
    {
        constexpr const char* kMarker = "/foyer/data/cache/last_session.txt";
        struct stat mst{};
        // 86400s window covers an overnight gap between "user quit
        // the core, console went to sleep, user woke it up the next
        // morning" — the original 300s gate dropped them on Home
        // even though they very clearly wanted to keep playing the
        // same rom. The marker is unlinked on consume below, so a
        // stale marker can only ever fire once.
        if (::stat(kMarker, &mst) == 0
            && std::time(nullptr) - mst.st_mtime < 86400) {
            if (auto* m = std::fopen(kMarker, "rb")) {
                char buf[512];
                const auto n = std::fread(buf, 1, sizeof(buf) - 1, m);
                std::fclose(m);
                buf[n] = '\0';
                std::string text{buf};
                const auto nl = text.find('\n');
                if (nl != std::string::npos) {
                    std::string folder = text.substr(0, nl);
                    std::string path   = text.substr(nl + 1);
                    while (!path.empty()
                        && (path.back() == '\n' || path.back() == '\r'))
                        path.pop_back();
                    foyer::log::write(
                        "[boot] return-from-core: folder=%s path=%s\n",
                        folder.c_str(), path.c_str());
                    // Bail from the fast-path if the switch_titles
                    // cache is empty / missing — load_switch_titles
                    // would otherwise enumerate 200 installed
                    // applications via nsGetApplicationControlData
                    // synchronously (~1 min) before pushing Home
                    // and the user sees nothing rendered. The splash
                    // path has a progress callback wired to the bar,
                    // so route through it instead.
                    struct stat cst{};
                    const bool cache_warm =
                        ::stat("/foyer/data/cache/switch_titles.cache", &cst) == 0
                        && cst.st_size > 16;
                    if (!cache_warm) {
                        foyer::log::write(
                            "[boot] return-from-core: switch_titles "
                            "cache cold — falling through to splash\n");
                        // Leave fast_returned=false so the main
                        // else-branch below runs the full splash.
                    } else {
                        // 0.7.18 ran load_switch_titles_cached() +
                        // refresh_switch_titles_async() here for a
                        // faster chain-back, but the async worker's
                        // libnx IPC chain crashed on real hardware
                        // (User Break right after live_app_ids).
                        // Reverted to the original blocking call;
                        // cache hits make it microseconds anyway.
                        foyer::library::load_switch_titles();
                        foyer::browser::library_state::rescan();
                        // Position-restore: seed each activity with
                        // the tile the user was on before launching
                        // the core. Home → System B-back lands on
                        // the system tile (matches `folder`); System
                        // → Game B-back lands on the game tile
                        // (matches `path`).
                        //
                        // Deferred-population: Home and System are
                        // pushed under GameActivity and only become
                        // visible when the user B-backs. Skipping
                        // their populateCarousel + cover preload at
                        // push time cuts the chain-back blank-screen
                        // window dramatically (no point decoding
                        // dozens of JPEGs nobody's about to see).
                        // onResume picks up the deferred work when
                        // each activity actually surfaces.
                        auto* home = new ::foyer::browser::HomeActivity();
                        home->setPreselectSystem(folder);
                        home->setDeferredPopulation(true);
                        brls::Application::pushActivity(home);
                        auto* sys = new ::foyer::browser::SystemActivity(folder, folder);
                        sys->setPreselectGame(path);
                        sys->setDeferredPopulation(true);
                        brls::Application::pushActivity(sys);
                        brls::Application::pushActivity(
                            new ::foyer::browser::GameActivity(folder, path));
                        fast_returned = true;
                    }
                }
            }
        }
        ::unlink(kMarker);
    }

    if (fast_returned) {
        // Skip the splash / wizard / manifest checks branch — the
        // user is back where they left off.
    } else if (!::foyer::browser::first_run::is_complete()) {
        foyer::log::write("[boot] first-run marker missing — wizard\n");
        ::foyer::browser::manifest_cache::prefetch({});
        foyer::log::write("[boot] pushing HomeActivity\n");
        brls::Application::pushActivity(new ::foyer::browser::HomeActivity());
        brls::Application::pushActivity(new ::foyer::browser::WizardActivity());
    } else {
        // Splash-only stack until the worker finishes — Home is
        // pushed from inside SplashActivity::handoff(). Walking
        // a two-deep stack (Home under Splash) during boot let
        // Home's action row + carousel bleed through brls's
        // first frames before Splash's image fully decoded.
        // Splash-only avoids any chance of a translucent draw
        // showing the layer below.
        foyer::log::write("[boot] pushing SplashActivity (sole)\n");
        brls::Application::pushActivity(
            new ::foyer::browser::SplashActivity(),
            brls::TransitionAnimation::NONE);

        // Boot-time update check moved into SplashActivity's
        // worker so the splash blocks on the user's decision —
        // see SplashActivity::onContentAvailable and
        // update_check::kick_boot. Previously kicking it here
        // raced the splash handoff: dialog landed on Home while
        // boot had already completed underneath.
    }
    foyer::log::write("[boot] entering main loop\n");

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
