#include "core_options_picker_activity.hpp"

#include "libretro/core_options.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <algorithm>

using namespace brls::literals;

namespace foyer::player {

brls::View* CoreOptionsPickerActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    const auto& opts = foyer::libretro::CoreOptions::instance().options();
    if (opts.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("No options");
        hint->detail->setText("This core hasn't registered any.");
        host->addView(hint);
    } else {
        for (const auto& opt : opts) {
            const std::string key = opt.key;
            const auto& choices    = opt.choices;
            int initial = 0;
            for (std::size_t i = 0; i < choices.size(); i++) {
                if (choices[i] == opt.value) {
                    initial = static_cast<int>(i);
                    break;
                }
            }
            auto* cell = new brls::SelectorCell();
            cell->init(opt.desc, choices, initial,
                       [](int) {},
                       [key, choices](int selected) {
                           if (selected < 0
                               || selected >= (int)choices.size()) return;
                           foyer::libretro::CoreOptions::instance().set(
                               key, choices[selected]);
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
    frame->setTitle("Core options");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void CoreOptionsPickerActivity::onContentAvailable() {
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
