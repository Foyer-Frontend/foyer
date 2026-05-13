#include "launch.hpp"
#include "library/config.hpp"
#include "library/scanner.hpp"
#include "library/switch_titles.hpp"
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

    // 0.5.5 Switch-title launcher: paths formatted "switch://<id>"
    // route through appletRequestLaunchApplication. The id is the
    // application_id we pulled out of nsListApplicationRecord at
    // boot. After this returns the firmware terminates foyer and
    // boots the title; the caller's app.quit() drains the rest.
    if (game.path.starts_with("switch://")) {
        const std::uint64_t app_id =
            ::foyer::library::switch_id_from_path(game.path);
        if (app_id == 0) {
            foyer::log::write("[launch] bad switch:// path: %s\n",
                game.path.c_str());
            return false;
        }
        if (!::foyer::library::launch_switch_title(app_id)) return false;
        ::foyer::library::mark_per_game_played(game.path);
        return true;
    }

    // External-launcher path: when the user has a standalone Switch
    // emulator nro for this system (PPSSPP, Dolphin, …) configured in
    // general.jsonc's external_cores AND the file exists on disk,
    // chain straight to it. The standalone owns its own UI; foyer's
    // libretro player loop is bypassed entirely.
    //
    // For virtual systems (Recent / Favorites) we need to resolve
    // through the rom's origin system to find the right external
    // entry, just like the libretro path does for cores.
    {
        const auto* eff_def = library::is_virtual_system(*sys.def)
            ? library::origin_system_for_rom(game.path)
            : sys.def;
        if (eff_def) {
            const auto ext_nro =
                library::config().external_core_for(eff_def->folder_name);
            if (!ext_nro.empty()) {
                struct stat est{};
                if (::stat(ext_nro.c_str(), &est) == 0) {
                    const std::string sd_ext = std::string{"sdmc:"} + ext_nro;
                    const std::string sd_rom = std::string{"sdmc:"} + game.path;
                    char ext_argv[768];
                    std::snprintf(ext_argv, sizeof(ext_argv),
                        "\"%s\" \"%s\"", sd_ext.c_str(), sd_rom.c_str());
                    if (R_FAILED(envSetNextLoad(ext_nro.c_str(), ext_argv))) {
                        foyer::log::write(
                            "[launch] envSetNextLoad(external=%s) failed\n",
                            ext_nro.c_str());
                        return false;
                    }
                    library::mark_per_game_played(game.path);
                    foyer::log::write(
                        "[launch] queued external %s rom=%s\n",
                        ext_nro.c_str(), sd_rom.c_str());
                    return true;
                } else {
                    foyer::log::write(
                        "[launch] external nro for %.*s not on disk: %s\n",
                        (int)eff_def->folder_name.size(),
                        eff_def->folder_name.data(),
                        ext_nro.c_str());
                    // Fall through to libretro path; almost always
                    // there's no libretro core either, so launch_game
                    // ultimately fails — but the log line tells the
                    // user exactly what's missing.
                }
            }
        }
    }

    const auto* core = library::resolve_core(*sys.def, game.path);
    if (!core) {
        foyer::log::write("[launch] no core mapped for system %.*s\n",
            (int)sys.def->folder_name.size(), sys.def->folder_name.data());
        return false;
    }

    char nro_path[256];
    std::snprintf(nro_path, sizeof(nro_path),
        "/foyer/content/cores/foyer-%.*s.nro",
        (int)core->name.size(), core->name.data());

    struct stat st{};
    if (::stat(nro_path, &st) != 0) {
        foyer::log::write("[launch] core nro missing: %s\n", nro_path);
        return false;
    }

    // hbloader expects argv quoted with sdmc: prefix.
    std::string sd_nro = std::string{"sdmc:"} + nro_path;
    std::string sd_rom = std::string{"sdmc:"} + game.path;

    // Resolve the active shader: per-rom override beats the general
    // default. "none" / empty = no shader applied. The player parses
    // an argv "shader=<name>" token and passes it to its
    // shader_pipeline.
    std::string shader = library::per_game_shader(game.path);
    if (shader.empty()) shader = library::config().shader_name;
    if (shader.empty()) shader = "none";

    // Run-ahead lookahead frames: per-rom override (>=0) beats the
    // general default. -1 from the per-rom store means "inherit".
    int runahead = library::per_game_runahead(game.path);
    if (runahead < 0) runahead = library::config().runahead_frames;
    if (runahead < 0) runahead = 0;
    if (runahead > 4) runahead = 4;

    // argv[0] = nro path, argv[1] = rom path, argv[2] = our own path so the
    // player can chain back to us cleanly without hardcoding. argv[3..] are
    // optional "key=value" hint tokens parsed by the player main.
    char argv[1024];
    if (resume_slot >= 0) {
        std::snprintf(argv, sizeof(argv),
            "\"%s\" \"%s\" \"%s\" \"resume=%d\" \"shader=%s\" \"runahead=%d\"",
            sd_nro.c_str(), sd_rom.c_str(),
            browser_self_path().c_str(), resume_slot,
            shader.c_str(), runahead);
    } else {
        std::snprintf(argv, sizeof(argv),
            "\"%s\" \"%s\" \"%s\" \"shader=%s\" \"runahead=%d\"",
            sd_nro.c_str(), sd_rom.c_str(),
            browser_self_path().c_str(), shader.c_str(), runahead);
    }

    // Release the browser's open romfs fd so hbloader's unmap of
    // our NRO's segments doesn't fight with HOS still treating
    // the file as "in use" — same trick switchfin's updater uses
    // before its rename. Foyer is about to quit anyway; any
    // subsequent brls romfs read just fails harmlessly and the
    // process exits.
    const Result rc_rfsexit = romfsExit();
    foyer::log::write("[launch] romfsExit rc=0x%x\n", (unsigned)rc_rfsexit);

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

std::string queue_external_nro(std::initializer_list<std::string_view> candidates) {
    for (std::string_view c : candidates) {
        if (c.empty()) continue;
        std::string path{c};
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        // hbloader argv conventions: sdmc: prefix + argv[0] = nro path
        // (otherwise romfsInit fails when the chained NRO has its own
        // romfs section).
        const std::string sd_path = std::string{"sdmc:"} + path;
        char argv[1024];
        std::snprintf(argv, sizeof(argv), "\"%s\"", sd_path.c_str());
        if (R_FAILED(envSetNextLoad(sd_path.c_str(), argv))) {
            foyer::log::write(
                "[external] envSetNextLoad(%s) failed\n", sd_path.c_str());
            continue;
        }
        foyer::log::write("[external] queued %s\n", sd_path.c_str());
        return path;
    }
    return {};
}

} // namespace foyer::browser
