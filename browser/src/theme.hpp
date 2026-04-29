#pragma once

#include <nanovg.h>

namespace foyer::browser {

// Hardcoded ES-DE-inspired palette + metrics. Phase 8 will move this into a
// pluggable theme file under /foyer/config/.
struct Theme {
    NVGcolor bg          = nvgRGBA(0x10, 0x12, 0x16, 0xFF);
    NVGcolor bg_panel    = nvgRGBA(0x18, 0x1B, 0x21, 0xFF);
    NVGcolor bg_panel_hi = nvgRGBA(0x21, 0x25, 0x2D, 0xFF);

    NVGcolor accent      = nvgRGBA(0xF6, 0xC1, 0x42, 0xFF); // foyer yellow
    NVGcolor accent_dim  = nvgRGBA(0xC0, 0x96, 0x33, 0xFF);

    NVGcolor text_strong = nvgRGBA(0xF2, 0xF2, 0xF2, 0xFF);
    NVGcolor text        = nvgRGBA(0xCB, 0xCD, 0xD2, 0xFF);
    NVGcolor text_dim    = nvgRGBA(0x82, 0x86, 0x8E, 0xFF);

    NVGcolor border      = nvgRGBA(0x2D, 0x32, 0x3A, 0xFF);

    float    pad         = 24.0f;
    float    radius      = 10.0f;
    float    title_size  = 38.0f;
    float    head_size   = 26.0f;
    float    body_size   = 20.0f;
    float    label_size  = 16.0f;
};

const Theme& theme();

} // namespace foyer::browser
