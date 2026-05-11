#include "pause_activity.hpp"
#include "slot_picker_activity.hpp"

#include "libretro/savestate.hpp"
#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>
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
    // brls::AppletFrame gives us the standard HOS-style top bar
    // (title + hint cluster) and bottom hint bar for free. The
    // public ctor takes the content view directly, sidestepping
    // setContentView's protected access.
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

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
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        });

    add_cell("Save state", "Pick a slot",
        [rom, sys](brls::View*) {
            brls::Application::pushActivity(
                new SlotPickerActivity(SlotPickerActivity::Mode::Save,
                                       rom, sys),
                brls::TransitionAnimation::NONE);
            return true;
        });

    add_cell("Load state", "Pick a slot",
        [rom, sys](brls::View*) {
            brls::Application::pushActivity(
                new SlotPickerActivity(SlotPickerActivity::Mode::Load,
                                       rom, sys),
                brls::TransitionAnimation::NONE);
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

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Game paused");
    return frame;
}

void PauseActivity::onContentAvailable() {
    // B closes the pause menu without quitting the game — pops
    // this activity off the stack so EmulatorActivity resumes
    // ticking.
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

}  // namespace foyer::player
