#include "activity/game_activity.hpp"

#include "launch.hpp"
#include "library_state.hpp"

#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/label.hpp>

using namespace brls::literals;

namespace foyer::browser {

GameActivity::GameActivity(std::string_view system_folder,
                           std::string_view game_path)
    : m_system_folder(system_folder)
    , m_game_path(game_path)
{
}

brls::View* GameActivity::createContentView() {
    const auto* sys = library_state::find_system(m_system_folder);
    const ::foyer::library::Game* game = nullptr;
    if (sys) {
        for (const auto& g : sys->games) {
            if (g.path == m_game_path) { game = &g; break; }
        }
    }

    auto* outer = new brls::Box();
    outer->setAxis(brls::Axis::COLUMN);
    outer->setPadding(48.0f, 48.0f, 48.0f, 48.0f);
    outer->setAlignItems(brls::AlignItems::FLEX_START);

    auto* title = new brls::Label();
    title->setText(game ? game->display : std::string("Unknown game"));
    title->setFontSize(36.0f);
    title->setMarginBottom(8.0f);
    outer->addView(title);

    auto* path = new brls::Label();
    path->setText(m_game_path);
    path->setFontSize(18.0f);
    path->setMarginBottom(32.0f);
    outer->addView(path);

    auto* play = new brls::Button();
    play->setText("hints/play"_i18n);
    play->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    const std::string folder = m_system_folder;
    const std::string path_copy = m_game_path;
    play->registerClickAction([folder, path_copy](brls::View*) {
        const auto* sys = library_state::find_system(folder);
        if (!sys) return true;
        for (const auto& g : sys->games) {
            if (g.path != path_copy) continue;
            if (launch_game(*sys, g)) {
                // envSetNextLoad already queued; quitting brls
                // drains the chain-launch.
                brls::Application::quit();
            }
            return true;
        }
        return true;
    });
    outer->addView(play);

    auto* frame = new brls::AppletFrame(outer);
    frame->setTitle(game ? game->display : std::string("Game"));

    // B pops back to SystemActivity.
    frame->registerAction("hints/back"_i18n, brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    return frame;
}

}  // namespace foyer::browser
