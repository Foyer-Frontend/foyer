#include "emulator_view.hpp"

#include "libretro/video.hpp"

namespace foyer::player {

EmulatorView::EmulatorView() {
    // Fill the entire activity content area; the activity owns no
    // chrome above us.
    this->setGrow(1.0f);
    // Focus is irrelevant for the emulator surface itself — input
    // routes via the EmulatorActivity tick into libretro/input.cpp
    // rather than through brls focus traversal.
    this->setFocusable(false);
}

void EmulatorView::draw(NVGcontext* vg,
                        float x, float y, float w, float h,
                        brls::Style /*style*/,
                        brls::FrameContext* /*ctx*/) {
    // libretro/video.cpp expects a global origin; brls hands us
    // the View's screen position via (x, y), so translate before
    // blitting and restore after. Same nanovg API as our legacy
    // foyer_render shell — brls's vendored nanovg implements the
    // same surface contract.
    nvgSave(vg);
    nvgTranslate(vg, x, y);
    foyer::libretro::VideoSinkImpl::instance().draw(vg, w, h);
    nvgRestore(vg);
}

}  // namespace foyer::player
