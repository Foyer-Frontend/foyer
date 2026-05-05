#include "boot_splash.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace foyer::browser {
namespace {

// Curated tile lineup. Mixes well-known systems across generations so
// the mosaic feels recognisable even at low alpha. Order matters —
// the first N where N = grid cells get drawn; the rest are unused if
// the romfs file is missing. Splash filenames live at
// romfs:/systems/<slug>-splash.png.
constexpr const char* kTileSlugs[] = {
    "snes",      "genesis",   "nes",        "psx",
    "n64",       "gba",       "dreamcast",  "saturn",
    "gb",        "neogeo",    "pcengine",   "atari2600",
    "gc",        "ps2",       "psp",        "atarilynx",
    "wonderswan","mastersystem","3do",      "amiga",
};

// Read a file under romfs:/ into a buffer + nvg image handle. Same
// shape as views.cpp's CoverCache::get_or_load: libnx's romfs devoptab
// doesn't play well with stb's fopen path, so we read bytes ourselves
// and feed nvgCreateImageMem.
int load_image_from_path(NVGcontext* vg, const char* path) {
    std::FILE* fp = std::fopen(path, "rb");
    if (!fp) return 0;
    std::fseek(fp, 0, SEEK_END);
    const long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(fp); return 0; }
    std::vector<unsigned char> buf(static_cast<std::size_t>(sz));
    const auto read = std::fread(buf.data(), 1, buf.size(), fp);
    std::fclose(fp);
    if (read != buf.size()) return 0;
    return nvgCreateImageMem(vg, 0, buf.data(), static_cast<int>(buf.size()));
}

// Foyer wordmark + small controller glyph. Hand-drawn vector so we
// don't need to ship a PNG just for the splash, and so it scales
// crisply at 4K. Colours mirror assets/icon.jpg — purple gradient
// background, white wordmark, white rounded-rect controller body.
void draw_wordmark(NVGcontext* vg, float cx, float cy) {
    // "foyer" — bold lowercase, matches the icon's typography.
    nvgFontSize(vg, 132.0f);
    nvgFontBlur(vg, 0.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    // Soft drop shadow so the wordmark separates from the mosaic.
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 0xB0));
    nvgFontBlur(vg, 6.0f);
    nvgText(vg, cx + 2.0f, cy + 4.0f, "foyer", nullptr);

    nvgFontBlur(vg, 0.0f);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xFF));
    nvgText(vg, cx, cy, "foyer", nullptr);
}

// Friendly rounded-rect gamepad mirroring the controller silhouette
// in the app icon. Drawn at a fixed scale beneath the wordmark.
void draw_controller(NVGcontext* vg, float cx, float cy) {
    constexpr float w = 220.0f;
    constexpr float h = 100.0f;
    const float x = cx - w * 0.5f;
    const float y = cy - h * 0.5f;
    const NVGcolor white = nvgRGBA(0xFF, 0xFF, 0xFF, 0xE0);

    // Body
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, 28.0f);
    nvgFillColor(vg, white);
    nvgFill(vg);

    // D-pad (cross)
    const float dpx = x + 38.0f;
    const float dpy = cy;
    const NVGcolor body_bg = nvgRGBA(0x6A, 0x4E, 0xC2, 0xFF);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, dpx - 22.0f, dpy - 7.0f, 44.0f, 14.0f, 3.0f);
    nvgRoundedRect(vg, dpx - 7.0f,  dpy - 22.0f, 14.0f, 44.0f, 3.0f);
    nvgFillColor(vg, body_bg);
    nvgFill(vg);

    // Three stacked dots (centre)
    for (int i = -1; i <= 1; i++) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx + i * 12.0f, cy, 3.0f);
        nvgFillColor(vg, body_bg);
        nvgFill(vg);
    }

    // Face buttons (right cluster: + cross pattern, simplified to 4
    // dots like a SNES diamond)
    const float fbx = x + w - 38.0f;
    const float fby = cy;
    constexpr float r  = 8.0f;
    constexpr float dx = 18.0f;
    nvgBeginPath(vg);
    nvgCircle(vg, fbx,        fby - dx, r);
    nvgCircle(vg, fbx,        fby + dx, r);
    nvgCircle(vg, fbx - dx,   fby,      r);
    nvgCircle(vg, fbx + dx,   fby,      r);
    nvgFillColor(vg, body_bg);
    nvgFill(vg);
}

// Indeterminate progress strip below the status line. Wraps a single
// pip across a rounded track based on the frame counter so the user
// can tell the app is alive even when no banner is updating.
void draw_progress_strip(NVGcontext* vg, float cx, float cy, float frame) {
    constexpr float W = 320.0f;
    constexpr float H = 4.0f;
    const float x = cx - W * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, cy, W, H, H * 0.5f);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0x22));
    nvgFill(vg);

    // Sliding pip. Period ≈ 90 frames (1.5s @ 60fps).
    constexpr float kPipW   = 80.0f;
    constexpr float kPeriod = 90.0f;
    const float t = std::fmod(frame, kPeriod) / kPeriod;
    const float px = x + (W - kPipW) * t;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, px, cy, kPipW, H, H * 0.5f);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xCC));
    nvgFill(vg);
}

} // namespace

BootSplash::BootSplash(NVGcontext* vg) {
    char path[128];
    for (const auto* slug : kTileSlugs) {
        std::snprintf(path, sizeof(path),
            "romfs:/systems/%s-splash.png", slug);
        const int h = load_image_from_path(vg, path);
        if (h > 0) m_tiles.push_back(h);
    }
    foyer::log::write("[boot_splash] loaded %zu tile(s)\n", m_tiles.size());
}

void BootSplash::draw(NVGcontext* vg, float w, float h,
                      const std::string& status,
                      const char* version) {
    static std::uint32_t frame = 0;
    frame++;

    // Background — vertical gradient from indigo to deep purple. Same
    // family as the app icon's purple-violet palette so the splash
    // and home screen feel like one product.
    const NVGcolor top    = nvgRGB(0x3F, 0x2D, 0x8C);
    const NVGcolor bottom = nvgRGB(0x12, 0x0B, 0x2A);
    NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, h, top, bottom);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    // Tile mosaic. 5 columns × 4 rows = 20 cells, with a small gap.
    // Tiles are drawn at low alpha so the wordmark stays the focal
    // point. If we loaded fewer than 20 tiles, we just leave the
    // empty cells transparent — the gradient fills the gap.
    if (!m_tiles.empty()) {
        constexpr int kCols = 5;
        constexpr int kRows = 4;
        constexpr float kGap = 16.0f;
        const float cellW = (w - kGap * (kCols + 1)) / kCols;
        const float cellH = (h - kGap * (kRows + 1)) / kRows;

        for (int r = 0; r < kRows; r++) {
            for (int c = 0; c < kCols; c++) {
                const int idx = r * kCols + c;
                if (idx >= static_cast<int>(m_tiles.size())) break;
                const int img = m_tiles[idx];
                if (img <= 0) continue;
                const float x = kGap + c * (cellW + kGap);
                const float y = kGap + r * (cellH + kGap);
                NVGpaint pat = nvgImagePattern(
                    vg, x, y, cellW, cellH, 0.0f, img, 0.18f);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, x, y, cellW, cellH, 12.0f);
                nvgFillPaint(vg, pat);
                nvgFill(vg);
            }
        }
    }

    // Centre vignette behind the wordmark to lift it off the mosaic.
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    NVGpaint vignette = nvgRadialGradient(
        vg, cx, cy, 60.0f, 360.0f,
        nvgRGBA(0x10, 0x08, 0x24, 0xC8),
        nvgRGBA(0x10, 0x08, 0x24, 0x00));
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillPaint(vg, vignette);
    nvgFill(vg);

    // Wordmark + controller (centre).
    draw_wordmark  (vg, cx, cy - 60.0f);
    draw_controller(vg, cx, cy + 50.0f);

    // Status text + indeterminate progress strip + version footer.
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, nvgRGBA(0xCF, 0xC9, 0xE7, 0xE0));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, cy + 130.0f, status.c_str(), nullptr);

    draw_progress_strip(vg, cx, cy + 168.0f,
                        static_cast<float>(frame));

    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, nvgRGBA(0x9A, 0x90, 0xC2, 0xC0));
    nvgText(vg, cx, h - 28.0f, version, nullptr);
}

} // namespace foyer::browser
