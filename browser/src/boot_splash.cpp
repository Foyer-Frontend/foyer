#include "boot_splash.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace foyer::browser {
namespace {

// Splash background lookup order — first hit wins. The SD-side
// override lets users drop a custom 1920x1080 .png or .jpg at
// /foyer/assets/splash.<ext> without needing to rebuild the .nro.
// Fallback is the bundled default copied into romfs at build time
// (assets/romfs/splash.jpg).
constexpr const char* kSplashCandidates[] = {
    "/foyer/assets/splash.png",
    "/foyer/assets/splash.jpg",
    "romfs:/splash.png",
    "romfs:/splash.jpg",
};

bool exists(const char* path) {
    struct stat st{};
    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// Read a file (regular SD path or romfs:/) into nvgCreateImageMem,
// matching CoverCache::get_or_load over in views.cpp. We keep this
// inline rather than reusing CoverCache because BootSplash runs
// before any of the browser's caches exist.
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

void draw_wordmark(NVGcontext* vg, float cx, float cy) {
    nvgFontSize(vg, 132.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    // Soft drop shadow so the wordmark separates from any backdrop —
    // the bundled splash is dark but a user-supplied override could
    // be light, so the shadow protects legibility on either.
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 0xC0));
    nvgFontBlur(vg, 6.0f);
    nvgText(vg, cx + 2.0f, cy + 4.0f, "foyer", nullptr);

    nvgFontBlur(vg, 0.0f);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xFF));
    nvgText(vg, cx, cy, "foyer", nullptr);
}

void draw_progress_strip(NVGcontext* vg, float cx, float cy, float frame) {
    constexpr float W = 320.0f;
    constexpr float H = 4.0f;
    const float x = cx - W * 0.5f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, cy, W, H, H * 0.5f);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0x22));
    nvgFill(vg);

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
    int handle = 0;
    const char* used = nullptr;
    for (const auto* path : kSplashCandidates) {
        // exists() only handles SD paths; for romfs:/ we just try
        // the load and fall through on failure. Same shape either way.
        const bool on_sd = (std::strncmp(path, "romfs:/", 7) != 0);
        if (on_sd && !exists(path)) continue;
        handle = load_image_from_path(vg, path);
        if (handle > 0) { used = path; break; }
    }
    m_tiles.clear();
    if (handle > 0) {
        m_tiles.push_back(handle);
        foyer::log::write("[boot_splash] backdrop = %s\n", used);
    } else {
        foyer::log::write("[boot_splash] no backdrop image found, using gradient\n");
    }
}

void BootSplash::draw(NVGcontext* vg, float w, float h,
                      const std::string& status,
                      const char* version) {
    static std::uint32_t frame = 0;
    frame++;

    // Backdrop. If the splash image loaded, draw it full-bleed
    // aspect-fit (cover); otherwise fall back to the indigo→purple
    // gradient from the original implementation so the boot screen
    // never paints a black void.
    if (!m_tiles.empty() && m_tiles.front() > 0) {
        const int img = m_tiles.front();
        int iw = 0, ih = 0;
        nvgImageSize(vg, img, &iw, &ih);
        if (iw > 0 && ih > 0) {
            // Cover-fit: scale so the image fills the whole frame,
            // cropping the side that overflows. Looks closer to a
            // proper splash than a stretched fit.
            const float scale = std::max(w / (float)iw, h / (float)ih);
            const float dw = iw * scale;
            const float dh = ih * scale;
            const float dx = (w - dw) * 0.5f;
            const float dy = (h - dh) * 0.5f;
            NVGpaint pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, img, 1.0f);
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, w, h);
            nvgFillPaint(vg, pat);
            nvgFill(vg);
        }
        // Subtle dimming overlay so text on top stays legible
        // regardless of how bright the user's custom splash is.
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, w, h);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 0x55));
        nvgFill(vg);
    } else {
        const NVGcolor top    = nvgRGB(0x3F, 0x2D, 0x8C);
        const NVGcolor bottom = nvgRGB(0x12, 0x0B, 0x2A);
        NVGpaint bg = nvgLinearGradient(vg, 0, 0, 0, h, top, bottom);
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, w, h);
        nvgFillPaint(vg, bg);
        nvgFill(vg);
    }

    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    draw_wordmark(vg, cx, cy - 30.0f);

    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, nvgRGBA(0xE6, 0xE3, 0xF2, 0xFF));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, cx, cy + 80.0f, status.c_str(), nullptr);

    draw_progress_strip(vg, cx, cy + 118.0f,
                        static_cast<float>(frame));

    nvgFontSize(vg, 16.0f);
    nvgFillColor(vg, nvgRGBA(0xC8, 0xC1, 0xE0, 0xC0));
    nvgText(vg, cx, h - 28.0f, version, nullptr);
}

} // namespace foyer::browser
