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
#include "self_update.hpp"  // detect_paths only; player doesn't self-update

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
    foyer::log::init("/foyer/data/logs");

    if (argc < 2) {
        foyer::log::write("[player-brls] no rom path in argv\n");
        return 1;
    }
    const std::string rom_path = normalise_argv_path(argv[1]);
    foyer::log::write("[player-brls] booting with rom=%s\n", rom_path.c_str());

    if (!brls::Application::init()) {
        foyer::log::write("[player-brls] brls init failed\n");
        return 1;
    }
    brls::Application::createWindow("foyer");

    brls::Application::pushActivity(
        new foyer::player::EmulatorActivity(rom_path));

    while (brls::Application::mainLoop())
        ;

    return 0;
}
