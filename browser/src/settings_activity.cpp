#include "activity/settings_activity.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace brls::literals;

namespace foyer::browser {

namespace {

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

}  // namespace

SettingsActivity::~SettingsActivity() {
    if (m_clock_task) {
        m_clock_task->stop();
        delete m_clock_task;
        m_clock_task = nullptr;
    }
}

void SettingsActivity::onContentAvailable() {
    if (!m_clock_task) {
        m_clock_task = new ClockTask(this->clock);
        m_clock_task->start();
        m_clock_task->run();
    }

    // brls pushed activities don't auto-pop on B — AppletFrame's
    // built-in B action only dismisses inner content views. Wire
    // B to pop the whole activity so the user lands back on Home.
    if (auto* content = this->getContentView()) {
        content->registerAction("hints/back"_i18n, brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

}  // namespace foyer::browser
