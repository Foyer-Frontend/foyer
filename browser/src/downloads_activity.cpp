#include "activity/downloads_activity.hpp"

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

DownloadsActivity::~DownloadsActivity() {
    if (m_clock_task) {
        m_clock_task->stop();
        delete m_clock_task;
        m_clock_task = nullptr;
    }
}

void DownloadsActivity::onContentAvailable() {
    if (!m_clock_task) {
        m_clock_task = new ClockTask(this->clock);
        m_clock_task->start();
        m_clock_task->run();
    }

    if (auto* content = this->getContentView()) {
        content->registerAction("hints/back"_i18n, brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);

        // Y opens the download queue overlay — same binding shape
        // SettingsActivity used, but scoped to this Downloads-only
        // surface so it's contextually clear what the queue is
        // showing (downloads in flight, not Settings activity).
        content->registerAction("Download queue", brls::BUTTON_Y,
            [](brls::View*) {
                brls::Application::pushActivity(
                    new DownloadQueueActivity(),
                    brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_CLICK);

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
