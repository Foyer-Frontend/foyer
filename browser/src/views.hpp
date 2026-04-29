#pragma once

#include <cstddef>
#include <string>

#include <nanovg.h>
#include <switch.h>

#include "library/scanner.hpp"

namespace foyer::browser {

enum class View {
    Home,        // horizontal system carousel
    System,      // game list for the focused system
};

// Browser navigation state. Owned by main.cpp.
struct State {
    View view = View::Home;

    // Index into Library::systems for the currently focused system in the
    // home carousel. Also drives which system the System view shows.
    std::size_t system_index = 0;

    // Index into the current system's games vector for the System view.
    std::size_t game_index = 0;

    // Set by Update; read by the main loop to drive launches.
    bool        request_launch = false;

    // Set by Update; read by the main loop to trigger a one-shot scrape of
    // every rom in the focused system. The "kind" picks which scraper to use.
    enum class ScrapeKind { None, Libretro, ScreenScraper, SteamGridDB };
    ScrapeKind  request_scrape_kind = ScrapeKind::None;

    // Optional banner shown at the top of the screen for ~120 frames.
    // Cleared automatically by draw() when ttl reaches zero.
    std::string banner_text{};
    int         banner_ttl  = 0;
};

// One full library snapshot — taken once at startup, refreshed when the user
// triggers a rescan.
struct Library {
    std::vector<library::System> systems;
};

// Per-frame update + draw. Called from the browser's tick loop. `held` and
// `down` are the standard libnx pad bitmasks for this frame.
void update(State& s, const Library& lib, std::uint64_t held, std::uint64_t down);
void draw  (NVGcontext* vg, float w, float h, const State& s, const Library& lib);

// Drop the cached nanovg handles for box art so the next draw re-reads the
// files from disk. Call this after a scrape completes so newly-downloaded
// covers show without restarting the browser.
void invalidate_cover_cache(NVGcontext* vg);

} // namespace foyer::browser
