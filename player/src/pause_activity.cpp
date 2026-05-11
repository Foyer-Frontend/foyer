#include "pause_activity.hpp"

#include "libretro/savestate.hpp"
#include "platform/log.hpp"

#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

using namespace brls::literals;

namespace foyer::player {

PauseActivity::PauseActivity(std::string rom_path,
                             std::string system_folder,
                             QuitCallback on_quit)
    : m_rom_path(std::move(rom_path))
    , m_system_folder(std::move(system_folder))
    , m_on_quit(std::move(on_quit)) {}

brls::View* PauseActivity::createContentView() {
    // Full-screen Box with theme background — the
    // EmulatorActivity ticker stops while this is on top, so
    // letting the theme paint over the running game gives the
    // user a clear "paused" signal.
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setAlignItems(brls::AlignItems::STRETCH);
    auto th = brls::Application::getTheme();
    root->setBackgroundColor(th.getColor("brls/background"));

    auto* title = new brls::Label();
    title->setText("Game paused");
    title->setFontSize(28.0f);
    title->setMargins(20.0f, 32.0f, 12.0f, 32.0f);
    title->setTextColor(nvgRGB(0xD0, 0x3A, 0x3A));
    root->addView(title);

    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(0.0f, 32.0f, 32.0f, 32.0f);

    auto add_cell = [&](const std::string& title, const std::string& detail,
                        std::function<bool(brls::View*)> on_click) {
        auto* c = new brls::DetailCell();
        c->title->setText(title);
        c->detail->setText(detail);
        c->registerClickAction(std::move(on_click));
        host->addView(c);
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

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);
    root->addView(scroll);
    return root;
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
