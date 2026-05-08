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
#include "tab/settings_tab.hpp"
#include "self_update.hpp"

#include "i18n/i18n.hpp"
#include "library/config.hpp"

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

    if (!brls::Application::init()) {
        brls::Logger::error("foyer: brls::Application::init failed");
        return EXIT_FAILURE;
    }

    // brls now owns the romfs fd. Safe to swap the staged-update file in.
    foyer::browser::self_update::apply_staged_if_present();
    foyer::browser::self_update::scrub_legacy_default_bezel();

    // Pull foyer's language overrides + i18n catalogues. Independent of
    // brls's own i18n (which serves brls strings). Both live side-by-
    // side on romfs:/i18n.
    foyer::i18n::init();
    apply_saved_language();

    brls::Application::createWindow("foyer/title"_i18n);
    brls::Application::setGlobalQuit(false);

    // Custom XML views referenced from XML layouts must register before
    // the first activity push so the inflater finds them.
    brls::Application::registerXMLView(
        "FoyerSettingsTab", ::foyer::browser::SettingsTab::create);

    brls::Application::pushActivity(new ::foyer::browser::HomeActivity());

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
