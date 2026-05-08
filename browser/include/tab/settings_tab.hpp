#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Phase B: minimal Settings tab. Two cells wired to foyer's existing
// service-layer config getters/setters — proves the bridge between
// brls cells and our config layer works. The remaining ~50 settings
// (display tweaks, scrapers, retroachievements, system hide list,
// etc.) get added cell-by-cell in subsequent alphas.
class SettingsTab : public brls::Box
{
public:
    SettingsTab();

    BRLS_BIND(brls::SelectorCell, language,    "foyer/settings/language");
    BRLS_BIND(brls::SelectorCell, themeColor,  "foyer/settings/theme_color");

    static brls::View* create();
};

}  // namespace foyer::browser
