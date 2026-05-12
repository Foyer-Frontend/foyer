#pragma once

#include "scanner.hpp"
#include "worker.hpp"

#include <string>
#include <string_view>

namespace foyer::library {

// Source enumeration kept under this name for back-compat with existing
// callers (system_activity / game_activity). The class wrapper around an
// owned Worker is gone — every scrape now flows through
// foyer::browser::install_queue, and run_system_scrape() is the inline
// body queued workers call.
namespace ScrapeJob {
    enum class Source { Libretro, ScreenScraper, SteamGridDB };
}

// Walks every game in `sys`, fetches cover art (+ title / snap for the
// libretro source), and reports progress through `w.set_status`. Honors
// `w.cancelled()` at each game boundary so a queued scrape can be
// aborted via install_queue::stop. Caller should construct sys.def +
// sys.games before calling.
//
// Returned struct exposes per-batch counters for callers that want to
// surface them in a completion toast. Counters are valid only after the
// function returns; mid-flight progress lives in the status string
// (parseable as "Scraping <short> [<src>]  N / M").
struct ScrapeStats { int total = 0; int done = 0; int hits = 0; };
ScrapeStats run_system_scrape(const System& sys, ScrapeJob::Source src,
                              Worker& w);

// Single-game scrape — used by game_activity's Y rescrape. Drops the
// bundle's metadata.json + legacy cover BEFORE the lookup so the cache
// gate in run_system_scrape's loop doesn't short-circuit.
bool run_one_scrape(std::string_view system_folder,
                    std::string_view rom_path,
                    std::string_view rom_stem,
                    Worker& w);

} // namespace foyer::library
