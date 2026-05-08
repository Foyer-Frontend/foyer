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

// One carousel tile. Pure image — the alekfull-nx splash fills the
// tile entirely, no text overlay. Tiles for systems without a splash
// fall back to a flat-coloured rectangle (no label).
class SystemTile : public brls::Box {
public:
    SystemTile(std::string_view folder, std::string_view /*label*/) {
        this->setWidth(220.0f);
        this->setHeight(220.0f);
        this->setMargins(0.0f, 12.0f, 0.0f, 12.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));

        auto* img = new brls::Image();
        img->setWidthPercentage(100.0f);
        img->setHeightPercentage(100.0f);
        img->setScalingType(brls::ImageScalingType::FILL);
        const std::string path =
            "themes/foyer/systems/" + std::string(folder) + "/splash.png";
        img->setImageFromRes(path);
        this->addView(img);
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
