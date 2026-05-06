#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::scrapers::steamgriddb {

// Search SteamGridDB for `rom_stem` in the given platform context, then
// download the first matching grid art and write to `dest_png`. Requires
// foyer::scrapers::accounts().steamgriddb.ready().
bool fetch_cover(std::string_view system_folder,
                 std::string_view rom_stem,
                 const std::string& dest_png);

// Interactive cover-pick path: fetch up to `limit` candidate grids,
// download each to <dest_dir>/cand_NN.png, return the list of saved
// paths. Used by foyer's "Pick cover..." popup so the user sees
// thumbnails of each option and picks one.
std::vector<std::string> fetch_cover_candidates(
    std::string_view rom_stem,
    const std::string& dest_dir,
    std::size_t limit = 8);

} // namespace foyer::scrapers::steamgriddb
