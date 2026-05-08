#include "activity/system_activity.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/label.hpp>

namespace foyer::browser {

SystemActivity::SystemActivity(std::string_view folder,
                               std::string_view display_name)
    : m_folder(folder)
    , m_display_name(display_name)
{
    // Build content programmatically — small enough that an XML file
    // would just add ceremony. AppletFrame gives us the HOS settings-
    // style header + footer with hint bar; the rom list will hang off
    // its content view in alpha.8 once the library scanner is wired
    // back into the brls build.
    auto* placeholder = new brls::Label();
    placeholder->setText("Game list arrives in the next alpha.");
    placeholder->setFontSize(24.0f);
    placeholder->setMargins(48.0f, 0.0f, 0.0f, 48.0f);

    // AppletFrame::setContentView is protected, but the constructor
    // takes a content view directly — that's the public path.
    auto* frame = new brls::AppletFrame(placeholder);
    frame->setTitle(m_display_name);

    this->setContentView(frame);
}

void SystemActivity::onContentAvailable() {
    // Reserved for the alpha.8 wiring: kick off library::scan() for
    // m_folder, populate the RecyclerFrame, etc.
}

}  // namespace foyer::browser
