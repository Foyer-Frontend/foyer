#include "scrape_job.hpp"

#include "system_db.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/libretro_thumbnails.hpp"
#include "scrapers/screenscraper.hpp"
#include "scrapers/steamgriddb.hpp"

#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

#include <switch.h>

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

bool fetch_one(ScrapeJob::Source src,
               const std::string& folder,
               const std::string& thumbs_db,
               const std::string& game_path,
               const std::string& game_stem,
               const std::string& dest_cover) {
    using S = ScrapeJob::Source;
    switch (src) {
        case S::Libretro: {
            const bool ok = scrapers::libretro_thumb::fetch_cover(
                thumbs_db, game_stem, dest_cover);
            scrapers::libretro_thumb::fetch_title(
                thumbs_db, game_stem,
                scrapers::title_path(folder, game_stem));
            scrapers::libretro_thumb::fetch_screenshot(
                thumbs_db, game_stem,
                scrapers::snap_path(folder, game_stem));
            return ok;
        }
        case S::ScreenScraper:
            return scrapers::screenscraper::fetch_cover(
                folder, game_path, game_stem, dest_cover);
        case S::SteamGridDB:
            return scrapers::steamgriddb::fetch_cover(
                folder, game_stem, dest_cover);
    }
    return false;
}

std::uint64_t throttle_ns(ScrapeJob::Source src) {
    // ScreenScraper anon tier caps ~1 req/s; SGDB is friendlier;
    // libretro-thumbnails is plain GitHub CDN with no rate cap.
    switch (src) {
        case ScrapeJob::Source::ScreenScraper: return 1'100'000'000ULL;
        case ScrapeJob::Source::SteamGridDB:   return   500'000'000ULL;
        case ScrapeJob::Source::Libretro:      return    50'000'000ULL;
    }
    return 0;
}

} // namespace

ScrapeStats run_system_scrape(const System& sys, ScrapeJob::Source src,
                              Worker& w) {
    ScrapeStats out;
    if (!sys.def) return out;

    struct GameRow { std::string path; std::string stem; };
    std::vector<GameRow> games;
    games.reserve(sys.games.size());
    for (const auto& g : sys.games) games.push_back({g.path, g.stem});

    const std::string folder     {sys.def->folder_name};
    const std::string short_name {sys.def->short_name};
    const std::string thumbs_db  {sys.def->thumbnails_db};
    const char* label = source_label(src);
    out.total = (int)games.size();

    for (const auto& g : games) {
        if (w.cancelled()) {
            w.set_status("Scrape cancelled");
            break;
        }
        out.done++;
        char banner[200];
        std::snprintf(banner, sizeof(banner),
            "Scraping %s [%s]  %d / %d",
            short_name.c_str(), label, out.done, out.total);
        w.set_status(banner);

        // Cache gate — skip games that already have a metadata.json
        // bundle (the only marker that survives across foyer
        // versions; old covers don't qualify).
        const auto bundle_meta =
            scrapers::game_asset_dir(folder, g.stem) + "metadata.json";
        struct stat st{};
        if (::stat(bundle_meta.c_str(), &st) == 0) continue;

        const auto dest = scrapers::cover_path(folder, g.stem);
        if (fetch_one(src, folder, thumbs_db, g.path, g.stem, dest)) {
            out.hits++;
        }
        svcSleepThread(throttle_ns(src));
    }

    char done[160];
    std::snprintf(done, sizeof(done),
        "Scrape %s done: %d hits / %d games",
        short_name.c_str(), out.hits, out.done);
    w.set_status(done);
    return out;
}

bool run_one_scrape(std::string_view system_folder,
                    std::string_view rom_path,
                    std::string_view rom_stem,
                    Worker& w) {
    char banner[160];
    std::snprintf(banner, sizeof(banner), "Rescraping %.*s…",
        (int)rom_stem.size(), rom_stem.data());
    w.set_status(banner);

    const std::string folder{system_folder};
    const std::string stem{rom_stem};

    // Drop the cache marker so the next fetch actually re-pulls.
    const auto bundle = scrapers::game_asset_dir(folder, stem);
    ::unlink((bundle + "metadata.json").c_str());
    ::unlink(scrapers::cover_path(folder, stem).c_str());

    const auto dest = scrapers::cover_path(folder, stem);
    const std::string path{rom_path};
    const bool ok = scrapers::screenscraper::fetch_cover(
        folder, path, stem, dest);

    std::snprintf(banner, sizeof(banner),
        ok ? "Rescraped %.*s" : "Rescrape failed for %.*s",
        (int)stem.size(), stem.c_str());
    w.set_status(banner);
    return ok;
}

} // namespace foyer::library
