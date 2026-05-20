#pragma once

#include <borealis.hpp>
#include <functional>
#include <string>

namespace foyer::browser {

// HOS-style round action button — slate circle by default; on focus
// the circle fills with the theme's highlight colour and a label
// appears below it (also tinted to the highlight colour). The icon
// itself stays white at all times: nvgImagePattern multiplies the
// texture by paint colour, and grayscale icons multiplied by an
// arbitrary highlight tint produce a muddy mid-tone that doesn't
// match brls's animated highlight stroke. Filling the background
// instead keeps the icon contrast crisp and avoids that mismatch.
class ActionButton : public brls::Box {
public:
    ActionButton(const std::string& icon_res,
                 const std::string& label_text,
                 std::function<bool(brls::View*)> on_click);
    ~ActionButton() override;

    void onChildFocusGained(brls::View* directChild,
                            brls::View* focusedView) override;
    void onChildFocusLost(brls::View* directChild,
                          brls::View* focusedView) override;

private:
    brls::Box*   m_circle      = nullptr;
    brls::Image* m_icon        = nullptr;
    brls::Box*   m_label_chip  = nullptr;
    brls::Label* m_label       = nullptr;

    NVGcolor     m_idle_bg{};

    // theme_change subscription id so a live HOS Light↔Dark flip
    // re-tints every action button immediately, not just the one
    // the user happens to focus next.
    int          m_theme_sub   = -1;

    // Refresh chip bg + label text from the current theme. Called
    // on focus gain so live HOS theme flips re-skin the chip on
    // the next focus event without needing an activity rebuild.
    void apply_chip_theme();
};

} // namespace foyer::browser
