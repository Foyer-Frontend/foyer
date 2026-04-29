// foyer browser entry — Phase 3.
//
// Scans /foyer/roms for known systems, draws an ES-DE-style carousel + game
// list, and chain-launches the matching foyer-<core>.nro player when the
// user picks a game.

#include <switch.h>

#include "platform/app.hpp"
#include "platform/log.hpp"
#include "library/scanner.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/libretro_thumbnails.hpp"
#include "scrapers/screenscraper.hpp"
#include "scrapers/steamgriddb.hpp"
#include "scrapers/accounts.hpp"
#include "views.hpp"
#include "launch.hpp"

#include <cstdio>
#include <sys/stat.h>

int main(int /*argc*/, char** /*argv*/) {
    foyer::platform::App app;

    foyer::library::ScanOptions opts;
    opts.rom_root = foyer::library::config().rom_root;
    foyer::browser::Library lib;
    lib.systems = foyer::library::scan_library(opts);

    foyer::browser::State state;

    app.set_draw_fn([&](NVGcontext* vg, float w, float h) {
        foyer::browser::draw(vg, w, h, state, lib);
    });

    while (app.tick()) {
        const auto held = padGetButtons(&app.pad());
        const auto down = padGetButtonsDown(&app.pad());

        // Global exit — only when at the home view (so System view's B can
        // walk back without quitting the app).
        if ((down & HidNpadButton_Plus)
            && state.view == foyer::browser::View::Home) {
            app.quit();
            continue;
        }

        foyer::browser::update(state, lib, held, down);

        if (state.request_rescan) {
            state.request_rescan = false;
            foyer::library::reload_config();
            opts.rom_root = foyer::library::config().rom_root;
            lib.systems = foyer::library::scan_library(opts);
            // Cursors might point past the end of the rescanned library.
            if (state.system_index >= lib.systems.size()) state.system_index = 0;
            state.game_index = 0;
            foyer::browser::invalidate_cover_cache(app.vg());
            state.banner_text = "Library rescanned";
            state.banner_ttl  = 120;
        }
        if (state.request_invalidate_covers) {
            state.request_invalidate_covers = false;
            foyer::browser::invalidate_cover_cache(app.vg());
        }

        if (state.request_launch) {
            state.request_launch = false;
            const auto& sys  = lib.systems[state.system_index];
            const auto& game = sys.games[state.game_index];
            if (foyer::browser::launch_game(sys, game)) {
                app.quit();
            } else {
                const auto* core = foyer::library::resolve_core(*sys.def, game.path);
                state.banner_text = std::string{"Core not installed: foyer-"}
                    + (core ? std::string{core->name} : "?") + ".nro";
                state.banner_ttl  = 180;
            }
        }

        if (state.request_scrape_kind != foyer::browser::State::ScrapeKind::None) {
            const auto kind = state.request_scrape_kind;
            state.request_scrape_kind = foyer::browser::State::ScrapeKind::None;
            const auto& sys = lib.systems[state.system_index];
            const char* kind_label =
                (kind == foyer::browser::State::ScrapeKind::ScreenScraper) ? "ScreenScraper" :
                (kind == foyer::browser::State::ScrapeKind::SteamGridDB)   ? "SteamGridDB"   :
                                                                              "libretro";
            int total = (int)sys.games.size();
            int done  = 0;
            int hits  = 0;
            for (const auto& g : sys.games) {
                done++;
                char banner[160];
                std::snprintf(banner, sizeof(banner),
                    "Scraping %.*s [%s]  %d / %d",
                    (int)sys.def->short_name.size(), sys.def->short_name.data(),
                    kind_label, done, total);
                state.banner_text = banner;
                state.banner_ttl  = 30;
                if (!app.tick()) break;

                const auto dest = foyer::scrapers::cover_path(
                    sys.def->folder_name, g.stem);
                struct stat st{};
                if (::stat(dest.c_str(), &st) == 0) continue;

                bool ok = false;
                switch (kind) {
                    case foyer::browser::State::ScrapeKind::Libretro:
                        ok = foyer::scrapers::libretro_thumb::fetch_cover(
                            sys.def->thumbnails_db, g.stem, dest);
                        break;
                    case foyer::browser::State::ScrapeKind::ScreenScraper:
                        ok = foyer::scrapers::screenscraper::fetch_cover(
                            sys.def->folder_name, g.path, g.stem, dest);
                        break;
                    case foyer::browser::State::ScrapeKind::SteamGridDB:
                        ok = foyer::scrapers::steamgriddb::fetch_cover(
                            sys.def->folder_name, g.stem, dest);
                        break;
                    default: break;
                }
                if (ok) hits++;
            }
            // Drop cached nanovg handles so newly-downloaded files show up.
            foyer::browser::invalidate_cover_cache(app.vg());

            char done_msg[200];
            std::snprintf(done_msg, sizeof(done_msg),
                "Scrape done [%s] — %d / %d covers", kind_label, hits, total);
            state.banner_text = done_msg;
            state.banner_ttl  = 240;
        }
    }
    return 0;
}
