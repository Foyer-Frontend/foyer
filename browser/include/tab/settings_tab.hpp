#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Phase B: minimal Settings tab. Language only — the theme follows the
// Switch system Light/Dark setting automatically (brls reads it via
// setsysGetColorSetId), so no theme picker. The remaining ~50
// settings (display tweaks, scrapers, retroachievements, system hide
// list, etc.) get added cell-by-cell in subsequent alphas.
class SettingsTab : public brls::Box
{
public:
    SettingsTab();

    BRLS_BIND(brls::SelectorCell, language, "foyer/settings/language");

    static brls::View* create();
};

}  // namespace foyer::browser
