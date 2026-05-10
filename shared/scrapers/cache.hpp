#pragma once

#include <string>
#include <string_view>

namespace foyer::scrapers {

// Local on-disk asset paths. Files are written by scrapers and read by the
// browser when it draws a system / game.
//
// Layout mirrors Tico's:
//   /foyer/assets/covers/<system>/<rom-stem>.png
//   /foyer/assets/backgrounds/<system>/<rom-stem>.jpg
//   /foyer/assets/systems/<system>.png

std::string cover_path(std::string_view system_folder,
                       std::string_view rom_stem);

// In-game screenshot from libretro Named_Snaps / equivalent.
std::string snap_path(std::string_view system_folder,
                      std::string_view rom_stem);

// Title-screen capture from libretro Named_Titles / equivalent.
std::string title_path(std::string_view system_folder,
                       std::string_view rom_stem);

std::string background_path(std::string_view system_folder,
                            std::string_view rom_stem);

std::string system_logo_path(std::string_view system_folder);

// Per-rom metadata sidecar (title, year, publisher, ...) — written by the
// scrapers and read by the browser to populate the System view sidebar.
//   /foyer/assets/metadata/<system>/<rom-stem>.json
std::string metadata_path(std::string_view system_folder,
                          std::string_view rom_stem);

// Per-game asset bundle directory. ScreenScraper-driven path layout
// where every media kind for a given rom lives next to the others —
// box-2D, sstitle, ss, fanart, bezel-16-9, video-normalized,
// metadata.json. Mirrors the structure the user already curates by
// hand, e.g.
//   /foyer/assets/system/nes/Super Mario Bros. (World)/box-2D(eu).png
std::string game_asset_dir(std::string_view system_folder,
                           std::string_view rom_stem);

// First file in `dir` whose name starts with `prefix`. Empty string
// when nothing matches. Used to resolve region-tagged media
// ("box-2D(us).png" / "box-2D(eu).png" / …) without needing to know
// which region the scraper actually saved.
std::string find_in_dir(std::string_view dir, std::string_view prefix);

// Ensure the parent dir of `path` exists.
void ensure_parent_dir(std::string_view path);

} // namespace foyer::scrapers
