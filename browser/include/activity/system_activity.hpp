#pragma once

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

    // Cancels + joins the file-static scrape job if one is active.
    // Called from HomeActivity's Quit handler so foyer doesn't tear
    // down while a curl transfer is mid-flight on a worker thread.
    static void cancel_pending_scrape();

    BRLS_BIND(brls::Label, clock,         "foyer/clock");
    BRLS_BIND(brls::Image, backdrop,      "foyer/backdrop");
    BRLS_BIND(brls::Box,   logoHolder,    "foyer/logo_holder");
    BRLS_BIND(brls::Box,   actionRow,     "foyer/action_row");
    BRLS_BIND(brls::Box,   carousel,      "foyer/carousel");
    BRLS_BIND(brls::HScrollingFrame, carouselScroll, "foyer/carousel_scroll");
    BRLS_BIND(brls::Label, scrapeStatus,  "foyer/scrape_status");

    // Called from the scrape-status RepeatingTask to refresh the
    // foyer/scrape_status label from the in-flight ScrapeJob's
    // counters. Public so the file-static task class can dispatch.
    void refreshScrapeStatus();

    // Called by a GameTile when it gains focus, with its index
    // inside the carousel. Drives the sliding-window preload of
    // game covers.
    void onTileFocused(int idx);

private:
    std::string m_folder;
    std::string m_display_name;

    brls::RepeatingTask* m_clockTask        = nullptr;
    brls::RepeatingTask* m_scrapeStatusTask = nullptr;

    // Sliding-window preload state. Tiles in [0, m_loaded_until)
    // have had load_cover() called; the next batch fires when
    // focus crosses (m_loaded_until - kThreshold).
    int m_loaded_until = 0;

    void buildLogo();
    void buildActionRow();
    void populateCarousel();
};

}  // namespace foyer::browser
