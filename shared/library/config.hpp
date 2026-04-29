#pragma once

#include <string>

namespace foyer::library {

// Browser-wide preferences read from /foyer/config/general.jsonc on first
// access. A stub is created on missing-file so the user can edit via MTP.
struct Config {
    enum class Scraper { Libretro, ScreenScraper, SteamGridDB };

    Scraper      preferred_scraper = Scraper::Libretro;
    std::string  rom_root          = "/foyer/roms";
};

const Config& config();
void          reload_config();

// Persist the current Config back to disk. Called by Settings UI when the
// user changes a value.
void          save_config();

// Mutators (used by the settings UI; persists immediately).
void          set_preferred_scraper(Config::Scraper s);

} // namespace foyer::library
