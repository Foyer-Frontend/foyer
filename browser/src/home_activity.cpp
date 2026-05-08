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

// One carousel tile. Programmatic-only Image (no XML — the demo uses
// the image="@res/..." attribute and CaptionedImage forwards it to
// brls::Image::setImageFromRes; we replicate that in code so we can
// feed the path from foyer's system_db). Image is sized in absolute
// pixels because percentage sizing relies on yoga having computed
// the parent's size, which doesn't always happen before draw().
class SystemTile : public brls::Box {
public:
    SystemTile(std::string_view folder, std::string_view label) {
        constexpr float kSize = 280.0f;
        this->setWidth(kSize);
        this->setHeight(kSize);
        this->setMargins(0.0f, 14.0f, 0.0f, 14.0f);
        this->setFocusable(true);
        this->setHighlightCornerRadius(6.0f);
        this->setBackgroundColor(nvgRGB(40, 50, 70));

        auto* img = new brls::Image();
        img->setWidth(kSize);
        img->setHeight(kSize);
        img->setScalingType(brls::ImageScalingType::FIT);
        const std::string path =
            "themes/foyer/systems/" + std::string(folder) + "/splash.png";
        img->setImageFromRes(path);
        this->addView(img);

        // Click → push SystemActivity for this system. Phase D ships
        // the stub; library scan + game list arrive in alpha.8.
        m_folder = folder;
        m_label  = label;
        this->registerClickAction([this](brls::View*) {
            brls::Application::pushActivity(
                new SystemActivity(m_folder, m_label));
            return true;
        });
    }

private:
    std::string m_folder;
    std::string m_label;
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
