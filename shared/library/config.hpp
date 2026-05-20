#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {

// Browser-wide preferences read from /foyer/config/general.jsonc on first
// access. A default file is created on missing-file so the user can edit via MTP.
struct Config {
    enum class Scraper { Libretro, ScreenScraper, SteamGridDB };

    // Game-list ordering inside a System view. Applied in scanner.cpp
    // post-scan + on the synthesised Recent / Favorites / Search
    // index views.
    enum class SortMode {
        Name,        // alphabetical by display name (default)
        Recent,      // last_played descending; never-played at the bottom
        Playtime,    // playtime descending; never-played at the bottom
        Favorites,   // favorites first, then alphabetical
    };

    Scraper      preferred_scraper = Scraper::Libretro;
    std::string  rom_root          = "/foyer/roms";
    // Default to the bundled Alekfull NX theme pack — square HOS-style
    // tile art per system, sourced from
    // https://github.com/anthonycaccese/alekfull-nx-es-de (a port of
    // fagnerpc's Alekfull NX EmulationStation theme). Older configs
    // that saved their own theme_name keep their preference. The
    // theme has multiple color schemes (light / dark) selected via
    // theme_color below.
    std::string  theme_name        = "alekfull-nx";
    // Color scheme variant of theme_name. "light" loads
    // <theme>/theme.jsonc; any other value loads
    // <theme>/theme-<color>.jsonc, e.g. theme-dark.jsonc.
    std::string  theme_color       = "light";
    SortMode     sort_mode         = SortMode::Name;

    // Order of the Home carousel system tiles. ScannerOrder is the
    // default (whatever scan_library emits); the others sort the
    // populated systems alphabetically / by populated game count
    // descending / by an explicit user-defined ordering held in
    // system_custom_order. Virtual systems (Recent / Favorites)
    // always pin to the front — they're not affected by this knob.
    enum class SystemSortMode { ScannerOrder, Alphabetical, GameCount, Custom };
    SystemSortMode           system_sort_mode = SystemSortMode::ScannerOrder;
    // For Custom mode: list of system folder slugs in the order the
    // user wants them surfaced. Folders not in the list fall back to
    // alphabetical at the end so a fresh system doesn't disappear.
    std::vector<std::string> system_custom_order;
    // Default post-process shader applied to every game's framebuffer.
    // Built-in names: "none", "scanlines", "crt_simple", "lcd_grid",
    // "gb_dmg", "gba_correct". Anything else is treated as a path
    // stem under /foyer/shaders/<name>.glsl by the player. Per-game
    // overrides go through per_game.jsonc's "shader" field.
    std::string  shader_name       = "none";

    // Library + UI toggles (mirror the Tico-style Settings categories).
    bool         scan_subfolders   = true;
    bool         show_clock        = true;
    bool         show_backgrounds  = true;
    bool         show_covers       = true;
    // Display the bezel art (per-rom -> per-system -> default fallback)
    // around the emulator output. Off short-circuits the resolution
    // chain entirely so even the bundled default.png is hidden, useful
    // when the user prefers integer-scale or a vendor overlay.
    bool         show_bezels       = true;
    // 0.5.0 chrome knobs:
    //   rounded_tiles  — when on, the home tiles get a 14 px corner
    //                    radius. Off matches HOS exactly (square tiles).
    //   action_row_dock — when on, the Home action row sits inside one
    //                    rounded "dock" pill (newer firmware look).
    //                    Off renders individual circle buttons with
    //                    a separator line above (older firmware look).
    bool         rounded_tiles     = false;
    bool         action_row_dock   = true;
    // When true, the home carousel skips systems whose rom folder is
    // empty. Foyer auto-creates a <rom_root>/<system>/ subdirectory
    // for every supported system on first boot, so the carousel
    // would otherwise be cluttered with 60+ empty tiles. Default on.
    bool         hide_empty_systems = true;

    // UI language override. Empty string = follow Switch system
    // language. Other values = ISO-style language code that
    // i18n::map_switch_language() recognises ("es", "pt-BR", ...).
    // Loaded at boot; takes effect immediately on the next frame
    // since translation is stateless.
    std::string  language          = "";

    // UI theme override. "" / "auto" = follow HOS ColorSetId via
    // theme_watcher (default). "light" / "dark" = ignore HOS state
    // and pin brls's ThemeVariant. theme_watcher honours this on
    // every poll, so flipping HOS while pinned is a no-op.
    std::string  theme_override     = "";

    // Boot-time update check toggle. true = on splash we fire a
    // silent update_check::kick(false) so the user sees a prompt
    // when the manifest carries a newer version; false skips
    // entirely. Settings → General exposes this so users who
    // don't want network on boot can opt out.
    bool         update_check_on_boot = true;

    // Preferred metadata region. "" = auto (scraper falls back to
    // wor → us → eu → jp). Explicit values: "us", "eu", "jp",
    // "br", "wor". Used by the ScreenScraper region picker and
    // surfaced as the per-game "release date / publisher / name"
    // language hint when the scraper offers multiple regional
    // variants.
    std::string  region              = "";

    // Off by default — keeping extracted .zip / .7z roms around in
    // /foyer/data/extract/ lets a hot game skip the re-unzip on
    // its next launch. When the user opts in, anything in that
    // directory whose mtime hasn't been touched in
    // scrub_extracted_days days gets unlinked on browser boot.
    bool         scrub_extracted_enabled = false;
    int          scrub_extracted_days    = 10;

    // 0.5.2 Home action-row external app chain-launch targets. The
    // eShop / Album buttons walk this list in order until one resolves
    // to an existing .nro on the SD; we then envSetNextLoad it so
    // the user lands in their preferred installer / viewer instead of
    // foyer attempting an unreliable libapplet route.
    //
    // Empty path entries are skipped silently. The defaults cover the
    // most-common installer locations; users with bespoke folder
    // layouts edit /foyer/data/general.jsonc directly until the
    // dedicated Settings UI lands in 0.5.3.
    std::string  external_eshop_nro =
        "/switch/Tinfoil/Tinfoil.nro";
    std::string  external_eshop_nro_alt =
        "/switch/Awoo Installer/Awoo Installer.nro";
    std::string  external_album_nro = "";  // user-configured

    // Run-ahead lookahead frames. 0 disables (default); 1..4 trade CPU
    // for reduced visible input lag — each enabled frame adds one extra
    // retro_run() call per displayed frame. Cores with a 0-byte
    // serialize state silently fall back to no run-ahead at runtime.
    int          runahead_frames   = 0;

    // Experimental knobs — exposed under the Experimental category only.
    bool         mtp_autostart     = false;
    bool         debug_log         = false;

    // libhaze MTP exposure toggles. mtp_expose_roms drives the
    // /foyer/roms mount; mtp_expose_logs adds a second mount over
    // /foyer/data/logs so the user can pull crash + session logs
    // off the SD without unmounting the card. Both default off —
    // MTP only spins up when at least one is on.
    bool         mtp_expose_roms   = true;
    bool         mtp_expose_logs   = false;

    // Where Settings → Updates → Install/update cores reads its manifest.
    // Defaults to the foyer-frontend release; override for forks running
    // their own cores release.
    std::string  cores_manifest_url =
        "https://github.com/foyer-frontend/foyer-cores/releases/latest/download/manifest.json";

    // Where the self-update flow looks for a newer foyer.nro. Same fork
    // semantics — point at a different repo to follow a custom release
    // line.
    std::string  foyer_manifest_url =
        "https://github.com/foyer-frontend/foyer/releases/latest/download/foyer-manifest.json";

    // foyer-shaders preset catalogue. Same redistribution model as
    // cores: a manifest with a list of preset zips, each unpacking
    // into /foyer/shaders/<name>/.
    std::string  shaders_manifest_url =
        "https://github.com/foyer-frontend/foyer-shaders/releases/latest/download/manifest.json";

    // foyer-cheats per-system pack catalogue. CI re-slices
    // libretro-database/cht weekly + on each upstream release; the
    // manifest tracks which upstream version the packs were sliced
    // from so foyer can show "based on libretro-database vX.Y.Z".
    std::string  cheats_manifest_url =
        "https://github.com/foyer-frontend/foyer-cheats/releases/latest/download/manifest.json";

    // foyer-bezels per-system bezel catalogue. CI re-slices
    // libretro/common-overlays weekly. Per-system PNGs land at
    // /foyer/bezels/<system>.png; the player falls back to
    // /foyer/bezels/default.png (bundled with the browser) for
    // anything not covered.
    std::string  bezels_manifest_url =
        "https://github.com/foyer-frontend/foyer-bezels/releases/latest/download/manifest.json";

    // Direct URL of the foyer-assets.zip published alongside foyer.nro on
    // every foyer release. The zip carries the systems/ + themes/ art
    // that used to ship inside foyer.nro's romfs; first-run download
    // keeps the binary under the hbloader chain-launch unmap threshold.
    std::string  foyer_assets_url =
        "https://github.com/foyer-frontend/foyer/releases/latest/download/foyer-assets.zip";

    // Per-system core override. Stored flat to keep the header light; the
    // list is short (≤20 systems) so linear lookup is fine.
    struct PerSystemCore { std::string folder; std::string core; };
    std::vector<PerSystemCore> default_core_per_system;

    // Per-system bezel + shader overrides. Same shape, value is the
    // basename of the installed asset (matches install_bezels /
    // install_shaders sidecar layout). Empty string in the lookup =
    // "no per-system default for this system, fall back to general".
    struct PerSystemAsset { std::string folder; std::string name; };
    std::vector<PerSystemAsset> default_bezel_per_system;
    std::vector<PerSystemAsset> default_shader_per_system;

    // External standalone-emulator launchers. Keyed by system folder
    // name; value is an SD path to the standalone nro that ships its
    // own UI (no libretro wrapper). When set AND the nro exists, foyer
    // chain-launches the standalone with the rom path as argv[1] —
    // bypassing the libretro player loop entirely.
    //
    // Used for systems where a libretro Switch port doesn't exist
    // upstream but a working standalone Switch nro does. PPSSPP and
    // Dolphin are the canonical entries; the defaults match the
    // install paths their official Switch releases use.
    struct ExternalCore { std::string folder; std::string nro_path; };
    std::vector<ExternalCore> external_cores = {
        // Defaults match the install paths PPSSPP and Dolphin's
        // official Switch nightlies write to. Users who installed to
        // a different path can edit /foyer/config/general.jsonc.
        { "psp", "/switch/PPSSPP/PPSSPP.nro" },
        { "gc",  "/switch/dolphin-emu/dolphin-emu.nro" },
    };

    // Returns the user-set default core name for a system folder, or
    // nullptr if none is configured.
    const char* default_core_for(std::string_view folder) const;

    // Same shape for the per-system bezel + shader overrides.
    // Returns nullptr (no override) so callers can chain
    //   per_game_<x>(rom) > default_<x>_for(folder) > general
    // without ambiguity between "configured as empty string" and
    // "not configured at all".
    const char* default_bezel_for(std::string_view folder) const;
    const char* default_shader_for(std::string_view folder) const;

    // Returns the configured external standalone path for a system
    // folder, or "" if none is set. Caller is expected to stat() the
    // path before chain-launching.
    std::string external_core_for(std::string_view folder) const;
};

const Config& config();
void          reload_config();
void          save_config();

void          set_preferred_scraper(Config::Scraper s);
void          set_default_core_for(std::string_view folder,
                                   std::string_view core_name);
void          set_theme_name(std::string_view name);
void          set_theme_color(std::string_view color);

// UI language override. Empty string = follow Switch system language.
// Other values: ISO-style code parsed by foyer::i18n::map_switch_language()
// — e.g. "es", "pt-BR".
void          set_language(std::string_view code);

// UI theme override. "" / "auto" = follow HOS ColorSetId.
// "light" / "dark" = pinned variant. Persisted to /foyer/data/config/.
void          set_theme_override(std::string_view value);

// "" = auto. Other values: "us", "eu", "jp", "br", "wor".
void          set_region(std::string_view value);

void          set_update_check_on_boot(bool enabled);

void          set_scrub_extracted_enabled(bool enabled);
void          set_scrub_extracted_days(int days);
void          set_sort_mode(Config::SortMode mode);
void          set_system_sort_mode(Config::SystemSortMode mode);
void          set_system_custom_order(std::vector<std::string> order);
void          set_shader_name(std::string_view name);
void          set_runahead_frames(int frames);

// Per-system bezel + shader overrides. Empty value removes the
// entry (caller wants to revert to general default).
void          set_default_bezel_for(std::string_view folder,
                                    std::string_view bezel_name);
void          set_default_shader_for(std::string_view folder,
                                     std::string_view shader_name);
void          set_bool(std::string_view key, bool value);  // accepts the field names below

} // namespace foyer::library
