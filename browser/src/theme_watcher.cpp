#include "theme_watcher.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"
#include "theme_change.hpp"

#include <borealis.hpp>
#include <switch.h>

#include <mutex>
#include <utility>
#include <vector>

namespace foyer::browser::theme_watcher {

namespace {

class WatcherTask : public brls::RepeatingTask {
public:
    WatcherTask() : brls::RepeatingTask(1000) {}
    void run() override {
        const auto& cfg = ::foyer::library::config();
        const std::string& ov = cfg.theme_override;

        brls::ThemeVariant want;
        if (ov == "light") {
            want = brls::ThemeVariant::LIGHT;
        } else if (ov == "dark") {
            want = brls::ThemeVariant::DARK;
        } else {
            ColorSetId id;
            if (R_FAILED(setsysGetColorSetId(&id))) return;
            want = (id == ColorSetId_Dark)
                ? brls::ThemeVariant::DARK
                : brls::ThemeVariant::LIGHT;
        }

        if (brls::Application::getThemeVariant() != want) {
            foyer::log::write(
                "[theme_watcher] applying %s (override=%s)\n",
                want == brls::ThemeVariant::DARK ? "DARK" : "LIGHT",
                ov.empty() ? "auto" : ov.c_str());
            brls::Application::getPlatform()->setThemeVariant(want);
            // Fire the foyer-side notifier so widgets that sample
            // theme colours at construction (ActionButton's idle bg
            // / icon tint, SystemTile's bg, …) re-sample now that
            // the variant has flipped. brls's native XML attrs
            // (textColor="@theme/…") stay frozen because brls caches
            // resolved colors at parse time — covered by a brls
            // patch in a separate change.
            ::foyer::browser::theme_change::notify(want);
        }
    }
};

WatcherTask* g_task = nullptr;

}  // namespace

// ---- theme_change broadcaster -------------------------------------------

}  // namespace foyer::browser::theme_watcher

namespace foyer::browser::theme_change {

namespace {
std::mutex                          g_hooks_mu;
std::vector<std::pair<int, Hook>>   g_hooks;
int                                 g_next_id = 1;
}

int subscribe(Hook hook) {
    std::scoped_lock lk{g_hooks_mu};
    const int id = g_next_id++;
    g_hooks.emplace_back(id, std::move(hook));
    return id;
}

void unsubscribe(int id) {
    if (id < 0) return;
    std::scoped_lock lk{g_hooks_mu};
    for (auto it = g_hooks.begin(); it != g_hooks.end(); ++it) {
        if (it->first == id) { g_hooks.erase(it); return; }
    }
}

void notify(brls::ThemeVariant variant) {
    // Snapshot under lock so a hook that calls unsubscribe doesn't
    // invalidate the iterator.
    std::vector<std::pair<int, Hook>> copy;
    {
        std::scoped_lock lk{g_hooks_mu};
        copy = g_hooks;
    }
    auto fire = [copy = std::move(copy), variant]() {
        for (auto& [_, h] : copy) if (h) h(variant);
    };
    // notify() runs on the WatcherTask which is brls::RepeatingTask
    // — already on the UI thread — but we route through brls::sync
    // anyway so external callers (e.g. a desktop preview) are safe.
    brls::sync(fire);
}

}  // namespace foyer::browser::theme_change

namespace foyer::browser::theme_watcher {

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
