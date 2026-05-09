#include "activity/power_activity.hpp"

#include "power_actions.hpp"

#include <borealis.hpp>
#include <borealis/views/label.hpp>

#include <functional>
#include <string>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// One row in the right-side power panel. Rectangular focusable Box
// with the action label centred. brls's focus highlight handles the
// hover state, so no per-row colour state to manage.
brls::Box* make_power_row(const std::string& label,
                          std::function<void()> on_pick)
{
    auto* row = new brls::Box();
    row->setHeight(72.0f);
    row->setWidthPercentage(100.0f);
    row->setFocusable(true);
    row->setHighlightCornerRadius(0.0f);
    row->setJustifyContent(brls::JustifyContent::CENTER);
    row->setAlignItems(brls::AlignItems::FLEX_START);
    row->setPaddingLeft(32.0f);

    auto* lbl = new brls::Label();
    lbl->setText(label);
    lbl->setFontSize(22.0f);
    row->addView(lbl);

    row->registerClickAction([on_pick](brls::View*) {
        on_pick();
        return true;
    });
    row->addGestureRecognizer(new brls::TapGestureRecognizer(row));
    return row;
}

}  // namespace

brls::View* PowerActivity::createContentView() {
    // Outer Box covers the full screen with a translucent dark
    // overlay; the panel sits on the right.
    auto* outer = new brls::Box();
    outer->setAxis(brls::Axis::ROW);
    outer->setBackgroundColor(nvgRGBA(0, 0, 0, 140));  // ~55% scrim
    outer->setJustifyContent(brls::JustifyContent::FLEX_END);
    outer->setAlignItems(brls::AlignItems::STRETCH);

    // Tap-outside-to-close: the scrim itself is focusable + clicks
    // pop the activity. Means "B" and "tap outside" both dismiss,
    // matching HOS behaviour.
    outer->setFocusable(true);
    outer->registerClickAction([](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(420.0f);
    panel->setBackgroundColor(nvgRGB(34, 34, 38));
    panel->setPadding(48.0f, 0.0f, 0.0f, 0.0f);
    panel->setAlignItems(brls::AlignItems::STRETCH);

    panel->addView(make_power_row("Sleep",
        []() {
            brls::Application::popActivity();
            power_actions::sleep();
        }));
    panel->addView(make_power_row("Restart",
        []() {
            brls::Application::popActivity();
            power_actions::reboot();
        }));
    panel->addView(make_power_row("Power off",
        []() {
            brls::Application::popActivity();
            power_actions::shutdown();
        }));
    panel->addView(make_power_row("Reboot to Hekate",
        []() {
            brls::Application::popActivity();
            power_actions::reboot_hekate();
        }));

    outer->addView(panel);

    // B closes the activity.
    outer->registerAction("hints/back"_i18n, brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    return outer;
}

}  // namespace foyer::browser
