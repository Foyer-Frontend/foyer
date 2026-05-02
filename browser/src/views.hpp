#pragma once

#include <cstddef>
#include <string>

#include <nanovg.h>
#include <switch.h>

#include "library/scanner.hpp"
#include "platform/app.hpp"

namespace foyer::browser {

enum class View {
    Home,        // horizontal system carousel
    System,      // game list for the focused system
    GameDetail,  // per-rom screen — cover, metadata, core picker
    Settings,    // browser-wide preferences
};

// Browser navigation state. Owned by main.cpp.
struct State {
    View view = View::Home;

    // Index into Library::systems for the currently focused system in the
    // home carousel. Also drives which system the System view shows.
    std::size_t system_index = 0;

    // Index into the current system's games vector for the System view.
    std::size_t game_index = 0;

    // Cursor on the GameDetail core picker.
    std::size_t detail_core_index = 0;

    // Settings cursor: sidebar category + per-category content row + which
    // column has focus. Each category renders an independent content list,
    // so the row index is reset on category change.
    int  settings_category = 0;
    int  settings_row      = 0;
    bool settings_in_content = false;

    // Tico-style modal popup launched with `+`. Shadows the underlying view
    // until B cancels or A picks an entry.
    bool popup_open  = false;
    int  popup_index = 0;

    // Yes/No quit confirmation. B on the Home view opens this; A on Yes
    // raises request_quit, A on No closes it.
    bool quit_confirm_open = false;
    int  quit_confirm_index = 1; // 0=Yes 1=No, default to safer "No"

    // Set by the popup "Exit" item; main.cpp drains it and quits the app.
    bool request_quit = false;

    // Set by Update; read by the main loop to drive launches.
    bool        request_launch = false;

    // Save-state slot the player should auto-load right after the rom boots.
    // -1 means "fresh start" — set by the GameDetail Continue row.
    int         request_resume_slot = -1;

    // Settings actions handed off to main.cpp.
    bool        request_rescan           = false;
    bool        request_invalidate_covers = false;
    bool        request_install_cores     = false;

    // Set by Update; read by the main loop to trigger a one-shot scrape of
    // every rom in the focused system. The "kind" picks which scraper to use.
    enum class ScrapeKind { None, Libretro, ScreenScraper, SteamGridDB };
    ScrapeKind  request_scrape_kind = ScrapeKind::None;

    // Optional banner shown at the top of the screen for ~120 frames.
    // Cleared automatically by draw() when ttl reaches zero.
    std::string banner_text{};
    int         banner_ttl  = 0;

    // Per-frame counter incremented at the top of update(). Used as a coarse
    // "now" for hold/touch durations — seconds = frames / 60.
    std::uint32_t frame_counter = 0;

    // Shoulder-button hold counters. Reset to 0 the frame the button is
    // released, otherwise increment. Auto-repeat fires once `hold > 30` and
    // the cadence accelerates again past 90 (~1.5s) for the "spin" feel.
    int hold_l_frames = 0;
    int hold_r_frames = 0;

    // Touch gesture tracking. A gesture begins on tap_started, evolves while
    // a finger remains down, and resolves to either a tap or a swipe/flick
    // when the finger lifts.
    bool          touch_active      = false;
    bool          touch_was_swipe   = false; // crossed drag threshold → no tap
    float         touch_start_x     = 0.0f;
    float         touch_start_y     = 0.0f;
    float         touch_last_x      = 0.0f;
    std::uint32_t touch_start_frame = 0;
    float         touch_swipe_acc   = 0.0f;  // accumulated px since last step
};

// One full library snapshot — taken once at startup, refreshed when the user
// triggers a rescan.
struct Library {
    std::vector<library::System> systems;
};

// Per-frame update + draw. Called from the browser's tick loop. `held` and
// `down` are the standard libnx pad bitmasks for this frame. `touch` is the
// per-frame touch snapshot from the platform layer; `w`/`h` are the
// framebuffer dimensions used for hit testing.
void update(State& s, const Library& lib,
            std::uint64_t held, std::uint64_t down,
            const platform::App::Touch& touch,
            float w, float h);
void draw  (NVGcontext* vg, float w, float h, const State& s, const Library& lib);

// Vertical metrics for the persistent sphaira-style top + bottom bars.
constexpr float kTopBarH    = 64.0f;
constexpr float kBottomBarH = 56.0f;

// Drop the cached nanovg handles for box art so the next draw re-reads the
// files from disk. Call this after a scrape completes so newly-downloaded
// covers show without restarting the browser.
void invalidate_cover_cache(NVGcontext* vg);

} // namespace foyer::browser
