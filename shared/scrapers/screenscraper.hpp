#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace foyer::scrapers::screenscraper {

// ScreenScraper system IDs — needed by jeuInfos.php to disambiguate when a
// CRC matches multiple platforms.
//   https://www.screenscraper.fr/api2/systemesListe.php
int system_id_for_folder(std::string_view folder_name);

// Compute the CRC32 of the rom file at `rom_path` (zlib polynomial). Used
// as the lookup key against ScreenScraper.
std::uint32_t crc32_file(const std::string& rom_path);

// Fetch box art for a single rom and write it to `dest_png`. Requires that
// foyer::scrapers::accounts().screenscraper.ready() == true. Returns true
// on success.
bool fetch_cover(std::string_view system_folder,
                 const std::string& rom_path,
                 std::string_view rom_stem,
                 const std::string& dest_png);

} // namespace foyer::scrapers::screenscraper
