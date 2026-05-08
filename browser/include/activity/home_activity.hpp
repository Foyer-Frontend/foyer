#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Home view: top bar (status cluster + Settings button) + horizontal
// system carousel + per-system app backdrop. Tiles call setBackdrop()
// from their onFocusGained so the backdrop swaps to the focused
// system's background.jpg as the user pans the carousel.
class HomeActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

    // Swap the app backdrop to the given system folder's
    // background.jpg. Called by SystemTile on focus change.
    void setBackdrop(std::string_view folder);

    BRLS_BIND(brls::Label, clock,      "foyer/clock");
    BRLS_BIND(brls::Box,   carousel,   "foyer/carousel");
    BRLS_BIND(brls::Box,   actionRow,  "foyer/action_row");
    BRLS_BIND(brls::Image, backdrop,   "foyer/backdrop");

private:
    brls::RepeatingTask* clockTask = nullptr;

    void populateCarousel();
    void buildActionRow();
};

}  // namespace foyer::browser
