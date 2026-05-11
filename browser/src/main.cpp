/*
 * foyer 0.6.0 — borealis app shell.
 *
 * Phase A bootstrapped brls + a stub HomeActivity. Phase B wires service
 * init back in (i18n, library config, self-update) so users can keep
 * upgrading from one alpha to the next. Settings / library scan / Switch
 * title browser come back in subsequent alphas.
 *
 * Reference: borealis_template/demo/src/main.cpp + Moonlight Switch.
 */

#include <borealis.hpp>
#include <cstring>
#include <string_view>

#include "activity/home_activity.hpp"
#include "activity/splash_activity.hpp"
#include "activity/wizard_activity.hpp"
#include "tab/settings_tab.hpp"
#include "first_run.hpp"
#include "hos_status.hpp"
#include "library_state.hpp"
#include "manifest_cache.hpp"
#include "self_update.hpp"
#include "theme_watcher.hpp"
#include "update_check.hpp"

#include "i18n/i18n.hpp"
#include "library/config.hpp"
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

    // brls now owns the romfs fd. Safe to swap the staged-update file in.
    foyer::browser::self_update::apply_staged_if_present();
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

    // Library scan synchronous — fast on the cache fast-path. Done
    // before pushing Home so the tile game-count banners have real
    // values to display. The splash overlay below covers any visible
    // hitch.
    foyer::browser::library_state::rescan();
    foyer::log::write("[boot] library scan done\n");

    // Push Home first, then Splash on top. When the splash worker
    // finishes its background work (manifest prefetch on first-run,
    // otherwise a no-op short delay), it pops itself off the stack
    // and Home is revealed. This avoids the pop-and-push race that
    // crashed earlier alphas — only one transition fires.
    foyer::log::write("[boot] pushing HomeActivity\n");
    brls::Application::pushActivity(new ::foyer::browser::HomeActivity());

    if (!::foyer::browser::first_run::is_complete()) {
        foyer::log::write("[boot] first-run marker missing — wizard\n");
        ::foyer::browser::manifest_cache::prefetch();
        brls::Application::pushActivity(new ::foyer::browser::WizardActivity());
    } else {
        foyer::log::write("[boot] pushing SplashActivity\n");
        brls::Application::pushActivity(new ::foyer::browser::SplashActivity());

        // Boot-time update check — silent unless a newer manifest
        // version is published, in which case the user gets a
        // Yes/No prompt over the home screen. Gated on the
        // General → Check for updates on boot toggle; users who
        // want no network on boot opt out in Settings.
        if (::foyer::library::config().update_check_on_boot) {
            ::foyer::browser::update_check::kick(/*verbose=*/false);
        }
    }
    foyer::log::write("[boot] entering main loop\n");

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
