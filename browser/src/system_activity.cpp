#include "activity/system_activity.hpp"

#include "activity/game_activity.hpp"
#include "install_queue.hpp"
#include "launch.hpp"

#include <sys/stat.h>
#include <time.h>
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
#include "theme_change.hpp"

#include <fstream>
#include <unordered_map>
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

// Session-scoped focus memory: folder → last focused tile index.
// Lives for the lifetime of the foyer process and dies when the
// app quits (no on-disk persistence — cold-boot starts fresh,
// matching the user's stated expectation). Read by SystemActivity
// when it's about to be pushed (via the recall_session_focus
// helper used by main.cpp's chain-back path) and by the activity
// itself in onContentAvailable; written by onTileFocused so the
// most-recently-focused tile sticks.
std::unordered_map<std::string, int> g_session_focus;

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
    ::foyer::browser::theme_change::unsubscribe(m_theme_sub);
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
    if (!carousel) return;

    // Deferred fast-return: populateCarousel was skipped in
    // onContentAvailable to keep the chain-back blank-screen short.
    // Now that we're actually visible, run it. Resolves
    // m_preselect_game → m_last_focus_idx so the user lands on the
    // game that was active before launching the core.
    if (m_defer_population && !m_populated) {
        foyer::log::write("[system] %s onResume: deferred populate kicking in\n",
            m_folder.c_str());
        if (!m_preselect_game.empty()) {
            if (const auto* sys = library_state::find_system(m_folder)) {
                int i = 0;
                for (const auto& g : sys->games) {
                    if (g.path == m_preselect_game) {
                        m_last_focus_idx = i;
                        break;
                    }
                    i++;
                }
            }
        }
        populateCarousel();
        if (m_flow && m_flow->count() > 0) {
            brls::Application::giveFocus(m_flow);
        }
        return;
    }

    // The strip itself survives the push/pop — the old per-tile
    // teardown/giveFocus choreography (and its PC=0x30 focus-vtable
    // crash class) is gone. Rescrape-back just swaps the data in
    // place so fresh box art resolves on the next visible-window
    // pass; routine back restores focus to the same index.
    if (GameActivity::consume_rescrape_dirty()) {
        populateCarousel();
    }
    if (m_flow && m_flow->count() > 0) {
        m_flow->setIndex(m_last_focus_idx);
        brls::Application::giveFocus(m_flow);
    }
}

void SystemActivity::onContentAvailable() {
    foyer::log::write("[system] content available %s\n", m_folder.c_str());

    // Session-scoped focus recall: if the user already visited
    // this system this process-lifetime, jump back to whichever
    // tile they last focused. Seed m_last_focus_idx BEFORE
    // populateCarousel runs so the focus-region preload warms
    // the right window — landing the user mid-list with every
    // visible cover already decoded, not a cold blank around the
    // restored tile. Wins until the user quits.
    if (auto it = g_session_focus.find(m_folder);
        it != g_session_focus.end()) {
        m_last_focus_idx = it->second;
    }

    // Backdrop — same per-system art as Home shows on tile focus.
    // Async path: 1280×720 JPEG is the single heaviest decode on
    // every activity entry (~50-100 ms synchronously). brls's
    // setImageAsync hands the file I/O to a worker thread; the
    // JPEG decode + GL upload still happen on the UI thread when
    // the worker returns, but as a discrete sync callback rather
    // than blocking the entire onContentAvailable path.
    if (backdrop) {
        const std::string bg =
            ::foyer::library::asset_system_background(art_dir_for(m_folder));
        backdrop->setImageAsync(
            [bg](std::function<void(const std::string&, size_t)> cb) {
                std::ifstream f(bg, std::ios::binary | std::ios::ate);
                if (!f) { cb(std::string{}, 0); return; }
                const auto size = (std::size_t)f.tellg();
                f.seekg(0);
                std::string buf(size, '\0');
                f.read(buf.data(), (std::streamsize)size);
                cb(buf, size);
            });
    }

    if (clock && !m_clockTask) {
        m_clockTask = new ClockTask(this->clock);
        m_clockTask->start();
        m_clockTask->run();
    }

    // Re-skin the bar overlay (and the system logo, which is also
    // theme-aware via buildLogo) when HOS theme variant flips —
    // brls's XML @theme cache otherwise freezes the bg colour at
    // parse time.
    if (m_theme_sub < 0) {
        m_theme_sub = ::foyer::browser::theme_change::subscribe(
            [this](brls::ThemeVariant) {
                const auto bar = brls::Application::getTheme()
                    .getColor("foyer/bar_overlay");
                if (topBar)    topBar->setBackgroundColor(bar);
                if (bottomBar) bottomBar->setBackgroundColor(bar);
                buildLogo();
            });
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
        // Time the carousel build so future "feels slow" reports
        // can be diagnosed against a real number instead of
        // perception. clock_gettime(MONOTONIC) on Switch is a
        // single counter read — overhead negligible.
        struct timespec t0{}, t1{};
        ::clock_gettime(CLOCK_MONOTONIC, &t0);
        populateCarousel();
        ::clock_gettime(CLOCK_MONOTONIC, &t1);
        const auto elapsed_ms =
            (t1.tv_sec - t0.tv_sec) * 1000.0
            + (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
        foyer::log::write(
            "[system] populateCarousel %s: %d tiles in %.1f ms\n",
            m_folder.c_str(),
            carousel ? (int)carousel->getChildren().size() : 0,
            elapsed_ms);
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
    // Precedence — highest wins:
    //   1. chain-back-from-core preselect (main.cpp's fast_returned
    //      branch calls setPreselectGame(path))
    //   2. session-scoped focus recall (m_last_focus_idx seeded
    //      at the top of onContentAvailable from g_session_focus)
    //   3. tile 0 (fresh entry, never visited before this session)
    if (m_flow && m_flow->count() > 0) {
        // Seed from the session recall — kicks in when 1 doesn't.
        int target = m_last_focus_idx > 0 ? m_last_focus_idx : 0;
        if (!m_preselect_game.empty()) {
            if (const auto* sys = library_state::find_system(m_folder)) {
                int i = 0;
                for (const auto& g : sys->games) {
                    if (g.path == m_preselect_game) { target = i; break; }
                    i++;
                }
            }
        }
        m_last_focus_idx = target;
        m_flow->setIndex(target);
        brls::Application::giveFocus(m_flow);
        foyer::log::write("[system] strip focus at %d (preselect=%s)\n",
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
            // setEntries swaps data in place — never tear the strip
            // down (clearViews here would delete m_flow under the
            // active focus).
            m_last_focus_idx = 0;
            populateCarousel();
            brls::Application::notify(std::string("Sort: ") + label);
            return true;
        }, false, false, brls::SOUND_FOCUS_CHANGE);

    // L / R: jump 10 tiles in the cover-flow carousel. brls's
    // default carousel navigation is one-tile-at-a-time on the
    // d-pad / left stick; the shoulder buttons skim faster
    // through long systems (250+ NES/SNES games).
    auto jump_focus = [this](int delta) {
        if (!m_flow || m_flow->count() == 0) return false;
        const int n = m_flow->count();
        const int next = ((m_flow->index() + delta) % n + n) % n;
        m_flow->setIndex(next, /*animate=*/true);
        if (!m_flow->isFocused()) brls::Application::giveFocus(m_flow);
        return true;
    };
    // Page jump = number of tiles the viewport currently fits.
    // We measure the live strip width so the jump scales with
    // dock vs handheld display modes.
    auto page_size = [this]() {
        constexpr float kTilePitch = 264.0f;
        const float vp = carousel ? carousel->getWidth() : 1280.0f;
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
    // Clear any existing children first — buildLogo can run more
    // than once on the same activity instance (the theme_change
    // hook calls it on every HOS Light↔Dark flip so a refresh
    // re-picks any theme-aware art). Without the clear, each
    // flip appended a fresh Image + Label to logoHolder and the
    // tree slowly grew.
    logoHolder->clearViews();
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

    // Mount the strip once; subsequent populates just swap data.
    if (!m_flow) {
        m_flow = new CoverFlowView(280.0f);
        m_flow->setGrow(1.0f);
        m_flow->onFocusChangedCb = [this](int idx) { onTileFocused(idx); };
        m_flow->onOpenCb = [this](int idx) {
            if (const auto* e = m_flow->entryAt(idx)) {
                brls::Application::pushActivity(
                    new GameActivity(e->system, e->path));
            }
        };
        m_flow->onLaunchCb = [this](int idx) {
            const auto* e = m_flow->entryAt(idx);
            if (!e) return;
            const auto* origin = library_state::find_system(e->system);
            if (!origin) {
                brls::Application::notify("System not in library — rescan?");
                return;
            }
            for (const auto& g : origin->games) {
                if (g.path != e->path) continue;
                if (launch_game(*origin, g)) {
                    brls::Application::quit();
                } else {
                    brls::Application::notify(
                        "Player nro missing — install the core from Settings");
                }
                return;
            }
        };
        carousel->addView(m_flow);
    }

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

    std::vector<CoverFlowView::Entry> entries;
    entries.reserve(sys->games.size());
    if (!is_virtual) {
        const auto dims = box_dims_for_system(m_folder);
        const float scale = std::min(kMaxSide / dims.w, kMaxSide / dims.h);
        const float tile_w = dims.w * scale;
        const float tile_h = dims.h * scale;
        const auto splash =
            ::foyer::library::asset_system_splash(art_dir_for(m_folder));
        foyer::log::write("[system] %s tile size = %.0fx%.0f\n",
            m_folder.c_str(), tile_w, tile_h);
        for (const auto& g : sys->games) {
            CoverFlowView::Entry e;
            e.system  = m_folder;
            e.path    = g.path;
            e.stem    = g.stem;
            e.cover   = g.box_art;
            e.splash  = splash;
            e.tile_w  = tile_w;
            e.tile_h  = tile_h;
            entries.push_back(std::move(e));
        }
    } else {
        for (const auto& g : sys->games) {
            const auto game_sys = system_of_game(g.path);
            const auto dims = box_dims_for_system(game_sys);
            const float scale =
                std::min(kMaxSide / dims.w, kMaxSide / dims.h);
            CoverFlowView::Entry e;
            e.system  = game_sys;
            e.path    = g.path;
            e.stem    = g.stem;
            e.cover   = g.box_art;
            e.splash  = ::foyer::library::asset_system_splash(
                art_dir_for(game_sys));
            e.tile_w  = dims.w * scale;
            e.tile_h  = dims.h * scale;
            entries.push_back(std::move(e));
        }
    }

    // Hand the data to the strip. No warm batches needed any more
    // — the view streams visible-window covers in by itself, one
    // decode per frame, from whatever index focus lands on.
    m_flow->setEntries(std::move(entries), m_last_focus_idx);
    m_populated = true;
}

void SystemActivity::onTileFocused(int idx) {
    if (!m_flow) return;
    m_last_focus_idx = idx;
    // Persist for the rest of the process lifetime so re-entering
    // the same system restores focus to where the user left it.
    // Cold boot wipes the map (process-global), matching the
    // user's mental model.
    g_session_focus[m_folder] = idx;

    // Backdrop swap. If the focused game's per-game ScreenScraper
    // bundle has a fanart.jpg, paint that; otherwise fall back to
    // the system's bundled default backdrop. brls::Image dedupes by
    // path so revisiting the same game is free.
    const auto* entry = m_flow->entryAt(idx);
    if (!entry) return;

    const auto bundle = ::foyer::scrapers::game_asset_dir(
        entry->system, entry->stem);

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
                lbl->setText(entry->stem);
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
