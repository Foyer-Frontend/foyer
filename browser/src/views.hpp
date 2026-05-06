#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <nanovg.h>
#include <switch.h>

#include "library/scanner.hpp"
#include "library/core_installer.hpp"
#include "library/core_install_job.hpp"
#include "library/cheat_installer.hpp"
#include "library/bezel_installer.hpp"
#include "library/foyer_update_job.hpp"
#include "library/scrape_job.hpp"
#include "platform/app.hpp"

namespace foyer::browser {

enum class View {
    Home,        // horizontal system carousel
    System,      // game list for the focused system
    GameDetail,  // per-rom screen — cover, metadata, core picker
    Settings,    // browser-wide preferences
    Search,      // global text search across every system
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
    // Subpage within the active category. 0 = top-level (default).
    // >0 maps to a category-specific drill-down (Emulator's Cores
    // catalog, Bezel packs, Cheat packs, etc). B from a subpage
    // returns to the top-level Emulator list before exiting Settings.
    int  settings_subpage  = 0;

    // Tico-style modal popup launched with `+`. Shadows the underlying view
    // until B cancels or A picks an entry.
    bool popup_open  = false;
    int  popup_index = 0;

    // Yes/No quit confirmation. B on the Home view opens this; A on Yes
    // raises request_quit, A on No closes it.
    bool quit_confirm_open = false;
    int  quit_confirm_index = 1; // 0=Yes 1=No, default to safer "No"

    // Yes/No confirmation before downloading + staging a foyer self-update.
    // Opened by the OpUpdInstallFoyer action; A on Yes raises
    // request_install_foyer_update, A on No closes.
    bool update_confirm_open = false;
    int  update_confirm_index = 1; // default to "No"

    // Modal option picker. Opened from a Settings Cycle row when the
    // user presses A — instead of cycling one step, we surface the
    // full list as a scrollable menu so they can see / pick directly.
    // L/R on a Cycle row still does the legacy quick-cycle so power
    // users don't have to open the picker for a single-step change.
    //
    // The handler that originally owned the row writes `op` and `data`
    // (typically the system folder for OpEmuSysCore); on A inside the
    // picker the dispatcher reads those + `cursor` and applies the
    // chosen value via the same setter the L/R cycle path uses.
    struct OptionPicker {
        bool                     open    = false;
        std::string              title;
        std::vector<std::string> options;
        int                      current = 0;     // currently-set value (badge)
        int                      cursor  = 0;     // focused row in the picker
        int                      op      = 0;     // settings opcode
        std::string              data;            // opcode-specific payload
    };
    OptionPicker option_picker;

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
    bool        request_install_shaders   = false;
    bool        request_install_cheats    = false;
    bool        request_install_bezels    = false;
    bool        request_refresh_manifest  = false;
    bool        request_refresh_cheats_manifest = false;
    bool        request_refresh_bezels_manifest = false;
    // Set by the Updates page "Update everything" footer. main.cpp
    // chains the per-kind install paths once and clears the flag.
    bool        request_update_all        = false;
    // Set true on first Settings entry per app run so the cores /
    // cheats / bezels manifests start auto-fetching in the background.
    // Stays false thereafter so the user can still trigger a manual
    // re-fetch via the explicit refresh actions.
    bool        manifests_auto_refreshed       = false;
    // When non-empty, request_install_cores / cheats / bezels acts on
    // just that one entry. Cleared by main.cpp after each run.
    std::string install_only_core;
    std::string install_only_cheat;
    std::string install_only_bezel;
    // When true, install_cores is invoked with `force=true`, bypassing
    // the version-match skip. Used by the explicit "Re-install" path.
    // Cleared after the install runs.
    bool        install_force            = false;
    // Background workers. main.cpp polls each frame so the UI keeps
    // responding while curl blocks on socket I/O. cancel() can be
    // raised from the UI to abort the current transfer.
    library::CoreInstallJob install_job;
    library::FoyerUpdateJob foyer_job;
    library::ScrapeJob      scrape_job;

    // Search view state. `search_query` is the active filter text
    // (lower-cased for matching), `search_results` are pairs of
    // (system_index, game_index) into Library that match. `search_dirty`
    // is set whenever the query changes so views.cpp re-runs the
    // filter; cleared after recompute.
    std::string                          search_query;
    std::vector<std::pair<std::size_t, std::size_t>> search_results;
    int                                  search_index   = 0;
    bool                                 search_dirty   = false;
    // Cores manifest pull (Settings -> Updates -> Refresh manifest).
    // Plain Worker — the result is a CoreManifest written to
    // refresh_result before m_done flips.
    library::Worker         refresh_job;
    library::CoreManifest   refresh_result;

    // Self-update flow. Boot-time check populates `foyer_update_*` once
    // and a banner shows if a newer release is on GitHub.
    bool        request_check_foyer_update   = false;
    bool        request_install_foyer_update = false;
    bool        foyer_update_available       = false;
    std::string foyer_update_version;

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
    int hold_up_frames   = 0;
    int hold_down_frames = 0;

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

// Replace the cached foyer-cores manifest used by Settings → Updates to
// render per-core install rows. main.cpp calls this after the user
// triggers the "Refresh manifest" action.
void set_manifest_cache(library::CoreManifest manifest);

// Same shape for the cheat- and bezel-pack catalogues. Settings reads
// these to render per-pack install rows.
void set_cheats_manifest_cache(library::CheatManifest manifest);
void set_bezels_manifest_cache(library::BezelManifest manifest);

} // namespace foyer::browser
