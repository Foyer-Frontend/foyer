#include "launch.hpp"
#include "library/scanner.hpp"
#include "library/system_db.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#include <switch.h>

namespace foyer::browser {
namespace {

// argv[0] of the foyer process — the path hbloader used to launch us, or a
// reasonable default if the env was odd. Captured once at first call.
std::string browser_self_path() {
    static std::string s = []{
        std::string out;
        if (envHasArgv()) {
            // libnx exposes argv as a flat string list; argv[0] is the nro
            // path under sdmc:/. Pull the first token.
            const char* a = (const char*)envGetArgv();
            if (a) {
                while (*a == ' ') a++;
                if (*a == '"') {
                    a++;
                    const char* end = std::strchr(a, '"');
                    if (end) out.assign(a, end - a);
                } else {
                    const char* end = std::strchr(a, ' ');
                    out.assign(a, end ? (std::size_t)(end - a) : std::strlen(a));
                }
            }
        }
        if (out.empty()) {
            out = "sdmc:/switch/foyer/foyer.nro";
        }
        return out;
    }();
    return s;
}

} // namespace

bool launch_game(const library::System& sys, const library::Game& game) {
    if (!sys.def) return false;

    char nro_path[256];
    std::snprintf(nro_path, sizeof(nro_path),
        "/foyer/cores/foyer-%.*s.nro",
        (int)sys.def->core_name.size(), sys.def->core_name.data());

    struct stat st{};
    if (::stat(nro_path, &st) != 0) {
        foyer::log::write("[launch] core nro missing: %s\n", nro_path);
        return false;
    }

    // hbloader expects argv quoted with sdmc: prefix.
    std::string sd_nro = std::string{"sdmc:"} + nro_path;
    std::string sd_rom = std::string{"sdmc:"} + game.path;

    // argv[0] = nro path, argv[1] = rom path, argv[2] = our own path so the
    // player can chain back to us cleanly without hardcoding.
    char argv[1024];
    std::snprintf(argv, sizeof(argv), "\"%s\" \"%s\" \"%s\"",
        sd_nro.c_str(), sd_rom.c_str(), browser_self_path().c_str());

    if (R_FAILED(envSetNextLoad(sd_nro.c_str(), argv))) {
        foyer::log::write("[launch] envSetNextLoad failed\n");
        return false;
    }
    foyer::log::write("[launch] queued %s rom=%s back=%s\n",
        sd_nro.c_str(), sd_rom.c_str(), browser_self_path().c_str());
    return true;
}

} // namespace foyer::browser
