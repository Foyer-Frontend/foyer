#include "launch.hpp"
#include "library/scanner.hpp"
#include "library/system_db.hpp"
#include "library/per_game.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
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

bool launch_game(const library::System& sys, const library::Game& game,
                 int resume_slot) {
    if (!sys.def) return false;

    const auto* core = library::resolve_core(*sys.def, game.path);
    if (!core) {
        foyer::log::write("[launch] no core mapped for system %.*s\n",
            (int)sys.def->folder_name.size(), sys.def->folder_name.data());
        return false;
    }

    char nro_path[256];
    std::snprintf(nro_path, sizeof(nro_path),
        "/foyer/cores/foyer-%.*s.nro",
        (int)core->name.size(), core->name.data());

    struct stat st{};
    if (::stat(nro_path, &st) != 0) {
        foyer::log::write("[launch] core nro missing: %s\n", nro_path);
        return false;
    }

    // hbloader expects argv quoted with sdmc: prefix.
    std::string sd_nro = std::string{"sdmc:"} + nro_path;
    std::string sd_rom = std::string{"sdmc:"} + game.path;

    // argv[0] = nro path, argv[1] = rom path, argv[2] = our own path so the
    // player can chain back to us cleanly without hardcoding. argv[3] is an
    // optional "resume=<slot>" hint.
    char argv[1024];
    if (resume_slot >= 0) {
        std::snprintf(argv, sizeof(argv),
            "\"%s\" \"%s\" \"%s\" \"resume=%d\"",
            sd_nro.c_str(), sd_rom.c_str(),
            browser_self_path().c_str(), resume_slot);
    } else {
        std::snprintf(argv, sizeof(argv), "\"%s\" \"%s\" \"%s\"",
            sd_nro.c_str(), sd_rom.c_str(), browser_self_path().c_str());
    }

    if (R_FAILED(envSetNextLoad(sd_nro.c_str(), argv))) {
        foyer::log::write("[launch] envSetNextLoad failed\n");
        return false;
    }
    foyer::log::write("[launch] queued %s rom=%s back=%s resume=%d\n",
        sd_nro.c_str(), sd_rom.c_str(),
        browser_self_path().c_str(), resume_slot);
    return true;
}

int latest_state_slot(const library::System& sys, const library::Game& game) {
    if (!sys.def) return -1;

    // States are written under the rom's REAL system folder, never
    // under a virtual carousel tile. Recover the real folder from the
    // rom path when needed so Resume Last + GameDetail's Continue
    // affordance work correctly when the user opens a game from
    // Recent or Favorites.
    const auto* def = library::is_virtual_system(*sys.def)
        ? library::origin_system_for_rom(game.path)
        : sys.def;
    if (!def) return -1;

    int best_slot = -1;
    std::time_t best_mtime = 0;
    for (int slot = 0; slot < 10; slot++) {
        char path[256];
        std::snprintf(path, sizeof(path),
            "/foyer/states/%.*s/%s.%d.state",
            (int)def->folder_name.size(), def->folder_name.data(),
            game.stem.c_str(), slot);
        struct stat st{};
        if (::stat(path, &st) != 0) continue;
        if (st.st_mtime > best_mtime) {
            best_mtime = st.st_mtime;
            best_slot  = slot;
        }
    }
    return best_slot;
}

} // namespace foyer::browser
