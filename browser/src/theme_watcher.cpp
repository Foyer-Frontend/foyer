#include "theme_watcher.hpp"

#include "platform/log.hpp"

#include <borealis.hpp>
#include <switch.h>

namespace foyer::browser::theme_watcher {

namespace {

class WatcherTask : public brls::RepeatingTask {
public:
    WatcherTask() : brls::RepeatingTask(1000) {}
    void run() override {
        ColorSetId id;
        if (R_FAILED(setsysGetColorSetId(&id))) return;

        const brls::ThemeVariant want = (id == ColorSetId_Dark)
            ? brls::ThemeVariant::DARK
            : brls::ThemeVariant::LIGHT;

        if (brls::Application::getThemeVariant() != want) {
            foyer::log::write(
                "[theme_watcher] HOS theme changed to %s — applying\n",
                want == brls::ThemeVariant::DARK ? "DARK" : "LIGHT");
            brls::Application::getPlatform()->setThemeVariant(want);
        }
    }
};

WatcherTask* g_task = nullptr;

}  // namespace

void start() {
    if (g_task) return;
    g_task = new WatcherTask();
    g_task->start();
}

void stop() {
    if (!g_task) return;
    g_task->stop();
    delete g_task;
    g_task = nullptr;
}

} // namespace foyer::browser::theme_watcher
