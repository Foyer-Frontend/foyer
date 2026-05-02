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

std::string background_path(std::string_view system_folder,
                            std::string_view rom_stem);

std::string system_logo_path(std::string_view system_folder);

// Per-rom metadata sidecar (title, year, publisher, ...) — written by the
// scrapers and read by the browser to populate the System view sidebar.
//   /foyer/assets/metadata/<system>/<rom-stem>.json
std::string metadata_path(std::string_view system_folder,
                          std::string_view rom_stem);

// Ensure the parent dir of `path` exists.
void ensure_parent_dir(std::string_view path);

} // namespace foyer::scrapers
