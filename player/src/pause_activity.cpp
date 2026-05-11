#include "pause_activity.hpp"

#include "libretro/savestate.hpp"
#include "platform/log.hpp"

#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/header.hpp>

using namespace brls::literals;

namespace foyer::player {

PauseActivity::PauseActivity(std::string rom_path,
                             std::string system_folder,
                             QuitCallback on_quit)
    : m_rom_path(std::move(rom_path))
    , m_system_folder(std::move(system_folder))
    , m_on_quit(std::move(on_quit)) {}

brls::View* PauseActivity::createContentView() {
    // Translucent overlay: dim the running game behind us so the
    // menu reads on any frame.
    auto* outer = new brls::Box();
    outer->setAxis(brls::Axis::COLUMN);
    outer->setAlignItems(brls::AlignItems::CENTER);
    outer->setJustifyContent(brls::JustifyContent::CENTER);
    outer->setBackgroundColor(nvgRGBA(0, 0, 0, 178));

    // Inner panel — fixed width column of cells.
    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setAlignItems(brls::AlignItems::STRETCH);
    panel->setWidth(540.0f);
    panel->setMaxHeight(580.0f);
    auto th = brls::Application::getTheme();
    panel->setBackgroundColor(th.getColor("brls/background"));
    panel->setCornerRadius(10.0f);
    panel->setPadding(16.0f, 24.0f, 24.0f, 24.0f);

    auto* header = new brls::Header();
    header->setTitle("Game paused");
    panel->addView(header);

    auto add_cell = [&](const std::string& title, const std::string& detail,
                        std::function<bool(brls::View*)> on_click) {
        auto* c = new brls::DetailCell();
        c->title->setText(title);
        c->detail->setText(detail);
        c->registerClickAction(std::move(on_click));
        panel->addView(c);
    };

    auto soon = [](const std::string& name) {
        return [name](brls::View*) {
            brls::Application::notify(name + " — coming soon");
            return true;
        };
    };

    const std::string rom = m_rom_path;
    const std::string sys = m_system_folder;

    add_cell("Resume", "Close menu",
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        });

    add_cell("Save state", "Quick slot",
        [rom, sys](brls::View*) {
            const auto path =
                ::foyer::libretro::state_path_for(rom, sys, /*slot=*/0);
            if (::foyer::libretro::save_state(path)) {
                brls::Application::notify("Saved state");
                foyer::log::write("[player-brls] saved state %s\n",
                    path.c_str());
            } else {
                brls::Application::notify("Save state failed");
            }
            return true;
        });

    add_cell("Load state", "Quick slot",
        [rom, sys](brls::View*) {
            const auto path =
                ::foyer::libretro::state_path_for(rom, sys, /*slot=*/0);
            if (::foyer::libretro::load_state(path)) {
                brls::Application::notify("Loaded state");
                brls::Application::popActivity();
                foyer::log::write("[player-brls] loaded state %s\n",
                    path.c_str());
            } else {
                brls::Application::notify("Load state failed");
            }
            return true;
        });

    add_cell("Core options", "Per-core knobs", soon("Core options"));
    add_cell("Display",      "Aspect / scale", soon("Display settings"));
    add_cell("Shaders",      "Pick a preset",  soon("Shaders"));
    add_cell("Cheats",       "Toggle cheats",  soon("Cheats"));

    add_cell("Quit", "End the game",
        [this](brls::View*) {
            if (m_on_quit) m_on_quit();
            brls::Application::quit();
            return true;
        });

    outer->addView(panel);
    return outer;
}

void PauseActivity::onContentAvailable() {
    // B closes the pause menu without quitting the game — pops
    // this activity off the stack so EmulatorActivity resumes
    // ticking.
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

}  // namespace foyer::player
