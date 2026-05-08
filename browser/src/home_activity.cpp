#include "activity/home_activity.hpp"
#include "activity/settings_activity.hpp"

#include "library/system_db.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// One carousel tile. Layered: full-size brls Image with the
// alekfull-nx splash, plus a Label fallback that shows the system's
// short name when the splash is missing. The image's setScalingType
// fills the tile, preserving the splash's intended composition.
class SystemTile : public brls::Box {
public:
    SystemTile(std::string_view folder, std::string_view label) {
        this->setWidth(220.0f);
        this->setHeight(220.0f);
        this->setMargins(0.0f, 12.0f, 0.0f, 12.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);

        // Splash image — alekfull-nx ships portrait splash.png per
        // system at romfs:/themes/alekfull-nx/systems/<folder>/.
        auto* img = new brls::Image();
        img->setWidthPercentage(100.0f);
        img->setHeightPercentage(100.0f);
        img->setScalingType(brls::ImageScalingType::FILL);
        img->setPositionType(brls::PositionType::ABSOLUTE);
        img->setPositionTop(0.0f);
        img->setPositionLeft(0.0f);
        std::string path =
            "themes/alekfull-nx/systems/" + std::string(folder) + "/splash.png";
        img->setImageFromRes(path);
        this->addView(img);

        // Text fallback — sits behind the image. If the image loads,
        // it covers the label; if it fails, the label shows through.
        auto* lbl = new brls::Label();
        lbl->setText(std::string{label});
        lbl->setFontSize(36.0f);
        lbl->setTextColor(nvgRGB(0xFF, 0xFF, 0xFF));
        this->addView(lbl);
    }
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
        carousel->addView(new SystemTile(sys.folder_name, label));
    }
}

void HomeActivity::wireSettingsButton() {
    if (!btnSettings) return;
    btnSettings->registerClickAction([](brls::View* v) {
        brls::Application::pushActivity(new SettingsActivity());
        return true;
    });
}

}  // namespace foyer::browser
