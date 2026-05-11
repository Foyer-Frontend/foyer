#include "emulator_view.hpp"

#include "libretro/video.hpp"

namespace foyer::player {

EmulatorView::EmulatorView() {
    // Fill the entire activity content area; the activity owns no
    // chrome above us.
    this->setGrow(1.0f);
    // Focusable so brls's pop-restore-focus path has a valid
    // target when PauseActivity / SlotPickerActivity close.
    // Otherwise the focus pointer stays on the dying overlay's
    // cell, and the next A press faults dereferencing a freed
    // view. Libretro input still routes via libnx directly, so
    // owning brls focus here costs nothing.
    this->setFocusable(true);
    this->setHideHighlight(true);
    this->setHideHighlightBackground(true);
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
    // Paint the whole bounding rect black before delegating to
    // libretro's video draw. brls's deko3d backend doesn't
    // always clear pixels that no view claims to own this
    // frame, so anything that was on screen before the
    // EmulatorView (pause overlay panel, brls splash) bleeds
    // through as the artifact bands the user reported.
    nvgSave(vg);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);
    nvgTranslate(vg, x, y);
    foyer::libretro::VideoSinkImpl::instance().draw(vg, w, h);
    nvgRestore(vg);
}

}  // namespace foyer::player
