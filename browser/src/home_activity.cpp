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

// One carousel tile. brls Box with a colored background and the
// system's short name centred. Replaces the legacy nanovg-direct
// tile painting.
class SystemTile : public brls::Box {
public:
    SystemTile(std::string_view label) {
        this->setWidth(220.0f);
        this->setHeight(220.0f);
        this->setMargins(0.0f, 12.0f, 0.0f, 12.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));
        this->setJustifyContent(brls::JustifyContent::CENTER);
        this->setAlignItems(brls::AlignItems::CENTER);

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
        carousel->addView(new SystemTile(label));
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
