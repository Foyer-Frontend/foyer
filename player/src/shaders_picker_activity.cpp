#include "shaders_picker_activity.hpp"

#include "libretro/shader.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <string>

using namespace brls::literals;

namespace foyer::player {

brls::View* ShadersPickerActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    const auto presets =
        foyer::libretro::ShaderPipeline::available_presets();
    const std::string active =
        foyer::libretro::shader_pipeline().active();

    // Add a "None" entry first so the user can turn off the
    // post-process chain without hunting for the right preset.
    auto add_row = [&](const std::string& name, const std::string& label) {
        auto* cell = new brls::DetailCell();
        cell->title->setText(label);
        cell->detail->setText(name == active ? "Active" : "");
        const std::string n = name;
        cell->registerClickAction([n](brls::View*) {
            foyer::libretro::shader_pipeline().set_preset(n);
            brls::Application::popActivity(brls::TransitionAnimation::NONE);
            return true;
        });
        host->addView(cell);
    };

    add_row("none", "None");
    for (const auto& p : presets) {
        if (p.name == "none") continue;  // already shown
        add_row(p.name, p.label.empty() ? p.name : p.label);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Shaders");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void ShadersPickerActivity::onContentAvailable() {
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
