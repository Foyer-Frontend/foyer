#include "scrape_job.hpp"

#include "system_db.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/libretro_thumbnails.hpp"
#include "scrapers/screenscraper.hpp"
#include "scrapers/steamgriddb.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace foyer::library {
namespace {

const char* source_label(ScrapeJob::Source s) {
    switch (s) {
        case ScrapeJob::Source::ScreenScraper: return "ScreenScraper";
        case ScrapeJob::Source::SteamGridDB:   return "SteamGridDB";
        case ScrapeJob::Source::Libretro:      return "libretro";
    }
    return "?";
}

} // namespace

bool ScrapeJob::start(const System& sys, Source src) {
    if (!sys.def) return false;

    // Snapshot the system fields the worker needs. Library may be
    // rescanned mid-job; the worker keeps the older snapshot.
    struct GameRow { std::string path; std::string stem; };
    std::vector<GameRow> games;
    games.reserve(sys.games.size());
    for (const auto& g : sys.games) games.push_back({g.path, g.stem});

    std::string folder      = std::string{sys.def->folder_name};
    std::string short_name  = std::string{sys.def->short_name};
    std::string thumbs_db   = std::string{sys.def->thumbnails_db};

    m_total   = (int)games.size();
    m_done_ct = 0;
    m_hits    = 0;

    // 1 MB stack — the SS jeuInfos response is multi-KB JSON
    // walked recursively by yyjson + we hold libcurl's TLS state
    // simultaneously. The default 256 KB worker stack overflows
    // mid-scrape on bigger systems (50+ games), and a stack-fault
    // looks identical to a generic crash with no last-line log.
    constexpr std::size_t kScrapeStack = 0x100000;
    return m_worker.start(
        [this, games = std::move(games),
               folder, short_name, thumbs_db, src](Worker& w) {
            const char* label = source_label(src);
            for (const auto& g : games) {
                if (w.cancelled()) {
                    w.set_status("Scrape cancelled");
                    break;
                }
                m_done_ct++;
                char banner[200];
                std::snprintf(banner, sizeof(banner),
                    "Scraping %s [%s]  %d / %d",
                    short_name.c_str(), label, m_done_ct, m_total);
                w.set_status(banner);

                const auto dest = scrapers::cover_path(folder, g.stem);
                // Cache marker is the per-game metadata.json now,
                // not the legacy cover. Old foyer versions only
                // wrote box-2D into /foyer/assets/covers; users
                // upgrading would have those files but no bundle
                // companions (bezel/fanart/sstitle/ss/video/meta),
                // so we re-fetch until the bundle's metadata.json
                // shows up — that's only written once a successful
                // SS lookup completes.
                const auto bundle_meta =
                    scrapers::game_asset_dir(folder, g.stem) + "metadata.json";
                struct stat st{};
                if (::stat(bundle_meta.c_str(), &st) == 0) continue; // cached

                bool ok = false;
                switch (src) {
                    case Source::Libretro:
                        // Box art is the primary; title + snap are
                        // best-effort secondary downloads. Only the
                        // box art counts toward the hit total since
                        // it's what the System view tile renders;
                        // the others land alongside for future use.
                        ok = scrapers::libretro_thumb::fetch_cover(
                            thumbs_db, g.stem, dest);
                        scrapers::libretro_thumb::fetch_title(
                            thumbs_db, g.stem,
                            scrapers::title_path(folder, g.stem));
                        scrapers::libretro_thumb::fetch_screenshot(
                            thumbs_db, g.stem,
                            scrapers::snap_path(folder, g.stem));
                        break;
                    case Source::ScreenScraper:
                        ok = scrapers::screenscraper::fetch_cover(
                            folder, g.path, g.stem, dest);
                        break;
                    case Source::SteamGridDB:
                        ok = scrapers::steamgriddb::fetch_cover(
                            folder, g.stem, dest);
                        break;
                }
                if (ok) m_hits++;
                // Throttle between games. ScreenScraper's anonymous
                // tier (the kFallbackDevid/password EmuDeck pair we
                // ship until foyer's own creds land) caps at ~1
                // req/s — the previous 50 ms gap meant a 22-game
                // batch hit the rate-limit on game #3 and every
                // subsequent jeuInfos call came back empty, so the
                // whole batch reported zero hits. 1100 ms keeps us
                // just below the SS bucket refill, and per-source
                // overrides cover scrapers that don't rate-limit.
                std::uint64_t gap_ns;
                switch (src) {
                    case Source::ScreenScraper:
                        gap_ns = 1'100'000'000ULL;
                        break;
                    case Source::SteamGridDB:
                        gap_ns =   500'000'000ULL;
                        break;
                    case Source::Libretro:
                    default:
                        gap_ns =    50'000'000ULL;
                        break;
                }
                svcSleepThread(gap_ns);
            }
            char done[160];
            std::snprintf(done, sizeof(done),
                "Scrape %s done: %d hits / %d games",
                short_name.c_str(), m_hits, m_done_ct);
            w.set_status(done);
        }, kScrapeStack);
}

} // namespace foyer::library
