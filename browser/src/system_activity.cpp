#include "activity/system_activity.hpp"

#include "activity/game_activity.hpp"
#include "library_state.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

using namespace brls::literals;

namespace foyer::browser {

SystemActivity::SystemActivity(std::string_view folder,
                               std::string_view display_name)
    : m_folder(folder)
    , m_display_name(display_name)
{
}

brls::View* SystemActivity::createContentView() {
    const auto* sys = library_state::find_system(m_folder);

    brls::View* content = nullptr;

    if (!sys || sys->games.empty()) {
        auto* placeholder = new brls::Label();
        placeholder->setText(sys
            ? std::string("No games in this system folder yet.")
            : std::string("This system folder is empty or absent on disk."));
        placeholder->setFontSize(20.0f);
        placeholder->setMargins(48.0f, 0.0f, 0.0f, 48.0f);
        content = placeholder;
    } else {
        auto* scroll = new brls::ScrollingFrame();
        auto* list   = new brls::Box();
        list->setAxis(brls::Axis::COLUMN);
        list->setMargins(16.0f, 32.0f, 16.0f, 32.0f);

        const std::string folder = m_folder;
        for (const auto& g : sys->games) {
            auto* cell = new brls::DetailCell();
            cell->title->setText(g.display);
            cell->detail->setText(g.filename);
            const std::string path = g.path;
            cell->registerClickAction(
                [folder, path](brls::View*) {
                    brls::Application::pushActivity(
                        new GameActivity(folder, path));
                    return true;
                });
            list->addView(cell);
        }
        scroll->setContentView(list);
        content = scroll;
    }

    auto* frame = new brls::AppletFrame(content);
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
    // No deferred wiring needed — game list populates synchronously
    // inside createContentView() from library_state's cached scan.
}

}  // namespace foyer::browser
