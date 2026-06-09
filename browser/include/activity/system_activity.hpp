#pragma once

#include "widgets/cover_flow.hpp"

#include <borealis.hpp>
#include <string>
#include <string_view>

namespace foyer::browser {

// System view — opened when the user clicks a system tile on Home.
// Mirrors HomeActivity's chrome (status cluster top-right, action
// row up top, cover-flow carousel along the bottom edge) so the
// navigation feels continuous. The middle band hosts the per-system
// logo, tinted to the current theme's text colour at runtime.
class SystemActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/system.xml");

    SystemActivity(std::string_view folder, std::string_view display_name);
    ~SystemActivity() override;

    void onContentAvailable() override;
    // Rebuild the cover-flow carousel on every reappear so a
    // single-game rescrape (kicked from GameActivity) surfaces the
    // freshly-downloaded box art / wheel / fanart when the user
    // pops back. Cheap — populateCarousel re-uses the cached
    // library_state vector and rebuilds tiles in place.
    void onResume() override;

    // Cancels + joins the file-static scrape job if one is active.
    // Called from HomeActivity's Quit handler so foyer doesn't tear
    // down while a curl transfer is mid-flight on a worker thread.
    static void cancel_pending_scrape();

    BRLS_BIND(brls::Label, clock,         "foyer/clock");
    BRLS_BIND(brls::Image, backdrop,      "foyer/backdrop");
    BRLS_BIND(brls::Box,   logoHolder,    "foyer/logo_holder");
    BRLS_BIND(brls::Box,   actionRow,     "foyer/action_row");
    BRLS_BIND(brls::Box,   carousel,      "foyer/carousel");
    BRLS_BIND(brls::Label, scrapeStatus,  "foyer/scrape_status");
    BRLS_BIND(brls::Box,   topBar,        "foyer/top_bar");
    BRLS_BIND(brls::Box,   bottomBar,     "foyer/bottom_bar");

    // Called from the scrape-status RepeatingTask to refresh the
    // foyer/scrape_status label from the in-flight ScrapeJob's
    // counters. Public so the file-static task class can dispatch.
    void refreshScrapeStatus();

    // Focus-follow side effects (backdrop swap, logo / wheel art).
    // Driven by the CoverFlowView's onFocusChangedCb.
    void onTileFocused(int idx);

    // Preselect a game tile so initial focus lands on it instead
    // of the first carousel child. Used by main.cpp's fast_returned
    // branch after a chain-launch back from a core: last_session.txt
    // tells us which game the user just played, so B-back from the
    // restored GameActivity should land on its tile. Empty string =
    // no preselect (default behaviour: first tile or m_last_focus_idx
    // on reentry). MUST be called before pushActivity so the hint
    // lands in time for onContentAvailable.
    void setPreselectGame(std::string game_path)
    { m_preselect_game = std::move(game_path); }

    // Defer the heavy populate (game-tile creation + initial cover
    // preload) to onResume so the chain-back-from-core push chain
    // doesn't synchronously walk hundreds of tiles before the user
    // ever sees GameActivity render. See HomeActivity::setDeferredPopulation
    // for the same trick on the system carousel side.
    void setDeferredPopulation(bool deferred)
    { m_defer_population = deferred; }

private:
    std::string m_folder;
    std::string m_display_name;

    brls::RepeatingTask* m_clockTask        = nullptr;
    brls::RepeatingTask* m_scrapeStatusTask = nullptr;

    // The cover strip — owned by the carousel host box once
    // mounted. Never torn down across resumes; setEntries swaps
    // the data in place.
    CoverFlowView* m_flow = nullptr;

    // Last focused strip index reported via onTileFocused. Stashed
    // so resume paths can return focus to the same game.
    int m_last_focus_idx = 0;

    // Set via setPreselectGame() before pushActivity on the
    // chain-back-from-core path. Consumed once in onContentAvailable
    // to seed m_last_focus_idx + giveFocus to the matching tile.
    std::string m_preselect_game;

    // Deferred-population state: m_defer_population is set via the
    // setter before pushActivity; m_populated flips true once
    // populateCarousel + initial preload have actually run (either
    // synchronously in onContentAvailable, or lazily in onResume).
    bool m_defer_population = false;
    bool m_populated        = false;
    // theme_change subscription id; refreshes top/bottom bar bg
    // when HOS theme variant flips.
    int  m_theme_sub        = -1;

    void buildLogo();
    void buildActionRow();
    void populateCarousel();
};

}  // namespace foyer::browser
