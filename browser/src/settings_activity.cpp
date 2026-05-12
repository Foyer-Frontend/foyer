#include "activity/settings_activity.hpp"

#include "activity/download_queue_activity.hpp"

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

        // Y on any Settings tab opens the download queue overlay.
        // (X was a duplicate binding — dropped per user request so
        // only one shortcut surfaces in the hint bar.)
        content->registerAction("Downloads", brls::BUTTON_Y,
            [](brls::View*) {
                brls::Application::pushActivity(
                    new DownloadQueueActivity(),
                    brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_CLICK);

        // brls's AppletFrame footer (BottomBar) ships its own
        // time / battery / wifi cluster. Settings already shows
        // those in the custom top status row, so flip the
        // footer duplicates GONE — hint pills stay since they
        // live in a separate Box.
        for (const char* id : {"brls/hints/time",
                               "brls/battery",
                               "brls/wireless"}) {
            if (auto* v = content->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
}

}  // namespace foyer::browser
