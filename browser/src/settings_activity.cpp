#include "activity/settings_activity.hpp"

using namespace brls::literals;

namespace foyer::browser {

void SettingsActivity::onContentAvailable() {
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
