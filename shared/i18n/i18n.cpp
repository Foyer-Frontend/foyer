#include "i18n.hpp"

#include <array>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace foyer::i18n {
namespace {

constexpr std::size_t kCount =
    static_cast<std::size_t>(StringId::kStringIdCount);

using Catalogue = std::array<const char*, kCount>;

// English — the source of truth. Every other catalogue uses this as
// the fallback when an entry is missing (null) or empty.
//
// Order MUST match the StringId enum exactly. Add new strings at the
// end; never reorder existing ones.
constexpr Catalogue kEnglishStrings = [] {
    Catalogue a{};
    a[(std::size_t)StringId::Home]                       = "Home";
    a[(std::size_t)StringId::Library]                    = "Library";
    a[(std::size_t)StringId::Settings]                   = "Settings";
    a[(std::size_t)StringId::Search]                     = "Search";
    a[(std::size_t)StringId::Updates]                    = "Updates";
    a[(std::size_t)StringId::Back]                       = "Back";
    a[(std::size_t)StringId::Quit]                       = "Quit";

    a[(std::size_t)StringId::SettingsDisplay]            = "Display";
    a[(std::size_t)StringId::SettingsEmulator]           = "Emulator";
    a[(std::size_t)StringId::SettingsLibrary]            = "Library";
    a[(std::size_t)StringId::SettingsScrapers]           = "Scrapers";
    a[(std::size_t)StringId::SettingsNetworking]         = "Networking";
    a[(std::size_t)StringId::SettingsAbout]              = "About";

    a[(std::size_t)StringId::UpdatesCheckForUpdates]     = "Check for updates";
    a[(std::size_t)StringId::UpdatesUpToDate]            = "Up to date";
    a[(std::size_t)StringId::UpdatesAvailable]           = "Update available";
    a[(std::size_t)StringId::UpdatesInstall]             = "Install";
    a[(std::size_t)StringId::UpdatesRestartNow]          = "Restart now";
    a[(std::size_t)StringId::UpdatesRestartLater]        = "Restart later";
    a[(std::size_t)StringId::UpdatesDownloading]         = "Downloading...";
    a[(std::size_t)StringId::UpdatesFailed]              = "Update failed";

    a[(std::size_t)StringId::LibraryEmptyTitle]          = "No games yet";
    a[(std::size_t)StringId::LibraryEmptyHint]           =
        "Drop ROMs into /foyer/roms/<system>/ on your SD card.";
    a[(std::size_t)StringId::LibraryRecentlyPlayed]      = "Recently played";
    a[(std::size_t)StringId::LibraryFavorites]           = "Favorites";
    a[(std::size_t)StringId::LibraryAllGames]            = "All games";
    a[(std::size_t)StringId::LibrarySortBy]              = "Sort by";
    a[(std::size_t)StringId::LibrarySortAlphabetical]    = "Alphabetical";
    a[(std::size_t)StringId::LibrarySortByGameCount]     = "Game count";
    a[(std::size_t)StringId::LibrarySortCustom]          = "Custom";

    a[(std::size_t)StringId::GamePlay]                   = "Play";
    a[(std::size_t)StringId::GameResume]                 = "Resume";
    a[(std::size_t)StringId::GameRestart]                = "Restart";
    a[(std::size_t)StringId::GameAddToFavorites]         = "Add to favorites";
    a[(std::size_t)StringId::GameRemoveFromFavorites]    = "Remove from favorites";
    a[(std::size_t)StringId::GameScrape]                 = "Scrape metadata";
    a[(std::size_t)StringId::GamePickCover]              = "Pick cover";
    a[(std::size_t)StringId::GameClearPlaytime]          = "Clear playtime";
    a[(std::size_t)StringId::GameDelete]                 = "Delete";

    a[(std::size_t)StringId::ScraperLibretroThumbnails]  = "libretro-thumbnails";
    a[(std::size_t)StringId::ScraperScreenscraper]       = "ScreenScraper";
    a[(std::size_t)StringId::ScraperSteamgriddb]         = "SteamGridDB";
    a[(std::size_t)StringId::ScraperPickCover]           = "Pick cover";
    a[(std::size_t)StringId::ScraperNoMatches]           = "No matches found";

    a[(std::size_t)StringId::CoresInstall]               = "Install";
    a[(std::size_t)StringId::CoresUpdate]                = "Update";
    a[(std::size_t)StringId::CoresDownloadFailed]        = "Download failed";
    a[(std::size_t)StringId::CoresClearAllBezels]        = "Clear all bezels";

    a[(std::size_t)StringId::Yes]                        = "Yes";
    a[(std::size_t)StringId::No]                         = "No";
    a[(std::size_t)StringId::OK]                         = "OK";
    a[(std::size_t)StringId::Cancel]                     = "Cancel";
    a[(std::size_t)StringId::Confirm]                    = "Confirm";
    a[(std::size_t)StringId::Close]                      = "Close";
    return a;
}();

// Forward-declare per-language catalogues that live in their own .cpp.
extern const Catalogue kSpanishStrings;
extern const Catalogue kPortugueseBrazilStrings;

// Catalogue table. Indexed by Language. Add per-language arrays here
// as community translations land — declare them in their own .cpp
// (e.g. i18n_es.cpp) and add the extern + table entry above.
//
// Slots that point to nullptr (or to empty strings inside an array)
// fall through to English at lookup time, so a partial translation
// is fine — nobody sees a blank space.
const std::array<const Catalogue*, (std::size_t)Language::kLanguageCount>
    kCatalogues = {
        &kEnglishStrings,
        &kSpanishStrings,
        &kPortugueseBrazilStrings,
};

Language g_language = Language::English;

// Map a libnx language code (u64 with the locale string packed into
// the low bytes — "en-US\0\0\0", "pt-BR\0\0\0", "es-419\0\0", "es",
// "ja", etc.) onto our supported Language set. Anything we don't
// ship a catalogue for falls through to English; add cases here
// as new community translations land.
Language map_switch_language(std::uint64_t language_code) {
    char code[9] = {};
    std::memcpy(code, &language_code, 8);
    // Iberian + Latin American Spanish both target our es catalogue.
    // Latin-American conventions throughout, acceptable for both
    // audiences as a starting point.
    if (std::strncmp(code, "es", 2) == 0) {
        return Language::Spanish;
    }
    // Brazilian Portuguese only — pt-BR is a distinct catalogue from
    // European Portuguese (which we don't ship yet; "pt" without a
    // region falls through to English).
    if (std::strncmp(code, "pt-BR", 5) == 0) {
        return Language::PortugueseBrazil;
    }
    return Language::English;
}

} // namespace

void init() {
#ifdef __SWITCH__
    // setGetSystemLanguage returns a language-code u64 (libnx packs
    // the locale string into 8 bytes — "en-US\0\0\0", "ja\0...",
    // "es-419\0", etc.). Translate to our Language enum.
    Result rc = setInitialize();
    if (R_SUCCEEDED(rc)) {
        std::uint64_t code = 0;
        if (R_SUCCEEDED(setGetSystemLanguage(&code))) {
            g_language = map_switch_language(code);
        }
        setExit();
    }
#endif
}

void set_language(Language lang) {
    if ((std::size_t)lang >= (std::size_t)Language::kLanguageCount)
        lang = Language::English;
    g_language = lang;
}

Language language() { return g_language; }

const char* tr_en(StringId id) {
    auto idx = static_cast<std::size_t>(id);
    if (idx >= kCount) return "";
    const char* s = kEnglishStrings[idx];
    return s ? s : "";
}

const char* tr(StringId id) {
    auto idx = static_cast<std::size_t>(id);
    if (idx >= kCount) return "";
    auto lang_idx = static_cast<std::size_t>(g_language);
    if (lang_idx < kCatalogues.size() && kCatalogues[lang_idx]) {
        const char* s = (*kCatalogues[lang_idx])[idx];
        if (s && s[0]) return s;
    }
    // Fall through — English is always present and complete.
    const char* s = kEnglishStrings[idx];
    return s ? s : "";
}

} // namespace foyer::i18n
