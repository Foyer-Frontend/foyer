#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace foyer::scrapers::libretro_thumb {

// Returns the canonical filename stem (without .png extension) from
// the libretro-thumbnails per-system index that best matches `stem`,
// or empty string if nothing matches.
//
// Index is fetched once via the GitHub trees endpoint, cached at
// /foyer/cache/libretro_index/<db>_<category>.json with a 30-day
// TTL. Lookup is normalized (lower-case, region/translation tags
// stripped) so polluted dump filenames like
// "Ice Climber (USA, Europe, Korea) [!]" can hit the canonical
// "Ice Climber (USA, Europe)" entry.
//
// db       : libretro-thumbnails system slug, e.g. "Nintendo -
//            Nintendo Entertainment System". Same value foyer's
//            SystemDef::thumbnails_db carries.
// stem     : user's rom basename (without extension).
// category : "Named_Boxarts" / "Named_Snaps" / "Named_Titles".
std::string find_match(std::string_view db,
                       std::string_view stem,
                       const char* category);

} // namespace foyer::scrapers::libretro_thumb
