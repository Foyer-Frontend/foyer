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
    m_downloads_mode = 0;
    // Re-focus the Downloads sidebar entry (index 3 in
    // foyer_settings.xml: General(0) / Online accounts(1) /
    // Library(2) / Downloads(3)). No re-entry loop now that
    // the swap is gated by A-press rather than focus.
    mainFrame->focusTab(3);
}

void SettingsActivity::onContentAvailable() {
    g_live = this;
    if (!m_clock_task) {
        m_clock_task = new ClockTask(this->clock);
        m_clock_task->start();
        m_clock_task->run();
    }

    // Override the Downloads sidebar item's A handler so a click
    // triggers the swap to the downloads frame instead of brls's
    // default "nav right" (move focus to the content panel). Just
    // navigating up/down past Downloads on the way to Emulators /
    // Updates / About must NOT swap — only A on the entry does.
    // The Sidebar lives inside the TabFrame with id
    // "brls/tab_frame/sidebar" (set by brls's internal XML).
    if (mainFrame) {
        // brls's TabFrame.sidebar is a Box subclass (Sidebar) — we
        // need the Box API to iterate its SidebarItem children.
        if (auto* sidebar = dynamic_cast<brls::Box*>(
                mainFrame->getView("brls/tab_frame/sidebar"))) {
            const auto& items = sidebar->getChildren();
            // Indices in foyer_settings.xml main TabFrame:
            //   0 General  · 1 Online accounts · 2 Library
            //   3 Downloads · 4 Emulators · 5 Updates
            //   6 Separator (not a SidebarItem, but counted by
            //     Box::getChildren) · 7 About
            // The Separator isn't focusable so it can't be hit;
            // the index for Downloads is 3.
            constexpr std::size_t kDownloadsIdx = 3;
            if (items.size() > kDownloadsIdx && items[kDownloadsIdx]) {
                items[kDownloadsIdx]->registerAction(
                    "Open", brls::BUTTON_A,
                    [](brls::View*) {
                        SettingsActivity::enter_downloads_mode();
                        return true;
                    }, false, false, brls::SOUND_CLICK);
            }
        }
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
