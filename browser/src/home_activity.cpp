#include "activity/home_activity.hpp"
#include "activity/settings_activity.hpp"
#include "activity/system_activity.hpp"

#include "library/system_db.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// One carousel tile.
// - Image sized via setPositionType(ABSOLUTE) + 100% width/height so
//   the splash fills the tile regardless of how yoga sizes the box.
// - addView is called BEFORE setImageFromRes so the Image has a
//   parent (and an NVG context) when the texture is allocated.
// - TapGestureRecognizer added in addition to registerClickAction so
//   touch taps on the tile fire the same handler that controller A
//   does (registerClickAction alone only binds the gamepad button).
// - On focus gain the tile pings its owning HomeActivity so the
//   per-system app backdrop swaps to match.
class SystemTile : public brls::Box {
public:
    SystemTile(HomeActivity* host, std::string_view folder,
               std::string_view label)
        : m_host(host), m_folder(folder), m_label(label)
    {
        constexpr float kSize = 280.0f;
        this->setWidth(kSize);
        this->setHeight(kSize);
        this->setMargins(0.0f, 7.0f, 0.0f, 7.0f);  // halved (was 14)
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));

        auto* img = new brls::Image();
        img->setPositionType(brls::PositionType::ABSOLUTE);
        img->setPositionTop(0.0f);
        img->setPositionLeft(0.0f);
        img->setWidthPercentage(100.0f);
        img->setHeightPercentage(100.0f);
        img->setScalingType(brls::ImageScalingType::FILL);
        this->addView(img);  // attach BEFORE loading the texture
        const std::string path =
            "themes/foyer/systems/" + std::string(folder) + "/splash.png";
        img->setImageFromRes(path);

        this->registerClickAction([this](brls::View*) {
            brls::Application::pushActivity(
                new SystemActivity(m_folder, m_label));
            return true;
        });
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
    }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (m_host) m_host->setBackdrop(m_folder);
    }

private:
    HomeActivity* m_host;
    std::string   m_folder;
    std::string   m_label;
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

void HomeActivity::onContentAvailable() {
    if (!clockTask) {
        clockTask = new ClockTask(this->clock);
        clockTask->start();
        clockTask->run();
    }
    populateCarousel();
    buildActionRow();
    buildProfiles();
}

void HomeActivity::populateCarousel() {
    if (!carousel) return;
    // Static system list for Phase C — real library scan + per-tile
    // game-count badge come back in alpha.5 when scanner.cpp is
    // re-introduced into the brls build.
    for (const auto& sys : ::foyer::library::all_systems()) {
        const std::string label = sys.short_name.empty()
            ? std::string(sys.folder_name)
            : std::string(sys.short_name);
        carousel->addView(new SystemTile(this, sys.folder_name, label));
    }
}

void HomeActivity::setBackdrop(std::string_view folder) {
    if (!backdrop) return;
    const std::string path =
        "themes/foyer/systems/" + std::string(folder) + "/background.jpg";
    backdrop->setImageFromRes(path);
}

void HomeActivity::buildProfiles() {
    if (!profiles) return;

    // Active-user placeholder. Phase F wires libnx accountsService
    // and pulls real avatar JPEGs (the legacy hos_status path), but
    // for now a single accent-coloured circle marks the slot so the
    // HOS-style top-left "profile" affordance reads correctly.
    auto* avatar = new brls::Box();
    constexpr float kAvatarSize = 44.0f;
    avatar->setWidth(kAvatarSize);
    avatar->setHeight(kAvatarSize);
    avatar->setCornerRadius(kAvatarSize * 0.5f);
    avatar->setBackgroundColor(nvgRGB(0x4C, 0xA9, 0xE7));  // brls accent-ish
    avatar->setMargins(0.0f, 12.0f, 0.0f, 0.0f);
    profiles->addView(avatar);
}

namespace {

// Build one HOS-style round action button with the given icon path
// (relative to romfs:/) and click handler. Used for the Home action
// row — every entry there shares the same shape, so factoring keeps
// the dispatch table at the call site readable.
brls::Box* make_action_button(const std::string& icon_res,
                              std::function<bool(brls::View*)> on_click)
{
    constexpr float kBtnSize  = 72.0f;
    constexpr float kIconSize = 44.0f;

    auto* btn = new brls::Box();
    btn->setWidth(kBtnSize);
    btn->setHeight(kBtnSize);
    btn->setMargins(0.0f, 12.0f, 0.0f, 12.0f);
    btn->setFocusable(true);
    btn->setHighlightCornerRadius(kBtnSize * 0.5f);
    btn->setCornerRadius(kBtnSize * 0.5f);
    btn->setBackgroundColor(nvgRGB(45, 55, 75));
    btn->setJustifyContent(brls::JustifyContent::CENTER);
    btn->setAlignItems(brls::AlignItems::CENTER);

    auto* icon = new brls::Image();
    icon->setWidth(kIconSize);
    icon->setHeight(kIconSize);
    icon->setScalingType(brls::ImageScalingType::FIT);
    btn->addView(icon);
    icon->setImageFromRes(icon_res);

    btn->registerClickAction(on_click);
    btn->addGestureRecognizer(new brls::TapGestureRecognizer(btn));
    return btn;
}

}  // namespace

void HomeActivity::buildActionRow() {
    if (!actionRow) return;

    // HOS-style 5-button action cluster (centred horizontally by
    // the parent Box's justifyContent). Settings is the only one
    // wired up so far; the others log a "coming soon" debug line
    // and fire no action — handlers land in alpha.E.
    actionRow->addView(make_action_button("img/actions/news.png",
        [](brls::View*) {
            brls::Logger::info("foyer: News action — coming soon");
            return true;
        }));
    actionRow->addView(make_action_button("img/actions/eshop.png",
        [](brls::View*) {
            brls::Logger::info("foyer: eShop action — coming soon");
            return true;
        }));
    actionRow->addView(make_action_button("img/actions/gallery.png",
        [](brls::View*) {
            brls::Logger::info("foyer: Gallery action — coming soon");
            return true;
        }));
    actionRow->addView(make_action_button("img/actions/settings.png",
        [](brls::View*) {
            brls::Application::pushActivity(new SettingsActivity());
            return true;
        }));
    actionRow->addView(make_action_button("img/actions/power.png",
        [](brls::View*) {
            brls::Logger::info("foyer: Power action — coming soon");
            return true;
        }));
}

}  // namespace foyer::browser
