#include "activity/system_activity.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/label.hpp>

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
    return frame;
}

void SystemActivity::onContentAvailable() {
    // Reserved for alpha.8: kick off library::scan() for m_folder,
    // populate the RecyclerFrame, etc.
}

}  // namespace foyer::browser
