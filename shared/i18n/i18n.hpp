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

    // ----- Sweep: banners (main.cpp + views.cpp) -----
    BannerLibraryRescanned,
    BannerManifestFetchFail,
    BannerCoresManifestFetchFail,
    BannerCheatsManifestFetchFail,
    BannerBezelsManifestFetchFail,
    BannerShadersManifestFetchFail,
    BannerFetchingShaderManifest,
    BannerFetchingCheatsManifest,
    BannerFetchingBezelsManifest,
    BannerNoCoverCandidates,
    BannerScrapeWorkerFailed,
    BannerScrapeAlreadyRunning,
    BannerScrapeNoCovers,
    BannerDownloadingFoyerUpdate,
    BannerRescanning,
    BannerCheckingFoyerUpdate,
    BannerNoRecentlyPlayed,
    BannerSetSteamgriddbApiKey,
    BannerFetchingCovers,
    BannerVirtualSystemReorderBlock,  // "Recents/Favorites can't be moved"
    BannerAlreadyAtEdge,
    BannerSystemReordered,
    BannerScrapeQueued,
    BannerShaderOverrideCleared,
    BannerRunaheadOverrideCleared,
    BannerPerGameOverrideCleared,
    BannerSortChanged,
    BannerRescraping,
    BannerInstallingBezelPack,    // "Installing bezel pack: %s..."
    BannerCoresFailedCheckLog,    // "%d cores failed - check log"
    BannerBezelPacksFailed,       // "%d bezel packs failed - check log"
    BannerBezelPacksReady,        // "Bezel packs ready (%d new, %d updated, %d skipped)"
    BannerCoreNotInManifest,      // "Core not in manifest: %s"
    BannerCoreNotInstalled,       // "Core not installed: foyer-%s.nro"

    // ----- Restart-confirm button label -----
    RestartNow,

    // ----- Sweep v0.4.11: remaining hardcoded UI strings -----
    BannerAddedToFavorites,
    BannerRemovedFromFavorites,
    BannerPerGameShaderSet,            // "Per-game shader: %s"
    BannerPerGameRunaheadOff,
    BannerPerGameRunaheadOneFrame,
    BannerPerGameRunaheadNFrames,      // "Per-game run-ahead: %d frames"
    BannerPerGameCoreSet,              // "Per-game core set: %s"
    BannerSystemDefaultCoreSet,        // "System default core set: %s"
    BannerThemeChanged,                // "Theme: %s"
    BannerShaderChanged,               // "Shader: %s"
    BannerRunaheadOff,                 // "Run-ahead: off"
    BannerRunaheadOneFrame,            // "Run-ahead: %d frame"
    BannerRunaheadNFrames,             // "Run-ahead: %d frames"
    BannerCoverCacheCleared,
    BannerScrapeBulkHint,              // "Open a system and press Y — bulk scrape next pass"
    BannerFetchingCoresManifest,
    BannerInstallingCheatPacks,        // "Installing all cheat packs..."
    BannerInstallingBezelPacks,        // "Installing all bezel packs..."
    BannerInstallingCheatPack,         // "Installing cheat pack: %s..."
    BannerCancelling,
    BannerUpdatingEverything,
    BannerDrillDownComingNextPass,
    BannerScraperFieldSaved,           // "%s saved"

    // ----- v0.4.11 deep sweep: dialog text + asset bucket headers -----
    UpdateFoyerHintFull,               // "Downloads foyer.nro to /switch/foyer/foyer.nro.new — applied next boot."
    UpdateRestartHintFull,             // "Replaces foyer.nro and re-launches. No on-disk save loss."
    BucketBezels,                      // "Bezels" (Updates page bucket header)
    BucketCheats,                      // "Cheats"
    PickerActionTitle,                 // "Action" (single-item action picker)
    PickerUpdateTitle,                 // "Update %s"
    PickerInstallTitle,                // "Install %s"
    PickerCandidatePrefix,             // "Candidate %d"
    PickCoverForGame,                  // "Pick cover for %s"
    SwkbdSearchGuide,                  // "Search games by name"
    GameDetailContinueLabel,           // "Continue" (resume action button)

    // Shader display names (fallback when no localized name exists)
    ShaderPrettyScanlines,             // "Scanlines"
    ShaderPrettyCrtSimple,             // "CRT (simple)"
    ShaderPrettyLcdGrid,               // "LCD grid"
    ShaderPrettyGbDmg,                 // "Game Boy DMG"
    ShaderPrettyGbaCorrect,            // "GBA color correct"
    ShaderPrettyNone,                  // "Off"

    // Game-detail resume row
    ResumeQuickSlot,                   // "quick slot"
    ResumeSlotN,                       // "slot %d"
    ResumeBadge,                       // "resume"

    // Game detail header + per-game knob badges
    DetailHeaderResumeAndCore,         // "Continue / Core"
    DetailHeaderCoreOnly,              // "Core"
    KnobBadgePerGame,                  // "per-game"
    KnobBadgeDefault,                  // "default"
    DetailShaderRowLabel,              // "Shader" (knob row label)
    DetailRunaheadRowLabel,            // "Run-ahead" (knob row label)
    DetailRunaheadOff,                 // "Off"
    DetailRunaheadOneFrame,            // "1 frame"
    DetailRunaheadNFrames,             // "%d frames"

    // Bottom-bar verbs (chord with glyph, e.g. "A: <verb>")
    HintNavigate,                      // "navigate"
    HintBack,                          // "back"
    HintQuit,                          // "quit"
    HintMenu,                          // "menu"
    HintSettings,                      // "settings"
    HintPick,                          // "pick"
    HintEnter,                         // "enter"
    HintLaunch,                        // "launch"
    HintDetails,                       // "details"
    HintScrape,                        // "scrape"
    HintToggle,                        // "toggle"
    HintEdit,                          // "edit"
    HintSelect,                        // "select"
    HintChange,                        // "change"
    HintRun,                           // "run"
    HintContinueVerb,                  // "continue" (lowercase verb form)
    HintCycleShader,                   // "cycle shader"
    HintCycleRunAhead,                 // "cycle run-ahead"
    HintClearOverride,                 // "clear override"
    HintSetPerGame,                    // "set per-game"
    HintSetSysDefault,                 // "set sys default"
    HintNewQuery,                      // "new query"
    HintOpen,                          // "open"

    // Counters
    CountMatchSingular,                // "%zu match"
    CountMatchPlural,                  // "%zu matches"
    CountItemsPlural,                  // "%zu items"
    CountItemsWithMb,                  // "%zu items   ~%.0f MB"
    CountItemsWithKb,                  // "%zu items   ~%llu KB"
    CountAchievements,                 // "%d / %d achievements"
    CountGameSingular,                 // "game"
    CountGamePlural,                   // "games"

    // Boot splash phase labels
    BootStarting,                      // "Starting..."
    BootSeedingAssets,                 // "Seeding assets..."
    BootInitNetwork,                   // "Initialising network..."
    BootLoadingTheme,                  // "Loading theme..."
    BootScanningLibrary,               // "Scanning library..."
    BootReady,                         // "Ready"

    // Game detail core-row tags (right-aligned accent text)
    CoreTagPerGame,                    // "per-game"
    CoreTagSystemDefault,              // "system default"
    CoreTagActive,                     // "active"
    CoreTagBuiltInDefault,             // "built-in default"

    // Per-row install progress verbs (banner: "[2/5] fceumm - installed")
    ActionPastSkipped,                 // "skipped"
    ActionPastUpdated,                 // "updated"
    ActionPastInstalled,               // "installed"
    ActionPastFailed,                  // "FAILED"

    // Misc account/value labels
    AccountUnset,                      // "unset" (placeholder for empty credentials)
    UpdatesMetadataKindLabel,          // "metadata" (Scrape-all-systems row value)

    // GameDetail per-game picker rows + values
    DetailBezelRowLabel,               // "Bezel"
    DetailCoreRowLabel,                // "Core"
    DetailUseSystemDefault,            // "(system default)"
    DetailValueNone,                   // "(none)"
    DetailUseSystemDefaultPicker,      // "System default"
    BannerPerGameBezelSet,             // "Per-game bezel: %s"
    BannerPerGameBezelCleared,         // "Per-game bezel cleared"

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
