#include "i18n.hpp"

#include <array>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace foyer::i18n {

// kCount + Catalogue alias live at namespace scope (external linkage)
// so the `extern const Catalogue kSpanishStrings` forward decls below
// match the definitions in i18n_es.cpp / i18n_pt_br.cpp. Putting them
// inside an anonymous namespace would give the externs internal
// linkage and produce link errors on the catalogue references.
inline constexpr std::size_t kCount =
    static_cast<std::size_t>(StringId::kStringIdCount);

using Catalogue = std::array<const char*, kCount>;

namespace {

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
    a[(std::size_t)StringId::Later]                      = "Later";
    a[(std::size_t)StringId::Exit]                       = "Exit";

    a[(std::size_t)StringId::EmptyNoSystemsFound]        = "No systems found";
    a[(std::size_t)StringId::EmptyDropRomsHint]          =
        "drop roms into /foyer/roms/<system>/ and rescan";
    a[(std::size_t)StringId::EmptyNoSystems]             = "No systems";
    a[(std::size_t)StringId::EmptyNoRomsInFolder]        = "no roms in this folder";
    a[(std::size_t)StringId::EmptyNoCoverScrapeHint]     = "no cover (Y to scrape)";

    a[(std::size_t)StringId::GamePublisher]              = "Publisher";
    a[(std::size_t)StringId::GameDeveloper]              = "Developer";
    a[(std::size_t)StringId::GamePlayers]                = "Players";
    a[(std::size_t)StringId::GameRating]                 = "Rating";

    a[(std::size_t)StringId::ActionResumeLast]           = "Resume Last";
    a[(std::size_t)StringId::ActionMoveUp]               = "Move up";
    a[(std::size_t)StringId::ActionMoveDown]             = "Move down";
    a[(std::size_t)StringId::ActionRescanGames]          = "Rescan Games";
    a[(std::size_t)StringId::ActionToggleFavorite]       = "Toggle Favorite";
    a[(std::size_t)StringId::ActionPickCover]            = "Pick cover...";
    a[(std::size_t)StringId::ActionFavoriteAll]          = "Favorite all";
    a[(std::size_t)StringId::ActionClearAllFavorites]    = "Clear all favorites";
    a[(std::size_t)StringId::ActionScrapeSystem]         = "Scrape this system";

    a[(std::size_t)StringId::QuitConfirmTitle]           = "Quit foyer?";
    a[(std::size_t)StringId::UpdateRestartTitle]         = "Restart foyer to apply v";
    a[(std::size_t)StringId::UpdateRestartHint]          =
        "Replaces foyer.nro and re-launches. No on-disk save loss.";
    a[(std::size_t)StringId::UpdateFoyerTitle]           = "Update foyer to v";
    a[(std::size_t)StringId::UpdateFoyerHint]            =
        "Downloads foyer.nro to /switch/foyer/foyer.nro.new — applied next boot.";

    a[(std::size_t)StringId::SettingsGeneral]            = "General";
    a[(std::size_t)StringId::SettingsAudio]              = "Audio";
    a[(std::size_t)StringId::SettingsAccounts]           = "Accounts";
    a[(std::size_t)StringId::SettingsExperimental]       = "Experimental";
    a[(std::size_t)StringId::SettingsPreferredScraper]   = "Preferred scraper";
    a[(std::size_t)StringId::SettingsPreferredScraperHint] =
        "Provider used when Y scrapes a game.";
    a[(std::size_t)StringId::SettingsRomRoot]            = "Rom root";
    a[(std::size_t)StringId::SettingsRomRootHint]        = "Where foyer scans for roms.";
    a[(std::size_t)StringId::SettingsScanSubfolders]     = "Scan subfolders";
    a[(std::size_t)StringId::SettingsScanSubfoldersHint] = "Walk subdirectories on scan.";
    a[(std::size_t)StringId::SettingsTheme]              = "Theme";
    a[(std::size_t)StringId::SettingsThemeHint]          = "Active palette + wallpaper.";
    a[(std::size_t)StringId::SettingsShowClock]          = "Show clock";
    a[(std::size_t)StringId::SettingsShowClockHint]      = "Top-bar clock.";
    a[(std::size_t)StringId::SettingsLanguage]           = "Language";
    a[(std::size_t)StringId::SettingsLanguageHint]       =
        "Override the system language. Restart for full effect.";

    a[(std::size_t)StringId::DisplayShowBackgrounds]     = "Show backgrounds";
    a[(std::size_t)StringId::DisplayShowBackgroundsHint] = "Per-game backdrop in System view.";
    a[(std::size_t)StringId::DisplayShowCovers]          = "Show covers";
    a[(std::size_t)StringId::DisplayShowCoversHint]      = "Box-art tiles in the game grid.";
    a[(std::size_t)StringId::DisplayShowBezels]          = "Show bezels";
    a[(std::size_t)StringId::DisplayShowBezelsHint]      = "Overlay per-system PNG around output.";
    a[(std::size_t)StringId::DisplayShader]              = "Shader";
    a[(std::size_t)StringId::DisplayShaderHint]          = "Post-process pass applied per frame.";
    a[(std::size_t)StringId::DisplayRunahead]            = "Run-ahead";
    a[(std::size_t)StringId::DisplayRunaheadHint]        = "Reduce input lag by emulating ahead.";
    a[(std::size_t)StringId::ShaderNone]                 = "None";
    a[(std::size_t)StringId::ShaderScanlines]            = "Scanlines";
    a[(std::size_t)StringId::ShaderCrtSimple]            = "CRT (simple)";
    a[(std::size_t)StringId::ShaderLcdGrid]              = "LCD grid";
    a[(std::size_t)StringId::ShaderGbDmg]                = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderGbaCorrect]           = "GBA correction";
    a[(std::size_t)StringId::RunaheadOff]                = "Off";
    a[(std::size_t)StringId::RunaheadOneFrame]           = "1 frame";
    a[(std::size_t)StringId::RunaheadNFrames]            = "%d frames";

    a[(std::size_t)StringId::AudioSystemNote]            =
        "System volume controls live in the Switch home menu";
    a[(std::size_t)StringId::AudioSystemNoteHint]        =
        "Per-core audio settings are exposed in the in-game pause overlay.";

    a[(std::size_t)StringId::LibraryRescan]              = "Rescan library";
    a[(std::size_t)StringId::LibraryRescanHint]          = "Walks /foyer/roms/ + rebuilds cache.";
    a[(std::size_t)StringId::LibraryInvalidateCovers]    = "Invalidate cover cache";
    a[(std::size_t)StringId::LibraryInvalidateCoversHint] = "Reload box-art from disk.";
    a[(std::size_t)StringId::LibrarySortGames]           = "Sort games by";
    a[(std::size_t)StringId::LibrarySortGamesHint]       = "Order of the per-system game grid.";
    a[(std::size_t)StringId::LibrarySortSystems]         = "Sort systems by";
    a[(std::size_t)StringId::LibrarySortSystemsHint]     = "Order of the Home carousel tiles.";
    a[(std::size_t)StringId::LibraryHideEmpty]           = "Hide empty systems";
    a[(std::size_t)StringId::LibraryHideEmptyHint]       =
        "Hide systems whose rom folder has no scannable games.";
    a[(std::size_t)StringId::SortByName]                 = "Name";
    a[(std::size_t)StringId::SortByRecent]               = "Recently played";
    a[(std::size_t)StringId::SortByPlaytime]             = "Playtime";
    a[(std::size_t)StringId::SortByFavoritesFirst]       = "Favorites first";
    a[(std::size_t)StringId::SystemSortScannerOrder]     = "Scanner order";
    a[(std::size_t)StringId::SystemSortAlphabetical]     = "Alphabetical";
    a[(std::size_t)StringId::SystemSortGameCount]        = "Game count";
    a[(std::size_t)StringId::SystemSortCustom]           = "Custom";

    a[(std::size_t)StringId::EmuDefaultCore]             = "Default core per system";
    a[(std::size_t)StringId::EmuDefaultCoreHint]         = "configure";
    a[(std::size_t)StringId::EmuCoresCatalog]            = "Cores catalog";
    a[(std::size_t)StringId::EmuBezelPacks]              = "Bezel packs";
    a[(std::size_t)StringId::EmuCheatPacks]              = "Cheat packs";
    a[(std::size_t)StringId::EmuShaderPresets]           = "Shader presets";
    a[(std::size_t)StringId::EmuStandalones]             = "External standalone emulators";
    a[(std::size_t)StringId::EmuStandalonesHint]         = "PSP / GC status";
    a[(std::size_t)StringId::EmuBezelPerSystem]          = "Bezel per system";
    a[(std::size_t)StringId::EmuBezelPerSystemHint]      = "pick or clear";
    a[(std::size_t)StringId::EmuRefreshManifest]         = "Refresh manifest";
    a[(std::size_t)StringId::EmuLoadingCatalog]          = "Loading catalog...";
    a[(std::size_t)StringId::EmuInstallAllBezels]        = "Install all bezel packs";
    a[(std::size_t)StringId::EmuInstallAllBezelsHint]    =
        "Walks every pack above; skips ones already at the manifest's version.";
    a[(std::size_t)StringId::EmuRefreshHintCores]        =
        "Pulls the latest foyer-cores release listing from GitHub.";
    a[(std::size_t)StringId::EmuRefreshHintBezels]       = "Pulls the latest foyer-bezels release listing.";
    a[(std::size_t)StringId::EmuRefreshHintCheats]       = "Pulls the latest foyer-cheats release listing.";
    a[(std::size_t)StringId::EmuRefreshHintShaders]      = "Pulls the latest foyer-shaders release listing.";

    a[(std::size_t)StringId::VerbDownload]               = "download";
    a[(std::size_t)StringId::VerbInstalled]              = "installed";
    a[(std::size_t)StringId::VerbInstalledReinstall]     = "installed - reinstall";
    a[(std::size_t)StringId::VerbUpdateAvailable]        = "update available";
    a[(std::size_t)StringId::VerbFetch]                  = "fetch";
    a[(std::size_t)StringId::VerbRun]                    = "run";
    a[(std::size_t)StringId::VerbRefresh]                = "refresh";
    a[(std::size_t)StringId::VerbConfigure]              = "configure";
    a[(std::size_t)StringId::VerbBrowseInstall]          = "browse / install";
    a[(std::size_t)StringId::VerbPickOrClear]            = "pick or clear";
    a[(std::size_t)StringId::VerbStatusInfo]             = "PSP / GC status";

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "foyer";
    a[(std::size_t)StringId::UpdatesCores]               = "Cores";
    a[(std::size_t)StringId::UpdatesBezels]              = "Bezels";
    a[(std::size_t)StringId::UpdatesCheats]              = "Cheats";
    a[(std::size_t)StringId::UpdatesShaders]             = "Shaders";
    a[(std::size_t)StringId::UpdatesCheckAll]            = "Check for updates";
    a[(std::size_t)StringId::UpdatesCheckAllHint]        = "Refresh every manifest in one pass.";
    a[(std::size_t)StringId::UpdatesUpToDateCores]       = "All cores up to date";
    a[(std::size_t)StringId::UpdatesNewCores]            = "%d new";
    a[(std::size_t)StringId::UpdatesUpdatedCores]        = "%d updated";
    a[(std::size_t)StringId::UpdatesFailedCores]         = "%d failed";
    a[(std::size_t)StringId::UpdatesInstalling]          = "Installing %s...";
    a[(std::size_t)StringId::UpdatesScanning]            = "Scanning...";
    a[(std::size_t)StringId::UpdatesNoData]              = "No data — refresh first";
    a[(std::size_t)StringId::UpdatesFetchManifest]       = "Fetching manifest...";

    a[(std::size_t)StringId::AccScreenscraperDevId]      = "ScreenScraper dev ID";
    a[(std::size_t)StringId::AccScreenscraperDevPwd]     = "ScreenScraper dev password";
    a[(std::size_t)StringId::AccScreenscraperUser]       = "ScreenScraper username";
    a[(std::size_t)StringId::AccScreenscraperPwd]        = "ScreenScraper password";
    a[(std::size_t)StringId::AccSteamgriddbApiKey]       = "SteamGridDB API key";
    a[(std::size_t)StringId::AccRetroachUser]            = "RetroAchievements username";
    a[(std::size_t)StringId::AccRetroachToken]           = "RetroAchievements API token";
    a[(std::size_t)StringId::AccLoginRequired]           = "Required to access this scraper";
    a[(std::size_t)StringId::AccCleared]                 = "Cleared";

    a[(std::size_t)StringId::PickCoverTitle]             = "Pick cover";
    a[(std::size_t)StringId::PickCoverHint]              = "A select   B cancel   D-pad navigate";
    a[(std::size_t)StringId::PickerCancel]               = "Cancel";
    a[(std::size_t)StringId::PickerNoResults]            = "No covers found";
    a[(std::size_t)StringId::PickerLoading]              = "Loading...";

    a[(std::size_t)StringId::BannerInstalling]           = "Installing %s...";
    a[(std::size_t)StringId::BannerDone]                 = "%s done";
    a[(std::size_t)StringId::BannerFailed]               = "%s failed";
    a[(std::size_t)StringId::BannerCancelled]            = "%s cancelled";

    a[(std::size_t)StringId::SearchPlaceholder]          = "Search...";
    a[(std::size_t)StringId::SearchEmptyHint]            = "Type to filter the library";
    a[(std::size_t)StringId::SearchNoResults]            = "No results";

    a[(std::size_t)StringId::LangEnglish]                = "English";
    a[(std::size_t)StringId::LangSpanish]                = "Español";
    a[(std::size_t)StringId::LangPortugueseBrazil]       = "Português (Brasil)";
    a[(std::size_t)StringId::LangSystemDefault]          = "System default";

    a[(std::size_t)StringId::EmuInstallAllCheats]        = "Install all cheat packs";
    a[(std::size_t)StringId::EmuInstallAllCheatsHint]    =
        "Walks every pack above; skips ones already at the manifest's version.";
    a[(std::size_t)StringId::EmuInstallShaders]          = "Install shader presets";
    a[(std::size_t)StringId::EmuInstallShadersHint]      =
        "Downloads the foyer-shaders catalogue into /foyer/shaders/.";
    a[(std::size_t)StringId::EmuBezelPickHint]           =
        "Pick a PNG to overlay around the emulator output. (none) keeps the system clean.";
    a[(std::size_t)StringId::EmuBezelClearAll]           = "Clear all bezels";
    a[(std::size_t)StringId::EmuBezelClearAllHint]       =
        "Removes every per-system PNG so emulator output renders bare. "
        "Catalog files stay installed; pick again per system to re-apply.";
    a[(std::size_t)StringId::BezelForPrefix]             = "Bezel for %s";
    a[(std::size_t)StringId::BezelNoneOption]            = "(none — no bezel)";
    a[(std::size_t)StringId::DefaultCorePrefix]          = "Default core (%s)";

    a[(std::size_t)StringId::RunaheadTwoFrames]          = "2 frames";
    a[(std::size_t)StringId::RunaheadThreeFrames]        = "3 frames";
    a[(std::size_t)StringId::RunaheadFourFrames]         = "4 frames";

    a[(std::size_t)StringId::AccNotConfigured]           = "(none configured — edit %s)";
    a[(std::size_t)StringId::AccNotInstalled]            = "not installed";
    a[(std::size_t)StringId::AccEditedViaOsk]            = "Edited via the on-screen keyboard.";

    a[(std::size_t)StringId::WorkerBackgroundRunning]    = "Background job running";
    a[(std::size_t)StringId::WorkerCancelHint]           =
        "Aborts the in-flight transfer at the next callback.";

    a[(std::size_t)StringId::UpdatesEverythingUpToDate]  = "Everything is up to date";
    a[(std::size_t)StringId::UpdatesUpdateEverything]    = "Update everything";
    a[(std::size_t)StringId::UpdatesLastJustNow]         = "Last: just now";
    a[(std::size_t)StringId::UpdatesRescrapeNow]         = "Re-scrape now";
    a[(std::size_t)StringId::UpdatesRescrapeHint]        =
        "Refreshes the cores / bezels / cheats manifests.";
    a[(std::size_t)StringId::UpdatesScrapeAllSystems]    = "Scrape all systems";
    a[(std::size_t)StringId::UpdatesScrapeAllSystemsHint]=
        "Walks every system using the preferred scraper.";

    a[(std::size_t)StringId::ExpRomsOverUsb]             = "Roms over USB";
    a[(std::size_t)StringId::ExpRomsOverUsbHint]         =
        "Spin up libhaze MTP scoped to /foyer/roms.";
    a[(std::size_t)StringId::ExpAutoStartUsb]            = "Auto-start USB on boot";
    a[(std::size_t)StringId::ExpAutoStartUsbHint]        =
        "Skip the manual toggle on every launch.";
    a[(std::size_t)StringId::ExpVerboseLog]              = "Verbose log";
    a[(std::size_t)StringId::ExpVerboseLogHint]          =
        "Write extra diagnostics to /foyer/data/log.txt.";

    a[(std::size_t)StringId::VerbAction]                 = "Action";
    a[(std::size_t)StringId::VerbUpdate]                 = "Update %s";
    a[(std::size_t)StringId::VerbInstall]                = "Install %s";
    a[(std::size_t)StringId::VerbSkipVersion]            = "Skip this version";
    a[(std::size_t)StringId::VerbUpdateNow]              = "Update now";
    a[(std::size_t)StringId::VerbReinstall]              = "Re-install";
    return a;
}();

} // namespace (close anon — externs need external linkage)

// Forward-declare per-language catalogues defined in their own .cpp
// at foyer::i18n scope (e.g. i18n_es.cpp). External linkage required.
extern const Catalogue kSpanishStrings;
extern const Catalogue kPortugueseBrazilStrings;

namespace {

// Catalogue table. Indexed by Language. Slots that point to nullptr
// (or to empty strings inside an array) fall through to English at
// lookup time, so a partial translation is fine — nobody sees a
// blank space.
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
