#include "activity/home_activity.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace foyer::browser {

namespace {

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
    if (clockTask) return;
    clockTask = new ClockTask(this->clock);
    clockTask->start();
    // First tick immediately so the label doesn't show "--:--" for a
    // full second after the activity appears.
    clockTask->run();
}

}  // namespace foyer::browser
