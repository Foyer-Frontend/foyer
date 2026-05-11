#include "activity/system_activity.hpp"

#include "activity/game_activity.hpp"
#include "launch.hpp"
#include "activity/per_game_activity.hpp"
#include "activity/per_system_activity.hpp"
#include "library_state.hpp"
#include "library/per_game.hpp"
#include "library/config.hpp"
#include "library/scrape_job.hpp"
#include "library/system_db.hpp"
#include "scrapers/cache.hpp"
#include "platform/log.hpp"
#include "widgets/action_button.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Map a SystemDef folder name to the on-disk art directory name
// under themes/foyer/systems/. Real systems use their folder
// name verbatim; virtuals route to the "auto-*" assets the
// theme packs ship for those.
std::string art_dir_for(std::string_view folder) {
    if (folder == "__recent")    return "auto-lastplayed";
    if (folder == "__favorites") return "auto-favorites";
    if (folder == "__allgames")  return "auto-allgames";
    return std::string{folder};
}

// Long-lived scrape job. Survives the activity popping so the
// download keeps running while the user is back on Home.
// unique_ptr so we can replace it cleanly on subsequent runs.
std::unique_ptr<::foyer::library::ScrapeJob> g_scrape_job;

::foyer::library::ScrapeJob::Source preferred_scrape_source() {
    using S = ::foyer::library::Config::Scraper;
    using J = ::foyer::library::ScrapeJob::Source;
    switch (::foyer::library::config().preferred_scraper) {
        case S::ScreenScraper: return J::ScreenScraper;
        case S::SteamGridDB:   return J::SteamGridDB;
        case S::Libretro:
        default:               return J::Libretro;
    }
}

// Polls g_scrape_job and pushes counter text into
// SystemActivity's scrape_status label. Stops automatically when
// the job is no longer active.
class ScrapeStatusTask : public brls::RepeatingTask {
public:
    ScrapeStatusTask(SystemActivity* host)
        : brls::RepeatingTask(500), m_host(host) {}
    void run() override {
        if (m_host) m_host->refreshScrapeStatus();
    }
private:
    SystemActivity* m_host;
};

// Clock task — same shape as HomeActivity's. Updates the
// hh:mm label every second.
class ClockTask : public brls::RepeatingTask {
public:
    ClockTask(brls::Label* label) : brls::RepeatingTask(1000), label(label) {}
    void run() override {
        if (!label) return;
        const auto now = std::chrono::system_clock::now();
        const auto t   = std::chrono::system_clock::to_time_t(now);
        std::tm tm     = *std::localtime(&t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%H:%M");
        label->setText(ss.str());
    }
private:
    brls::Label* label;
};

// Just an int-pair for the box-size table below.
struct ImgDims { int w; int h; };

// ScreenScraper's box-2D scan resolution per platform. User-
// confirmed values. The carousel uses these to size every tile
// in a system identically — probe-based detection got tripped up
// by per-game odd crops. Folders not listed here fall back to
// 484×680 portrait, which is the modal SS scan size.
struct SsBoxSize { std::string_view folder; int w; int h; };
constexpr SsBoxSize kBoxSizes[] = {
    {"nes",             497, 680},  {"snes",            680, 497},
    {"n64",             680, 498},  {"genesis",         484, 680},
    {"megadrive",       484, 680},  {"gb",              700, 700},
    {"gbc",             700, 700},  {"gba",             705, 700},
    {"virtualboy",      513, 458},  {"gc",              486, 680},
    {"nds",             514, 458},  {"wii",             486, 680},
    {"3ds",             514, 458},  {"wiiu",            486, 680},
    {"ps2",             486, 680},  {"amstradcpc",      621, 800},
    {"c64",             536, 680},  {"atari2600",       498, 680},
    {"atari5200",       498, 680},  {"atari7800",       498, 680},
    {"atari800",        498, 680},  {"atarilynx",       554, 680},
    {"atarijaguar",     724,1000},  {"mastersystem",    484, 680},
    {"32x",             484, 680},  {"segacd",          481, 680},
    {"gamegear",        496, 680},  {"saturn",          441, 680},
    {"dc",              680, 680},  {"psp",             291, 500},
    {"psx",             792, 680},  {"atarist",         552, 680},
    {"amiga",           555, 680},  {"amiga600",        555, 680},
    {"amiga1200",       555, 680},  {"amigacd32",       945, 945},
    {"cdtv",            722, 922},  {"3do",             370, 700},
    {"intellivision",   498, 680},  {"gameandwatch",    930, 954},
    {"wonderswan",      495, 680},  {"wonderswancolor", 498, 720},
    {"pcengine",        580, 680},  {"pcenginecd",      595, 600},
    {"supergrafx",      513, 458},  {"pcfx",            462, 607},
    {"ngp",             599, 700},  {"ngpc",            599, 700},
    {"msx",             484, 680},  {"msx2",            484, 680},
    {"dos",             582, 680},  {"pokemini",        700, 700},
};

ImgDims box_dims_for_system(std::string_view folder) {
    for (const auto& s : kBoxSizes) {
        if (s.folder == folder) return ImgDims{s.w, s.h};
    }
    return ImgDims{484, 680};   // safe portrait fallback
}

// Cover-flow tile for one game.
//
// Loading 200+ scraped PNGs synchronously at activity-push time
// freezes the UI for seconds. Mitigation: each tile starts as the
// system splash; covers stream in via a sliding 10-tile window
// driven by SystemActivity::onTileFocused. The first 10 load on
// entry; navigating into the right edge of the window triggers
// the next 10 batch. m_idx + m_host let the tile call back to
// the activity on focus events.
class GameTile : public brls::Box {
public:
    GameTile(SystemActivity* host, int idx,
             std::string_view system_folder, std::string_view game_path,
             std::string_view game_stem, std::string_view box_art_path,
             float tile_w, float tile_h)
        : m_host(host), m_idx(idx),
          m_system(system_folder), m_path(game_path)
    {
        m_tile_w = tile_w;
        m_tile_h = tile_h;
        this->setMargins(0.0f, 7.0f, 0.0f, 7.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));
        this->setWidth(m_tile_w);
        this->setHeight(m_tile_h);

        // Inner Image absolute-positioned at the bottom edge.
        // brls's FIT scaling always centers vertically (the
        // setImageAlign field is ignored on FIT) — we size the
        // Image manually to its native aspect and anchor it bottom
        // so the cover bottom matches the tile bottom regardless
        // of the cover's height-vs-tile-height delta.
        m_img = new brls::Image();
        m_img->setPositionType(brls::PositionType::ABSOLUTE);
        m_img->setScalingType(brls::ImageScalingType::STRETCH);
        m_img->setWidth(m_tile_w);
        m_img->setHeight(m_tile_h);
        m_img->setPositionLeft(0.0f);
        m_img->setPositionBottom(0.0f);
        this->addView(m_img);

        // Always start with the splash so first-frame paint is
        // instant. The splash file is 49 unique PNGs already in
        // the romfs cache — brls dedupes by path.
        m_img->setImageFromRes(
            "themes/foyer/systems/" + art_dir_for(system_folder)
            + "/splash.png");
        fit_to_bottom();

        // Resolve the cover candidate path. Probe in this order:
        //   1) library_state's box_art (set by an old scan).
        //   2) new per-game asset bundle: any file starting with
        //      "box-2D" — region tag is appended by the scraper
        //      based on what SS actually returned.
        //   3) legacy /foyer/assets/covers/<sys>/<stem>.png.
        // Don't load yet — onFocusGained() does that lazily on
        // first focus.
        std::string cover = std::string(box_art_path);
        if (cover.empty()
            || !std::filesystem::exists(std::filesystem::path(cover))) {
            const auto bundle_dir = scrapers::game_asset_dir(
                system_folder, game_stem);
            const auto bundle_box = scrapers::find_in_dir(
                bundle_dir, "box-2D");
            if (!bundle_box.empty()) {
                cover = bundle_box;
            } else {
                const auto canon = scrapers::cover_path(
                    system_folder, game_stem);
                if (std::filesystem::exists(std::filesystem::path(canon))) {
                    cover = canon;
                } else {
                    cover.clear();
                }
            }
        }
        m_cover_path = std::move(cover);

        // A — launch the rom directly (chain-launch into the
        // player nro). registerAction (not registerClickAction)
        // so brls's hint bar shows "Launch" instead of the
        // default "OK" label. B / Y / X / + below are tile-
        // scoped so they fire only when this tile has focus.
        this->registerAction(
            "Launch", brls::BUTTON_A,
            [this](brls::View*) {
                launch_focused_game();
                return true;
            }, false, false, brls::SOUND_CLICK);
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

        // Y — open the Game details view (cover, screenshots,
        // metadata).
        this->registerAction(
            "Details", brls::BUTTON_Y,
            [this](brls::View*) {
                brls::Application::pushActivity(
                    new GameActivity(m_system, m_path));
                return true;
            }, false, false, brls::SOUND_CLICK);

        // X — toggle favourite. per_game.jsonc persists across
        // launches; scanner reapplies on next rescan.
        this->registerAction(
            "Favourite", brls::BUTTON_X,
            [this](brls::View*) {
                const bool was = ::foyer::library::per_game_favorite(m_path);
                ::foyer::library::set_per_game_favorite(m_path, !was);
                brls::Application::notify(was
                    ? "Removed from favourites"
                    : "Added to favourites");
                return true;
            }, false, false, brls::SOUND_CLICK);

        // + — per-game settings override.
        this->registerAction(
            "Settings", brls::BUTTON_START,
            [this](brls::View*) {
                brls::Application::pushActivity(
                    new PerGameActivity(m_system, m_path),
                    brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_CLICK);
    }

    // Idempotent — second call is a no-op. Public so the
    // sliding-window preload in SystemActivity can warm a batch of
    // tiles ahead of focus.
    void load_cover() {
        if (m_cover_loaded || m_cover_path.empty() || !m_img) return;
        m_img->setImageFromFile(m_cover_path);
        m_cover_loaded = true;
        fit_to_bottom();
    }

    // Tile size is fixed per system; only the inner Image
    // changes shape to match the cover's native aspect, then
    // anchors at the tile's bottom edge. Empty space (when the
    // image's aspect doesn't fill the tile) sits at the top —
    // tile bg fills it.
    void fit_to_bottom() {
        if (!m_img) return;
        const float ow = m_img->getOriginalImageWidth();
        const float oh = m_img->getOriginalImageHeight();
        if (ow <= 0.0f || oh <= 0.0f) return;
        // Scale to tile width first; if that overflows the tile
        // height, scale by tile height instead.
        float w = m_tile_w;
        float h = w * (oh / ow);
        if (h > m_tile_h) {
            h = m_tile_h;
            w = h * (ow / oh);
        }
        m_img->setWidth(w);
        m_img->setHeight(h);
        m_img->setPositionLeft((m_tile_w - w) * 0.5f);
        m_img->setPositionBottom(0.0f);
    }

    int  index() const { return m_idx; }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        load_cover();
        if (m_host) m_host->onTileFocused(m_idx);
    }

    void launch_focused_game() {
        const auto* sys = library_state::find_system(m_system);
        if (!sys) {
            brls::Application::notify("System not in library — rescan?");
            return;
        }
        for (const auto& g : sys->games) {
            if (g.path != m_path) continue;
            if (launch_game(*sys, g)) {
                brls::Application::quit();
            } else {
                brls::Application::notify(
                    "Player nro missing — install the core from Settings");
            }
            return;
        }
    }

private:
    SystemActivity* m_host = nullptr;
    int             m_idx  = 0;
    std::string     m_system;
    std::string     m_path;
    std::string     m_cover_path;
    brls::Image*    m_img = nullptr;
    bool            m_cover_loaded = false;
    float           m_tile_w = 0.0f;
    float           m_tile_h = 0.0f;
};

}  // namespace

void SystemActivity::cancel_pending_scrape() {
    if (!g_scrape_job) return;
    if (g_scrape_job->active() && !g_scrape_job->done()) {
        foyer::log::write(
            "[system] signalling scrape cancel on quit (no join)\n");
        g_scrape_job->cancel();
        // Intentionally do NOT call finish() / unique_ptr reset
        // here. finish() blocks on threadWaitForExit, and a
        // worker mid-curl_easy_perform can take many seconds to
        // honour the cancel — the deko3d watchdog or HOS sees
        // the UI thread stalled and kills the process. Leak the
        // unique_ptr; the OS reaps the thread + sockets when
        // the process exits a moment later.
        (void)g_scrape_job.release();
    } else {
        // Worker already done — finish() is a fast threadClose,
        // safe to call inline.
        g_scrape_job->finish();
        g_scrape_job.reset();
    }
}

SystemActivity::SystemActivity(std::string_view folder,
                               std::string_view display_name)
    : m_folder(folder)
    , m_display_name(display_name)
{
    foyer::log::write("[system] enter %s\n", m_folder.c_str());
}

SystemActivity::~SystemActivity() {
    // Critical: stop + delete per-instance RepeatingTasks before
    // the bound Labels are destroyed. brls::TaskManager keeps
    // repeating tasks alive on its own queue; without explicit
    // stop the task fires once more after this Activity (and its
    // Labels) are freed and segfaults inside run().
    foyer::log::write("[system] leave %s\n", m_folder.c_str());
    if (m_clockTask) {
        m_clockTask->stop();
        delete m_clockTask;
        m_clockTask = nullptr;
    }
    if (m_scrapeStatusTask) {
        m_scrapeStatusTask->stop();
        delete m_scrapeStatusTask;
        m_scrapeStatusTask = nullptr;
    }
}

void SystemActivity::refreshScrapeStatus() {
    if (!scrapeStatus) return;
    if (!g_scrape_job || !g_scrape_job->active()) {
        // Job ended — clear label, drop task. finish() reclaims
        // the worker thread handle.
        if (g_scrape_job && g_scrape_job->done()) {
            const int hits = g_scrape_job->hits();
            const int total = g_scrape_job->total();
            g_scrape_job->finish();
            scrapeStatus->setText("");
            brls::Application::notify(
                "Scrape done — " + std::to_string(hits)
                + "/" + std::to_string(total) + " hits");
        } else {
            scrapeStatus->setText("");
        }
        if (m_scrapeStatusTask) {
            m_scrapeStatusTask->stop();
            delete m_scrapeStatusTask;
            m_scrapeStatusTask = nullptr;
        }
        return;
    }
    const int done  = g_scrape_job->done_ct();
    const int total = g_scrape_job->total();
    const int hits  = g_scrape_job->hits();
    std::stringstream ss;
    ss << "Scraping " << done << "/" << total
       << " (" << hits << " hits)";
    scrapeStatus->setText(ss.str());
}

void SystemActivity::onContentAvailable() {
    foyer::log::write("[system] content available %s\n", m_folder.c_str());

    // Backdrop — same per-system art as Home shows on tile focus.
    if (backdrop) {
        backdrop->setImageFromRes(
            "themes/foyer/systems/" + art_dir_for(m_folder) + "/background.jpg");
    }

    if (clock && !m_clockTask) {
        m_clockTask = new ClockTask(this->clock);
        m_clockTask->start();
        m_clockTask->run();
    }

    buildLogo();
    buildActionRow();
    populateCarousel();

    // If the user started a scrape from this same system, popped
    // back to Home, then re-entered, the job is still running on
    // its background thread. Resume showing progress on the new
    // activity instance.
    if (g_scrape_job && g_scrape_job->active() && !m_scrapeStatusTask) {
        m_scrapeStatusTask = new ScrapeStatusTask(this);
        m_scrapeStatusTask->start();
    }

    // Default focus on the first cover tile (matches Home's
    // first-system-tile default focus). Falls back to action row
    // when the system has no scanned games.
    if (carousel && !carousel->getChildren().empty()) {
        brls::Application::giveFocus(carousel->getChildren()[0]);
    } else if (actionRow && !actionRow->getChildren().empty()) {
        brls::Application::giveFocus(actionRow->getChildren()[0]);
    }

    // B button pops back to Home, mirroring the old AppletFrame's
    // built-in back hint.
    this->getContentView()->registerAction(
        "hints/back"_i18n, brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    // L / R: jump 10 tiles in the cover-flow carousel. brls's
    // default carousel navigation is one-tile-at-a-time on the
    // d-pad / left stick; the shoulder buttons skim faster
    // through long systems (250+ NES/SNES games).
    auto jump_focus = [this](int delta) {
        if (!carousel) return false;
        const auto& kids = carousel->getChildren();
        if (kids.empty()) return false;
        // Find the currently focused tile by walking the children
        // and matching against brls::Application::getCurrentFocus.
        auto* focus = brls::Application::getCurrentFocus();
        int cur = -1;
        for (int i = 0; i < (int)kids.size(); i++) {
            if (kids[i] == focus) { cur = i; break; }
        }
        if (cur < 0) cur = 0;
        const int n = (int)kids.size();
        // Wrap-around: pressing L on tile 0 jumps to the last tile;
        // pressing R on the last tile jumps to 0. Modulo handles
        // both directions cleanly.
        int next = ((cur + delta) % n + n) % n;

        // Preload covers along the path between cur and next so the
        // tile at the new focus already has its texture decoded by
        // the time the carousel scrolls there. Without this the
        // first-focus load_cover call inside onFocusGained stalls
        // a frame.
        const int lo = std::min(cur, next);
        const int hi = std::max(cur, next);
        for (int i = lo; i <= hi; i++) {
            if (auto* t = dynamic_cast<GameTile*>(kids[i])) {
                t->load_cover();
            }
        }
        if (next > m_loaded_until) m_loaded_until = next + 1;

        brls::Application::giveFocus(kids[next]);
        return true;
    };
    // Page jump = number of tiles the viewport currently fits.
    // tile pitch = 250px tile + 14px margins (7 each side). We
    // measure the live HScrollingFrame width so the jump scales
    // with dock vs handheld display modes.
    auto page_size = [this]() {
        constexpr float kTilePitch = 264.0f;
        const float vp = carouselScroll ? carouselScroll->getWidth() : 1280.0f;
        int n = (int)(vp / kTilePitch);
        if (n < 1) n = 1;
        return n;
    };
    this->getContentView()->registerAction(
        "Prev page", brls::BUTTON_LB,
        [jump_focus, page_size](brls::View*) {
            return jump_focus(-page_size());
        },
        false, true, brls::SOUND_FOCUS_CHANGE);
    this->getContentView()->registerAction(
        "Next page", brls::BUTTON_RB,
        [jump_focus, page_size](brls::View*) {
            return jump_focus(+page_size());
        },
        false, true, brls::SOUND_FOCUS_CHANGE);
}

void SystemActivity::buildLogo() {
    if (!logoHolder) return;

    // Sized to fit the 80px top bar with breathing room above /
    // below. FIT scaling preserves aspect, so wide logos stay
    // proportional and the holder gives them up to 240px of
    // horizontal room.
    constexpr float kLogoW = 240.0f;
    constexpr float kLogoH = 56.0f;

    // Pre-rendered variants live alongside splash + background;
    // pick the one whose foreground colour reads against the
    // current theme's background. (Runtime tinting via
    // nvgImagePattern looked muddy on hardware — the texture
    // multiplication doesn't preserve antialiased edges cleanly,
    // so the user requested separate dark / light PNGs.)
    const bool dark =
        brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
    const std::string variant = dark ? "logo_dark.png" : "logo_light.png";

    auto* logo = new brls::Image();
    logo->setWidth(kLogoW);
    logo->setHeight(kLogoH);
    logo->setScalingType(brls::ImageScalingType::FIT);
    logoHolder->addView(logo);
    logo->setImageFromRes(
        "themes/foyer/systems/" + art_dir_for(m_folder) + "/" + variant);
}

void SystemActivity::buildActionRow() {
    if (!actionRow) return;

    auto coming_soon = [](const std::string& msg) {
        return [msg](brls::View*) {
            auto* dlg = new brls::Dialog(msg);
            dlg->addButton("hints/ok"_i18n, []() {});
            dlg->open();
            return true;
        };
    };

    actionRow->addView(new ActionButton(
        "img/actions/gallery.png", "Scrape",
        [this](brls::View*) {
            const auto* sys = library_state::find_system(m_folder);
            if (!sys || sys->games.empty()) {
                brls::Application::notify("Nothing to scrape — no games scanned");
                return true;
            }
            if (g_scrape_job) {
                if (g_scrape_job->active() && !g_scrape_job->done()) {
                    brls::Application::notify("Scrape already running");
                    return true;
                }
                // Job finished but the polling task was torn down
                // before it could finish() the worker (user popped
                // back to Home or never opened the system view
                // again). Reclaim the thread handle and start
                // fresh.
                if (g_scrape_job->done()) g_scrape_job->finish();
                g_scrape_job.reset();
            }
            g_scrape_job = std::make_unique<::foyer::library::ScrapeJob>();
            const auto src = preferred_scrape_source();
            if (g_scrape_job->start(*sys, src)) {
                foyer::log::write(
                    "[system] kicked scrape for %s (%zu games)\n",
                    m_folder.c_str(), sys->games.size());
                brls::Application::notify(
                    "Scraping " + std::to_string(sys->games.size()) + " games…");
                if (!m_scrapeStatusTask) {
                    m_scrapeStatusTask = new ScrapeStatusTask(this);
                    m_scrapeStatusTask->start();
                }
            } else {
                foyer::log::write(
                    "[system] scrape job failed to start for %s\n",
                    m_folder.c_str());
                brls::Application::notify("Scrape failed to start");
                g_scrape_job.reset();
            }
            return true;
        }));

    actionRow->addView(new ActionButton(
        "img/actions/search.png", "Scan",
        [](brls::View*) {
            library_state::rescan();
            brls::Application::notify("Library rescanned");
            return true;
        }));

    actionRow->addView(new ActionButton(
        "img/actions/search.png", "Search",
        coming_soon("Per-system search — coming soon.")));

    actionRow->addView(new ActionButton(
        "img/actions/settings.png", "Settings",
        [this](brls::View*) {
            brls::Application::pushActivity(
                new PerSystemActivity(m_folder, m_display_name),
                brls::TransitionAnimation::NONE);
            return true;
        }));
}

void SystemActivity::populateCarousel() {
    if (!carousel) return;

    const auto* sys = library_state::find_system(m_folder);
    if (!sys) {
        foyer::log::write("[system] %s — system not found in scan\n",
            m_folder.c_str());
        return;
    }

    // Use ScreenScraper's per-system canonical box-2D scan size
    // to drive tile aspect. SS returns every cover for a given
    // platform at the same resolution; pulling that from the
    // hardcoded kBoxSizes table is more reliable than probing
    // the on-disk files (which can include hand-edited crops).
    constexpr float kMaxSide = 200.0f;

    // For real systems every game shares the same box dims (the
    // system's SS box-2D ratio). For the virtual carousels
    // (Favourites / Recent / All Games) games are mixed across
    // systems, so each tile gets its own dims derived from its
    // rom path's parent folder.
    const bool is_virtual = m_folder.rfind("__", 0) == 0;
    auto system_of_game = [](const std::string& rom_path) {
        const auto sl = rom_path.find_last_of('/');
        if (sl == std::string::npos) return std::string{};
        const auto upper = rom_path.substr(0, sl);
        const auto up_sl = upper.find_last_of('/');
        return up_sl == std::string::npos
            ? upper : upper.substr(up_sl + 1);
    };

    int idx = 0;
    if (!is_virtual) {
        const auto dims = box_dims_for_system(m_folder);
        const float scale = std::min(kMaxSide / dims.w, kMaxSide / dims.h);
        const float tile_w = dims.w * scale;
        const float tile_h = dims.h * scale;
        foyer::log::write("[system] %s tile size = %.0fx%.0f\n",
            m_folder.c_str(), tile_w, tile_h);
        for (const auto& g : sys->games) {
            carousel->addView(
                new GameTile(this, idx++, m_folder, g.path, g.stem,
                             g.box_art, tile_w, tile_h));
        }
    } else {
        for (const auto& g : sys->games) {
            const auto game_sys = system_of_game(g.path);
            const auto dims = box_dims_for_system(game_sys);
            const float scale =
                std::min(kMaxSide / dims.w, kMaxSide / dims.h);
            const float tile_w = dims.w * scale;
            const float tile_h = dims.h * scale;
            carousel->addView(
                new GameTile(this, idx++, game_sys, g.path, g.stem,
                             g.box_art, tile_w, tile_h));
        }
    }

    // Warm the first batch so the user sees real covers as soon
    // as the activity paints. Subsequent batches stream in via
    // onTileFocused as focus approaches the loaded edge.
    constexpr int kInitialBatch = 10;
    const int n = (int)carousel->getChildren().size();
    for (int i = 0; i < std::min(n, kInitialBatch); i++) {
        if (auto* t = dynamic_cast<GameTile*>(carousel->getChildren()[i])) {
            t->load_cover();
        }
    }
    m_loaded_until = std::min(n, kInitialBatch);
}

void SystemActivity::onTileFocused(int idx) {
    if (!carousel) return;
    constexpr int kThreshold = 5;   // start preloading 5 tiles ahead of edge
    constexpr int kBatch     = 10;
    const int n = (int)carousel->getChildren().size();
    if (idx + kThreshold >= m_loaded_until && m_loaded_until < n) {
        const int next_until = std::min(n, m_loaded_until + kBatch);
        for (int i = m_loaded_until; i < next_until; i++) {
            if (auto* t = dynamic_cast<GameTile*>(carousel->getChildren()[i])) {
                t->load_cover();
            }
        }
        foyer::log::write("[system] preloaded tiles %d..%d (%s)\n",
            m_loaded_until, next_until - 1, m_folder.c_str());
        m_loaded_until = next_until;
    }
}

}  // namespace foyer::browser
