// foyer player entry — borealis variant.
//
// Mirrors the existing main.cpp's argv contract:
//   argv[0]  = nro path on sd
//   argv[1]  = rom path
//   argv[2+] = optional "key=value" hint tokens
//
// Replaces the foyer_render (nanovg-deko3d direct) window loop with
// brls. The EmulatorActivity owns the libretro Frontend and drives
// retro_run from a brls RepeatingTask; an EmulatorView hosts the
// video surface inside brls's nanovg context.

#include "emulator_activity.hpp"

#include "platform/log.hpp"

#include <borealis.hpp>

#include <cstring>
#include <string>
#include <string_view>

namespace {

// Strip the "sdmc:" devoptab prefix that hbloader argv carries —
// POSIX stdlib opens want the bare path.
std::string normalise_argv_path(std::string_view in) {
    if (in.starts_with("\"") && in.ends_with("\"")) {
        in = in.substr(1, in.size() - 2);
    }
    if (in.starts_with("sdmc:")) {
        in = in.substr(5);
    }
    return std::string{in};
}

}  // namespace

int main(int argc, char** argv) {
    foyer::log::init_file();

    if (argc < 2) {
        foyer::log::write("[player-brls] no rom path in argv\n");
        return 1;
    }
    const std::string rom_path = normalise_argv_path(argv[1]);
    // argv[2] is the launcher's own nro path so we can chain-launch
    // back on Quit. Browser fills it in launch::launch_game; if the
    // user double-tapped foyer-<core>.nro from hbmenu directly,
    // argv[2] is absent and Quit just exits to homebrew menu.
    const std::string back_nro =
        (argc >= 3) ? normalise_argv_path(argv[2]) : std::string{};
    foyer::log::write("[player-brls] booting with rom=%s back=%s\n",
        rom_path.c_str(), back_nro.c_str());

    if (!brls::Application::init()) {
        foyer::log::write("[player-brls] brls init failed\n");
        return 1;
    }
    brls::Application::createWindow("foyer");

    brls::Application::pushActivity(
        new foyer::player::EmulatorActivity(rom_path, back_nro));

    while (brls::Application::mainLoop())
        ;

    return 0;
}
