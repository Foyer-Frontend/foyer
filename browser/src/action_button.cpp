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

    // Theme-aware circle bg + icon tint so the action row inverts
    // cleanly between HOS Light and Dark. Idle bg follows brls's
    // background colour (light in light, dark in dark); icon follows
    // brls/text (dark in light, light in dark) via the foyer-patched
    // Image::setTintColor — see cmake/patch_brls_image_tint.cmake.
    // The cached m_idle_bg is set from the live theme inside
    // apply_chip_theme() below.

    // Inner circle is the actual focusable + clickable surface.
    m_circle = new brls::Box();
    m_circle->setWidth(kBtnSize);
    m_circle->setHeight(kBtnSize);
    m_circle->setFocusable(true);
    m_circle->setHighlightCornerRadius(kBtnSize * 0.5f);
    m_circle->setCornerRadius(kBtnSize * 0.5f);
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
    const NVGcolor bg   = th.getColor("brls/background");
    const NVGcolor text = th.getColor("brls/text");
    m_label_chip->setBackgroundColor(bg);
    m_label->setTextColor(text);

    // Apply the same theming to the circle bg + icon tint. brls's
    // background is light in HOS Light and dark in HOS Dark; the
    // contrasting text colour is exactly what we want for the icon.
    // Cached as m_idle_bg so the focus-gain/lost handlers can flip
    // back to this state on demand.
    if (m_circle) {
        m_idle_bg = bg;
        m_circle->setBackgroundColor(m_idle_bg);
        // brls's default focus highlight fills the focused view with
        // theme["brls/highlight/background"] before drawing the
        // border stroke. Hide that so the focus state is the
        // animated stroke alone (preserves the themed bg).
        m_circle->setHideHighlightBackground(true);
    }
    if (m_icon) {
        m_icon->setTintColor(text);
    }
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
