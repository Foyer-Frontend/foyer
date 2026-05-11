#include "display_picker_activity.hpp"

#include "libretro/aspect.hpp"
#include "libretro/video.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <array>
#include <string>

using namespace brls::literals;

namespace foyer::player {

brls::View* DisplayPickerActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    struct Option {
        const char* label;
        foyer::libretro::AspectMode mode;
    };
    static const std::array<Option, 7> kOptions = {{
        { "Core aspect",      foyer::libretro::AspectMode::DisplayCore },
        { "4:3",              foyer::libretro::AspectMode::Display43   },
        { "16:9",             foyer::libretro::AspectMode::Display169  },
        { "Stretch",          foyer::libretro::AspectMode::Stretch     },
        { "Integer 1x",       foyer::libretro::AspectMode::Integer1x   },
        { "Integer 2x",       foyer::libretro::AspectMode::Integer2x   },
        { "Integer auto",     foyer::libretro::AspectMode::IntegerAuto },
    }};

    const auto current =
        foyer::libretro::VideoSinkImpl::instance().aspect();

    for (const auto& opt : kOptions) {
        auto* cell = new brls::DetailCell();
        cell->title->setText(opt.label);
        cell->detail->setText(opt.mode == current ? "Active" : "");
        const auto mode = opt.mode;
        cell->registerClickAction([mode](brls::View*) {
            foyer::libretro::VideoSinkImpl::instance().set_aspect(mode);
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
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
    frame->setTitle("Display");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void DisplayPickerActivity::onContentAvailable() {
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
