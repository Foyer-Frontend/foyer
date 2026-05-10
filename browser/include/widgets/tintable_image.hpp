#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// brls::Image with a settable tint colour. nvgImagePattern stores the
// modulation colour in paint.innerColor / outerColor (multiplied by
// each texel during fill), so a white-on-transparent PNG turns into
// "any colour we want" at runtime — used for the action-row icons
// (highlight on focus) and for the per-system logos that need to
// flip between light/dark theme automatically.
class TintableImage : public brls::Image {
public:
    void setTintColor(NVGcolor color) {
        m_tint   = color;
        m_tinted = true;
    }
    void clearTintColor() { m_tinted = false; }

    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
        const NVGcolor c = m_tinted
            ? m_tint
            : nvgRGBA(255, 255, 255, 255);
        this->paint.innerColor = c;
        this->paint.outerColor = c;
        brls::Image::draw(vg, x, y, w, h, style, ctx);
    }

private:
    NVGcolor m_tint{};
    bool     m_tinted = false;
};

} // namespace foyer::browser
