#pragma once

// foyer i18n — compile-time enum-keyed catalogue.
//
// Every user-visible string in the browser UI gets a StringId here.
// Catalogues for each shipped language are arrays of `const char*`
// indexed by the enum value, so a lookup is one bounds-check + one
// array index — no hash, no parsing, no runtime allocation.
//
// Adding a string:
//   1. Append a new entry to the StringId enum below (before
//      kStringIdCount). Don't reorder existing entries — that
//      shifts every translation by one slot.
//   2. Add the English text to the kEnglishStrings array in
//      i18n.cpp at the matching index.
//   3. Run tools/i18n_scan.py to verify every other language file
//      either has a non-null entry at that index or explicitly
//      marks it kI18nMissing (which falls through to English).
//
// At call sites: `i18n::tr(StringId::Settings)` returns the right
// string for the active locale, or English when the key is missing.

#include <cstdint>

namespace foyer::i18n {

// Stable, append-only. Don't reorder — index = catalogue array slot.
// Group related strings; the comments above each block exist for
// translators, not for the runtime.
enum class StringId : std::uint16_t {
    // ----- Top-level navigation / chrome -----
    Home,
    Library,
    Settings,
    Search,
    Updates,
    Back,
    Quit,

    // ----- Settings sections -----
    SettingsDisplay,
    SettingsEmulator,
    SettingsLibrary,
    SettingsScrapers,
    SettingsNetworking,
    SettingsAbout,

    // ----- Updates page -----
    UpdatesCheckForUpdates,
    UpdatesUpToDate,
    UpdatesAvailable,
    UpdatesInstall,
    UpdatesRestartNow,
    UpdatesRestartLater,
    UpdatesDownloading,
    UpdatesFailed,

    // ----- Library / game list -----
    LibraryEmptyTitle,
    LibraryEmptyHint,
    LibraryRecentlyPlayed,
    LibraryFavorites,
    LibraryAllGames,
    LibrarySortBy,
    LibrarySortAlphabetical,
    LibrarySortByGameCount,
    LibrarySortCustom,

    // ----- Game actions -----
    GamePlay,
    GameResume,
    GameRestart,
    GameAddToFavorites,
    GameRemoveFromFavorites,
    GameScrape,
    GamePickCover,
    GameClearPlaytime,
    GameDelete,

    // ----- Scrapers -----
    ScraperLibretroThumbnails,
    ScraperScreenscraper,
    ScraperSteamgriddb,
    ScraperPickCover,
    ScraperNoMatches,

    // ----- Cores / installer -----
    CoresInstall,
    CoresUpdate,
    CoresDownloadFailed,
    CoresClearAllBezels,

    // ----- Common modal verbs -----
    Yes,
    No,
    OK,
    Cancel,
    Confirm,
    Close,
    Later,
    Exit,

    // ----- Empty-state messages -----
    EmptyNoSystemsFound,
    EmptyDropRomsHint,
    EmptyNoSystems,
    EmptyNoRomsInFolder,
    EmptyNoCoverScrapeHint,

    // ----- Game detail panel labels -----
    GamePublisher,
    GameDeveloper,
    GamePlayers,
    GameRating,

    // ----- System-row action menu -----
    ActionResumeLast,
    ActionMoveUp,
    ActionMoveDown,
    ActionRescanGames,
    ActionToggleFavorite,
    ActionPickCover,
    ActionFavoriteAll,
    ActionClearAllFavorites,
    ActionScrapeSystem,

    // ----- Quit / restart confirms -----
    QuitConfirmTitle,
    UpdateRestartTitle,
    UpdateRestartHint,
    UpdateFoyerTitle,
    UpdateFoyerHint,

    // ----- Settings sub-tabs / labels -----
    SettingsGeneral,
    SettingsAudio,
    SettingsAccounts,
    SettingsExperimental,
    SettingsPreferredScraper,
    SettingsPreferredScraperHint,
    SettingsRomRoot,
    SettingsRomRootHint,
    SettingsScanSubfolders,
    SettingsScanSubfoldersHint,
    SettingsTheme,
    SettingsThemeHint,
    SettingsShowClock,
    SettingsShowClockHint,
    SettingsLanguage,
    SettingsLanguageHint,

    // ----- Display extras -----
    DisplayShowBackgrounds,
    DisplayShowBackgroundsHint,
    DisplayShowCovers,
    DisplayShowCoversHint,
    DisplayShowBezels,
    DisplayShowBezelsHint,
    DisplayShader,
    DisplayShaderHint,
    DisplayRunahead,
    DisplayRunaheadHint,
    ShaderNone,
    ShaderScanlines,
    ShaderCrtSimple,
    ShaderLcdGrid,
    ShaderGbDmg,
    ShaderGbaCorrect,
    RunaheadOff,
    RunaheadOneFrame,
    RunaheadNFrames,        // "%d frames"

    // ----- Audio info -----
    AudioSystemNote,
    AudioSystemNoteHint,

    // ----- Library tab -----
    LibraryRescan,
    LibraryRescanHint,
    LibraryInvalidateCovers,
    LibraryInvalidateCoversHint,
    LibrarySortGames,
    LibrarySortGamesHint,
    LibrarySortSystems,
    LibrarySortSystemsHint,
    LibraryHideEmpty,
    LibraryHideEmptyHint,
    SortByName,
    SortByRecent,
    SortByPlaytime,
    SortByFavoritesFirst,
    SystemSortScannerOrder,
    SystemSortAlphabetical,
    SystemSortGameCount,
    SystemSortCustom,

    // ----- Emulator subpages (top-level Drill rows) -----
    EmuDefaultCore,
    EmuDefaultCoreHint,
    EmuCoresCatalog,
    EmuBezelPacks,
    EmuCheatPacks,
    EmuShaderPresets,
    EmuStandalones,
    EmuStandalonesHint,
    EmuBezelPerSystem,
    EmuBezelPerSystemHint,
    EmuRefreshManifest,
    EmuLoadingCatalog,
    EmuInstallAllBezels,
    EmuInstallAllBezelsHint,
    EmuRefreshHintCores,
    EmuRefreshHintBezels,
    EmuRefreshHintCheats,
    EmuRefreshHintShaders,

    // ----- Per-row verbs (cycle/action values) -----
    VerbDownload,
    VerbInstalled,
    VerbInstalledReinstall,
    VerbUpdateAvailable,
    VerbFetch,
    VerbRun,
    VerbRefresh,
    VerbConfigure,
    VerbBrowseInstall,
    VerbPickOrClear,
    VerbStatusInfo,

    // ----- Updates page -----
    UpdatesFoyerSelf,
    UpdatesCores,
    UpdatesBezels,
    UpdatesCheats,
    UpdatesShaders,
    UpdatesCheckAll,
    UpdatesCheckAllHint,
    UpdatesUpToDateCores,
    UpdatesNewCores,
    UpdatesUpdatedCores,
    UpdatesFailedCores,
    UpdatesInstalling,
    UpdatesScanning,
    UpdatesNoData,
    UpdatesFetchManifest,

    // ----- Accounts -----
    AccScreenscraperDevId,
    AccScreenscraperDevPwd,
    AccScreenscraperUser,
    AccScreenscraperPwd,
    AccSteamgriddbApiKey,
    AccRetroachUser,
    AccRetroachToken,
    AccLoginRequired,
    AccCleared,

    // ----- Picker dialogs -----
    PickCoverTitle,
    PickCoverHint,
    PickerCancel,
    PickerNoResults,
    PickerLoading,

    // ----- Bezels / shaders / cheats banners -----
    BannerInstalling,
    BannerDone,
    BannerFailed,
    BannerCancelled,

    // ----- Search view -----
    SearchPlaceholder,
    SearchEmptyHint,
    SearchNoResults,

    // ----- Languages (for the picker UI) -----
    LangEnglish,
    LangSpanish,
    LangPortugueseBrazil,
    LangSystemDefault,

    // ----- Emulator subpage details (cheats / shaders / bezels) -----
    EmuInstallAllCheats,
    EmuInstallAllCheatsHint,
    EmuInstallShaders,
    EmuInstallShadersHint,
    EmuBezelPickHint,
    EmuBezelClearAll,
    EmuBezelClearAllHint,
    BezelForPrefix,            // "Bezel for %s"
    BezelNoneOption,           // "(none — no bezel)"
    DefaultCorePrefix,         // "Default core (%s)"

    // ----- Run-ahead frame labels -----
    RunaheadTwoFrames,
    RunaheadThreeFrames,
    RunaheadFourFrames,

    // ----- Accounts misc -----
    AccNotConfigured,           // "(none configured — edit %s)"
    AccNotInstalled,            // "not installed"
    AccEditedViaOsk,            // "Edited via the on-screen keyboard."

    // ----- Worker / background job -----
    WorkerBackgroundRunning,
    WorkerCancelHint,

    // ----- Updates page extras -----
    UpdatesEverythingUpToDate,
    UpdatesUpdateEverything,
    UpdatesLastJustNow,
    UpdatesRescrapeNow,
    UpdatesRescrapeHint,
    UpdatesScrapeAllSystems,
    UpdatesScrapeAllSystemsHint,

    // ----- Experimental tab -----
    ExpRomsOverUsb,
    ExpRomsOverUsbHint,
    ExpAutoStartUsb,
    ExpAutoStartUsbHint,
    ExpVerboseLog,
    ExpVerboseLogHint,

    // ----- Per-row Action verbs -----
    VerbAction,
    VerbUpdate,
    VerbInstall,
    VerbSkipVersion,
    VerbUpdateNow,
    VerbReinstall,

    // ----- Cores Catalog -----
    UpdateAllAvailableCores,
    UpdateAllAvailableCoresHint,

    // ----- Search view -----
    SearchTitlePrefix,            // "Search: "
    SearchTypeToSearch,
    SearchPressYToEnter,
    SearchNoMatches,
    SearchPressYToRefine,

    // ----- Game grid / system view -----
    GridNoGames,
    GridNoCover,

    // ----- Banner / status verbs (with %s for item name) -----
    BannerCoverSaved,             // "Cover saved for %s"
    BannerClearedBezel,           // "Cleared bezel for %s"
    BannerSkippedItem,            // "Skipped %s"
    BannerInstallingItem,         // "Installing %s..."
    BannerReinstallingItem,       // "Re-installing %s..."
    BannerUpdatingItem,           // "Updating %s..."

    // ----- Picker decoration -----
    PickerCurrentMarker,          // "● current"

    // ----- Last-played / playtime relative time -----
    NeverPlayed,
    PlayedJustNow,
    PlayedMinAgo,                 // "played %d min ago"
    PlayedHrAgo,
    PlayedDaysAgo,
    PlayedWkAgo,
    PlayedMoAgo,
    NoPlaytime,
    PlaytimeSec,                  // "%d sec"
    PlaytimeMin,                  // "%d min"
    PlaytimeHr,                   // "%d hr"
    PlaytimeHrMin,                // "%d hr %d min"

    // ----- Pre-launch core-update prompt -----
    UpdatePromptTitle,            // "Update %s before playing?"
    UpdatePromptHint,             // "Version %s is available."
    UpdatePromptUpdate,
    UpdatePromptPlayAnyway,

    // Sentinel — keep last.
    kStringIdCount,
};

// Languages foyer ships catalogues for. Switch system language gets
// mapped onto these via map_switch_language() in i18n.cpp.
//
// Append-only; don't reorder existing entries.
enum class Language : std::uint8_t {
    English,
    Spanish,           // es / es-419 (Latin American)
    PortugueseBrazil,  // pt-BR

    kLanguageCount,
};

// Initialise i18n from the Switch system language. Safe to call
// multiple times; subsequent calls are cheap. Defaults to English
// if libnx returns an unsupported / unrecognised code.
void init();

// Override the active language explicitly. Mostly for the Settings
// menu's "Language" picker — boot-time detection uses init().
void set_language(Language lang);
Language language();

// Translate. Falls back to English if the active language's slot
// for this id is null or empty.
const char* tr(StringId id);

// Force English — used for log messages + CI / debug paths that
// shouldn't follow the user's locale.
const char* tr_en(StringId id);

} // namespace foyer::i18n
