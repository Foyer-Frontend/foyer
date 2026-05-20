#include "activity/system_activity.hpp"

#include "activity/game_activity.hpp"
#include "install_queue.hpp"
#include "launch.hpp"

#include <sys/stat.h>
#include "activity/per_game_activity.hpp"
#include "activity/per_system_activity.hpp"
#include "library_state.hpp"
#include "library/per_game.hpp"
#include "library/asset_pack.hpp"
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

// Mirrors install_queue's current status into the scrape banner
// when the active tag looks like a scrape job. install_queue runs
// every long-lived background op now (cores / bezels / shaders /
// cheats / foyer self-update / scrapes), so the banner only
// surfaces scrape activity by sniffing the tag prefix.
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
    // SuperGrafx HuCards share the PCE box form factor, not the
    // 513x458 portrait. Match pcengine so the covers don't render
    // squished in the carousel.
    {"supergrafx",      580, 680},  {"pcfx",            462, 607},
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
          m_system(system_folder), m_path(game_path),
          m_stem(game_stem)
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
        // instant. Art now lives on SD (downloaded asset pack);
        // setImageFromFile silently no-ops when the file isn't
        // present, which is fine while the first-run pack is
        // still downloading.
        m_img->setImageFromFile(
            ::foyer::library::asset_system_splash(
                art_dir_for(system_folder)));
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

        // Hint cleanup (0.6.88): A = Open Details (canonical "OK"
        // affordance on Switch), Y = Quick Launch (skip the detail
        // panel). X stays as Favourite. Per-game Settings moved to
        // GameActivity since the detail panel now sits between the
        // tile and the game. B / L / R still cycle pages and back
        // (the L/R hints are hidden — registerAction with the
        // hidden=true flag — to clean up the bottom-bar).
        this->registerAction(
            "Open details", brls::BUTTON_A,
            [this](brls::View*) {
                brls::Application::pushActivity(
                    new GameActivity(m_system, m_path));
                return true;
            }, false, false, brls::SOUND_CLICK);
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

        // Y — quick-launch, skipping the detail panel.
        this->registerAction(
            "Quick launch", brls::BUTTON_Y,
            [this](brls::View*) {
                launch_focused_game();
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

    int                index() const  { return m_idx;   }
    const std::string& system() const { return m_system; }
    const std::string& stem() const   { return m_stem;   }

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
    std::string     m_stem;
    std::string     m_cover_path;
    brls::Image*    m_img = nullptr;
    bool            m_cover_loaded = false;
    float           m_tile_w = 0.0f;
    float           m_tile_h = 0.0f;
};

}  // namespace

void SystemActivity::cancel_pending_scrape() {
    // Scrapes now ride install_queue, which HomeActivity's quit
    // drain already calls stop() on. Left as a no-op so the
    // SystemActivity public interface doesn't change.
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
    const auto snap = ::foyer::browser::install_queue::snapshot();
    // Only surface scrape activity. Other install_queue jobs (core
    // installs, foyer self-update, etc.) get their own banners
    // elsewhere; the scrape label stays empty for those.
    const bool is_scrape =
        !snap.active_tag.empty()
        && (snap.active_tag.rfind("Scrape ",   0) == 0
         || snap.active_tag.rfind("Rescrape ", 0) == 0);
    if (is_scrape && !snap.last_status.empty()) {
        scrapeStatus->setText(snap.last_status);
    } else {
        scrapeStatus->setText("");
        if (m_scrapeStatusTask) {
            m_scrapeStatusTask->stop();
            delete m_scrapeStatusTask;
            m_scrapeStatusTask = nullptr;
        }
    }
}

void SystemActivity::onResume() {
    brls::Activity::onResume();
    // GameActivity's Y rescrape writes fresh assets into
    // /foyer/assets/system/<sys>/<stem>/ — but our cover-flow tiles
    // were built with whatever box_art existed at first
    // onContentAvailable. Rebuild on every reappear so the new
    // cover shows up the moment the user pops back.
    //
    // Sequence matters: brls's popActivity restores focus to the
    // GameTile that was focused before the push. If we clearViews()
    // while that tile is the active focus, the next frame
    // dereferences a freed View (PC=0x30 vtable miss — same class
    // of crash that bit the install_queue completion path). Move
    // focus to actionRow first so the carousel can be torn down
    // without the focus pointer dangling, defer the rebuild a
    // frame via brls::sync to let pop's focus-restore drain, then
    // hand focus back to the freshly-built first tile.
    if (!carousel) return;

    // Deferred fast-return: populateCarousel was skipped in
    // onContentAvailable to keep the chain-back blank-screen short.
    // Now that we're actually visible, run it. Resolves
    // m_preselect_game → m_last_focus_idx so the user lands on the
    // game tile that was active before launching the core.
    if (m_defer_population && !m_populated) {
        foyer::log::write("[system] %s onResume: deferred populate kicking in\n",
            m_folder.c_str());
        populateCarousel();
        const auto& kids = carousel->getChildren();
        if (!kids.empty()) {
            int target = 0;
            if (!m_preselect_game.empty()) {
                if (const auto* sys = library_state::find_system(m_folder)) {
                    int i = 0;
                    for (const auto& g : sys->games) {
                        if (g.path == m_preselect_game) { target = i; break; }
                        i++;
                    }
                }
            }
            if (target < 0 || target >= (int)kids.size()) target = 0;
            m_last_focus_idx = target;
            brls::Application::giveFocus(kids[target]);
        }
        return;
    }

    if (actionRow) brls::Application::giveFocus(actionRow);

    auto* self = this;
    brls::sync([self]() {
        if (!self || !self->carousel) return;
        self->carousel->clearViews();
        self->populateCarousel();
        const auto& kids = self->carousel->getChildren();
        if (!kids.empty()) {
            // Restore focus to the tile the user was on before
            // pushing GameActivity (tracked via onTileFocused).
            // Clamp in case the rescan dropped tiles that pushed
            // the count below the cached index.
            const int n = (int)kids.size();
            int idx = self->m_last_focus_idx;
            if (idx < 0) idx = 0;
            if (idx >= n) idx = n - 1;
            brls::Application::giveFocus(kids[idx]);
        }
    });
}

void SystemActivity::onContentAvailable() {
    foyer::log::write("[system] content available %s\n", m_folder.c_str());

    // Backdrop — same per-system art as Home shows on tile focus.
    if (backdrop) {
        backdrop->setImageFromFile(
            ::foyer::library::asset_system_background(art_dir_for(m_folder)));
    }

    if (clock && !m_clockTask) {
        m_clockTask = new ClockTask(this->clock);
        m_clockTask->start();
        m_clockTask->run();
    }

    buildLogo();
    buildActionRow();

    // Deferred fast-return path: skip the heavy populateCarousel +
    // cover preload when this activity is being pushed under
    // GameActivity. onResume runs them when the user actually
    // B-backs to this view. Cuts hundreds of ms (for libraries with
    // hundreds of games) off the chain-back blank-screen window.
    //
    // IMPORTANT: only the populate runs late; the registerAction
    // block at the bottom of this function runs unconditionally so
    // L/R page-jump, B-back, Minus sort, etc. are bound regardless.
    // Previously the early-return here silently broke L/R on
    // chain-back (regression in db5ddd0).
    if (!m_defer_population) {
        populateCarousel();
    } else {
        foyer::log::write("[system] %s deferred populate\n", m_folder.c_str());
    }

    // Resume the status banner if install_queue is currently
    // running a scrape (the user popped back to Home + re-entered
    // while a scrape was still in flight).
    const auto snap = ::foyer::browser::install_queue::snapshot();
    if ((snap.active_tag.rfind("Scrape ",   0) == 0
      || snap.active_tag.rfind("Rescrape ", 0) == 0)
        && !m_scrapeStatusTask) {
        m_scrapeStatusTask = new ScrapeStatusTask(this);
        m_scrapeStatusTask->start();
    }

    // Default focus on the first cover tile (matches Home's
    // first-system-tile default focus). Falls back to action row
    // when the system has no scanned games.
    //
    // Chain-back-from-core path: main.cpp's fast_returned branch
    // called setPreselectGame() with the rom path the user just
    // played. Resolve it to a tile index by walking the scanned
    // game list (same order populateCarousel iterates) and focus
    // that tile instead of the first one. Also seed m_last_focus_idx
    // so a subsequent onResume rebuild returns to it.
    if (carousel && !carousel->getChildren().empty()) {
        int target = 0;
        if (!m_preselect_game.empty()) {
            if (const auto* sys = library_state::find_system(m_folder)) {
                int i = 0;
                for (const auto& g : sys->games) {
                    if (g.path == m_preselect_game) { target = i; break; }
                    i++;
                }
            }
        }
        const auto& kids = carousel->getChildren();
        if (target < 0 || target >= (int)kids.size()) target = 0;
        m_last_focus_idx = target;
        brls::Application::giveFocus(kids[target]);
        foyer::log::write("[system] gave focus to tile %d (preselect=%s)\n",
            target, m_preselect_game.c_str());
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

    // Minus — cycle the game sort mode for this view. We rescan
    // so the cached library_state::systems() is re-sorted by
    // the new SortMode, then rebuild the carousel in place.
    this->getContentView()->registerAction(
        "Sort", brls::BUTTON_BACK,
        [this](brls::View*) {
            using M = ::foyer::library::Config::SortMode;
            M cur = ::foyer::library::config().sort_mode;
            M nxt = cur;
            const char* label = "";
            switch (cur) {
                case M::Name:      nxt = M::Recent;    label = "Recent";    break;
                case M::Recent:    nxt = M::Playtime;  label = "Playtime";  break;
                case M::Playtime:  nxt = M::Favorites; label = "Favorites"; break;
                case M::Favorites:
                default:           nxt = M::Name;      label = "Name";      break;
            }
            ::foyer::library::set_sort_mode(nxt);
            library_state::rescan();
            if (carousel) {
                carousel->clearViews();
                populateCarousel();
            }
            brls::Application::notify(std::string("Sort: ") + label);
            return true;
        }, false, false, brls::SOUND_FOCUS_CHANGE);

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
    // L / R cycle pages but the hint bar suppresses their chips
    // (registerAction hidden=true) so the bottom bar stays clean
    // — only A / Y / X / B chips are visible. The actions still
    // fire on button press.
    this->getContentView()->registerAction(
        "Prev page", brls::BUTTON_LB,
        [jump_focus, page_size](brls::View*) {
            return jump_focus(-page_size());
        },
        /*hidden=*/true, /*allowRepeating=*/true, brls::SOUND_FOCUS_CHANGE);
    this->getContentView()->registerAction(
        "Next page", brls::BUTTON_RB,
        [jump_focus, page_size](brls::View*) {
            return jump_focus(+page_size());
        },
        /*hidden=*/true, /*allowRepeating=*/true, brls::SOUND_FOCUS_CHANGE);
}

void SystemActivity::buildLogo() {
    if (!logoHolder) return;
    // Top-bar logo on SystemActivity reflects the focused GAME.
    // Two children: an Image (wheel art) and a Label (text
    // fallback). Image is height-only constrained (width=auto)
    // so the natural aspect of each wheel is preserved — wide
    // wheels stretch right from the left edge of logo_holder,
    // narrow ones stay narrow. logo_holder itself sits at the
    // left of the top bar so every wheel starts at the same
    // x coordinate regardless of size.
    constexpr float kLogoH = 56.0f;
    auto* logo = new brls::Image();
    logo->setHeight(kLogoH);
    logo->setScalingType(brls::ImageScalingType::FIT);
    logoHolder->addView(logo);
    logo->setVisibility(brls::Visibility::GONE);

    auto* lbl = new brls::Label();
    lbl->setText("");
    lbl->setFontSize(24.0f);
    logoHolder->addView(lbl);
    lbl->setVisibility(brls::Visibility::GONE);
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
        "img/actions/scrape.png", "Scrape",
        [this](brls::View*) {
            const auto* sys = library_state::find_system(m_folder);
            if (!sys || sys->games.empty()) {
                brls::Application::notify("Nothing to scrape — no games scanned");
                return true;
            }
            const auto src = preferred_scrape_source();
            const std::string short_name{sys->def->short_name};
            const std::string tag = "Scrape " + short_name;

            // Snapshot the system into a heap copy so the queued
            // worker body can outlive this lambda invocation.
            auto sys_copy =
                std::make_shared<::foyer::library::System>(*sys);
            ::foyer::browser::install_queue::enqueue(
                tag,
                [sys_copy, src](::foyer::library::Worker& w) {
                    ::foyer::library::run_system_scrape(*sys_copy, src, w);
                });
            foyer::log::write(
                "[system] queued scrape for %s (%zu games)\n",
                m_folder.c_str(), sys->games.size());
            // Spin up the local status task so the in-view banner
            // mirrors the queue's last_status. install_queue drives
            // its own "Installing/Installed" toasts independently.
            if (!m_scrapeStatusTask) {
                m_scrapeStatusTask = new ScrapeStatusTask(this);
                m_scrapeStatusTask->start();
            }
            return true;
        }));

    actionRow->addView(new ActionButton(
        "img/actions/scan.png", "Scan",
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
    constexpr float kMaxSide = 280.0f;

    // For real systems every game shares the same box dims (the
    // system's SS box-2D ratio). For the virtual carousels
    // (Favourites / Recent / All Games) games are mixed across
    // systems, so each tile gets its own dims derived from its
    // rom path's parent folder.
    const bool is_virtual = m_folder.rfind("__", 0) == 0;
    auto system_of_game = [](const std::string& rom_path) {
        // Switch installed-title pseudo-paths don't have a folder
        // parent on disk — they're "switch://<hex>". The Switch
        // virtual system owns all of them; route every "switch://"
        // tile back to __switch so find_system + box dims resolve.
        if (rom_path.rfind("switch://", 0) == 0) {
            return std::string{"__switch"};
        }
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
    //
    // Tail batch: also preload the LAST kInitialBatch tiles up
    // front so wrap-around nav (L-shoulder from tile 0, or →
    // pressed past the end) lands on a tile whose cover is already
    // decoded instead of a blank frame. The middle range (between
    // the head batch and the tail batch) still streams in lazily
    // as focus crosses the loaded edge.
    //
    // Batch size tuned for entry-stall vs visual completeness.
    // brls::Image::setImageFromFile decodes the JPEG synchronously
    // on the UI thread; each cover is ~25-40 ms on Switch hardware.
    // 4 tiles fit in the carousel viewport at standard 1280-wide
    // resolution, so warming 4 head + 4 tail is enough to hide
    // every visible tile and the wrap-around target. Larger
    // up-front batches (10/10 from db5ddd0) added noticeable entry
    // stall on small libraries with no perceptual win — the
    // sliding-window preload in onTileFocused already covers the
    // rest as the user moves focus.
    constexpr int kInitialBatch = 4;
    const int n = (int)carousel->getChildren().size();
    for (int i = 0; i < std::min(n, kInitialBatch); i++) {
        if (auto* t = dynamic_cast<GameTile*>(carousel->getChildren()[i])) {
            t->load_cover();
        }
    }
    m_loaded_until = std::min(n, kInitialBatch);
    // Tail-batch only kicks in when the library has more than
    // 2*kInitialBatch tiles; smaller libraries are already fully
    // covered by the head batch.
    if (n > 2 * kInitialBatch) {
        const int tail_start = n - kInitialBatch;
        for (int i = tail_start; i < n; i++) {
            if (auto* t = dynamic_cast<GameTile*>(carousel->getChildren()[i])) {
                t->load_cover();
            }
        }
    }
    m_populated = true;
}

void SystemActivity::onTileFocused(int idx) {
    if (!carousel) return;
    m_last_focus_idx = idx;
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

    // Backdrop swap. If the focused tile's per-game ScreenScraper
    // bundle has a fanart.jpg, paint that; otherwise fall back to
    // the system's bundled default backdrop. Reads disk on every
    // focus change but the file (a single 1280-wide jpeg) decodes
    // in < 30 ms even on Switch — well under the carousel scroll
    // animation duration. brls::Image dedupes by path so revisiting
    // the same tile is free.
    if (idx < 0 || idx >= n) return;
    auto* tile = dynamic_cast<GameTile*>(carousel->getChildren()[idx]);
    if (!tile) return;

    const auto bundle = ::foyer::scrapers::game_asset_dir(
        tile->system(), tile->stem());

    // Top-bar logo follows the focused game. Wheel art when SS
    // dropped one; text fallback when not.
    if (logoHolder && logoHolder->getChildren().size() >= 2) {
        auto* logo = dynamic_cast<brls::Image*>(
            logoHolder->getChildren()[0]);
        auto* lbl  = dynamic_cast<brls::Label*>(
            logoHolder->getChildren()[1]);
        const auto wheel_path =
            ::foyer::scrapers::find_in_dir(bundle, "wheel");
        if (logo && !wheel_path.empty()) {
            logo->setImageFromFile(wheel_path);
            logo->setVisibility(brls::Visibility::VISIBLE);
            if (lbl) lbl->setVisibility(brls::Visibility::GONE);
        } else {
            if (logo) logo->setVisibility(brls::Visibility::GONE);
            if (lbl) {
                lbl->setText(tile->stem());
                lbl->setVisibility(brls::Visibility::VISIBLE);
            }
        }
    }

    if (!backdrop) return;
    const auto fanart = bundle + "fanart.jpg";
    struct stat st{};
    if (::stat(fanart.c_str(), &st) == 0 && st.st_size > 0) {
        backdrop->setImageFromFile(fanart);
    } else {
        backdrop->setImageFromFile(
            ::foyer::library::asset_system_background(art_dir_for(m_folder)));
    }
}

}  // namespace foyer::browser
