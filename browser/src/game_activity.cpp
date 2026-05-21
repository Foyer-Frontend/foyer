#include "activity/game_activity.hpp"
#include "activity/per_game_activity.hpp"

#include "install_queue.hpp"
#include "launch.hpp"
#include "library_state.hpp"
#include "library/game_meta.hpp"
#include "library/per_game.hpp"
#include "library/ra_progress.hpp"
#include "library/scrape_job.hpp"
#include "scrapers/accounts.hpp"
#include "library/switch_titles.hpp"
#include "library/worker.hpp"
#include "platform/log.hpp"
#include "scrapers/cache.hpp"
#include "scrapers/screenscraper.hpp"

#include <yyjson.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Rescrape now flows through install_queue::enqueue (see Y action
// registration below). No per-activity Worker / lifetime juggling —
// the queue owns the in-flight job and serialises against every
// other install_queue user.

// Live observer pointer + path. The rescrape worker's completion
// lambda runs through brls::sync (UI thread) and checks these to
// refresh the game-details view in place when the user is still
// looking at it. Cleared in ~GameActivity so a popped-then-
// finished scrape can't write into a dead view.
GameActivity* g_live_activity = nullptr;
std::string   g_live_game_path;

// Set true by the rescrape worker's completion lambda on success
// so SystemActivity::onResume knows to clear+rebuild its
// carousel one time to pick up the fresh box art. Plain bool —
// the rescrape callback runs on the brls UI thread (wrapped in
// brls::sync), same thread that reads + clears it in
// SystemActivity::onResume, so no atomics needed.
bool g_rescrape_dirty = false;

// Clock task — same shape as Home/System.
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

// Pull a string field out of the bundled metadata.json. Returns
// empty string when the file is missing or the field isn't present.
std::string read_meta_field(const std::string& bundle_dir,
                            const char* key)
{
    std::ifstream in{bundle_dir + "metadata.json"};
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string body = ss.str();
    if (body.empty()) return {};
    auto* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) return {};
    std::string out;
    if (auto* root = yyjson_doc_get_root(doc); root && yyjson_is_obj(root)) {
        if (auto* v = yyjson_obj_get(root, key); v && yyjson_is_str(v)) {
            out = yyjson_get_str(v);
        }
    }
    yyjson_doc_free(doc);
    return out;
}

void add_meta_row(brls::Box* host, const std::string& key,
                  const std::string& value)
{
    if (value.empty()) return;
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::FLEX_START);
    row->setMargins(0.0f, 0.0f, 6.0f, 0.0f);

    auto* k = new brls::Label();
    k->setText(key);
    k->setFontSize(18.0f);
    k->setWidth(140.0f);
    // Theme-independent red so the label reads against both the
    // light and dark theme backgrounds; brls's "text_disabled"
    // was too faint to scan in either mode.
    k->setTextColor(nvgRGB(0xD0, 0x3A, 0x3A));
    row->addView(k);

    auto* v = new brls::Label();
    v->setText(value);
    v->setFontSize(18.0f);
    v->setMaxWidth(620.0f);
    auto theme = brls::Application::getTheme();
    v->setTextColor(theme.getColor("brls/text"));
    row->addView(v);

    host->addView(row);
}

}  // namespace

GameActivity::GameActivity(std::string_view system_folder,
                           std::string_view game_path)
    : m_system_folder(system_folder)
    , m_game_path(game_path)
{
    // Switch installed-title pseudo-paths ("switch://<hex>") don't
    // have a filename to strip — the basename IS the application_id
    // in hex, which is what was getting sent to ScreenScraper as
    // the search term (returning zero hits, since SS indexes by
    // human-readable name). Look the title up in the NACP cache
    // and use its name as the stem so the scrape query is the
    // same string the user sees on the tile.
    if (game_path.rfind("switch://", 0) == 0) {
        const auto app_id =
            ::foyer::library::switch_id_from_path(game_path);
        for (const auto& t : ::foyer::library::switch_titles()) {
            if (t.application_id == app_id) {
                m_game_stem = t.name.empty()
                    ? std::string{"Untitled"} : t.name;
                return;
            }
        }
        m_game_stem = "Untitled";
        return;
    }

    // Real rom paths: strip directory + extension.
    std::string p{game_path};
    const auto slash = p.find_last_of('/');
    std::string base = (slash == std::string::npos) ? p : p.substr(slash + 1);
    const auto dot = base.find_last_of('.');
    m_game_stem = (dot == std::string::npos) ? base : base.substr(0, dot);
}

GameActivity::~GameActivity() {
    if (m_clockTask) {
        m_clockTask->stop();
        delete m_clockTask;
        m_clockTask = nullptr;
    }
    if (g_live_activity == this) {
        g_live_activity = nullptr;
        g_live_game_path.clear();
    }
}

void GameActivity::onContentAvailable() {
    foyer::log::write("[game] onContentAvailable sys=%s stem=%s path=%s\n",
        m_system_folder.c_str(), m_game_stem.c_str(), m_game_path.c_str());
    g_live_activity = this;
    g_live_game_path = m_game_path;

    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);
    foyer::log::write("[game] bundle=%s\n", bundle.c_str());

    // Fanart background. JPEG without a region tag.
    if (fanart) {
        const std::string fart = bundle + "fanart.jpg";
        struct stat st{};
        if (::stat(fart.c_str(), &st) == 0) {
            foyer::log::write("[game] fanart load %s\n", fart.c_str());
            fanart->setImageFromFile(fart);
            foyer::log::write("[game] fanart ok\n");
        }
    }

    // Game title — use scraped name when available, else game's
    // display name from the scan, else stem.
    std::string title = read_meta_field(bundle, "name");
    if (title.empty()) {
        if (const auto* sys = library_state::find_system(m_system_folder)) {
            for (const auto& g : sys->games) {
                if (g.path == m_game_path) {
                    title = g.display.empty() ? g.stem : g.display;
                    break;
                }
            }
        }
    }
    if (title.empty()) title = m_game_stem;

    // If SS dropped a wheel(XX).png in the per-game bundle, show
    // that instead of the plain text title. Wheel is a transparent
    // PNG of the game logo and reads better than a 28pt label.
    // Fall back to text when no wheel exists.
    const auto bundle_dir = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);
    const auto wheel_path = ::foyer::scrapers::find_in_dir(
        bundle_dir, "wheel");
    if (!wheel_path.empty() && gameWheel && gameTitle) {
        foyer::log::write("[game] wheel load %s\n", wheel_path.c_str());
        gameWheel->setImageFromFile(wheel_path);
        gameWheel->setVisibility(brls::Visibility::VISIBLE);
        gameTitle->setVisibility(brls::Visibility::GONE);
    } else {
        if (gameTitle) {
            gameTitle->setText(title);
            gameTitle->setVisibility(brls::Visibility::VISIBLE);
        }
        if (gameWheel) gameWheel->setVisibility(brls::Visibility::GONE);
    }
    foyer::log::write("[game] header done\n");

    buildGallery();
    foyer::log::write("[game] gallery done\n");

    if (clock && !m_clockTask) {
        m_clockTask = new ClockTask(this->clock);
        m_clockTask->start();
        m_clockTask->run();
    }

    // Translucent body panel — theme background at ~78% alpha
    // so the metadata stays legible over arbitrary fanart.
    // The "black/white border around the screenshots" the user
    // reported is actually the FIT scaling letterbox; that's
    // handled in show_slide() by resizing the Image widget to
    // the source's native aspect so no padding pixels remain.
    if (body) {
        auto th = brls::Application::getTheme();
        NVGcolor bg = th.getColor("brls/background");
        bg.a = 0.78f;
        body->setBackgroundColor(bg);
    }

    buildMetaPanel();
    foyer::log::write("[game] meta done\n");

    // Pre-play RA progress prefetch — kick a detached worker that
    // hashes the rom, asks RA for the matching game id, then pulls
    // the user's current progress and writes it to the per-rom
    // metadata sidecar. On success we re-render the meta panel via
    // brls::sync so the "Achievements N/M" row pops in without the
    // user needing to leave + re-enter the page.
    //
    // Bails immediately when the user hasn't set
    // accounts.retroachievements.{user, web_api_key} or the sidecar
    // already has cheevos counts (player binary populates these on
    // every unlock — REST is just the cold-boot fallback).
    {
        const auto& ra = ::foyer::scrapers::accounts().retroachievements;
        const auto cur = ::foyer::library::load_meta(
            m_system_folder, m_game_stem);
        if (ra.rest_ready() && cur.cheevos_total < 0) {
            const std::string sys  = m_system_folder;
            const std::string stem = m_game_stem;
            const std::string path = m_game_path;
            std::thread([sys, stem, path]() {
                const bool ok = ::foyer::library::fetch_progress(
                    sys, stem, path);
                if (!ok) return;
                brls::sync([]() {
                    auto* a = dynamic_cast<GameActivity*>(
                        brls::Application::getActivitiesStack().back());
                    if (a) a->refreshMetaPanel();
                });
            }).detach();
        }
    }

    // Gamepad shortcuts for the game-detail view. No on-screen
    // buttons; the actions live as brls hint chips on the bottom
    // bar. Avoids the focus-fall-through bug too — actions on the
    // contentView always fire on this activity, not whatever tile
    // is alive below.
    const std::string folder = m_system_folder;
    const std::string path_copy = m_game_path;
    const std::string stem_copy = m_game_stem;

    if (auto* cv = this->getContentView()) {
        // A — Play
        cv->registerAction(
            "Play", brls::BUTTON_A,
            [folder, path_copy](brls::View*) {
                const auto* sys = library_state::find_system(folder);
                if (!sys) return true;
                for (const auto& g : sys->games) {
                    if (g.path != path_copy) continue;
                    if (launch_game(*sys, g)) {
                        brls::Application::quit();
                    }
                    return true;
                }
                return true;
            }, false, false, brls::SOUND_CLICK);

        // B — Back
        cv->registerAction(
            "hints/back"_i18n, brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);

        // X — Toggle favourite. per_game.jsonc persists across
        // launches; scanner reapplies via apply_per_game_state.
        cv->registerAction(
            "Favourite", brls::BUTTON_X,
            [path_copy](brls::View*) {
                const bool was = ::foyer::library::per_game_favorite(path_copy);
                ::foyer::library::set_per_game_favorite(path_copy, !was);
                brls::Application::notify(
                    was ? "Removed from favourites"
                        : "Added to favourites");
                return true;
            }, false, false, brls::SOUND_CLICK);

        // Y — Rescrape this single rom. Enqueues a one-shot scrape
        // on install_queue so it serializes against every other
        // long-running op (core installs, system scrapes, foyer
        // self-update). install_queue dedup means mash-tapping Y
        // won't queue the same rescrape twice.
        cv->registerAction(
            "Rescrape", brls::BUTTON_Y,
            [folder, path_copy, stem_copy](brls::View*) {
                ::foyer::browser::install_queue::enqueue(
                    "Rescrape " + stem_copy,
                    [folder, path_copy, stem_copy]
                    (::foyer::library::Worker& w) {
                        const bool ok = ::foyer::library::run_one_scrape(
                            folder, path_copy, stem_copy, w);
                        const std::string stem_done = stem_copy;
                        const std::string path_done = path_copy;
                        brls::sync([stem_done, path_done, ok]() {
                            if (ok) {
                                // Mark the carousel under us as
                                // dirty so SystemActivity::onResume
                                // rebuilds on B-back to pick up the
                                // freshly-downloaded box art.
                                g_rescrape_dirty = true;
                                if (g_live_activity
                                    && g_live_game_path == path_done) {
                                    g_live_activity->refresh_from_disk();
                                }
                            }
                        });
                    });
                return true;
            }, false, false, brls::SOUND_CLICK);

        // + (Start) — Per-game settings (moved here from SystemActivity
        // in 0.6.88 so the bottom bar on the carousel stays clean).
        cv->registerAction(
            "Settings", brls::BUTTON_START,
            [path_copy, sys_copy = m_system_folder](brls::View*) {
                brls::Application::pushActivity(
                    new PerGameActivity(sys_copy, path_copy),
                    brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_CLICK);
    }
    foyer::log::write("[game] onContentAvailable done\n");
}

void GameActivity::rebuildGalleryContent() {
    if (!slide || !galleryHolder) return;
    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);

    m_slides.clear();
    m_slide_idx = 0;

    // Pull every screenshot-shaped file out of the bundle. Skip
    // box-2D / bezel / fanart / video — those serve different
    // roles in the layout. sstitle goes first so the title
    // screen is the default slide.
    DIR* d = ::opendir(bundle.c_str());
    if (d) {
        std::vector<std::string> titles;
        std::vector<std::string> shots;
        while (auto* ent = ::readdir(d)) {
            const std::string nm = ent->d_name;
            if (nm.size() < 5) continue;
            const auto ext = nm.substr(nm.size() - 4);
            if (ext != ".png" && ext != ".jpg") continue;
            if (nm.rfind("box-2D", 0) == 0)  continue;
            if (nm.rfind("bezel-", 0) == 0)  continue;
            if (nm == "fanart.jpg")          continue;
            if (nm.rfind("sstitle", 0) == 0) titles.push_back(nm);
            else                             shots.push_back(nm);
        }
        ::closedir(d);
        std::sort(titles.begin(), titles.end());
        std::sort(shots.begin(),  shots.end());
        for (const auto& nm : titles) m_slides.push_back(bundle + nm);
        for (const auto& nm : shots)  m_slides.push_back(bundle + nm);
    }

    if (!m_slides.empty()) {
        show_slide(0);
    } else if (slideCaption) {
        slideCaption->setText("");
    }
}

void GameActivity::buildGallery() {
    if (!slide || !galleryHolder) return;

    rebuildGalleryContent();

    // d-pad up / down on the focused gallery holder rotates the
    // current slide. Brls's default focus walking would jump out
    // of the panel on these keys; intercepting the action keeps
    // focus put while the image cycles.
    galleryHolder->registerAction(
        "Prev", brls::BUTTON_UP,
        [this](brls::View*) {
            if (m_slides.empty()) return false;
            int n = (int)m_slides.size();
            show_slide((m_slide_idx - 1 + n) % n);
            return true;
        }, false, true, brls::SOUND_FOCUS_CHANGE);
    galleryHolder->registerAction(
        "Next", brls::BUTTON_DOWN,
        [this](brls::View*) {
            if (m_slides.empty()) return false;
            int n = (int)m_slides.size();
            show_slide((m_slide_idx + 1) % n);
            return true;
        }, false, true, brls::SOUND_FOCUS_CHANGE);
}

void GameActivity::show_slide(int idx) {
    if (m_slides.empty() || !slide) return;
    if (idx < 0) idx = 0;
    if (idx >= (int)m_slides.size()) idx = (int)m_slides.size() - 1;
    m_slide_idx = idx;
    slide->setImageFromFile(m_slides[idx]);
    // Resize the slide widget itself to match the source's
    // native aspect so the FIT scaler has no padding to draw —
    // gets rid of the black/white letterbox bands the user
    // hit. The Image slot in game.xml is 480x320; we cap to
    // those bounds, scale by the limiting dimension, and let
    // the parent Box's alignItems=center handle horizontal
    // centring.
    slide->setScalingType(brls::ImageScalingType::STRETCH);
    constexpr float kSlotW = 480.0f;
    constexpr float kSlotH = 320.0f;
    const float ow = slide->getOriginalImageWidth();
    const float oh = slide->getOriginalImageHeight();
    if (ow > 0.0f && oh > 0.0f) {
        float w = kSlotW;
        float h = w * (oh / ow);
        if (h > kSlotH) {
            h = kSlotH;
            w = h * (ow / oh);
        }
        slide->setWidth(w);
        slide->setHeight(h);
    }
    if (slideCaption) {
        const auto& p = m_slides[idx];
        const auto slash = p.find_last_of('/');
        const std::string nm = (slash == std::string::npos) ? p : p.substr(slash + 1);
        // Friendly caption mapped from the ScreenScraper kind
        // prefix instead of the raw filename — "sstitle(us).png"
        // -> "Title screen", "ss(wor).png" -> "Gameplay", etc.
        std::string kind;
        if      (nm.rfind("sstitle",       0) == 0) kind = "Title screen";
        else if (nm.rfind("ss",            0) == 0) kind = "Gameplay";
        else if (nm.rfind("box-2D",        0) == 0) kind = "Box art";
        else if (nm.rfind("box-3D",        0) == 0) kind = "Box (3D)";
        else if (nm.rfind("fanart",        0) == 0) kind = "Fanart";
        else if (nm.rfind("screenmarquee", 0) == 0) kind = "Marquee";
        else if (nm.rfind("bezel",         0) == 0) kind = "Bezel";
        else                                        kind = "Screenshot";
        slideCaption->setText(
            kind + "   " + std::to_string(idx + 1)
            + "/" + std::to_string(m_slides.size()));
    }
}

void GameActivity::refreshMetaPanel() {
    if (!metaHolder) return;
    metaHolder->clearViews();
    buildMetaPanel();
}

void GameActivity::buildMetaPanel() {
    if (!metaHolder) return;
    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);

    struct stat st{};
    const bool has_meta =
        ::stat((bundle + "metadata.json").c_str(), &st) == 0;

    // Big title at the top of the panel.
    auto* big = new brls::Label();
    auto title = read_meta_field(bundle, "name");
    if (title.empty()) title = m_game_stem;
    big->setText(title);
    big->setFontSize(32.0f);
    big->setMargins(0.0f, 0.0f, 12.0f, 0.0f);
    big->setMaxWidth(760.0f);
    metaHolder->addView(big);

    // Empty-state hint: when the bundle has no metadata.json,
    // surface the Y shortcut so the user knows the new view is
    // working and just needs a scrape pass.
    if (!has_meta) {
        auto* empty = new brls::Label();
        empty->setText(
            "No metadata yet — press Y to scrape this game from "
            "ScreenScraper.");
        empty->setFontSize(18.0f);
        empty->setMaxWidth(760.0f);
        auto theme = brls::Application::getTheme();
        empty->setTextColor(theme.getColor("brls/text_disabled"));
        metaHolder->addView(empty);
        return;
    }

    add_meta_row(metaHolder, "Developer",    read_meta_field(bundle, "developer"));
    add_meta_row(metaHolder, "Publisher",    read_meta_field(bundle, "publisher"));
    add_meta_row(metaHolder, "Players",      read_meta_field(bundle, "players"));
    add_meta_row(metaHolder, "Rating",       read_meta_field(bundle, "rating"));
    add_meta_row(metaHolder, "Genre",        read_meta_field(bundle, "genre"));
    add_meta_row(metaHolder, "Release date", read_meta_field(bundle, "release_date"));

    // RetroAchievements progress, sourced from the per-rom sidecar
    // written by the player on every unlock. Empty when the rom
    // hasn't been booted with valid RA creds yet; the REST prefetch
    // (rcheevos hash → API_GetGameInfoAndUserProgress) below pops
    // a row in once the worker fills the sidecar.
    {
        const auto meta = ::foyer::library::load_meta(
            m_system_folder, m_game_stem);
        if (meta.cheevos_total > 0) {
            const int pct = (int)((100.0 * meta.cheevos_unlocked)
                                 / meta.cheevos_total + 0.5);
            std::string v = std::to_string(meta.cheevos_unlocked)
                + " / " + std::to_string(meta.cheevos_total)
                + "   (" + std::to_string(pct) + "%)";
            add_meta_row(metaHolder, "Achievements", v);
        }
    }

    // Synopsis spans full width, multi-line.
    const auto synopsis = read_meta_field(bundle, "synopsis");
    if (!synopsis.empty()) {
        auto* spacer = new brls::Box();
        spacer->setHeight(12.0f);
        metaHolder->addView(spacer);

        auto* hdr = new brls::Label();
        hdr->setText("Synopsis");
        hdr->setFontSize(20.0f);
        hdr->setMargins(0.0f, 0.0f, 4.0f, 0.0f);
        // Match the metadata key labels above — same red across
        // both themes so the section header stays readable
        // against arbitrary fanart.
        hdr->setTextColor(nvgRGB(0xD0, 0x3A, 0x3A));
        metaHolder->addView(hdr);

        auto* body = new brls::Label();
        body->setText(synopsis);
        body->setFontSize(18.0f);
        body->setMaxWidth(760.0f);
        auto theme = brls::Application::getTheme();
        body->setTextColor(theme.getColor("brls/text"));
        metaHolder->addView(body);
    }
}

void GameActivity::refresh_from_disk() {
    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);

    // Fanart may have just landed — re-set even if previous load
    // returned a placeholder. brls::Image is tolerant of repeated
    // setImageFromFile calls.
    if (fanart) {
        const std::string fart = bundle + "fanart.jpg";
        struct stat st{};
        if (::stat(fart.c_str(), &st) == 0 && st.st_size > 0) {
            fanart->setImageFromFile(fart);
        }
    }

    // Title — prefer scraped value, fall back to scan display, then stem.
    if (gameTitle) {
        std::string title = read_meta_field(bundle, "name");
        if (title.empty()) {
            if (const auto* sys = library_state::find_system(m_system_folder)) {
                for (const auto& g : sys->games) {
                    if (g.path == m_game_path) {
                        title = g.display.empty() ? g.stem : g.display;
                        break;
                    }
                }
            }
        }
        if (title.empty()) title = m_game_stem;
        gameTitle->setText(title);
    }

    if (metaHolder) {
        metaHolder->clearViews();
        buildMetaPanel();
    }

    rebuildGalleryContent();
}

bool GameActivity::consume_rescrape_dirty() {
    if (!g_rescrape_dirty) return false;
    g_rescrape_dirty = false;
    return true;
}

}  // namespace foyer::browser
