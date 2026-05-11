#pragma once

#include <borealis.hpp>

namespace foyer::player {

// brls View that owns the libretro frame texture and blits it
// aspect-fit across its bounding rect. Drawing trampolines into
// foyer::libretro::video::draw() with the View's frame, so all
// the aspect / integer-scale rules already implemented in
// shared/libretro/video.cpp stay authoritative.
//
// The View is a leaf renderer: no children, no focus children. The
// pause overlay (when ported) will live on a sibling Activity that
// presents over the EmulatorActivity, not as a child of this View.
class EmulatorView : public brls::View {
public:
    EmulatorView();

    void draw(NVGcontext* vg,
              float x, float y, float width, float height,
              brls::Style style,
              brls::FrameContext* ctx) override;
};

}  // namespace foyer::player
