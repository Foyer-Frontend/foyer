#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {

// Browser-wide preferences read from /foyer/config/general.jsonc on first
// access. A stub is created on missing-file so the user can edit via MTP.
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
    std::string  theme_name        = "default";
    SortMode     sort_mode         = SortMode::Name;
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

    // Experimental knobs — exposed under the Experimental category only.
    bool         mtp_autostart     = false;
    bool         debug_log         = false;

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

    // Per-system core override. Stored flat to keep the header light; the
    // list is short (≤20 systems) so linear lookup is fine.
    struct PerSystemCore { std::string folder; std::string core; };
    std::vector<PerSystemCore> default_core_per_system;

    // Returns the user-set default core name for a system folder, or
    // nullptr if none is configured.
    const char* default_core_for(std::string_view folder) const;
};

const Config& config();
void          reload_config();
void          save_config();

void          set_preferred_scraper(Config::Scraper s);
void          set_default_core_for(std::string_view folder,
                                   std::string_view core_name);
void          set_theme_name(std::string_view name);
void          set_sort_mode(Config::SortMode mode);
void          set_shader_name(std::string_view name);
void          set_bool(std::string_view key, bool value);  // accepts the field names below

} // namespace foyer::library
