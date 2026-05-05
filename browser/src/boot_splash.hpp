#pragma once

#include <string>
#include <vector>

#include <nanovg.h>

namespace foyer::browser {

// Boot splash: drawn from the moment the App's render context is up
// until the main browser UI takes over. Loads a curated set of system
// splash PNGs from romfs into a tile mosaic, paints a foyer wordmark
// + controller silhouette in the centre, and a status line + version
// at the bottom. The status string is owned by main.cpp and updated
// between init phases — we just read it.
//
// All nvgImage handles live for the lifetime of the BootSplash; the
// splash screen is short-lived (a few seconds at most), so we don't
// bother freeing them — the App tears down its NVG context on quit
// and the kernel reaps the GPU memory.
class BootSplash {
public:
    // Pre-load tiles using `vg` so the first paint already has all
    // images decoded — keeps the boot frame budget tight.
    explicit BootSplash(NVGcontext* vg);

    // Paint one frame. `status` is the current init-phase label
    // ("Seeding assets...", "Scanning library...", etc.). `version`
    // is the FOYER_DISPLAY_VERSION string for the bottom corner.
    void draw(NVGcontext* vg, float w, float h,
              const std::string& status,
              const char* version);

private:
    std::vector<int> m_tiles; // nvg image handles, one per mosaic cell
};

} // namespace foyer::browser
