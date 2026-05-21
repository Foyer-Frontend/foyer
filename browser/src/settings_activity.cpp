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

SettingsActivity* SettingsActivity::g_live = nullptr;

SettingsActivity::~SettingsActivity() {
    if (m_clock_task) {
        m_clock_task->stop();
        delete m_clock_task;
        m_clock_task = nullptr;
    }
    if (g_live == this) g_live = nullptr;
}

void SettingsActivity::enter_downloads_mode() {
    if (!g_live) return;
    auto* self = g_live;
    // If we're in the just-exited cooldown, consume it and stay in
    // main mode. This breaks the re-entry loop that otherwise fires
    // when exit_downloads_mode re-focuses the Downloads sidebar
    // entry and that focus immediately triggers willAppear again.
    if (self->m_downloads_mode == 2) {
        self->m_downloads_mode = 0;
        return;
    }
    if (self->m_downloads_mode == 1) return;  // already there

    if (!self->mainFrame || !self->downloadsFrame) return;
    self->mainFrame->setVisibility(brls::Visibility::GONE);
    self->downloadsFrame->setVisibility(brls::Visibility::VISIBLE);
    self->downloadsFrame->focusTab(0);   // land on Cores
    self->m_downloads_mode = 1;
}

void SettingsActivity::exit_downloads_mode() {
    if (m_downloads_mode != 1) return;
    if (!mainFrame || !downloadsFrame) return;
    downloadsFrame->setVisibility(brls::Visibility::GONE);
    mainFrame->setVisibility(brls::Visibility::VISIBLE);
    // Re-focus the main frame on the Downloads tab (index 3 in
    // foyer_settings.xml: General(0) / Online accounts(1) /
    // Library(2) / Downloads(3)). The focus will re-fire
    // FoyerDownloadsTab::willAppear which would normally re-enter
    // downloads mode — state=2 suppresses that one bounce.
    m_downloads_mode = 2;
    mainFrame->focusTab(3);
}

void SettingsActivity::onContentAvailable() {
    g_live = this;
    if (!m_clock_task) {
        m_clock_task = new ClockTask(this->clock);
        m_clock_task->start();
        m_clock_task->run();
    }

    // B button — context sensitive:
    //   downloads mode → swap back to main frame, focus Downloads
    //   main mode      → pop the activity (back to Home)
    if (auto* content = this->getContentView()) {
        content->registerAction("hints/back"_i18n, brls::BUTTON_B,
            [this](brls::View*) {
                if (m_downloads_mode == 1) {
                    exit_downloads_mode();
                } else {
                    brls::Application::popActivity();
                }
                return true;
            }, false, false, brls::SOUND_BACK);

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
