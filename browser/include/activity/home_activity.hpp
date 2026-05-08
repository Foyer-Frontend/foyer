#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Home view: top bar (status cluster + Settings button) + horizontal
// system carousel + bottom hints. Phase C populates the carousel from
// foyer's system_db; library scan integration arrives in a later
// alpha when ROMs feed into per-tile game counts.
class HomeActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

    BRLS_BIND(brls::Label, clock,        "foyer/clock");
    BRLS_BIND(brls::Box,   carousel,     "foyer/carousel");
    BRLS_BIND(brls::Box,   btnSettings,  "foyer/btn_settings");

private:
    brls::RepeatingTask* clockTask = nullptr;

    void populateCarousel();
    void wireSettingsButton();
};

}  // namespace foyer::browser
