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

    a[(std::size_t)StringId::UpdatesFoyerSelf]           = "Foyer";
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

    a[(std::size_t)StringId::UpdateAllAvailableCores]    = "Update all available cores";
    a[(std::size_t)StringId::UpdateAllAvailableCoresHint] =
        "Walks every core flagged update-available and downloads them in order.";

    a[(std::size_t)StringId::SearchTitlePrefix]          = "Search: ";
    a[(std::size_t)StringId::SearchTypeToSearch]         = "Type to search";
    a[(std::size_t)StringId::SearchPressYToEnter]        = "Press Y to enter a query";
    a[(std::size_t)StringId::SearchNoMatches]            = "No matches";
    a[(std::size_t)StringId::SearchPressYToRefine]       = "Press Y to refine the query";

    a[(std::size_t)StringId::GridNoGames]                = "No games";
    a[(std::size_t)StringId::GridNoCover]                = "no cover";

    a[(std::size_t)StringId::BannerCoverSaved]           = "Cover saved for %s";
    a[(std::size_t)StringId::BannerClearedBezel]         = "Cleared bezel for %s";
    a[(std::size_t)StringId::BannerSkippedItem]          = "Skipped %s";
    a[(std::size_t)StringId::BannerInstallingItem]       = "Installing %s...";
    a[(std::size_t)StringId::BannerReinstallingItem]     = "Re-installing %s...";
    a[(std::size_t)StringId::BannerUpdatingItem]         = "Updating %s...";

    a[(std::size_t)StringId::PickerCurrentMarker]        = "● current";

    a[(std::size_t)StringId::NeverPlayed]                = "never played";
    a[(std::size_t)StringId::PlayedJustNow]              = "played just now";
    a[(std::size_t)StringId::PlayedMinAgo]               = "played %d min ago";
    a[(std::size_t)StringId::PlayedHrAgo]                = "played %d hr ago";
    a[(std::size_t)StringId::PlayedDaysAgo]              = "played %d days ago";
    a[(std::size_t)StringId::PlayedWkAgo]                = "played %d wk ago";
    a[(std::size_t)StringId::PlayedMoAgo]                = "played %d mo ago";
    a[(std::size_t)StringId::NoPlaytime]                 = "no playtime";
    a[(std::size_t)StringId::PlaytimeSec]                = "%d sec";
    a[(std::size_t)StringId::PlaytimeMin]                = "%d min";
    a[(std::size_t)StringId::PlaytimeHr]                 = "%d hr";
    a[(std::size_t)StringId::PlaytimeHrMin]              = "%d hr %d min";

    a[(std::size_t)StringId::UpdatePromptTitle]          = "Update %s before playing?";
    a[(std::size_t)StringId::UpdatePromptHint]           = "Version %s is available.";
    a[(std::size_t)StringId::UpdatePromptUpdate]         = "Update";
    a[(std::size_t)StringId::UpdatePromptPlayAnyway]     = "Play anyway";

    a[(std::size_t)StringId::BannerLibraryRescanned]        = "Library rescanned";
    a[(std::size_t)StringId::BannerManifestFetchFail]       = "Manifest fetch failed - check log";
    a[(std::size_t)StringId::BannerCoresManifestFetchFail]  = "Cores manifest fetch failed";
    a[(std::size_t)StringId::BannerCheatsManifestFetchFail] = "Cheats manifest fetch failed";
    a[(std::size_t)StringId::BannerBezelsManifestFetchFail] = "Bezels manifest fetch failed";
    a[(std::size_t)StringId::BannerShadersManifestFetchFail] = "Shader manifest fetch failed";
    a[(std::size_t)StringId::BannerFetchingShaderManifest]  = "Fetching shader manifest...";
    a[(std::size_t)StringId::BannerFetchingCheatsManifest]  = "Fetching cheats manifest...";
    a[(std::size_t)StringId::BannerFetchingBezelsManifest]  = "Fetching bezels manifest...";
    a[(std::size_t)StringId::BannerNoCoverCandidates]       = "No cover candidates found";
    a[(std::size_t)StringId::BannerScrapeWorkerFailed]      = "Scrape worker failed to start";
    a[(std::size_t)StringId::BannerScrapeAlreadyRunning]    = "Scrape already in progress";
    a[(std::size_t)StringId::BannerScrapeNoCovers]          = "Scrape found no covers - check log";
    a[(std::size_t)StringId::BannerDownloadingFoyerUpdate]  = "Downloading foyer update...";
    a[(std::size_t)StringId::BannerRescanning]              = "Rescanning library...";
    a[(std::size_t)StringId::BannerCheckingFoyerUpdate]     = "Checking for foyer update...";
    a[(std::size_t)StringId::BannerNoRecentlyPlayed]        = "No recently played games";
    a[(std::size_t)StringId::BannerSetSteamgriddbApiKey]    =
        "Set steamgriddb.api_key in accounts.jsonc first";
    a[(std::size_t)StringId::BannerFetchingCovers]          = "Fetching cover candidates...";
    a[(std::size_t)StringId::BannerVirtualSystemReorderBlock] =
        "Recents/Favorites can't be moved";
    a[(std::size_t)StringId::BannerAlreadyAtEdge]           = "Already at the edge";
    a[(std::size_t)StringId::BannerSystemReordered]         = "System reordered";
    a[(std::size_t)StringId::BannerScrapeQueued]            =
        "Scrape queued — runs on next pass";
    a[(std::size_t)StringId::BannerShaderOverrideCleared]   =
        "Shader override cleared (uses general default)";
    a[(std::size_t)StringId::BannerRunaheadOverrideCleared] =
        "Run-ahead override cleared (uses general default)";
    a[(std::size_t)StringId::BannerPerGameOverrideCleared]  = "Per-game override cleared";
    a[(std::size_t)StringId::BannerSortChanged]             = "Sort changed - rescanning...";
    a[(std::size_t)StringId::BannerRescraping]              = "Re-scraping manifests...";
    a[(std::size_t)StringId::BannerInstallingBezelPack]     = "Installing bezel pack: %s...";
    a[(std::size_t)StringId::BannerCoresFailedCheckLog]     = "%d core%s failed - check log";
    a[(std::size_t)StringId::BannerBezelPacksFailed]        =
        "%d bezel pack%s failed - check log";
    a[(std::size_t)StringId::BannerBezelPacksReady]         =
        "Bezel packs ready (%d new, %d updated, %d skipped)";
    a[(std::size_t)StringId::BannerCoreNotInManifest]       = "Core not in manifest: %s";
    a[(std::size_t)StringId::BannerCoreNotInstalled]        = "Core not installed: foyer-%s.nro";

    a[(std::size_t)StringId::RestartNow]                    = "Restart now";

    // Sweep v0.4.11
    a[(std::size_t)StringId::BannerAddedToFavorites]        = "Added to favorites";
    a[(std::size_t)StringId::BannerRemovedFromFavorites]    = "Removed from favorites";
    a[(std::size_t)StringId::BannerPerGameShaderSet]        = "Per-game shader: %s";
    a[(std::size_t)StringId::BannerPerGameRunaheadOff]      = "Per-game run-ahead: off";
    a[(std::size_t)StringId::BannerPerGameRunaheadOneFrame] = "Per-game run-ahead: 1 frame";
    a[(std::size_t)StringId::BannerPerGameRunaheadNFrames]  = "Per-game run-ahead: %d frames";
    a[(std::size_t)StringId::BannerPerGameCoreSet]          = "Per-game core set: %s";
    a[(std::size_t)StringId::BannerSystemDefaultCoreSet]    = "System default core set: %s";
    a[(std::size_t)StringId::BannerThemeChanged]            = "Theme: %s";
    a[(std::size_t)StringId::BannerShaderChanged]           = "Shader: %s";
    a[(std::size_t)StringId::BannerRunaheadOff]             = "Run-ahead: off";
    a[(std::size_t)StringId::BannerRunaheadOneFrame]        = "Run-ahead: %d frame";
    a[(std::size_t)StringId::BannerRunaheadNFrames]         = "Run-ahead: %d frames";
    a[(std::size_t)StringId::BannerCoverCacheCleared]       = "Cover cache cleared";
    a[(std::size_t)StringId::BannerScrapeBulkHint]          =
        "Open a system and press Y — bulk scrape next pass";
    a[(std::size_t)StringId::BannerFetchingCoresManifest]   = "Fetching cores manifest...";
    a[(std::size_t)StringId::BannerInstallingCheatPacks]    = "Installing all cheat packs...";
    a[(std::size_t)StringId::BannerInstallingBezelPacks]    = "Installing all bezel packs...";
    a[(std::size_t)StringId::BannerInstallingCheatPack]     = "Installing cheat pack: %s...";
    a[(std::size_t)StringId::BannerCancelling]              = "Cancelling...";
    a[(std::size_t)StringId::BannerUpdatingEverything]      = "Updating everything...";
    a[(std::size_t)StringId::BannerDrillDownComingNextPass] =
        "Drill-down view comes in the next pass";
    a[(std::size_t)StringId::BannerScraperFieldSaved]       = "%s saved";

    // Deep sweep
    a[(std::size_t)StringId::UpdateFoyerHintFull]           =
        "Downloads foyer.nro to /switch/foyer/foyer.nro.new — applied next boot.";
    a[(std::size_t)StringId::UpdateRestartHintFull]         =
        "Replaces foyer.nro and re-launches. No on-disk save loss.";
    a[(std::size_t)StringId::BucketBezels]                  = "Bezels";
    a[(std::size_t)StringId::BucketCheats]                  = "Cheats";
    a[(std::size_t)StringId::PickerActionTitle]             = "Action";
    a[(std::size_t)StringId::PickerUpdateTitle]             = "Update %s";
    a[(std::size_t)StringId::PickerInstallTitle]            = "Install %s";
    a[(std::size_t)StringId::PickerCandidatePrefix]         = "Candidate %d";
    a[(std::size_t)StringId::PickCoverForGame]              = "Pick cover for %s";
    a[(std::size_t)StringId::SwkbdSearchGuide]              = "Search games by name";
    a[(std::size_t)StringId::GameDetailContinueLabel]       = "Continue";

    a[(std::size_t)StringId::ShaderPrettyScanlines]         = "Scanlines";
    a[(std::size_t)StringId::ShaderPrettyCrtSimple]         = "CRT (simple)";
    a[(std::size_t)StringId::ShaderPrettyLcdGrid]           = "LCD grid";
    a[(std::size_t)StringId::ShaderPrettyGbDmg]             = "Game Boy DMG";
    a[(std::size_t)StringId::ShaderPrettyGbaCorrect]        = "GBA color correct";
    a[(std::size_t)StringId::ShaderPrettyNone]              = "Off";

    a[(std::size_t)StringId::ResumeQuickSlot]               = "quick slot";
    a[(std::size_t)StringId::ResumeSlotN]                   = "slot %d";
    a[(std::size_t)StringId::ResumeBadge]                   = "resume";

    a[(std::size_t)StringId::DetailHeaderResumeAndCore]     = "Continue / Core";
    a[(std::size_t)StringId::DetailHeaderCoreOnly]          = "Core";
    a[(std::size_t)StringId::KnobBadgePerGame]              = "per-game";
    a[(std::size_t)StringId::KnobBadgeDefault]              = "default";
    a[(std::size_t)StringId::DetailShaderRowLabel]          = "Shader";
    a[(std::size_t)StringId::DetailRunaheadRowLabel]        = "Run-ahead";
    a[(std::size_t)StringId::DetailRunaheadOff]             = "Off";
    a[(std::size_t)StringId::DetailRunaheadOneFrame]        = "1 frame";
    a[(std::size_t)StringId::DetailRunaheadNFrames]         = "%d frames";

    a[(std::size_t)StringId::HintNavigate]                  = "navigate";
    a[(std::size_t)StringId::HintBack]                      = "back";
    a[(std::size_t)StringId::HintQuit]                      = "quit";
    a[(std::size_t)StringId::HintMenu]                      = "menu";
    a[(std::size_t)StringId::HintSettings]                  = "settings";
    a[(std::size_t)StringId::HintPick]                      = "pick";
    a[(std::size_t)StringId::HintEnter]                     = "enter";
    a[(std::size_t)StringId::HintLaunch]                    = "launch";
    a[(std::size_t)StringId::HintDetails]                   = "details";
    a[(std::size_t)StringId::HintScrape]                    = "scrape";
    a[(std::size_t)StringId::HintToggle]                    = "toggle";
    a[(std::size_t)StringId::HintEdit]                      = "edit";
    a[(std::size_t)StringId::HintSelect]                    = "select";
    a[(std::size_t)StringId::HintChange]                    = "change";
    a[(std::size_t)StringId::HintRun]                       = "run";
    a[(std::size_t)StringId::HintContinueVerb]              = "continue";
    a[(std::size_t)StringId::HintCycleShader]               = "cycle shader";
    a[(std::size_t)StringId::HintCycleRunAhead]             = "cycle run-ahead";
    a[(std::size_t)StringId::HintClearOverride]             = "clear override";
    a[(std::size_t)StringId::HintSetPerGame]                = "set per-game";
    a[(std::size_t)StringId::HintSetSysDefault]             = "set sys default";
    a[(std::size_t)StringId::HintNewQuery]                  = "new query";
    a[(std::size_t)StringId::HintOpen]                      = "open";

    a[(std::size_t)StringId::CountMatchSingular]            = "%zu match";
    a[(std::size_t)StringId::CountMatchPlural]              = "%zu matches";
    a[(std::size_t)StringId::CountItemsPlural]              = "%zu items";
    a[(std::size_t)StringId::CountItemsWithMb]              = "%zu items   ~%.0f MB";
    a[(std::size_t)StringId::CountItemsWithKb]              = "%zu items   ~%llu KB";
    a[(std::size_t)StringId::CountAchievements]             = "%d / %d achievements";
    a[(std::size_t)StringId::CountGameSingular]             = "game";
    a[(std::size_t)StringId::CountGamePlural]               = "games";
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
