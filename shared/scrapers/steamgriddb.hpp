#pragma once

#include <string>
#include <string_view>

namespace foyer::scrapers::steamgriddb {

// Search SteamGridDB for `rom_stem` in the given platform context, then
// download the first matching grid art and write to `dest_png`. Requires
// foyer::scrapers::accounts().steamgriddb.ready().
bool fetch_cover(std::string_view system_folder,
                 std::string_view rom_stem,
                 const std::string& dest_png);

} // namespace foyer::scrapers::steamgriddb
