#include "activity/game_activity.hpp"

#include "launch.hpp"
#include "library_state.hpp"
#include "library/per_game.hpp"
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
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Per-game rescrape worker. Survives the activity popping so the
// download keeps running while the user is back on Home or the
// system view. unique_ptr so subsequent rescrapes get a clean
// instance instead of touching a half-finished Worker.
std::unique_ptr<::foyer::library::Worker> g_rescrape_worker;

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
    // Derive stem from game_path: strip directory + extension.
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
}

void GameActivity::onContentAvailable() {
    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);

    // Fanart background. JPEG without a region tag.
    if (fanart) {
        const std::string fart = bundle + "fanart.jpg";
        struct stat st{};
        if (::stat(fart.c_str(), &st) == 0) {
            fanart->setImageFromFile(fart);
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
    if (gameTitle) gameTitle->setText(title);

    buildGallery();

    if (clock && !m_clockTask) {
        m_clockTask = new ClockTask(this->clock);
        m_clockTask->start();
        m_clockTask->run();
    }

    // No body background — the title + screenshots now float on
    // the fanart underneath. The previous translucent theme
    // backdrop read as a hard black/white surround on both
    // themes; letting fanart show through keeps the layout
    // cleaner and matches the rest of the views.
    if (body) {
        body->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    }

    buildMetaPanel();

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

        // Y — Rescrape this single rom. Drops the bundle's
        // metadata.json (cache marker) + the legacy cover, then
        // kicks a one-shot Worker that calls SS for this rom.
        cv->registerAction(
            "Rescrape", brls::BUTTON_Y,
            [folder, path_copy, stem_copy](brls::View*) {
                if (g_rescrape_worker && g_rescrape_worker->active()
                    && !g_rescrape_worker->done()) {
                    brls::Application::notify("Rescrape already running");
                    return true;
                }
                if (g_rescrape_worker) {
                    if (g_rescrape_worker->done()) g_rescrape_worker->finish();
                    g_rescrape_worker.reset();
                }

                const auto bundle = ::foyer::scrapers::game_asset_dir(
                    folder, stem_copy);
                ::unlink((bundle + "metadata.json").c_str());
                const auto legacy = ::foyer::scrapers::cover_path(
                    folder, stem_copy);
                ::unlink(legacy.c_str());

                g_rescrape_worker = std::make_unique<::foyer::library::Worker>();
                g_rescrape_worker->start(
                    [folder, path_copy, stem_copy](::foyer::library::Worker& w) {
                        w.set_status("Rescraping…");
                        const auto dest = ::foyer::scrapers::cover_path(
                            folder, stem_copy);
                        const bool ok =
                            ::foyer::scrapers::screenscraper::fetch_cover(
                                folder, path_copy, stem_copy, dest);
                        // Worker bodies run off the UI thread, so
                        // route the completion toast through
                        // brls::sync — Application::notify mutates
                        // brls's notification list which isn't
                        // thread-safe.
                        const std::string stem_done = stem_copy;
                        brls::sync([stem_done, ok]() {
                            brls::Application::notify(ok
                                ? std::string("Rescraped ") + stem_done
                                : std::string("Rescrape failed for ") + stem_done);
                        });
                    }, 0x100000);
                brls::Application::notify("Rescraping " + stem_copy + "…");
                return true;
            }, false, false, brls::SOUND_CLICK);

        // + (Start) — Per-game settings (placeholder for now).
        cv->registerAction(
            "Settings", brls::BUTTON_START,
            [](brls::View*) {
                auto* dlg = new brls::Dialog(
                    "Per-game settings — core override, runahead, "
                    "shader pick, save-state slot. Wired in a "
                    "later alpha.");
                dlg->addButton("hints/ok"_i18n, []() {});
                dlg->open();
                return true;
            }, false, false, brls::SOUND_CLICK);
    }
}

void GameActivity::buildGallery() {
    if (!slide || !galleryHolder) return;
    const auto bundle = ::foyer::scrapers::game_asset_dir(
        m_system_folder, m_game_stem);

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
    }

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
    // Aspect-fit instead of stretching — ScreenScraper screenshots
    // come in at the source console's resolution and the gallery
    // slot is 16:9, so a stretch chops/stretches the frame.
    slide->setScalingType(brls::ImageScalingType::FIT);
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
        auto theme = brls::Application::getTheme();
        hdr->setTextColor(theme.getColor("brls/text_disabled"));
        metaHolder->addView(hdr);

        auto* body = new brls::Label();
        body->setText(synopsis);
        body->setFontSize(18.0f);
        body->setMaxWidth(760.0f);
        body->setTextColor(theme.getColor("brls/text"));
        metaHolder->addView(body);
    }
}

}  // namespace foyer::browser
