#include "widgets/action_button.hpp"

namespace foyer::browser {

ActionButton::ActionButton(const std::string& icon_res,
                           const std::string& label_text,
                           std::function<bool(brls::View*)> on_click)
{
    constexpr float kBtnSize  = 72.0f;
    constexpr float kIconSize = 44.0f;
    constexpr float kLabelH   = 32.0f;

    // Outer wrapper is column-axis: focusable circle on top, label
    // (hidden until focus) below. Hold marginal padding so adjacent
    // buttons don't touch.
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setMargins(0.0f, 12.0f, 0.0f, 12.0f);

    m_idle_bg = nvgRGB(45, 55, 75);

    // Inner circle is the actual focusable + clickable surface.
    m_circle = new brls::Box();
    m_circle->setWidth(kBtnSize);
    m_circle->setHeight(kBtnSize);
    m_circle->setFocusable(true);
    m_circle->setHighlightCornerRadius(kBtnSize * 0.5f);
    m_circle->setCornerRadius(kBtnSize * 0.5f);
    m_circle->setBackgroundColor(m_idle_bg);
    // brls's default focus highlight fills the focused view with
    // theme["brls/highlight/background"] before drawing the border
    // stroke. That's what was tinting the button on focus despite
    // our handlers not touching the colour. Hide the background
    // so only the animated stroke + the label below indicate focus.
    m_circle->setHideHighlightBackground(true);
    m_circle->setJustifyContent(brls::JustifyContent::CENTER);
    m_circle->setAlignItems(brls::AlignItems::CENTER);

    m_icon = new brls::Image();
    m_icon->setWidth(kIconSize);
    m_icon->setHeight(kIconSize);
    m_icon->setScalingType(brls::ImageScalingType::FIT);
    m_circle->addView(m_icon);
    m_icon->setImageFromRes(icon_res);

    m_circle->registerClickAction(std::move(on_click));
    m_circle->addGestureRecognizer(new brls::TapGestureRecognizer(m_circle));
    this->addView(m_circle);

    // Label below the circle. Hidden until the circle is focused so
    // the row stays compact at rest. Wired via onChildFocusGained
    // since the circle is the focusable, not the wrapper.
    // Label is wrapped in a "chip" — a small Box with theme-aware
    // background colour so the text stays readable over arbitrary
    // backdrop images. brls/background flips dark↔light with the
    // theme variant; brls/text gives the contrasting foreground.
    m_label_chip = new brls::Box();
    m_label_chip->setAxis(brls::Axis::ROW);
    m_label_chip->setAlignItems(brls::AlignItems::CENTER);
    m_label_chip->setJustifyContent(brls::JustifyContent::CENTER);
    m_label_chip->setHeight(kLabelH);
    m_label_chip->setPadding(2.0f, 10.0f, 2.0f, 10.0f);
    m_label_chip->setCornerRadius(kLabelH * 0.5f);
    m_label_chip->setMargins(6.0f, 0.0f, 0.0f, 0.0f);

    m_label = new brls::Label();
    m_label->setText(label_text);
    m_label->setFontSize(22.0f);
    m_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_label_chip->addView(m_label);

    this->addView(m_label_chip);
    apply_chip_theme();
    m_label_chip->setVisibility(brls::Visibility::INVISIBLE);
}

void ActionButton::apply_chip_theme() {
    if (!m_label_chip || !m_label) return;
    auto th = brls::Application::getTheme();
    m_label_chip->setBackgroundColor(th.getColor("brls/background"));
    m_label->setTextColor(th.getColor("brls/text"));
}

void ActionButton::onChildFocusGained(brls::View* directChild,
                                      brls::View* focusedView) {
    brls::Box::onChildFocusGained(directChild, focusedView);
    if (m_label_chip) {
        // Re-pull theme colours on every focus gain so a HOS
        // Light↔Dark flip mid-session re-skins the chip the next
        // time the user lands on a button.
        apply_chip_theme();
        m_label_chip->setVisibility(brls::Visibility::VISIBLE);
    }
}

void ActionButton::onChildFocusLost(brls::View* directChild,
                                    brls::View* focusedView) {
    brls::Box::onChildFocusLost(directChild, focusedView);
    if (m_label_chip) m_label_chip->setVisibility(brls::Visibility::INVISIBLE);
}

} // namespace foyer::browser
