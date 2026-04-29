#pragma once

#include <string>
#include <string_view>

namespace foyer::scrapers::libretro_thumb {

// Fetches a box art PNG for the given rom stem from the public
// libretro-thumbnails repo and writes it to `dest_png`. Returns true on
// success. No auth required.
//
// `thumbnails_db` is the libretro-thumbnails system folder (matches the
// `thumbnails_db` field in foyer's SystemDef).
bool fetch_cover(std::string_view thumbnails_db,
                 std::string_view rom_stem,
                 const std::string& dest_png);

// Same as cover_path() but for "Named_Snaps" (in-game screenshot).
bool fetch_screenshot(std::string_view thumbnails_db,
                      std::string_view rom_stem,
                      const std::string& dest_png);

} // namespace foyer::scrapers::libretro_thumb
