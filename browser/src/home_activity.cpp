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
    wireSettingsButton();
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

void HomeActivity::wireSettingsButton() {
    if (!btnSettings) return;
    btnSettings->registerClickAction([](brls::View* v) {
        brls::Application::pushActivity(new SettingsActivity());
        return true;
    });
}

}  // namespace foyer::browser
