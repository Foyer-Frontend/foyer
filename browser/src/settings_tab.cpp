#include "tab/settings_tab.hpp"

#include "i18n/i18n.hpp"
#include "library/config.hpp"

#include <array>
#include <string>
#include <string_view>

using namespace brls::literals;  // for _i18n on user-visible strings

namespace foyer::browser {

namespace {

// Language picker. Order matches the legacy 0.5.x picker (System /
// English / Spanish / pt-BR). Empty config language = follow system;
// explicit code pins the override.
struct LanguageOption {
    const char* i18n_key;   // resolved at draw time so live language
                            // switches reflow correctly.
    const char* code;
};
constexpr std::array<LanguageOption, 4> kLanguages = {{
    {"foyer/settings/language_follow_system", ""},
    {"English",                               "en"},
    {"Español",                               "es"},
    {"Português",                             "pt-BR"},
}};

constexpr std::array<const char*, 6> kThemeColorI18nKeys = {
    "foyer/settings/color/light",
    "foyer/settings/color/dark",
    "foyer/settings/color/snow",
    "foyer/settings/color/nord",
    "foyer/settings/color/dracula",
    "foyer/settings/color/emerald",
};
constexpr std::array<const char*, 6> kThemeColorCodes = {
    "light", "dark", "snow", "nord", "dracula", "emerald",
};

template <std::size_t N>
int index_for_code(const std::array<const char*, N>& codes,
                   std::string_view current,
                   int fallback)
{
    for (std::size_t i = 0; i < codes.size(); i++) {
        if (codes[i] == current) return static_cast<int>(i);
    }
    return fallback;
}

int index_for_language(std::string_view current) {
    for (std::size_t i = 0; i < kLanguages.size(); i++) {
        if (kLanguages[i].code == current) return static_cast<int>(i);
    }
    return 0;  // Follow system
}

void apply_language(int idx) {
    if (idx < 0 || idx >= (int)kLanguages.size()) return;
    const std::string code = kLanguages[idx].code;
    foyer::library::set_language(code);
    using L = foyer::i18n::Language;
    if      (code.empty())   foyer::i18n::init();  // re-detect from system
    else if (code == "en")    foyer::i18n::set_language(L::English);
    else if (code == "es")    foyer::i18n::set_language(L::Spanish);
    else if (code == "pt-BR") foyer::i18n::set_language(L::PortugueseBrazil);
}

}  // namespace

SettingsTab::SettingsTab() {
    inflateFromXMLRes("xml/tabs/foyer_settings.xml");

    const auto& cfg = foyer::library::config();

    std::vector<std::string> lang_labels;
    lang_labels.reserve(kLanguages.size());
    for (const auto& l : kLanguages) {
        // i18n_key for the system option, raw label for the
        // explicit codes (English / Español / Português stay
        // self-named regardless of locale).
        const std::string key = l.i18n_key;
        if (key.find('/') != std::string::npos) {
            lang_labels.emplace_back(brls::getStr(key));
        } else {
            lang_labels.emplace_back(key);
        }
    }
    language->init("foyer/settings/language"_i18n,
                   lang_labels,
                   index_for_language(cfg.language),
                   [](int) {},
                   [](int selected) { apply_language(selected); });

    std::vector<std::string> theme_labels;
    theme_labels.reserve(kThemeColorI18nKeys.size());
    for (const auto* key : kThemeColorI18nKeys) {
        theme_labels.emplace_back(brls::getStr(key));
    }
    themeColor->init("foyer/settings/theme_color"_i18n,
                     theme_labels,
                     index_for_code(kThemeColorCodes, cfg.theme_color, 0),
                     [](int) {},
                     [](int selected) {
                         if (selected < 0 ||
                             selected >= (int)kThemeColorCodes.size()) return;
                         foyer::library::set_theme_color(
                             kThemeColorCodes[selected]);
                     });
}

brls::View* SettingsTab::create() {
    return new SettingsTab();
}

}  // namespace foyer::browser
