/*
 * foyer 0.6.0 — Phase A: borealis bring-up.
 *
 * Replaces the legacy nanovg-direct main with a borealis Application shell
 * that pushes a stub HomeActivity. Existing service-layer code (launch,
 * mtp, seed_assets, session, switch_titles, library scanner, updater)
 * lives on disk untouched and gets wired back in starting in Phase B —
 * Phase A's only job is to prove the build chain works on Switch.
 *
 * Reference: borealis_template/demo/src/main.cpp + Moonlight Switch.
 */

#include <borealis.hpp>

#include "activity/home_activity.hpp"

using namespace brls::literals;

int main(int argc, char* argv[])
{
    // CLI flags borrowed from borealis_template — useful when running on
    // PC for view debugging; harmless on Switch (argv is just the NRO
    // path + our chain-launch markers).
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

    brls::Application::createWindow("foyer/title"_i18n);
    brls::Application::setGlobalQuit(false);

    brls::Application::pushActivity(new ::foyer::browser::HomeActivity());

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}
