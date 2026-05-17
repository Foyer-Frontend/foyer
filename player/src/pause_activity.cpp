#include "pause_activity.hpp"
#include "cheats_picker_activity.hpp"
#include "core_options_picker_activity.hpp"
#include "display_picker_activity.hpp"
#include "shaders_picker_activity.hpp"
#include "slot_picker_activity.hpp"

#include "libretro/frontend.hpp"
#include "libretro/savestate.hpp"
#include "platform/log.hpp"

#include <switch.h>

extern "C" void retro_reset(void);

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>
#include <borealis/views/widgets/battery.hpp>
#include <borealis/views/widgets/wireless.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace brls::literals;

namespace foyer::player {

PauseActivity::PauseActivity(std::string rom_path,
                             std::string system_folder,
                             std::string back_nro,
                             QuitCallback on_quit)
    : m_rom_path(std::move(rom_path))
    , m_system_folder(std::move(system_folder))
    , m_back_nro(std::move(back_nro))
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

    add_cell("Restart", "Reset the game",
        [](brls::View*) {
            auto* dlg = new brls::Dialog(
                "Restart the game? Unsaved progress will be lost.");
            dlg->addButton("Cancel", []() {});
            dlg->addButton("Restart", []() {
                // libretro retro_reset — same effect as a soft
                // reset on the original hardware. The emulator
                // activity stays up and the frame ticker keeps
                // pumping retro_run, so the next frame paints
                // the post-reset state.
                ::retro_reset();
                foyer::log::write("[pause] retro_reset called\n");
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
            });
            dlg->open();
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

    add_cell("Core options", "Per-core knobs",
        [](brls::View*) {
            brls::Application::pushActivity(
                new CoreOptionsPickerActivity(),
                brls::TransitionAnimation::NONE);
            return true;
        });
    add_cell("Display",      "Aspect / scale",
        [](brls::View*) {
            brls::Application::pushActivity(
                new DisplayPickerActivity(),
                brls::TransitionAnimation::NONE);
            return true;
        });
    add_cell("Shaders",      "Pick a preset",
        [](brls::View*) {
            brls::Application::pushActivity(
                new ShadersPickerActivity(),
                brls::TransitionAnimation::NONE);
            return true;
        });
    {
        // Derive rom stem from the rom path for the cheats picker.
        std::string stem = rom;
        if (const auto sl = stem.find_last_of('/'); sl != std::string::npos)
            stem = stem.substr(sl + 1);
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos)
            stem = stem.substr(0, dot);
        const auto sys_copy = sys;
        const auto stem_copy = stem;
        add_cell("Cheats", "Toggle cheats",
            [sys_copy, stem_copy](brls::View*) {
                brls::Application::pushActivity(
                    new CheatsPickerActivity(sys_copy, stem_copy),
                    brls::TransitionAnimation::NONE);
                return true;
            });
    }

    const std::string back = m_back_nro;
    add_cell("Quit", "Back to foyer",
        [this, back](brls::View*) {
            if (m_on_quit) m_on_quit();
            // Flush SRAM proactively here — relying on
            // EmulatorActivity's destructor was racing the
            // chain-launch: brls::Application::quit doesn't
            // guarantee the activity stack drains before main()
            // returns, so dtors that call save_sram_for / unload_game
            // could be skipped entirely and cartridge-save games
            // (Zelda etc) lost their .srm. Explicitly persist
            // before envSetNextLoad so the file is written + closed
            // before the process tears down.
            ::foyer::libretro::Frontend::instance().flush_sram();
            // Chain-launch back to foyer.nro instead of dropping
            // the user on the homebrew menu. The browser stamped
            // its own path as argv[2] when launching us.
            if (!back.empty()) {
                envSetNextLoad(back.c_str(), back.c_str());
            }
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
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time",
                               "brls/battery",
                               "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }

    // Inject clock + wifi + battery into the AppletFrame
    // header's hint_box so the pause overlay's top bar mirrors
    // every other view's chrome.
    if (auto* header = frame->getHeader()) {
        auto* hint_box = dynamic_cast<brls::Box*>(
            header->getView("brls/applet_frame/hint_box"));
        if (hint_box) {
            auto* clock = new brls::Label();
            clock->setText("--:--");
            clock->setFontSize(22.0f);
            clock->setMargins(0.0f, 20.0f, 0.0f, 0.0f);
            hint_box->addView(clock);

            auto* wifi = new brls::WirelessWidget();
            wifi->setMargins(0.0f, 20.0f, 2.0f, 0.0f);
            hint_box->addView(wifi);

            auto* batt = new brls::BatteryWidget();
            batt->setMargins(0.0f, 0.0f, 2.0f, 0.0f);
            hint_box->addView(batt);

            // Keep the clock label up to date — RepeatingTimer
            // owns itself; we just stop() it from
            // onContentAvailable's caller via brls's auto-cleanup
            // when the activity tears down (Application::frame
            // owns the deletion pool).
            auto* timer = new brls::RepeatingTimer();
            timer->setPeriod(1000);
            timer->setCallback([clock]() {
                const auto now = std::chrono::system_clock::now();
                const auto t   = std::chrono::system_clock::to_time_t(now);
                std::tm tm     = *std::localtime(&t);
                std::stringstream ss;
                ss << std::put_time(&tm, "%H:%M");
                clock->setText(ss.str());
            });
            timer->start();
        }
    }
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
