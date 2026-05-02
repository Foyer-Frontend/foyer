#pragma once

#include <string>
#include <string_view>

namespace foyer::library {

// Per-rom metadata sidecar — populated by the scrapers and consumed by the
// browser's System view sidebar. Empty fields mean "not yet scraped" or
// "scraper didn't return that field"; the UI renders only the non-empty
// ones.
struct GameMeta {
    std::string title;
    std::string year;        // 4-digit ascii, e.g. "1990"
    std::string publisher;
    std::string developer;
    std::string genre;
    std::string players;     // free-form, e.g. "1", "1-2", "4"
    std::string rating;      // free-form, e.g. "16+", "PEGI 7", "0.85"
    std::string description; // long synopsis — wraps in the sidebar

    // RetroAchievements progress, last seen by the player. -1 means
    // "unknown" — the user hasn't booted this rom with valid RA creds.
    int cheevos_total    = -1;
    int cheevos_unlocked = -1;
};

// Load metadata from /foyer/assets/metadata/<sys>/<stem>.json. Missing or
// invalid file yields an empty GameMeta (all fields blank).
GameMeta load_meta(std::string_view system_folder, std::string_view rom_stem);

// Persist metadata. Creates parent dirs and overwrites any existing file.
// Returns true on success.
bool save_meta(std::string_view system_folder,
               std::string_view rom_stem,
               const GameMeta& meta);

// Whether the sidecar file exists. Used by scrapers to decide whether to
// hit the network at all.
bool meta_exists(std::string_view system_folder, std::string_view rom_stem);

} // namespace foyer::library
