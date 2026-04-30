#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {

// Browser-wide preferences read from /foyer/config/general.jsonc on first
// access. A stub is created on missing-file so the user can edit via MTP.
struct Config {
    enum class Scraper { Libretro, ScreenScraper, SteamGridDB };

    Scraper      preferred_scraper = Scraper::Libretro;
    std::string  rom_root          = "/foyer/roms";
    std::string  theme_name        = "default";

    // Library + UI toggles (mirror the Tico-style Settings categories).
    bool         scan_subfolders   = true;
    bool         show_clock        = true;
    bool         show_backgrounds  = true;
    bool         show_covers       = true;

    // Experimental knobs — exposed under the Experimental category only.
    bool         mtp_autostart     = false;
    bool         debug_log         = false;

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
void          set_bool(std::string_view key, bool value);  // accepts the field names below

} // namespace foyer::library
