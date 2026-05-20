#include "activity/home_activity.hpp"
#include "activity/power_activity.hpp"
#include "activity/search_activity.hpp"
#include "activity/settings_activity.hpp"
#include "activity/system_activity.hpp"
#include "install_queue.hpp"
#include "mtp.hpp"
#include "theme_watcher.hpp"
#include "update_check.hpp"

#include "hos_status.hpp"
#include "library/asset_pack.hpp"
#include "library/config.hpp"
#include "library_state.hpp"
#include "library/system_db.hpp"
#include "platform/log.hpp"
#include "widgets/action_button.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Map a SystemDef folder name to the on-disk art directory name
// under themes/foyer/systems/. Real systems use their folder name
// verbatim; virtuals route to the "auto-*" assets the theme
// packs ship for those.
std::string art_dir_for(std::string_view folder) {
    if (folder == "__recent")    return "auto-lastplayed";
    if (folder == "__favorites") return "auto-favorites";
    if (folder == "__allgames")  return "auto-allgames";
    return std::string{folder};
}

// One carousel tile.
//
// Square focusable Box with the splash image as a child. On focus the
// tile pings its owning HomeActivity so the per-system backdrop swaps
// to match, and reveals a translucent banner along the bottom edge
// with the live game count for that system.
class SystemTile : public brls::Box {
public:
    SystemTile(HomeActivity* host, std::string_view folder,
               std::string_view title, std::size_t game_count)
        : m_host(host), m_folder(folder), m_title(title)
    {
        constexpr float kSquare    = 240.0f;
        constexpr float kBannerH   = 42.0f;

        this->setWidth(kSquare);
        this->setHeight(kSquare);
        this->setMargins(0.0f, 7.0f, 0.0f, 7.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));

        auto* img = new brls::Image();
        img->setWidth(kSquare);
        img->setHeight(kSquare);
        img->setScalingType(brls::ImageScalingType::FILL);
        this->addView(img);
        const std::string path =
            ::foyer::library::asset_system_splash(art_dir_for(folder));
        foyer::log::write("[home] tile %.*s splash=%s\n",
            (int)folder.size(), folder.data(), path.c_str());
        img->setImageFromFile(path);

        // Translucent count banner overlaid along the bottom edge.
        m_banner = new brls::Box();
        m_banner->setPositionType(brls::PositionType::ABSOLUTE);
        m_banner->setPositionLeft(0.0f);
        m_banner->setPositionBottom(0.0f);
        m_banner->setWidth(kSquare);
        m_banner->setHeight(kBannerH);
        m_banner->setBackgroundColor(nvgRGBA(0, 0, 0, 160));
        m_banner->setJustifyContent(brls::JustifyContent::CENTER);
        m_banner->setAlignItems(brls::AlignItems::CENTER);

        const std::string label = std::to_string(game_count)
            + (game_count == 1 ? " game" : " games");
        auto* count = new brls::Label();
        count->setText(label);
        count->setFontSize(18.0f);
        count->setTextColor(nvgRGB(0xEC, 0xEC, 0xEC));
        m_banner->addView(count);
        this->addView(m_banner);
        m_banner->setVisibility(brls::Visibility::INVISIBLE);

        this->registerClickAction([this](brls::View*) {
            brls::Application::pushActivity(
                new SystemActivity(m_folder, m_title));
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (m_banner) m_banner->setVisibility(brls::Visibility::VISIBLE);
        if (m_host) m_host->onSystemFocused(m_folder, m_title);
    }

    void onFocusLost() override {
        brls::Box::onFocusLost();
        if (m_banner) m_banner->setVisibility(brls::Visibility::INVISIBLE);
    }

private:
    HomeActivity* m_host;
    std::string   m_folder;
    std::string   m_title;
    brls::Box*    m_banner = nullptr;
};

class ClockTask : public brls::RepeatingTask {
public:
    ClockTask(brls::Label* label) : brls::RepeatingTask(1000), label(label) {}
    void run() override {
        if (!label) return;
        const auto now  = std::chrono::system_clock::now();
        const auto t    = std::chrono::system_clock::to_time_t(now);
        std::tm tm      = *std::localtime(&t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%H:%M");
        label->setText(ss.str());
    }
private:
    brls::Label* label;
};

}  // namespace

HomeActivity::~HomeActivity() {
    if (clockTask) {
        clockTask->stop();
        delete clockTask;
        clockTask = nullptr;
    }
}

void HomeActivity::onContentAvailable() {
    if (!clockTask) {
        clockTask = new ClockTask(this->clock);
        clockTask->start();
        clockTask->run();
    }
    populateCarousel();
    m_library_gen = library_state::generation();
    foyer::log::write("[home] populateCarousel done\n");
    buildActionRow();
    foyer::log::write("[home] buildActionRow done\n");
    buildProfiles();
    foyer::log::write("[home] buildProfiles done\n");

    // Default focus to the first system tile, not the profile avatar.
    // Activity::getDefaultFocus() walks descendants front-to-back and
    // the profile cluster comes first in the XML, so giveFocus is the
    // direct way to override.
    //
    // Chain-back-from-core path: main.cpp's fast_returned branch
    // called setPreselectSystem() with the folder the user was last
    // in. Walk library_state::systems() to find its index (matches
    // populateCarousel's build order), then focus that tile so
    // B-back from the restored SystemActivity drops the user on
    // exactly where they were before launching the core.
    if (carousel && !carousel->getChildren().empty()) {
        std::size_t target = 0;
        if (!m_preselect_folder.empty()) {
            std::size_t i = 0;
            for (const auto& sys : library_state::systems()) {
                if (!sys.def) continue;
                if (sys.def->folder_name == m_preselect_folder) {
                    target = i;
                    break;
                }
                i++;
            }
        }
        const auto& kids = carousel->getChildren();
        if (target >= kids.size()) target = 0;
        brls::Application::giveFocus(kids[target]);
        foyer::log::write("[home] gave focus to tile %zu (preselect=%s)\n",
            target, m_preselect_folder.c_str());
    }

    // B on Home prompts for quit. brls::Application::setGlobalQuit
    // (called from main) was disabled so B can't accidentally exit;
    // this gives the user an explicit confirmation path instead.
    if (auto* cv = this->getContentView()) {
        // L/R: page-jump in the system carousel by viewport-fits
        // count, mirroring SystemActivity. Tiles here are 250px
        // square, +14px margins → ~264px pitch.
        auto jump_focus = [this](int delta) {
            if (!carousel) return false;
            const auto& kids = carousel->getChildren();
            if (kids.empty()) return false;
            auto* focus = brls::Application::getCurrentFocus();
            int cur = -1;
            for (int i = 0; i < (int)kids.size(); i++) {
                if (kids[i] == focus) { cur = i; break; }
            }
            if (cur < 0) cur = 0;
            const int n = (int)kids.size();
            const int next = ((cur + delta) % n + n) % n;
            brls::Application::giveFocus(kids[next]);
            return true;
        };
        auto page_size = []() {
            constexpr float kPitch = 189.0f;
            constexpr float kViewport = 1280.0f;
            int n = (int)(kViewport / kPitch);
            if (n < 1) n = 1;
            return n;
        };
        cv->registerAction(
            "Prev page", brls::BUTTON_LB,
            [jump_focus, page_size](brls::View*) {
                return jump_focus(-page_size());
            },
            false, true, brls::SOUND_FOCUS_CHANGE);
        cv->registerAction(
            "Next page", brls::BUTTON_RB,
            [jump_focus, page_size](brls::View*) {
                return jump_focus(+page_size());
            },
            false, true, brls::SOUND_FOCUS_CHANGE);

        cv->registerAction(
            "Sort", brls::BUTTON_BACK,
            [this](brls::View*) {
                using M = ::foyer::library::Config::SystemSortMode;
                M cur = ::foyer::library::config().system_sort_mode;
                M nxt = cur;
                const char* label = "";
                switch (cur) {
                    case M::ScannerOrder: nxt = M::Alphabetical; label = "Alphabetical"; break;
                    case M::Alphabetical: nxt = M::GameCount;    label = "Game count";   break;
                    case M::GameCount:    nxt = M::Custom;       label = "Custom order"; break;
                    case M::Custom:
                    default:              nxt = M::ScannerOrder; label = "Scanner order"; break;
                }
                ::foyer::library::set_system_sort_mode(nxt);
                library_state::rescan();
                if (this->carousel) {
                    this->carousel->clearViews();
                    populateCarousel();
                }
                brls::Application::notify(std::string("Sort: ") + label);
                return true;
            }, false, false, brls::SOUND_FOCUS_CHANGE);

        cv->registerAction(
            "Quit", brls::BUTTON_B,
            [](brls::View*) {
                auto* dlg = new brls::Dialog("Quit foyer?");
                dlg->addButton("No",  []() {});
                dlg->addButton("Yes", []() {
                    // Drain any background work before brls tears
                    // down — a still-running scrape worker on a
                    // detached thread will read freed memory if
                    // brls quits while curl is mid-transfer.
                    foyer::log::write("[home] quit requested — draining\n");
                    SystemActivity::cancel_pending_scrape();
                    theme_watcher::stop();
                    update_check::stop();
                    install_queue::stop();
                    // libhaze holds USB DMA buffers in foyer's
                    // heap region; leaving them resident at quit
                    // races hbloader's selfExit unmap and can
                    // surface as MAKERESULT(347, 26) on the way
                    // out. Same fix as launch_game / self-update.
                    if (::foyer::browser::mtp_running()) {
                        ::foyer::browser::mtp_stop();
                    }
                    brls::Application::quit();
                });
                dlg->open();
                return true;
            }, false, false, brls::SOUND_BACK);
        foyer::log::write("[home] registered B-quit action\n");
    } else {
        foyer::log::write("[home] no content view to register B on\n");
    }
    foyer::log::write("[home] onContentAvailable done\n");
}

void HomeActivity::onResume() {
    brls::Activity::onResume();
    // If the library was rescanned (Settings, sort cycle, etc.)
    // while Home was hidden behind another activity, the carousel
    // we built on first onContentAvailable is stale. Compare the
    // generation we snapshot at build time against the current
    // one and rebuild if it moved.
    const auto gen = library_state::generation();
    if (gen != m_library_gen && carousel) {
        foyer::log::write("[home] onResume: library generation %u -> %u — repopulating\n",
            m_library_gen, gen);
        carousel->clearViews();
        populateCarousel();
        m_library_gen = gen;
    }
}

void HomeActivity::populateCarousel() {
    if (!carousel) return;
    // Walk the scanned library list rather than all_systems():
    // scanner injects the virtual "Favourites" / "Recently
    // played" entries at the front of that vector, so iterating
    // it here surfaces them as their own carousel tiles. Real
    // systems come right after the virtuals in the same order
    // all_systems() declares them.
    for (const auto& sys : library_state::systems()) {
        if (!sys.def) continue;
        const std::string label = sys.def->display_name.empty()
            ? std::string(sys.def->folder_name)
            : std::string(sys.def->display_name);
        carousel->addView(
            new SystemTile(this, sys.def->folder_name,
                           label, sys.games.size()));
    }
}

void HomeActivity::onSystemFocused(std::string_view folder,
                                   std::string_view /*display_name*/)
{
    if (backdrop) {
        backdrop->setImageFromFile(
            ::foyer::library::asset_system_background(art_dir_for(folder)));
    }
    // Focused-system logo next to the avatar — replaces the
    // centered "blue label" affordance from v0.6.98. Theme-aware
    // (dark vs light pre-rendered variants live in the asset pack).
    if (focusLogo) {
        const bool dark =
            brls::Application::getThemeVariant() == brls::ThemeVariant::DARK;
        focusLogo->setImageFromFile(
            ::foyer::library::asset_system_logo(art_dir_for(folder), dark));
    }
}

void HomeActivity::buildProfiles() {
    if (!profiles) return;

    constexpr float kAvatarSize = 44.0f;

    // Outer focusable circle. When hos_status loaded a JPEG, an
    // inner brls::Image fills it with the actual avatar; otherwise
    // the bg colour shows through as the placeholder.
    auto* avatar = new brls::Box();
    avatar->setWidth(kAvatarSize);
    avatar->setHeight(kAvatarSize);
    avatar->setCornerRadius(kAvatarSize * 0.5f);
    avatar->setHighlightCornerRadius(kAvatarSize * 0.5f);
    avatar->setBackgroundColor(nvgRGB(0x4C, 0xA9, 0xE7));
    avatar->setMargins(0.0f, 12.0f, 0.0f, 0.0f);
    avatar->setFocusable(true);
    avatar->setClipsToBounds(true);  // round-clip the inner Image

    const auto& jpeg = ::foyer::browser::hos_status::avatar_jpeg();
    foyer::log::write("[home] profile jpeg bytes=%zu nick=%s\n",
        jpeg.size(),
        ::foyer::browser::hos_status::nickname().c_str());
    if (!jpeg.empty()) {
        auto* img = new brls::Image();
        img->setWidth(kAvatarSize);
        img->setHeight(kAvatarSize);
        img->setScalingType(brls::ImageScalingType::FILL);
        avatar->addView(img);
        // setImageFromMem decodes the JPEG once into brls's
        // texture cache. Cast away const because the brls API
        // takes a non-const pointer (it doesn't actually mutate).
        img->setImageFromMem(
            const_cast<unsigned char*>(jpeg.data()),
            static_cast<int>(jpeg.size()));
    }

    avatar->registerClickAction([this](brls::View*) {
        openProfilePicker();
        return true;
    });
    avatar->addGestureRecognizer(new brls::TapGestureRecognizer(avatar));
    profiles->addView(avatar);
}

void HomeActivity::openProfilePicker() {
    namespace hs = ::foyer::browser::hos_status;
    const int n = hs::other_avatar_count();

    // No secondary profiles → keep the simple "active user" toast.
    if (n <= 0) {
        const auto& nick = hs::nickname();
        brls::Application::notify(nick.empty()
            ? std::string("No other profiles on this console")
            : ("Active user: " + nick));
        return;
    }

    // Build the picker as a vertical list of focusable rows
    // (avatar circle + nickname). One row per secondary user.
    // Dialog is constructed first so each row's click handler can
    // capture the pointer and close the dialog after switching.
    auto* list = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setPadding(24.0f, 24.0f, 24.0f, 24.0f);

    auto* header = new brls::Label();
    header->setText("Switch active profile");
    header->setFontSize(22.0f);
    header->setMargins(0.0f, 0.0f, 12.0f, 0.0f);
    list->addView(header);

    auto* dlg = new brls::Dialog(list);
    dlg->addButton("hints/back"_i18n, []() {});

    constexpr float kRowAvatar = 36.0f;
    for (int i = 0; i < n; i++) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setHeight(48.0f);
        row->setFocusable(true);
        row->setHighlightCornerRadius(6.0f);

        auto* circle = new brls::Box();
        circle->setWidth(kRowAvatar);
        circle->setHeight(kRowAvatar);
        circle->setCornerRadius(kRowAvatar * 0.5f);
        circle->setBackgroundColor(nvgRGB(0x4C, 0xA9, 0xE7));
        circle->setClipsToBounds(true);
        circle->setMargins(0.0f, 12.0f, 0.0f, 0.0f);

        const auto& bytes = hs::other_avatar_jpeg(i);
        if (!bytes.empty()) {
            auto* img = new brls::Image();
            img->setWidth(kRowAvatar);
            img->setHeight(kRowAvatar);
            img->setScalingType(brls::ImageScalingType::FILL);
            circle->addView(img);
            img->setImageFromMem(
                const_cast<unsigned char*>(bytes.data()),
                static_cast<int>(bytes.size()));
        }
        row->addView(circle);

        auto* name = new brls::Label();
        const auto& nick = hs::other_nickname(i);
        name->setText(nick.empty() ? std::string("(unnamed)") : nick);
        name->setFontSize(20.0f);
        row->addView(name);

        const int idx = i;
        row->registerClickAction([idx, dlg](brls::View*) {
            ::foyer::browser::hos_status::switch_active(
                idx, brls::Application::getNVGContext());
            const auto& new_nick = ::foyer::browser::hos_status::nickname();
            brls::Application::notify(new_nick.empty()
                ? std::string("Switched user")
                : ("Switched to " + new_nick));
            dlg->close([] {});
            return true;
        });
        row->addGestureRecognizer(new brls::TapGestureRecognizer(row));
        list->addView(row);
    }

    dlg->open();
}

void HomeActivity::buildActionRow() {
    if (!actionRow) return;

    auto coming_soon = [](const std::string& msg) {
        return [msg](brls::View*) {
            auto* dlg = new brls::Dialog(msg);
            dlg->addButton("hints/ok"_i18n, []() {});
            dlg->open();
            return true;
        };
    };

    actionRow->addView(new ActionButton("img/actions/news.png", "News",
        coming_soon("News feed — coming soon.")));
    actionRow->addView(new ActionButton("img/actions/eshop.png", "eShop",
        coming_soon("eShop chain-launch — coming soon.")));
    actionRow->addView(new ActionButton("img/actions/gallery.png", "Album",
        coming_soon("Album viewer — coming soon.")));
    actionRow->addView(new ActionButton("img/actions/search.png", "Search",
        [](brls::View*) {
            brls::Application::pushActivity(new SearchActivity());
            return true;
        }));
    actionRow->addView(new ActionButton("img/actions/settings.png", "Settings",
        [](brls::View*) {
            brls::Application::pushActivity(new SettingsActivity());
            return true;
        }));
    actionRow->addView(new ActionButton("img/actions/power.png", "Power",
        [](brls::View*) {
            brls::Application::pushActivity(new PowerActivity());
            return true;
        }));
}

}  // namespace foyer::browser
