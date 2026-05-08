#include "activity/system_activity.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/label.hpp>

using namespace brls::literals;

namespace foyer::browser {

SystemActivity::SystemActivity(std::string_view folder,
                               std::string_view display_name)
    : m_folder(folder)
    , m_display_name(display_name)
{
}

brls::View* SystemActivity::createContentView() {
    auto* placeholder = new brls::Label();
    placeholder->setText("Game list arrives in the next alpha.");
    placeholder->setFontSize(24.0f);
    placeholder->setMargins(48.0f, 0.0f, 0.0f, 48.0f);

    auto* frame = new brls::AppletFrame(placeholder);
    frame->setTitle(m_display_name);

    // brls pushed activities don't auto-pop on B — AppletFrame's
    // built-in B action only dismisses content views inside its
    // own stack. Wire B to pop the whole activity off the
    // Application stack so the user lands back on Home.
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    return frame;
}

void SystemActivity::onContentAvailable() {
    // Reserved for alpha.8: kick off library::scan() for m_folder,
    // populate the RecyclerFrame, etc.
}

}  // namespace foyer::browser
