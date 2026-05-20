#pragma once

#include <borealis.hpp>

#include <cstdint>
#include <string>

namespace foyer::browser {

// Home view: top bar (status cluster + Settings button) + horizontal
// system carousel + per-system app backdrop. Tiles call setBackdrop()
// from their onFocusGained so the backdrop swaps to the focused
// system's background.jpg as the user pans the carousel.
class HomeActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/home.xml");

    ~HomeActivity() override;
    void onContentAvailable() override;
    // Re-check library_state generation on every reappear so a
    // Settings → Rescan triggered while Home was hidden surfaces
    // the new system tiles when the user comes back.
    void onResume() override;

    // Reflow chrome for the just-focused system: swap the
    // backdrop image to its background.jpg and update the title
    // label above the carousel. Called by SystemTile on focus
    // change.
    void onSystemFocused(std::string_view folder,
                         std::string_view display_name);

    // Preselect a system tile so initial focus lands on it instead
    // of the first carousel child. Used by main.cpp's fast_returned
    // branch after a chain-launch back from a core — last_session.txt
    // tells us which system the user was in, so B-back from the
    // restored SystemActivity should land on its tile. Empty string
    // = no preselect (default-focus first tile, original behaviour).
    // MUST be called before the activity is pushed so the hint
    // lands in time for onContentAvailable.
    void setPreselectSystem(std::string folder)
    { m_preselect_folder = std::move(folder); }

    // Defer the heavy population work (system tiles + action row +
    // profile cluster) to onResume rather than running it on the
    // pushActivity path. Used by main.cpp's fast_returned branch:
    // the activity is pushed under GameActivity, so the user never
    // sees it until they B-back — running populateCarousel
    // synchronously up-front just lengthens the blank-screen
    // window after chain-launching back from a core. With this on,
    // pushActivity returns in microseconds and the actual carousel
    // build happens when the activity becomes the visible top of
    // the stack.
    void setDeferredPopulation(bool deferred)
    { m_defer_population = deferred; }

    BRLS_BIND(brls::Label, clock,        "foyer/clock");
    BRLS_BIND(brls::Box,   carousel,     "foyer/carousel");
    BRLS_BIND(brls::Box,   actionRow,    "foyer/action_row");
    BRLS_BIND(brls::Box,   profiles,     "foyer/profiles");
    BRLS_BIND(brls::Image, backdrop,     "foyer/backdrop");
    BRLS_BIND(brls::Image, focusLogo,    "foyer/focus_logo");

private:
    brls::RepeatingTask* clockTask = nullptr;
    std::uint32_t        m_library_gen = 0;
    std::string          m_preselect_folder;
    bool                 m_defer_population = false;
    bool                 m_populated        = false;

    void populateCarousel();
    void buildActionRow();
    void buildProfiles();
    void openProfilePicker();
};

}  // namespace foyer::browser
