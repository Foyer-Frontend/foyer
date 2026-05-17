#include "slot_picker_activity.hpp"

#include "libretro/savestate.hpp"
#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <cstdio>
#include <ctime>

using namespace brls::literals;

namespace foyer::player {

SlotPickerActivity::SlotPickerActivity(Mode mode,
                                       std::string rom_path,
                                       std::string system_folder)
    : m_mode(mode)
    , m_rom_path(std::move(rom_path))
    , m_system_folder(std::move(system_folder)) {}

brls::View* SlotPickerActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    // Probe every slot up front so each row can show whether
    // there's already data on disk + when it was saved.
    foyer::libretro::StateSlot slots[foyer::libretro::kStateSlotCount];
    foyer::libretro::inspect_slots(m_rom_path, m_system_folder, slots);

    auto format_detail = [](const foyer::libretro::StateSlot& s) {
        if (!s.exists) return std::string{"Empty"};
        char buf[64];
        std::tm tm = *std::localtime(&s.mtime);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return std::string{buf};
    };

    const Mode mode      = m_mode;
    const std::string rom = m_rom_path;
    const std::string sys = m_system_folder;

    for (int i = 0; i < foyer::libretro::kStateSlotCount; i++) {
        const auto& s = slots[i];
        auto* cell = new brls::DetailCell();
        cell->title->setText(i == 0 ? "Quick"
                                    : "Slot " + std::to_string(i));
        cell->detail->setText(format_detail(s));
        cell->registerClickAction([mode, rom, sys, i](brls::View*) {
            const auto path =
                ::foyer::libretro::state_path_for(rom, sys, i);
            const Mode m = mode;
            if (m == Mode::Save) {
                if (::foyer::libretro::save_state(path)) {
                    brls::Application::notify(
                        "Saved to slot " + std::to_string(i));
                    foyer::log::write("[player-brls] saved state %s\n",
                        path.c_str());
                } else {
                    brls::Application::notify("Save failed");
                }
                // Defer the pop so brls finishes dispatching the
                // current click-action before tearing this activity
                // down — popping while we're still on the action
                // callback's stack frame hits brls's deletion pool
                // mid-dispatch and crashes (PC=0 via a freed
                // View*'s vtable on the next frame).
                brls::sync([]() {
                    brls::Application::popActivity(brls::TransitionAnimation::NONE);
                });
            } else {
                if (::foyer::libretro::load_state(path)) {
                    brls::Application::notify(
                        "Loaded slot " + std::to_string(i));
                    foyer::log::write("[player-brls] loaded state %s\n",
                        path.c_str());
                    // Pop the picker AND the pause overlay so
                    // the user is straight back in the game.
                    brls::sync([]() {
                        brls::Application::popActivity(brls::TransitionAnimation::NONE);
                        brls::Application::popActivity(brls::TransitionAnimation::NONE);
                    });
                } else {
                    brls::Application::notify("Load failed");
                }
            }
            return true;
        });
        host->addView(cell);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle(m_mode == Mode::Save ? "Save state" : "Load state");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time",
                               "brls/battery",
                               "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void SlotPickerActivity::onContentAvailable() {
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
