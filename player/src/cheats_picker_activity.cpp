#include "cheats_picker_activity.hpp"

#include "libretro/cheats.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <memory>
#include <vector>

using namespace brls::literals;

namespace foyer::player {

CheatsPickerActivity::CheatsPickerActivity(std::string system_folder,
                                           std::string rom_stem)
    : m_system_folder(std::move(system_folder))
    , m_rom_stem(std::move(rom_stem)) {}

brls::View* CheatsPickerActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    // Shared state — each row mutates the same vector, then
    // save+apply on toggle. shared_ptr so the lambdas don't go
    // dangling when the activity is later popped.
    auto cheats = std::make_shared<std::vector<foyer::libretro::Cheat>>(
        foyer::libretro::load_cheats_for(m_system_folder, m_rom_stem));

    if (cheats->empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("No cheats");
        hint->detail->setText("Drop a .cht next to the rom");
        host->addView(hint);
    } else {
        const auto sys  = m_system_folder;
        const auto stem = m_rom_stem;
        for (std::size_t i = 0; i < cheats->size(); i++) {
            const auto& c = (*cheats)[i];
            auto* cell = new brls::BooleanCell();
            cell->init(c.desc.empty() ? ("Cheat " + std::to_string(i))
                                      : c.desc,
                       c.enabled,
                       [cheats, i, sys, stem](bool value) {
                           if (i >= cheats->size()) return;
                           (*cheats)[i].enabled = value;
                           foyer::libretro::save_cheats_for(
                               sys, stem, *cheats);
                           foyer::libretro::apply_cheats_to_core(*cheats);
                       });
            host->addView(cell);
        }
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Cheats");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void CheatsPickerActivity::onContentAvailable() {
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
