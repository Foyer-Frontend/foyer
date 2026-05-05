#include "views.hpp"
#include "theme.hpp"
#include "launch.hpp"
#include "mtp.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/game_meta.hpp"
#include "platform/log.hpp"
#include "scrapers/accounts.hpp"
#include "scrapers/cache.hpp"
#include "ui/icons.hpp"

#include <switch.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace foyer::browser {
namespace {

// Cover image cache keyed by absolute path. Lazily populated on first draw
// where a cover file exists; entries with handle 0 indicate "tried + not on
// disk" so we don't restat every frame.
struct CoverCache {
    std::unordered_map<std::string, int> images;
    int get_or_load(NVGcontext* vg, const std::string& path) {
        auto it = images.find(path);
        if (it != images.end()) return it->second;

        // Path-based nvgCreateImage uses stb's fopen, which returns 0 for
        // libnx romfs files. Read the bytes ourselves and feed them through
        // nvgCreateImageMem so the decoder doesn't depend on devoptab.
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (!fp) {
            foyer::log::write("[img] fopen %s failed (errno=%d)\n",
                path.c_str(), errno);
            // One-shot sanity probe: did fopen on a known-good romfs path
            // succeed from the same call site? If shaders also fail, the
            // failure is global. If only JPG fails, the romfs metadata
            // listing has the file but libnx can't open it.
            static bool probed = false;
            if (!probed) {
                probed = true;
                std::FILE* shader = std::fopen("romfs:/shaders/fill_vsh.dksh", "rb");
                foyer::log::write("[img] probe shader fp=%p errno=%d\n",
                    (void*)shader, shader ? 0 : errno);
                if (shader) std::fclose(shader);
            }
            images[path] = 0;
            return 0;
        }
        std::fseek(fp, 0, SEEK_END);
        const long sz = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (sz <= 0) {
            foyer::log::write("[img] %s zero-size\n", path.c_str());
            std::fclose(fp);
            images[path] = 0;
            return 0;
        }
        std::vector<unsigned char> buf((std::size_t)sz);
        const auto read = std::fread(buf.data(), 1, buf.size(), fp);
        std::fclose(fp);
        if (read != buf.size()) {
            foyer::log::write("[img] %s short read %zu / %ld\n",
                path.c_str(), read, sz);
            images[path] = 0;
            return 0;
        }
        const int img = nvgCreateImageMem(vg, 0, buf.data(), (int)buf.size());
        foyer::log::write("[img] %s (%ld B) -> handle %d\n",
            path.c_str(), sz, img);
        images[path] = img;
        return img;
    }
    void clear(NVGcontext* vg) {
        for (auto& [_, h] : images) {
            if (h > 0) nvgDeleteImage(vg, h);
        }
        images.clear();
    }
};
CoverCache& cover_cache() {
    static CoverCache c;
    return c;
}

// Per-system logo + per-game backdrop caches share the same shape — separate
// instances let us invalidate them independently.
CoverCache& system_logo_cache() {
    static CoverCache c;
    return c;
}
CoverCache& backdrop_cache() {
    static CoverCache c;
    return c;
}
CoverCache& theme_bg_cache() {
    static CoverCache c;
    return c;
}
CoverCache& system_splash_cache() {
    static CoverCache c;
    return c;
}

// Per-game scraped metadata, keyed by "<system_folder>/<rom_stem>". Cleared
// when the user rescans or runs a fresh scrape so the sidebar picks up the
// newly-written sidecar files.
struct MetaCache {
    std::unordered_map<std::string, library::GameMeta> map;
    const library::GameMeta& get(std::string_view sys, std::string_view stem) {
        std::string key{sys};
        key.push_back('/');
        key.append(stem);
        auto it = map.find(key);
        if (it == map.end()) {
            it = map.emplace(key, library::load_meta(sys, stem)).first;
        }
        return it->second;
    }
    void clear() { map.clear(); }
};
MetaCache& meta_cache() {
    static MetaCache c;
    return c;
}

// Resolve the per-system splash image. Tries (in order):
//   1. <pack_dir>/systems/<folder>/splash.png  — when a theme pack is active
//   2. /foyer/assets/systems/<folder>-splash.png — SD override
//   3. /foyer/assets/systems/<folder>.jpg        — legacy SD JPG
//   4. romfs:/systems/<folder>-splash.png        — bundled artwork
// PNG used so the parallelogram alpha in the bundled art is preserved.
// Virtual-system folder names start with "__" to distinguish them
// from real-on-SD system folders. The bundled tile art lives under
// auto-* names because __-prefixed filenames look weird in a file
// browser, so map between the two before resolving.
std::string_view tile_asset_folder(std::string_view folder) {
    if (folder == "__recent")    return "auto-lastplayed";
    if (folder == "__favorites") return "auto-favorites";
    if (folder == "__unknown")   return "auto-allgames";
    return folder;
}

std::string system_splash_path(std::string_view folder) {
    auto exists = [](const std::string& p) {
        struct stat st{};
        if (::stat(p.c_str(), &st) == 0) return true;
        std::ifstream f{p};
        return (bool)f;
    };
    folder = tile_asset_folder(folder);
    const auto& th = theme();
    if (!th.pack_dir.empty()) {
        std::string p = th.pack_dir + "/systems/" + std::string{folder} + "/splash.png";
        if (exists(p)) return p;
        p = th.pack_dir + "/systems/" + std::string{folder} + "/splash.jpg";
        if (exists(p)) return p;
    }
    char sd_png[256], sd_jpg[256];
    std::snprintf(sd_png, sizeof(sd_png), "/foyer/assets/systems/%.*s-splash.png",
        (int)folder.size(), folder.data());
    if (exists(sd_png)) return sd_png;
    std::snprintf(sd_jpg, sizeof(sd_jpg), "/foyer/assets/systems/%.*s.jpg",
        (int)folder.size(), folder.data());
    if (exists(sd_jpg)) return sd_jpg;
    char rf[256];
    std::snprintf(rf, sizeof(rf), "romfs:/systems/%.*s-splash.png",
        (int)folder.size(), folder.data());
    return rf;
}

std::string system_logo_path(std::string_view folder) {
    // Resolution: theme pack → SD override → bundled romfs.
    auto exists = [](const std::string& p) {
        struct stat st{};
        if (::stat(p.c_str(), &st) == 0) return true;
        // stat doesn't always traverse libnx's romfs devoptab; fall back to
        // an fopen probe.
        std::ifstream f{p};
        return (bool)f;
    };
    folder = tile_asset_folder(folder);
    const auto& th = theme();
    if (!th.pack_dir.empty()) {
        std::string p = th.pack_dir + "/systems/" + std::string{folder} + "/logo.png";
        if (exists(p)) return p;
    }
    char sd[256];
    std::snprintf(sd, sizeof(sd), "/foyer/assets/systems/%.*s.png",
        (int)folder.size(), folder.data());
    if (exists(sd)) return sd;
    char rf[256];
    std::snprintf(rf, sizeof(rf), "romfs:/systems/%.*s.png",
        (int)folder.size(), folder.data());
    return rf;
}
std::string backdrop_path(std::string_view folder, std::string_view stem) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "/foyer/assets/backgrounds/%.*s/%.*s.jpg",
        (int)folder.size(), folder.data(),
        (int)stem.size(), stem.data());
    return buf;
}

// Aspect-fit blit a cached nanovg image into the given rect with rounded
// corners. No-op if the handle wasn't found on disk.
void blit_aspect_fit(NVGcontext* vg, int handle,
                     float x, float y, float w, float h, float radius,
                     float alpha = 1.0f) {
    if (handle <= 0) return;
    int iw = 0, ih = 0;
    nvgImageSize(vg, handle, &iw, &ih);
    if (iw <= 0 || ih <= 0) return;
    const float ar_img = (float)iw / (float)ih;
    const float ar_box = w / h;
    float dw = w, dh = h;
    if (ar_img > ar_box) dh = w / ar_img;
    else                 dw = h * ar_img;
    const float dx = x + (w - dw) * 0.5f;
    const float dy = y + (h - dh) * 0.5f;
    auto pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.f, handle, alpha);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, dx, dy, dw, dh, radius);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

// Same as blit_aspect_fit but with a true silhouette drop shadow:
// renders the same image again, offset down-right, with its
// pixels tinted to a translucent black via the NVGpaint's
// innerColor field. nvgImagePattern doesn't expose a tint
// parameter publicly, but the fragment shader multiplies the
// sampled texel by innerColor — so swapping innerColor on the
// returned paint gives us a silhouette shape that follows the
// logo's alpha mask, rather than a rectangle behind its bounding
// box.
void blit_aspect_fit_with_shadow(NVGcontext* vg, int handle,
                                 float x, float y, float w, float h,
                                 float radius, float alpha = 1.0f,
                                 float shadow_dx = 4.0f,
                                 float shadow_dy = 6.0f,
                                 NVGcolor shadow_tint =
                                    {{{ 0, 0, 0, 0.55f }}}) {
    if (handle <= 0) return;
    int iw = 0, ih = 0;
    nvgImageSize(vg, handle, &iw, &ih);
    if (iw <= 0 || ih <= 0) return;
    const float ar_img = (float)iw / (float)ih;
    const float ar_box = w / h;
    float dw = w, dh = h;
    if (ar_img > ar_box) dh = w / ar_img;
    else                 dw = h * ar_img;
    const float dx = x + (w - dw) * 0.5f;
    const float dy = y + (h - dh) * 0.5f;

    // Silhouette shadow — image rendered offset, tinted black via
    // innerColor. The image's transparent pixels stay transparent;
    // opaque pixels render as the tint color.
    NVGpaint shadow = nvgImagePattern(vg,
        dx + shadow_dx, dy + shadow_dy, dw, dh, 0.0f, handle, 1.0f);
    shadow.innerColor = shadow_tint;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, dx + shadow_dx, dy + shadow_dy, dw, dh, radius);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);

    // Real logo on top.
    NVGpaint pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.0f, handle, alpha);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, dx, dy, dw, dh, radius);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

// Aspect-fill (cover) a cached image across the full rect — used for the
// blurred-out backdrop behind the system + detail panels.
void blit_cover(NVGcontext* vg, int handle, float x, float y, float w, float h,
                float alpha) {
    if (handle <= 0) return;
    int iw = 0, ih = 0;
    nvgImageSize(vg, handle, &iw, &ih);
    if (iw <= 0 || ih <= 0) return;
    const float ar_img = (float)iw / (float)ih;
    const float ar_box = w / h;
    float dw, dh;
    if (ar_img > ar_box) {
        dh = h;
        dw = h * ar_img;
    } else {
        dw = w;
        dh = w / ar_img;
    }
    const float dx = x + (w - dw) * 0.5f;
    const float dy = y + (h - dh) * 0.5f;
    auto pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.f, handle, alpha);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

// Aspect-fill an image into a parallelogram-shaped tile. The shape leans
// left (top edge shifted right by `slant`, bottom edge shifted left by the
// same amount) so adjacent tiles interlock when negative-gapped.
void blit_cover_parallelogram(NVGcontext* vg, int handle,
                              float x, float y, float w, float h,
                              float slant, float alpha) {
    if (handle <= 0) return;
    int iw = 0, ih = 0;
    nvgImageSize(vg, handle, &iw, &ih);
    if (iw <= 0 || ih <= 0) return;
    const float ar_img = (float)iw / (float)ih;
    const float ar_box = w / h;
    float dw, dh;
    if (ar_img > ar_box) { dh = h; dw = h * ar_img; }
    else                  { dw = w; dh = w / ar_img; }
    const float dx = x + (w - dw) * 0.5f;
    const float dy = y + (h - dh) * 0.5f;
    auto pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.f, handle, alpha);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x + slant,     y);
    nvgLineTo(vg, x + w,         y);
    nvgLineTo(vg, x + w - slant, y + h);
    nvgLineTo(vg, x,             y + h);
    nvgClosePath(vg);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

// Empty-state helper used when the library has no systems / no games.
void draw_empty(NVGcontext* vg, float w, float h, const char* title, const char* hint) {
    const auto& th = theme();
    nvgFontSize(vg, th.title_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgText(vg, w * 0.5f, h * 0.5f - 16, title, nullptr);
    nvgFontSize(vg, th.body_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgText(vg, w * 0.5f, h * 0.5f + 16, hint, nullptr);
}

// Sphaira-style persistent top bar. Drawn once per frame from draw() — view
// painters set their own title through this helper but no longer manage the
// bar background or underline themselves.
void draw_topbar(NVGcontext* vg, float w, const char* left, const char* right) {
    const auto& th = theme();

    // Bar background — flat against the rest of the chrome, no accent line.
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, kTopBarH);
    nvgFillColor(vg, th.bg_panel);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, kTopBarH - 1.0f, w, 1.0f);
    nvgFillColor(vg, th.border);
    nvgFill(vg);

    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, th.pad, kTopBarH * 0.5f, left, nullptr);

    if (right && right[0]) {
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, w - th.pad, kTopBarH * 0.5f, right, nullptr);
    }
}

// Sphaira-style persistent bottom bar with the context-aware button hints.
void draw_bottombar(NVGcontext* vg, float w, float h, const char* hint) {
    const auto& th = theme();

    const float by = h - kBottomBarH;
    nvgBeginPath(vg);
    nvgRect(vg, 0, by, w, kBottomBarH);
    nvgFillColor(vg, th.bg_panel);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0, by, w, 1.0f);
    nvgFillColor(vg, th.border);
    nvgFill(vg);

    if (hint && hint[0]) {
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, th.pad, by + kBottomBarH * 0.5f, hint, nullptr);
    }

    // Right-aligned version stamp keeps parity with sphaira's "build x.y" hint.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, w - th.pad, by + kBottomBarH * 0.5f,
            FOYER_DISPLAY_VERSION, nullptr);
}

std::string clock_label() {
    std::time_t now = std::time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

// Round-rect helper.
void rrect(NVGcontext* vg, float x, float y, float ww, float hh, float r, NVGcolor fill) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, ww, hh, r);
    nvgFillColor(vg, fill);
    nvgFill(vg);
}

void rrect_outline(NVGcontext* vg, float x, float y, float ww, float hh, float r, NVGcolor c, float thick) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, ww, hh, r);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, thick);
    nvgStroke(vg);
}

// ---- HOME VIEW (system carousel) ------------------------------------------

// Tile metrics. Selected tile sits in the centre at full size; up to two
// neighbours each side render smaller and fade out.
// Portrait parallelogram tiles. Each tile is a slanted shape (top-right and
// bottom-left corners shifted by kHomeSlant) so adjacent tiles interlock
// exactly — kHomeGap = -kHomeSlant pulls them together along the slanted
// edges matching the ES-DE Art Book Next layout.
constexpr float kHomeTileW = 360.0f;
constexpr float kHomeTileH = 840.0f;
constexpr float kHomeSlant = 84.0f;
constexpr float kHomeGap   = -kHomeSlant;
constexpr float kHomePitch = kHomeTileW + kHomeGap; // 276 — centre-to-centre

// Map a touch position to the system index of the visible tile underneath,
// or -1 if no tile was tapped. Hit boxes use the centre-to-centre pitch so
// adjacent tiles never overlap, which matters for tap disambiguation.
int home_hit_test(float w, float h, std::size_t count,
                  std::size_t idx_centre, float tx, float ty) {
    if (count == 0) return -1;
    const float cy  = h * 0.5f;
    const float top = cy - kHomeTileH * 0.5f;
    const float bot = cy + kHomeTileH * 0.5f;
    if (ty < top || ty > bot) return -1;
    const int n = (int)count;
    for (int off = -2; off <= 2; off++) {
        const float cx    = w * 0.5f + (float)off * kHomePitch;
        const float left  = cx - kHomePitch * 0.5f;
        const float right = cx + kHomePitch * 0.5f;
        if (tx >= left && tx < right) {
            int idx = (int)idx_centre + off;
            return ((idx % n) + n) % n;
        }
    }
    return -1;
}

void draw_home(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    if (lib.systems.empty()) {
        draw_empty(vg, w, h,
            "No systems found",
            "drop roms into /foyer/roms/<system>/ and rescan");
        return;
    }

    constexpr float kTileW = kHomeTileW;
    constexpr float kTileH = kHomeTileH;
    constexpr float kSlant = kHomeSlant;
    constexpr float kGap   = kHomeGap;

    const auto idx_centre = (int)s.system_index;
    const auto count      = (int)lib.systems.size();
    const float cy        = h * 0.5f;

    // Two passes so the centre tile's logo overlay isn't clipped by the
    // adjacent (offset +1 / +2) tiles. Adjacent parallelograms interlock
    // with an 84 px slant overlap; in a single-pass left-to-right draw
    // the right-hand neighbours paint over the bottom-right wedge of the
    // centre logo. Pass 1: every tile's splash. Pass 2: only the centre
    // tile's logo, on top of all of them.
    auto centre_xy = [&](float& out_x, float& out_y, float& out_tw, float& out_thh) {
        out_tw  = kTileW;
        out_thh = kTileH;
        out_x   = w * 0.5f - out_tw * 0.5f;
        out_y   = cy - out_thh * 0.5f;
    };

    for (int offset = -2; offset <= 2; offset++) {
        if (count <= 0) break;
        // Circular wrap: NES → ... ← saturn so the user always sees two
        // systems on either side of the focus.
        int idx = idx_centre + offset;
        idx = ((idx % count) + count) % count;
        const auto& sys = lib.systems[idx];

        const float tw    = kTileW;
        const float thh   = kTileH;
        const float cx    = w * 0.5f + offset * (kTileW + kGap);
        const float x     = cx - tw * 0.5f;
        const float y     = cy - thh * 0.5f;

        const bool centre = (offset == 0);

        nvgSave(vg);
        // Soft parallelogram drop shadow behind every tile. Offset
        // by a few px and feather via a black-fill that's a hair
        // larger than the tile itself, so the splash sits visually
        // detached from the wallpaper.
        if (centre) {
            constexpr float kShadowDy   = 8.0f;
            constexpr float kShadowGrow = 6.0f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + kSlant - kShadowGrow,
                          y - kShadowGrow + kShadowDy);
            nvgLineTo(vg, x + tw      + kShadowGrow,
                          y - kShadowGrow + kShadowDy);
            nvgLineTo(vg, x + tw - kSlant + kShadowGrow,
                          y + thh + kShadowGrow + kShadowDy);
            nvgLineTo(vg, x           - kShadowGrow,
                          y + thh + kShadowGrow + kShadowDy);
            nvgClosePath(vg);
            nvgFillColor(vg, nvgRGBA(0, 0, 0, 110));
            nvgFill(vg);
        }
        const int strip_h = system_splash_cache().get_or_load(vg,
            system_splash_path(sys.def->folder_name));
        if (strip_h > 0) {
            blit_cover_parallelogram(vg, strip_h,
                x, y, tw, thh, kSlant,
                centre ? 1.0f : 0.55f);
        } else {
            // Fallback: virtual systems (Recent / Favorites) and any
            // real system whose splash png isn't on disk get a solid
            // accent-coloured parallelogram so the slot still reads
            // as a tile rather than a hole.
            const auto& th = theme();
            const auto fill = centre
                ? nvgRGBA((uint8_t)(th.accent.r * 255),
                          (uint8_t)(th.accent.g * 255),
                          (uint8_t)(th.accent.b * 255), 0xE0)
                : nvgRGBA((uint8_t)(th.bg_panel_hi.r * 255),
                          (uint8_t)(th.bg_panel_hi.g * 255),
                          (uint8_t)(th.bg_panel_hi.b * 255), 0xC0);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + kSlant,     y);
            nvgLineTo(vg, x + tw,         y);
            nvgLineTo(vg, x + tw - kSlant, y + thh);
            nvgLineTo(vg, x,              y + thh);
            nvgClosePath(vg);
            nvgFillColor(vg, fill);
            nvgFill(vg);
        }
        nvgRestore(vg);
    }

    // Pass 2: centre logo (ES-DE-style overlay). Drawn after every
    // splash so neighbour tiles can't clip it. Box is full tile width
    // and ~60% tile height so wide wordmarks (e.g. "PlayStation
    // Portable") render large; aspect-fit keeps the logo proportional.
    if (count > 0) {
        const auto& sys = lib.systems[idx_centre % count];
        float x, y, tw, thh;
        centre_xy(x, y, tw, thh);
        nvgSave(vg);
        const auto logo_path = system_logo_path(sys.def->folder_name);
        const int  logo_h    = system_logo_cache().get_or_load(vg, logo_path);
        if (logo_h > 0) {
            // True silhouette drop shadow: re-renders the logo
            // offset down-right with its pixels tinted black via
            // an innerColor hack on the returned NVGpaint. Follows
            // the logo's alpha mask instead of its bounding box.
            blit_aspect_fit_with_shadow(vg, logo_h,
                x, y + thh * 0.20f,
                tw, thh * 0.60f,
                0.0f, 1.0f);
        } else {
            // No logo art on disk — render the system's display name
            // (or short_name fallback) centered in the tile. Same
            // typography as the topbar so it reads as a deliberate
            // label and not a placeholder.
            const auto& th = theme();
            nvgFontSize(vg, 64.0f);
            nvgFillColor(vg, th.bg);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            const std::string label{sys.def->display_name};
            nvgText(vg, x + tw * 0.5f,
                    y + thh * 0.5f,
                    label.c_str(), nullptr);
        }
        nvgRestore(vg);
    }
}

// ---- SYSTEM VIEW (game list + sidebar) ------------------------------------

constexpr float kSystemRowH = 52.0f;
constexpr float kSystemRowFont = 24.0f;

// How many rows fit inside the System view's left list panel for the given
// framebuffer height. Used as the "page" size for shoulder-button paging.
int system_visible_rows(float h) {
    const float content_y = kTopBarH + 16.0f;
    const float content_h = h - content_y - kBottomBarH - 16.0f;
    const int   visible   = (int)((content_h - 16) / kSystemRowH);
    return std::max(1, visible);
}

// Map a touch position inside the System view to a row index in
// `sys.games`, or -1 if the tap missed the list. Mirrors the layout in
// draw_system().
int system_row_hit_test(float w, float h, const library::System& sys,
                        std::size_t game_index, float tx, float ty) {
    const float pad       = theme().pad;
    const float content_y = kTopBarH + 16.0f;
    const float content_h = h - content_y - kBottomBarH - 16.0f;
    const float list_w    = (w - pad * 3.0f) * 0.60f;

    if (tx < pad || tx >= pad + list_w)            return -1;
    if (ty < content_y || ty >= content_y + content_h) return -1;

    const int visible = system_visible_rows(h);
    const int total   = (int)sys.games.size();
    if (total == 0) return -1;

    int first = (int)game_index - visible / 2;
    if (first < 0) first = 0;
    if (first + visible > total) first = std::max(0, total - visible);

    const int row = (int)((ty - content_y - 8) / kSystemRowH);
    if (row < 0 || row >= visible) return -1;
    const int idx = first + row;
    if (idx < 0 || idx >= total)   return -1;
    return idx;
}

void draw_system(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    if (lib.systems.empty()) {
        draw_empty(vg, w, h, "No systems", "");
        return;
    }
    const auto& sys = lib.systems[s.system_index];

    // Faded full-screen backdrop. Per-game art (if scraped) wins; falls back
    // to the system splash so every System view has visual identity.
    if (library::config().show_backgrounds) {
        int handle = 0;
        if (!sys.games.empty()) {
            const auto& gsel = sys.games[s.game_index];
            handle = backdrop_cache().get_or_load(vg,
                backdrop_path(sys.def->folder_name, gsel.stem));
        }
        if (handle <= 0) {
            handle = system_splash_cache().get_or_load(vg,
                system_splash_path(sys.def->folder_name));
        }
        if (handle > 0) {
            blit_cover(vg, handle, 0, 0, w, h, 0.30f);
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, w, h);
            nvgFillColor(vg, nvgRGBAf(th.bg.r, th.bg.g, th.bg.b, 0.65f));
            nvgFill(vg);
        }
    }

    // Layout: left list 60%, right sidebar 40%.
    const float content_y  = kTopBarH + 16.0f;
    const float content_h  = h - content_y - kBottomBarH - 16.0f;
    const float list_w     = (w - th.pad * 3.0f) * 0.60f;
    const float side_x     = th.pad + list_w + th.pad;
    const float side_w     = w - side_x - th.pad;

    rrect(vg, th.pad, content_y, list_w, content_h, th.radius, th.bg_panel);
    rrect(vg, side_x, content_y, side_w, content_h, th.radius, th.bg_panel);

    if (sys.games.empty()) {
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, th.pad + list_w * 0.5f, content_y + content_h * 0.5f,
            "no roms in this folder", nullptr);
        return;
    }

    // ---- list ----
    constexpr float kRow = kSystemRowH;
    const int visible = system_visible_rows(h);
    const int total   = (int)sys.games.size();
    int first = (int)s.game_index - visible / 2;
    if (first < 0) first = 0;
    if (first + visible > total) first = std::max(0, total - visible);

    nvgSave(vg);
    nvgIntersectScissor(vg, th.pad, content_y, list_w, content_h);
    for (int row = 0; row < visible && first + row < total; row++) {
        const int idx  = first + row;
        const float ry = content_y + 8 + row * kRow;
        const bool sel = (idx == (int)s.game_index);
        if (sel) {
            rrect(vg, th.pad + 6, ry, list_w - 12, kRow - 4, 6.0f, th.bg_panel_hi);
        }

        nvgFontSize(vg, kSystemRowFont);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        const auto& gr = sys.games[idx];
        // Star prefix for favorites — left-padded so the title text
        // still aligns vertically across favorite and non-favorite
        // rows (the star + space is fixed width).
        const std::string label = gr.favorite
            ? std::string{"\xe2\x98\x85 "} + gr.display   // ★
            : std::string{"   "} + gr.display;
        nvgText(vg, th.pad + 18, ry + (kRow - 4) * 0.5f,
            label.c_str(), nullptr);

        // Right-side label: file extension on real systems; the
        // origin system's short_name on virtual systems so the user
        // can tell what each row belongs to.
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        std::string rhs = gr.ext;
        if (library::is_virtual_system(*sys.def)) {
            if (auto* od = library::origin_system_for_rom(gr.path)) {
                rhs = std::string{od->short_name};
            }
        }
        nvgText(vg, th.pad + list_w - 18, ry + (kRow - 4) * 0.5f,
            rhs.c_str(), nullptr);
    }
    nvgRestore(vg);

    // ---- sidebar ----
    const auto& g = sys.games[s.game_index];
    nvgSave(vg);
    nvgIntersectScissor(vg, side_x, content_y, side_w, content_h);

    // Box art at the top — given more vertical real estate so it carries
    // the visual weight of the sidebar. Aspect-fit inside a soft frame so
    // both portrait and landscape covers look intentional.
    const std::string cover = scrapers::cover_path(
        sys.def->folder_name, g.stem);
    const float cover_h = 360.0f;
    const float cover_x = side_x + th.pad;
    const float cover_y = content_y + th.pad;
    const float cover_w = side_w - th.pad * 2.0f;

    // Frame: filled panel + 1.5px outline so the cover always reads as a
    // distinct object even when the underlying image is dark or missing.
    rrect(vg, cover_x, cover_y, cover_w, cover_h, 10.0f, th.bg_panel_hi);
    rrect_outline(vg, cover_x, cover_y, cover_w, cover_h, 10.0f,
        nvgRGBAf(th.text_strong.r, th.text_strong.g, th.text_strong.b, 0.18f), 1.5f);

    const int handle = library::config().show_covers
        ? cover_cache().get_or_load(vg, cover)
        : 0;
    if (handle > 0) {
        // Aspect-fit centred inside the cover slot.
        int iw = 0, ih = 0;
        nvgImageSize(vg, handle, &iw, &ih);
        if (iw > 0 && ih > 0) {
            const float ar_img = (float)iw / (float)ih;
            const float ar_box = cover_w / cover_h;
            float dw = cover_w, dh = cover_h;
            if (ar_img > ar_box) { dh = cover_w / ar_img; }
            else                 { dw = cover_h * ar_img; }
            const float dx = cover_x + (cover_w - dw) * 0.5f;
            const float dy = cover_y + (cover_h - dh) * 0.5f;
            auto pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.f, handle, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, dx, dy, dw, dh, 8.0f);
            nvgFillPaint(vg, pat);
            nvgFill(vg);
        }
    } else {
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, cover_x + cover_w * 0.5f,
                cover_y + cover_h * 0.5f,
                "no cover (Y to scrape)", nullptr);
    }

    // ---- info block under the cover ----
    const auto& meta_info = meta_cache().get(sys.def->folder_name, g.stem);

    const float info_x = side_x + th.pad;
    const float info_w = side_w - th.pad * 2.0f;
    float       info_y = cover_y + cover_h + 18.0f;

    // Title — prefer the scraped name when we have one, fall back to the
    // filename-derived display name. This is the largest text in the panel.
    nvgFontSize(vg, th.head_size + 4.0f);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    const char* title_text = !meta_info.title.empty()
        ? meta_info.title.c_str() : g.display.c_str();
    {
        // Manual wrap: nanovg's text-box measure handles this if we feed it
        // a width. Bound the title to two lines to keep the layout stable.
        nvgTextLineHeight(vg, 1.05f);
        nvgTextBox(vg, info_x, info_y, info_w, title_text, nullptr);
        float bounds[4]{};
        nvgTextBoxBounds(vg, info_x, info_y, info_w, title_text, nullptr, bounds);
        info_y = bounds[3] + 6.0f;
    }

    // Subtitle: SYSTEM • YEAR • GENRE — the at-a-glance line.
    char sub[256];
    auto append_sep = [&](char* dst, std::size_t n, const char* part) {
        if (!part || !*part) return;
        if (dst[0]) std::snprintf(dst + std::strlen(dst), n - std::strlen(dst), "  ·  %s", part);
        else        std::snprintf(dst,                     n,                   "%s",     part);
    };
    sub[0] = 0;
    {
        char sys_short[32];
        std::snprintf(sys_short, sizeof(sys_short), "%.*s",
            (int)sys.def->short_name.size(), sys.def->short_name.data());
        append_sep(sub, sizeof(sub), sys_short);
        if (!meta_info.year.empty())  append_sep(sub, sizeof(sub), meta_info.year.c_str());
        if (!meta_info.genre.empty()) append_sep(sub, sizeof(sub), meta_info.genre.c_str());
    }
    nvgFontSize(vg, th.body_size);
    nvgFillColor(vg, th.text_dim);
    nvgText(vg, info_x, info_y, sub, nullptr);
    info_y += th.body_size + 12.0f;

    // Stat rows: label + value pairs. Skipped when the field is empty so a
    // sparse scrape doesn't leave gaps.
    auto stat_row = [&](const char* label, const std::string& value) {
        if (value.empty()) return;
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgText(vg, info_x, info_y, label, nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        nvgText(vg, info_x + info_w, info_y, value.c_str(), nullptr);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        info_y += th.label_size + 8.0f;
    };
    stat_row("Publisher", meta_info.publisher);
    stat_row("Developer", meta_info.developer);
    stat_row("Players",   meta_info.players);
    stat_row("Rating",    meta_info.rating);

    // Achievements — pulled from the rcheevos-written sidecar. Only shows
    // when the player has booted this rom at least once with valid creds.
    if (meta_info.cheevos_total >= 0) {
        char ach[64];
        const int unlocked = meta_info.cheevos_unlocked < 0 ? 0 : meta_info.cheevos_unlocked;
        std::snprintf(ach, sizeof(ach), "%d / %d achievements",
            unlocked, meta_info.cheevos_total);
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, (meta_info.cheevos_total > 0
                          && unlocked == meta_info.cheevos_total)
                         ? th.text_strong : th.text);
        nvgText(vg, info_x, info_y, ach, nullptr);
        info_y += th.label_size + 10.0f;
    }

    // Description — wraps to fill what's left of the sidebar. Clipped by
    // the existing scissor; long synopses simply truncate.
    if (!meta_info.description.empty()) {
        info_y += 4.0f;
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextLineHeight(vg, 1.25f);
        nvgTextBox(vg, info_x, info_y, info_w,
            meta_info.description.c_str(), nullptr);
    }
    nvgTextLineHeight(vg, 1.0f);

    nvgRestore(vg);
}

// ---- TICO-STYLE PLUS MENU ------------------------------------------------

// The popup floats above whatever view is underneath. Items vary by context.
struct PopupItem { const char* label; int op; };
enum PopupOp {
    PopRescan = 1,
    PopSettings,
    PopExit,
    PopBack,
    PopToggleFavorite,   // System view: flips favorite on the focused game
    PopResume,           // Home: jumps to + launches the most-recently-played
    PopSearch,           // Any: opens the Search view
};

std::vector<PopupItem> popup_items_for(View v) {
    switch (v) {
        case View::Home:
            return { {"Search",        PopSearch},
                     {"Resume Last",   PopResume},
                     {"Rescan Games",  PopRescan},
                     {"Settings",      PopSettings},
                     {"Exit",          PopExit} };
        case View::System:
            return { {"Toggle Favorite", PopToggleFavorite},
                     {"Search",          PopSearch},
                     {"Rescan Games",    PopRescan},
                     {"Settings",        PopSettings},
                     {"Back",            PopBack} };
        default:
            return { {"Search",        PopSearch},
                     {"Rescan Games",  PopRescan},
                     {"Settings",      PopSettings},
                     {"Back",          PopBack} };
    }
}

void draw_quit_confirm(NVGcontext* vg, float w, float h, const State& s) {
    if (!s.quit_confirm_open) return;
    const auto& th = theme();

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
    nvgFill(vg);

    constexpr float kCardW = 460.0f;
    constexpr float kCardH = 200.0f;
    const float cx = (w - kCardW) * 0.5f;
    const float cy = (h - kCardH) * 0.5f;

    rrect(vg, cx, cy, kCardW, kCardH, 14.0f, th.bg_panel);
    rrect_outline(vg, cx, cy, kCardW, kCardH, 14.0f, th.border, 1.0f);

    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgText(vg, cx + kCardW * 0.5f, cy + 28, "Quit foyer?", nullptr);

    constexpr float kBtnW = 140.0f;
    constexpr float kBtnH = 56.0f;
    const float by = cy + kCardH - 28 - kBtnH;
    const float yes_x = cx + kCardW * 0.25f - kBtnW * 0.5f;
    const float no_x  = cx + kCardW * 0.75f - kBtnW * 0.5f;

    auto button = [&](float bx, const char* label, bool sel) {
        rrect(vg, bx, by, kBtnW, kBtnH, 10.0f,
              sel ? th.accent : th.bg_panel_hi);
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.bg : th.text_strong);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, bx + kBtnW * 0.5f, by + kBtnH * 0.5f, label, nullptr);
    };
    button(yes_x, "Yes",  s.quit_confirm_index == 0);
    button(no_x,  "No",   s.quit_confirm_index == 1);
}

void draw_update_confirm(NVGcontext* vg, float w, float h, const State& s) {
    if (!s.update_confirm_open) return;
    const auto& th = theme();

    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
    nvgFill(vg);

    constexpr float kCardW = 540.0f;
    constexpr float kCardH = 240.0f;
    const float cx = (w - kCardW) * 0.5f;
    const float cy = (h - kCardH) * 0.5f;

    rrect(vg, cx, cy, kCardW, kCardH, 14.0f, th.bg_panel);
    rrect_outline(vg, cx, cy, kCardW, kCardH, 14.0f, th.border, 1.0f);

    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    const std::string title = "Update foyer to v" + s.foyer_update_version + "?";
    nvgText(vg, cx + kCardW * 0.5f, cy + 24, title.c_str(), nullptr);

    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgText(vg, cx + kCardW * 0.5f, cy + 24 + th.head_size + 12,
        "Downloads foyer.nro to /switch/foyer/foyer.nro.new — applied next boot.",
        nullptr);

    constexpr float kBtnW = 140.0f;
    constexpr float kBtnH = 56.0f;
    const float by = cy + kCardH - 28 - kBtnH;
    const float yes_x = cx + kCardW * 0.25f - kBtnW * 0.5f;
    const float no_x  = cx + kCardW * 0.75f - kBtnW * 0.5f;

    auto button = [&](float bx, const char* label, bool sel) {
        rrect(vg, bx, by, kBtnW, kBtnH, 10.0f,
              sel ? th.accent : th.bg_panel_hi);
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.bg : th.text_strong);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, bx + kBtnW * 0.5f, by + kBtnH * 0.5f, label, nullptr);
    };
    button(yes_x, "Yes", s.update_confirm_index == 0);
    button(no_x,  "No",  s.update_confirm_index == 1);
}

void draw_popup(NVGcontext* vg, float w, float h, const State& s) {
    if (!s.popup_open) return;
    const auto& th = theme();

    // Dim everything underneath.
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.45f));
    nvgFill(vg);

    const auto items = popup_items_for(s.view);
    constexpr float kRow   = 64.0f;
    constexpr float kCardW = 400.0f;
    const     float kCardH = 18.0f * 2 + items.size() * kRow;
    const     float cx = (w - kCardW) * 0.5f;
    const     float cy = (h - kCardH) * 0.5f;

    rrect(vg, cx, cy, kCardW, kCardH, 14.0f, th.bg_panel);
    rrect_outline(vg, cx, cy, kCardW, kCardH, 14.0f, th.border, 1.0f);

    nvgFontSize(vg, th.body_size);
    for (std::size_t i = 0; i < items.size(); i++) {
        const float ry = cy + 18 + i * kRow;
        const bool sel = ((int)i == s.popup_index);
        if (sel) {
            rrect(vg, cx + 12, ry, kCardW - 24, kRow - 8, 10.0f, th.bg_panel_hi);
        }
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, cx + 32, ry + (kRow - 8) * 0.5f, items[i].label, nullptr);
    }
}

// ---- SETTINGS VIEW (Tico-style sidebar + content) ------------------------

namespace settings {

enum class Category : int {
    General = 0,
    Display,
    Audio,
    Library,
    Emulator,
    Accounts,
    Updates,
    Experimental,
    Count_
};

struct CategoryEntry {
    const char* label;
    const char* hint;       // glyph stand-in (kept as a fallback for logs)
};

constexpr CategoryEntry kCategories[(int)Category::Count_] = {
    { "General",      "GEN" },
    { "Display",      "DIS" },
    { "Audio",        "AUD" },
    { "Library",      "LIB" },
    { "Emulator",     "EMU" },
    { "Accounts",     "ACC" },
    { "Updates",      "UPD" },
    { "Experimental", "EXP" },
};

// Vector icons drawn into the sidebar slot. Each draws a small glyph centered
// on (cx, cy) at the given size. Color follows accent-vs-dim of the row.
void draw_icon_general(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Gear: outer gear ring (8 teeth via dashed circle), inner hole.
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, sz * 0.45f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, sz * 0.18f);
    nvgStroke(vg);
    for (int i = 0; i < 8; i++) {
        const float a = i * 6.2832f / 8;
        const float x1 = cx + std::cos(a) * sz * 0.45f;
        const float y1 = cy + std::sin(a) * sz * 0.45f;
        const float x2 = cx + std::cos(a) * sz * 0.62f;
        const float y2 = cy + std::sin(a) * sz * 0.62f;
        nvgBeginPath(vg);
        nvgMoveTo(vg, x1, y1);
        nvgLineTo(vg, x2, y2);
        nvgStroke(vg);
    }
}
void draw_icon_display(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Monitor: rounded screen + stand.
    nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx - sz * 0.55f, cy - sz * 0.40f,
                       sz * 1.10f, sz * 0.70f, 4.0f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - sz * 0.25f, cy + sz * 0.50f);
    nvgLineTo(vg, cx + sz * 0.25f, cy + sz * 0.50f);
    nvgStroke(vg);
}
void draw_icon_audio(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Speaker triangle + sound waves.
    nvgFillColor(vg, c);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - sz * 0.35f, cy - sz * 0.18f);
    nvgLineTo(vg, cx - sz * 0.10f, cy - sz * 0.18f);
    nvgLineTo(vg, cx + sz * 0.15f, cy - sz * 0.40f);
    nvgLineTo(vg, cx + sz * 0.15f, cy + sz * 0.40f);
    nvgLineTo(vg, cx - sz * 0.10f, cy + sz * 0.18f);
    nvgLineTo(vg, cx - sz * 0.35f, cy + sz * 0.18f);
    nvgClosePath(vg);
    nvgFill(vg);
    nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 1.6f);
    for (int i = 0; i < 2; i++) {
        const float r = sz * (0.32f + i * 0.16f);
        nvgBeginPath(vg);
        nvgArc(vg, cx + sz * 0.10f, cy, r, -0.6f, 0.6f, NVG_CW);
        nvgStroke(vg);
    }
}
void draw_icon_library(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // 2x2 dotted grid.
    nvgFillColor(vg, c);
    const float r = sz * 0.10f;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg,
                cx + (col == 0 ? -sz * 0.35f : sz * 0.05f),
                cy + (row == 0 ? -sz * 0.35f : sz * 0.05f),
                sz * 0.30f, sz * 0.30f, r);
            nvgFill(vg);
        }
    }
}
void draw_icon_emulator(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Gamepad silhouette: rounded body + dpad + buttons.
    nvgFillColor(vg, c);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx - sz * 0.50f, cy - sz * 0.22f,
                       sz * 1.00f, sz * 0.45f, 8.0f);
    nvgFill(vg);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f));
    // dpad cross
    nvgBeginPath(vg);
    nvgRect(vg, cx - sz * 0.34f, cy - sz * 0.04f, sz * 0.18f, sz * 0.06f);
    nvgRect(vg, cx - sz * 0.28f, cy - sz * 0.10f, sz * 0.06f, sz * 0.18f);
    nvgFill(vg);
    // Two action buttons.
    nvgBeginPath(vg);
    nvgCircle(vg, cx + sz * 0.22f, cy + 1, sz * 0.07f);
    nvgCircle(vg, cx + sz * 0.36f, cy - sz * 0.06f, sz * 0.07f);
    nvgFill(vg);
}
void draw_icon_accounts(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Key: round head + shaft + tooth.
    nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgCircle(vg, cx - sz * 0.18f, cy, sz * 0.20f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx + sz * 0.02f, cy);
    nvgLineTo(vg, cx + sz * 0.45f, cy);
    nvgMoveTo(vg, cx + sz * 0.30f, cy);
    nvgLineTo(vg, cx + sz * 0.30f, cy + sz * 0.14f);
    nvgMoveTo(vg, cx + sz * 0.42f, cy);
    nvgLineTo(vg, cx + sz * 0.42f, cy + sz * 0.10f);
    nvgStroke(vg);
}
void draw_icon_updates(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Down arrow into a tray (download glyph).
    nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx, cy - sz * 0.40f);
    nvgLineTo(vg, cx, cy + sz * 0.10f);
    nvgMoveTo(vg, cx - sz * 0.18f, cy - sz * 0.08f);
    nvgLineTo(vg, cx,             cy + sz * 0.10f);
    nvgLineTo(vg, cx + sz * 0.18f, cy - sz * 0.08f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - sz * 0.36f, cy + sz * 0.30f);
    nvgLineTo(vg, cx + sz * 0.36f, cy + sz * 0.30f);
    nvgStroke(vg);
}
void draw_icon_experimental(NVGcontext* vg, float cx, float cy, float sz, NVGcolor c) {
    // Erlenmeyer flask outline.
    nvgStrokeColor(vg, c); nvgStrokeWidth(vg, 2.0f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - sz * 0.18f, cy - sz * 0.36f);
    nvgLineTo(vg, cx + sz * 0.18f, cy - sz * 0.36f);
    nvgMoveTo(vg, cx - sz * 0.16f, cy - sz * 0.36f);
    nvgLineTo(vg, cx - sz * 0.16f, cy - sz * 0.10f);
    nvgLineTo(vg, cx - sz * 0.42f, cy + sz * 0.34f);
    nvgLineTo(vg, cx + sz * 0.42f, cy + sz * 0.34f);
    nvgLineTo(vg, cx + sz * 0.16f, cy - sz * 0.10f);
    nvgLineTo(vg, cx + sz * 0.16f, cy - sz * 0.36f);
    nvgStroke(vg);
    // Bubble dot inside.
    nvgFillColor(vg, c);
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy + sz * 0.16f, sz * 0.05f);
    nvgFill(vg);
}

void draw_category_icon(NVGcontext* vg, Category cat, float cx, float cy, float sz, NVGcolor c) {
    switch (cat) {
        case Category::General:      draw_icon_general(vg, cx, cy, sz, c); break;
        case Category::Display:      draw_icon_display(vg, cx, cy, sz, c); break;
        case Category::Audio:        draw_icon_audio(vg, cx, cy, sz, c); break;
        case Category::Library:      draw_icon_library(vg, cx, cy, sz, c); break;
        case Category::Emulator:     draw_icon_emulator(vg, cx, cy, sz, c); break;
        case Category::Accounts:     draw_icon_accounts(vg, cx, cy, sz, c); break;
        case Category::Updates:      draw_icon_updates(vg, cx, cy, sz, c); break;
        case Category::Experimental: draw_icon_experimental(vg, cx, cy, sz, c); break;
        default: break;
    }
}

// Flat list of items inside a category — each row is one of these kinds so
// the input handler can dispatch generically.
enum class ItemKind { Cycle, Toggle, Action, Static, Drill, Subpage };

struct Item {
    ItemKind     kind;
    std::string  label;
    std::string  value;       // displayed right side
    std::string  hint;        // optional dim sub-label below the row
    int          payload = 0; // category-specific opcode
    std::string  data;        // opcode-specific payload (e.g. core name for OpUpdInstallSingleCore)
    int          subpage = 0; // ItemKind::Subpage destination (1-based)
};

// Lazily-populated copy of the foyer-cores manifest for the per-core
// install rows. Populated by main.cpp after the user triggers
// OpUpdRefreshManifest.
struct ManifestCache {
    library::CoreManifest data;
    bool loaded = false;
};
ManifestCache& manifest_cache() {
    static ManifestCache c;
    return c;
}

// Same shape for the cheat / bezel catalogues. Populated by main.cpp
// after a "Refresh <kind> manifest" action; read here to render per-
// pack install rows just like the cores list above.
struct CheatsManifestCache {
    library::CheatManifest data;
    bool loaded = false;
};
CheatsManifestCache& cheats_manifest_cache() {
    static CheatsManifestCache c;
    return c;
}

struct BezelsManifestCache {
    library::BezelManifest data;
    bool loaded = false;
};
BezelsManifestCache& bezels_manifest_cache() {
    static BezelsManifestCache c;
    return c;
}

const char* scraper_label(library::Config::Scraper sc) {
    switch (sc) {
        case library::Config::Scraper::ScreenScraper: return "ScreenScraper";
        case library::Config::Scraper::SteamGridDB:   return "SteamGridDB";
        case library::Config::Scraper::Libretro:
        default:                                      return "libretro-thumbnails";
    }
}

// Item payloads — opaque small ids that the input dispatcher matches on.
enum : int {
    OpScraper = 1, OpTheme, OpRomRoot, OpScanSub,
    OpShowClock, OpShowBg, OpShowCovers,
    OpRescan, OpInvalidateCovers,
    OpUpdScrapeAll, OpUpdInstalledCores, OpUpdInstallCores,
    OpUpdRefreshManifest, OpUpdInstallSingleCore, OpUpdReinstallSingleCore,
    OpUpdCancelJob,
    OpSortMode,
    OpShader,
    OpRunahead,
    OpInstallShaderPresets,
    OpInstallCheatPacks,         // Install/update every cheat pack at once
    OpInstallBezelPacks,         // Install/update every bezel pack at once
    OpRefreshCheatsManifest,     // Pull manifest only (per-pack rows after)
    OpRefreshBezelsManifest,
    OpInstallSingleCheatPack,    // Per-row install for one cheat pack
    OpInstallSingleBezelPack,    // Per-row install for one bezel pack
    OpUpdCheckFoyer, OpUpdInstallFoyer,
    OpEmuSysCore,    // Cycle through available cores for one system.
    OpExpMtp, OpExpMtpAutostart, OpExpDebugLog,
    // Account fields opened via on-screen keyboard.
    OpAccSsDevId, OpAccSsDevPw, OpAccSsUser, OpAccSsPw,
    OpAccSgKey,
    OpAccRaUser, OpAccRaToken,
};

// Maps an OpAcc* opcode to the (path, guide, current value) tuple needed to
// drive swkbd input.
struct AccountField {
    const char* path;
    const char* guide;
    std::string current;
};
AccountField account_field_for(int op) {
    const auto& a = scrapers::accounts();
    switch (op) {
        case OpAccSsDevId: return { "screenscraper.devid",       "ScreenScraper dev ID",         a.screenscraper.devid };
        case OpAccSsDevPw: return { "screenscraper.devpassword", "ScreenScraper dev password",   a.screenscraper.devpassword };
        case OpAccSsUser:  return { "screenscraper.ssid",        "ScreenScraper username",       a.screenscraper.ssid };
        case OpAccSsPw:    return { "screenscraper.sspassword",  "ScreenScraper password",       a.screenscraper.sspassword };
        case OpAccSgKey:   return { "steamgriddb.api_key",       "SteamGridDB API key",          a.steamgriddb.api_key };
        case OpAccRaUser:  return { "retroachievements.user",    "RetroAchievements username",   a.retroachievements.user };
        case OpAccRaToken: return { "retroachievements.token",   "RetroAchievements API token",  a.retroachievements.token };
    }
    return { nullptr, nullptr, {} };
}

std::string swkbd_prompt(const char* guide, const std::string& initial) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return initial;
    swkbdConfigMakePresetDefault(&kbd);
    if (guide) swkbdConfigSetGuideText(&kbd, guide);
    if (!initial.empty()) swkbdConfigSetInitialText(&kbd, initial.c_str());
    swkbdConfigSetType(&kbd, SwkbdType_Normal);
    char out[512] = {};
    const Result rc = swkbdShow(&kbd, out, sizeof(out));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return initial;
    return std::string{out};
}

std::string mask_credential(const std::string& s) {
    if (s.empty()) return "unset";
    if (s.size() <= 4) return std::string(s.size(), '*');
    return s.substr(0, 4) + std::string(s.size() - 4, '*');
}

std::vector<Item> build_items(Category cat, const State& s) {
    std::vector<Item> rows;
    const auto& cfg = library::config();
    switch (cat) {
        case Category::General:
            rows.push_back({ItemKind::Cycle,  "Preferred scraper", scraper_label(cfg.preferred_scraper),
                "Y on a game scrapes via this provider.",            OpScraper});
            rows.push_back({ItemKind::Static, "Rom root",          cfg.rom_root,
                "Edit /foyer/config/general.jsonc to change.",       OpRomRoot});
            rows.push_back({ItemKind::Toggle, "Scan subfolders",   "", "",     OpScanSub});
            break;
        case Category::Display: {
            rows.push_back({ItemKind::Cycle,  "Theme",        cfg.theme_name,
                "Drop palettes into /foyer/config/themes/.",          OpTheme});
            rows.push_back({ItemKind::Toggle, "Show clock",       "", "",     OpShowClock});
            rows.push_back({ItemKind::Toggle, "Show backgrounds", "",
                "Use /foyer/assets/backgrounds/<sys>/<stem>.jpg.",    OpShowBg});
            rows.push_back({ItemKind::Toggle, "Show covers",      "", "",     OpShowCovers});
            // Post-process shader, applied per-frame in every player.
            // Built-in presets ship with the player binary; users can
            // also drop their own /foyer/shaders/<name>.glsl files.
            const char* shader_label = cfg.shader_name.c_str();
            if (cfg.shader_name == "none"        || cfg.shader_name.empty())
                shader_label = "None";
            else if (cfg.shader_name == "scanlines")   shader_label = "Scanlines";
            else if (cfg.shader_name == "crt_simple")  shader_label = "CRT (simple)";
            else if (cfg.shader_name == "lcd_grid")    shader_label = "LCD grid";
            else if (cfg.shader_name == "gb_dmg")      shader_label = "Game Boy DMG";
            else if (cfg.shader_name == "gba_correct") shader_label = "GBA correction";
            rows.push_back({ItemKind::Cycle, "Shader", shader_label,
                "Built-ins or /foyer/shaders/<name>.glsl. Per-game "
                "overrides via per_game.jsonc.", OpShader});
            // Run-ahead trades CPU for visible input-lag reduction.
            // Each enabled frame adds one extra retro_run() per
            // displayed frame, so K=1 ~= 2x core load.
            const char* ra_label = "Off";
            char ra_buf[16];
            if (cfg.runahead_frames > 0) {
                std::snprintf(ra_buf, sizeof(ra_buf),
                    cfg.runahead_frames == 1 ? "%d frame" : "%d frames",
                    cfg.runahead_frames);
                ra_label = ra_buf;
            }
            rows.push_back({ItemKind::Cycle, "Run-ahead", ra_label,
                "Reduces visible input lag by emulating ahead. Costs "
                "extra CPU per frame; some cores can't serialize state "
                "and silently fall back to off.", OpRunahead});
            break;
        }
        case Category::Audio:
            rows.push_back({ItemKind::Static, "System volume controls live in the Switch home menu",
                "", "Per-core audio settings are exposed in the in-game pause overlay.", 0});
            break;
        case Category::Library: {
            rows.push_back({ItemKind::Action, "Rescan library",         "run",     "", OpRescan});
            rows.push_back({ItemKind::Action, "Invalidate cover cache", "refresh", "", OpInvalidateCovers});
            // Sort cycle. Cycling triggers a rescan so the new order
            // takes effect immediately.
            const char* sort_label = "Name";
            switch (library::config().sort_mode) {
                case library::Config::SortMode::Recent:    sort_label = "Recently played"; break;
                case library::Config::SortMode::Playtime:  sort_label = "Playtime";        break;
                case library::Config::SortMode::Favorites: sort_label = "Favorites first"; break;
                case library::Config::SortMode::Name:      sort_label = "Name";            break;
            }
            rows.push_back({ItemKind::Cycle, "Sort games by", sort_label,
                "Left/Right cycles. Re-sort applies on next library rescan.",
                OpSortMode});
            break;
        }
        case Category::Emulator: {
            // Subpage routing — Emulator is split into focused
            // sub-screens (cores, bezels, cheats, shaders, defaults,
            // external standalones) so the top-level Settings list
            // doesn't drown the user in a flat 80-row catalogue.
            //
            // settings_subpage = 0 → top-level (5 Subpage drill rows).
            //                   = 1 → Default core per system
            //                   = 2 → Cores catalog
            //                   = 3 → Bezel packs
            //                   = 4 → Cheat packs
            //                   = 5 → Shader presets
            //                   = 6 → External standalone emulators

            auto file_present = [](const std::string& path) -> bool {
                struct stat st{};
                return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
            };

            if (s.settings_subpage == 0) {
                // Top-level: navigate-only Drill rows.
                Item r1{ItemKind::Subpage, "Default core per system",
                        "configure", "", 0};            r1.subpage = 1;
                Item r2{ItemKind::Subpage, "Cores catalog",
                        "browse / install", "", 0};     r2.subpage = 2;
                Item r3{ItemKind::Subpage, "Bezel packs",
                        "browse / install", "", 0};     r3.subpage = 3;
                Item r4{ItemKind::Subpage, "Cheat packs",
                        "browse / install", "", 0};     r4.subpage = 4;
                Item r5{ItemKind::Subpage, "Shader presets",
                        "browse / install", "", 0};     r5.subpage = 5;
                Item r6{ItemKind::Subpage, "External standalone emulators",
                        "PSP / GC status", "", 0};      r6.subpage = 6;
                rows.push_back(std::move(r1));
                rows.push_back(std::move(r2));
                rows.push_back(std::move(r3));
                rows.push_back(std::move(r4));
                rows.push_back(std::move(r5));
                rows.push_back(std::move(r6));
                break;
            }

            if (s.settings_subpage == 1) {
                // Per-system default core picker — Cycle row per
                // system that has at least one core.
                rows.push_back({ItemKind::Static, "Default core per system",
                    "", "", 0});
                for (const auto& sys : library::all_systems()) {
                    if (sys.cores.empty()) continue;
                    std::string current = std::string{sys.cores.front().name};
                    if (auto* over = library::config().default_core_for(sys.folder_name)) {
                        current = over;
                    }
                    Item it{ItemKind::Cycle,
                            std::string{sys.short_name},
                            current,
                            "",
                            OpEmuSysCore};
                    it.data = sys.folder_name;
                    rows.push_back(std::move(it));
                }
                break;
            }

            if (s.settings_subpage == 2) {
                // Cores catalog: refresh + per-core install rows.
                rows.push_back({ItemKind::Action, "Refresh manifest", "fetch",
                    "Pulls the latest foyer-cores release listing from GitHub.",
                    OpUpdRefreshManifest});

                const auto& mc = manifest_cache();
                if (mc.loaded && !mc.data.cores.empty()) {
                    rows.push_back({ItemKind::Static,
                        std::string{"Available cores (manifest "}
                            + mc.data.version + ")",
                        "", "", 0});
                    for (const auto& c : mc.data.cores) {
                        const bool installed =
                            file_present(std::string{"/foyer/cores/"} + c.nro);
                        Item it;
                        it.kind  = ItemKind::Action;
                        it.label = c.name;
                        it.data  = c.name;
                        if (!installed) {
                            it.value   = "download";
                            it.payload = OpUpdInstallSingleCore;
                        } else {
                            const auto local_v =
                                library::installed_core_version(c.nro);
                            const bool up_to_date =
                                !local_v.empty() && local_v == c.version;
                            if (up_to_date) {
                                it.value   = "installed - reinstall";
                                it.payload = OpUpdReinstallSingleCore;
                            } else {
                                it.value   = "update available";
                                it.payload = OpUpdInstallSingleCore;
                            }
                        }
                        rows.push_back(std::move(it));
                    }
                } else {
                    rows.push_back({ItemKind::Static,
                        "Loading catalog...", "", "", 0});
                }
                break;
            }

            if (s.settings_subpage == 3) {
                // Bezel packs: refresh + per-pack install rows.
                rows.push_back({ItemKind::Action, "Refresh manifest", "fetch",
                    "Pulls the latest foyer-bezels release listing.",
                    OpRefreshBezelsManifest});

                const auto& bmc = bezels_manifest_cache();
                if (bmc.loaded && !bmc.data.packs.empty()) {
                    rows.push_back({ItemKind::Static,
                        std::string{"Bezel packs (manifest "}
                            + bmc.data.version + ")",
                        "", "", 0});
                    for (const auto& p : bmc.data.packs) {
                        Item it;
                        it.kind  = ItemKind::Action;
                        it.label = p.name;
                        it.data  = p.name;
                        const auto local_v =
                            library::installed_bezel_version(p.name);
                        if (local_v.empty()) {
                            it.value = "download";
                        } else if (local_v == p.version) {
                            it.value = "installed - reinstall";
                        } else {
                            it.value = "update available";
                        }
                        it.payload = OpInstallSingleBezelPack;
                        rows.push_back(std::move(it));
                    }
                    rows.push_back({ItemKind::Action,
                        "Install all bezel packs", "run",
                        "Walks every pack above; skips ones already at "
                        "the manifest's version.",
                        OpInstallBezelPacks});
                } else {
                    rows.push_back({ItemKind::Static,
                        "Loading catalog...", "", "", 0});
                }
                break;
            }

            if (s.settings_subpage == 4) {
                // Cheat packs: refresh + per-pack install rows.
                rows.push_back({ItemKind::Action, "Refresh manifest", "fetch",
                    "Pulls the latest foyer-cheats release listing.",
                    OpRefreshCheatsManifest});

                const auto& cmc = cheats_manifest_cache();
                if (cmc.loaded && !cmc.data.packs.empty()) {
                    rows.push_back({ItemKind::Static,
                        std::string{"Cheat packs (manifest "}
                            + cmc.data.version + ")",
                        "", "", 0});
                    for (const auto& p : cmc.data.packs) {
                        Item it;
                        it.kind  = ItemKind::Action;
                        it.label = p.name;
                        it.data  = p.name;
                        const auto local_v =
                            library::installed_cheat_version(p.name);
                        if (local_v.empty()) {
                            it.value = "download";
                        } else if (local_v == p.version) {
                            it.value = "installed - reinstall";
                        } else {
                            it.value = "update available";
                        }
                        it.payload = OpInstallSingleCheatPack;
                        rows.push_back(std::move(it));
                    }
                    rows.push_back({ItemKind::Action,
                        "Install all cheat packs", "run",
                        "Walks every pack above; skips ones already at "
                        "the manifest's version.",
                        OpInstallCheatPacks});
                } else {
                    rows.push_back({ItemKind::Static,
                        "Loading catalog...", "", "", 0});
                }
                break;
            }

            if (s.settings_subpage == 5) {
                // Shader presets: single install-all action (per-preset
                // picker can land later if the catalog grows further).
                rows.push_back({ItemKind::Action, "Install shader presets",
                    "run",
                    "Downloads the foyer-shaders catalogue into "
                    "/foyer/shaders/.",
                    OpInstallShaderPresets});
                break;
            }

            if (s.settings_subpage == 6) {
                // External standalones — read-only status list per
                // configured external_cores entry.
                rows.push_back({ItemKind::Static,
                    "External standalone emulators", "", "", 0});
                if (library::config().external_cores.empty()) {
                    rows.push_back({ItemKind::Static,
                        "(none configured — edit "
                        "/foyer/config/general.jsonc)", "", "", 0});
                } else {
                    for (const auto& ec : library::config().external_cores) {
                        struct stat sst{};
                        const bool present =
                            ::stat(ec.nro_path.c_str(), &sst) == 0;
                        Item it;
                        it.kind  = ItemKind::Static;
                        it.label = ec.folder;
                        it.value = present ? "installed" : "not installed";
                        it.hint  = ec.nro_path;
                        rows.push_back(std::move(it));
                    }
                }
                break;
            }
            break;
        }
        case Category::Accounts: {
            const auto& a = scrapers::accounts();
            rows.push_back({ItemKind::Drill, "ScreenScraper dev ID",
                mask_credential(a.screenscraper.devid),
                "Edited via the on-screen keyboard.", OpAccSsDevId});
            rows.push_back({ItemKind::Drill, "ScreenScraper dev password",
                mask_credential(a.screenscraper.devpassword), "", OpAccSsDevPw});
            rows.push_back({ItemKind::Drill, "ScreenScraper username",
                mask_credential(a.screenscraper.ssid), "", OpAccSsUser});
            rows.push_back({ItemKind::Drill, "ScreenScraper password",
                mask_credential(a.screenscraper.sspassword), "", OpAccSsPw});
            rows.push_back({ItemKind::Drill, "SteamGridDB API key",
                mask_credential(a.steamgriddb.api_key), "", OpAccSgKey});
            rows.push_back({ItemKind::Drill, "RetroAchievements username",
                mask_credential(a.retroachievements.user), "", OpAccRaUser});
            rows.push_back({ItemKind::Drill, "RetroAchievements API token",
                mask_credential(a.retroachievements.token), "", OpAccRaToken});
            break;
        }
        case Category::Updates: {
            // Updates is the "needs your attention" page. Anything
            // already up to date or not installed is hidden — those
            // browse / install flows live under Emulator subpages.
            //
            // Layout in priority order:
            //   1. Active background job (Cancel)
            //   2. Foyer self-update (Check / Install)
            //   3. Scrape all systems (always available)
            //   4. Pending core updates (only the outdated ones)
            //   5. Pending bezel-pack updates
            //   6. Pending cheat-pack updates
            //   7. "Everything up to date" if nothing pending

            const bool any_job_active = s.install_job.active()
                                     || s.foyer_job.active()
                                     || s.scrape_job.active()
                                     || s.refresh_job.active();
            if (any_job_active) {
                rows.push_back({ItemKind::Static,
                    "Background job running", "", "", 0});
                rows.push_back({ItemKind::Action, "Cancel", "stop",
                    "Aborts the in-flight transfer at the next callback.",
                    OpUpdCancelJob});
            }

            // Foyer self-update — install when known, check otherwise.
            if (s.foyer_update_available && !s.foyer_update_version.empty()) {
                rows.push_back({ItemKind::Action,
                    std::string{"Update foyer to v"} + s.foyer_update_version,
                    "install",
                    "Downloads foyer.nro to /switch/foyer/foyer.nro.new — "
                    "applied on next boot.",
                    OpUpdInstallFoyer});
            } else {
                rows.push_back({ItemKind::Action, "Check for foyer update",
                    "check",
                    "Compares this build against the foyer-frontend release.",
                    OpUpdCheckFoyer});
            }

            rows.push_back({ItemKind::Action, "Scrape all systems", "run",
                "Walks every system using the preferred scraper.",
                OpUpdScrapeAll});

            auto file_present = [](const std::string& path) -> bool {
                struct stat st{};
                return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
            };

            // Pending core updates — only outdated entries surface.
            const auto& mc = manifest_cache();
            int pending_total = 0;
            int pending_cores = 0;
            if (mc.loaded && !mc.data.cores.empty()) {
                for (const auto& c : mc.data.cores) {
                    if (!file_present(std::string{"/foyer/cores/"} + c.nro))
                        continue;
                    const auto local_v = library::installed_core_version(c.nro);
                    if (!local_v.empty() && local_v == c.version) continue;
                    if (pending_cores == 0) {
                        rows.push_back({ItemKind::Static,
                            "Pending core updates", "", "", 0});
                        rows.push_back({ItemKind::Action,
                            "Update all cores", "run",
                            "", OpUpdInstallCores});
                    }
                    Item it{ItemKind::Action, c.name, "update", "",
                            OpUpdInstallSingleCore};
                    it.data = c.name;
                    rows.push_back(std::move(it));
                    pending_cores++;
                }
            }
            pending_total += pending_cores;

            // Pending bezel-pack updates.
            const auto& bmc = bezels_manifest_cache();
            int pending_bezels = 0;
            if (bmc.loaded && !bmc.data.packs.empty()) {
                for (const auto& p : bmc.data.packs) {
                    const auto local_v = library::installed_bezel_version(p.name);
                    if (local_v.empty()) continue;          // never installed
                    if (local_v == p.version) continue;     // up to date
                    if (pending_bezels == 0) {
                        rows.push_back({ItemKind::Static,
                            "Pending bezel-pack updates", "", "", 0});
                        rows.push_back({ItemKind::Action,
                            "Update all bezel packs", "run",
                            "", OpInstallBezelPacks});
                    }
                    Item it{ItemKind::Action, p.name, "update", "",
                            OpInstallSingleBezelPack};
                    it.data = p.name;
                    rows.push_back(std::move(it));
                    pending_bezels++;
                }
            }
            pending_total += pending_bezels;

            // Pending cheat-pack updates.
            const auto& cmc = cheats_manifest_cache();
            int pending_cheats = 0;
            if (cmc.loaded && !cmc.data.packs.empty()) {
                for (const auto& p : cmc.data.packs) {
                    const auto local_v = library::installed_cheat_version(p.name);
                    if (local_v.empty()) continue;
                    if (local_v == p.version) continue;
                    if (pending_cheats == 0) {
                        rows.push_back({ItemKind::Static,
                            "Pending cheat-pack updates", "", "", 0});
                        rows.push_back({ItemKind::Action,
                            "Update all cheat packs", "run",
                            "", OpInstallCheatPacks});
                    }
                    Item it{ItemKind::Action, p.name, "update", "",
                            OpInstallSingleCheatPack};
                    it.data = p.name;
                    rows.push_back(std::move(it));
                    pending_cheats++;
                }
            }
            pending_total += pending_cheats;

            if (pending_total == 0) {
                rows.push_back({ItemKind::Static,
                    "Everything is up to date.", "", "", 0});
            }
            break;
        }
        case Category::Experimental:
            rows.push_back({ItemKind::Toggle, "Roms over USB",
                /*value*/ "", "Spin up libhaze MTP scoped to /foyer/roms.",   OpExpMtp});
            rows.push_back({ItemKind::Toggle, "Auto-start USB on boot",
                /*value*/ "", "Skip the manual toggle on every launch.",       OpExpMtpAutostart});
            rows.push_back({ItemKind::Toggle, "Verbose log",
                /*value*/ "", "Write extra diagnostics to /foyer/data/log.txt.", OpExpDebugLog});
            break;
        default: break;
    }
    return rows;
}

// True/false getter for whichever toggle the row points at.
bool toggle_get(int op) {
    const auto& cfg = library::config();
    switch (op) {
        case OpScanSub:          return cfg.scan_subfolders;
        case OpShowClock:        return cfg.show_clock;
        case OpShowBg:           return cfg.show_backgrounds;
        case OpShowCovers:       return cfg.show_covers;
        case OpExpMtpAutostart:  return cfg.mtp_autostart;
        case OpExpDebugLog:      return cfg.debug_log;
        case OpExpMtp:           return mtp_running();
    }
    return false;
}

void toggle_set(int op, bool val) {
    switch (op) {
        case OpScanSub:          library::set_bool("scan_subfolders",  val); break;
        case OpShowClock:        library::set_bool("show_clock",       val); break;
        case OpShowBg:           library::set_bool("show_backgrounds", val); break;
        case OpShowCovers:       library::set_bool("show_covers",      val); break;
        case OpExpMtpAutostart:  library::set_bool("mtp_autostart",    val); break;
        case OpExpDebugLog:      library::set_bool("debug_log",        val); break;
        case OpExpMtp:
            if (val) mtp_start(); else mtp_stop();
            break;
    }
}

// ---- widgets --------------------------------------------------------------

void draw_pill_toggle(NVGcontext* vg, float x, float y, float w, float h,
                      bool on, NVGcolor on_color, NVGcolor off_color) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, h * 0.5f);
    nvgFillColor(vg, on ? on_color : off_color);
    nvgFill(vg);

    const float r   = (h - 6.0f) * 0.5f;
    const float kx  = on ? (x + w - h * 0.5f) : (x + h * 0.5f);
    const float ky  = y + h * 0.5f;
    nvgBeginPath(vg);
    nvgCircle(vg, kx, ky, r);
    nvgFillColor(vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xFF));
    nvgFill(vg);
}

void draw_chevron_right(NVGcontext* vg, float cx, float cy, float size, NVGcolor c) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - size * 0.4f, cy - size * 0.6f);
    nvgLineTo(vg, cx + size * 0.4f, cy);
    nvgLineTo(vg, cx - size * 0.4f, cy + size * 0.6f);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 2.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

// Active-row outline drawn in the accent gradient — sphaira / Tico cue that
// "this is the focused element". Falls back to a flat accent when the theme
// only carries one accent color.
void draw_active_outline(NVGcontext* vg, float x, float y, float w, float h, float r,
                         NVGcolor a, NVGcolor b) {
    auto pat = nvgLinearGradient(vg, x, y, x + w, y, a, b);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgStrokePaint(vg, pat);
    nvgStrokeWidth(vg, 2.5f);
    nvgStroke(vg);
}

// ---- draw -----------------------------------------------------------------

void draw_settings(NVGcontext* vg, float w, float h, const State& s, const Library&) {
    const auto& th = theme();

    constexpr float kSidebarW   = 280.0f;
    constexpr float kSidebarPad = 16.0f;
    constexpr float kSideRowH   = 56.0f;
    constexpr float kCardPad    = 18.0f;

    const float content_y = kTopBarH + 12.0f;
    const float content_h = h - content_y - kBottomBarH - 12.0f;

    // Sidebar panel.
    rrect(vg, kSidebarPad, content_y, kSidebarW, content_h, 14.0f, th.bg_panel);

    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, kSidebarPad + 22, content_y + 28, "foyer", nullptr);
    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgText(vg, kSidebarPad + 22, content_y + 56, "Settings", nullptr);

    const float side_list_y = content_y + 96;
    for (int i = 0; i < (int)Category::Count_; i++) {
        const float ry  = side_list_y + i * kSideRowH;
        const bool sel  = (i == s.settings_category);
        const float rx  = kSidebarPad + 12;
        const float rw  = kSidebarW - 24;

        if (sel) {
            rrect(vg, rx, ry, rw, kSideRowH - 8, 12.0f, th.bg_panel_hi);
            // Gradient outline only when the focus is on the sidebar.
            if (!s.settings_in_content) {
                draw_active_outline(vg, rx, ry, rw, kSideRowH - 8, 12.0f,
                    nvgRGBA(0x6E, 0xC0, 0xFF, 0xFF),
                    nvgRGBA(0xC2, 0x86, 0xFF, 0xFF));
            }
        }

        // Vector icon for the category.
        const float icon_cx = rx + 28;
        const float icon_cy = ry + (kSideRowH - 8) * 0.5f;
        draw_category_icon(vg, (Category)i, icon_cx, icon_cy, 28.0f,
            sel ? th.accent : th.text_dim);

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, rx + 64, ry + (kSideRowH - 8) * 0.5f,
                kCategories[i].label, nullptr);
    }

    // Content panel.
    const float cx = kSidebarPad + kSidebarW + kSidebarPad;
    const float cy = content_y;
    const float cw = w - cx - kSidebarPad;
    const float ch = content_h;

    // Page title line above the card.
    nvgFontSize(vg, th.title_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, cx, cy + 4, kCategories[s.settings_category].label, nullptr);

    const auto rows = build_items((Category)s.settings_category, s);
    if (rows.empty()) return;

    const float card_x = cx;
    const float card_y = cy + 56;
    const float card_w = cw;
    const float card_h = ch - 56;

    rrect(vg, card_x, card_y, card_w, card_h, 14.0f, th.bg_panel);
    if (s.settings_in_content) {
        draw_active_outline(vg, card_x, card_y, card_w, card_h, 14.0f,
            nvgRGBA(0x6E, 0xC0, 0xFF, 0xFF),
            nvgRGBA(0xC2, 0x86, 0xFF, 0xFF));
    }

    // Two row sizes: compact for label-only rows, tall for rows with
    // a subtitle hint. Heights are chosen so the rhythm of an
    // alternating list still reads consistent — the title baseline
    // lands at the same vertical offset within both row variants.
    constexpr float kRowH      = 60.0f;
    constexpr float kRowH_Tall = 80.0f;

    const float inner_x = card_x + kCardPad;
    const float inner_w = card_w - kCardPad * 2.0f;

    auto row_height = [&](const Item& it) {
        return it.hint.empty() ? kRowH : kRowH_Tall;
    };

    // Scroll window keeping focused row visible. Visible count is
    // approximate (rows can vary in height); use compact height for
    // the page-size estimate, the loop below stops once we run out
    // of card_h either way.
    const int  total = (int)rows.size();
    const int  visible_est = std::max(1,
        (int)((card_h - kCardPad * 2.0f) / kRowH));
    int first = s.settings_row - visible_est / 2;
    if (first < 0) first = 0;
    if (first + visible_est > total) first = std::max(0, total - visible_est);

    nvgSave(vg);
    nvgIntersectScissor(vg, card_x, card_y, card_w, card_h);

    float ry = card_y + kCardPad;
    for (int i = first; i < total; i++) {
        const auto& it  = rows[i];
        const float rh  = row_height(it);
        if (ry + rh > card_y + card_h - kCardPad) break;

        const bool sel = s.settings_in_content && (i == s.settings_row);

        if (sel) {
            // Highlight covers the FULL row including the hint band so
            // the selection rectangle doesn't clip the subtitle. Inset
            // a few px on each side for a softer look.
            rrect(vg, inner_x - 4, ry - 2, inner_w + 8, rh - 6,
                  8.0f, th.bg_panel_hi);
        }

        const float title_y = it.hint.empty()
            ? ry + (rh - 12) * 0.5f
            : ry + 24.0f;

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, inner_x + 8, title_y, it.label.c_str(), nullptr);

        const float vx = inner_x + inner_w - 8;
        const float vy = title_y;

        switch (it.kind) {
            case ItemKind::Toggle: {
                const bool on = toggle_get(it.payload);
                draw_pill_toggle(vg,
                    vx - 56, vy - 14, 52.0f, 28.0f,
                    on, nvgRGBA(0x4C, 0xC2, 0x6F, 0xFF), th.border);
                break;
            }
            case ItemKind::Cycle:
            case ItemKind::Static: {
                nvgFontSize(vg, th.body_size);
                nvgFillColor(vg, th.text_dim);
                nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgText(vg, vx, vy, it.value.c_str(), nullptr);
                break;
            }
            case ItemKind::Action: {
                // Verb (e.g. "run") lives in it.value but is shown in
                // the bottom bar — not duplicated on the row.
                break;
            }
            case ItemKind::Drill:
            case ItemKind::Subpage: {
                nvgFontSize(vg, th.body_size);
                nvgFillColor(vg, th.text_dim);
                nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgText(vg, vx - 18, vy, it.value.c_str(), nullptr);
                draw_chevron_right(vg, vx - 8, vy, 7.0f, th.text_dim);
                break;
            }
        }

        if (!it.hint.empty()) {
            nvgFontSize(vg, th.label_size);
            nvgFillColor(vg, th.text_dim);
            nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
            nvgText(vg, inner_x + 8, ry + 48.0f, it.hint.c_str(), nullptr);
        }
        ry += rh;
    }
    nvgRestore(vg);
}

} // namespace settings

void draw_settings(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    settings::draw_settings(vg, w, h, s, lib);
}

// ---- SEARCH VIEW ----------------------------------------------------------
// Global text-search across every System's games. The query is captured via
// libnx swkbd (handled in the input update path); this function only paints
// the result list. Rows are "<system short_name>  <game display>".

void draw_search(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    constexpr float kRow = 48.0f;
    const float content_y = kTopBarH + 16.0f;
    const float content_h = h - content_y - kBottomBarH - 16.0f;

    // Header — query echo + match count.
    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    const std::string head = s.search_query.empty()
        ? std::string{"Search"}
        : std::string{"Search: \""} + s.search_query + "\"";
    nvgText(vg, th.pad, content_y, head.c_str(), nullptr);

    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    char count[64];
    std::snprintf(count, sizeof(count), "%zu match%s",
        s.search_results.size(), s.search_results.size() == 1 ? "" : "es");
    nvgText(vg, th.pad, content_y + th.head_size + 6, count, nullptr);

    if (s.search_results.empty()) {
        if (s.search_query.empty()) {
            draw_empty(vg, w, h,
                "Type to search",
                "Press Y to enter a query");
        } else {
            draw_empty(vg, w, h,
                "No matches",
                "Press Y to refine the query");
        }
        return;
    }

    // Result list.
    const float list_y     = content_y + 64.0f;
    const float list_h     = content_h - 64.0f;
    const int   visible    = std::max(1, (int)((list_h - 16) / kRow));
    const int   total      = (int)s.search_results.size();
    int         first_row  = std::max(0, s.search_index - visible / 2);
    if (first_row + visible > total) first_row = std::max(0, total - visible);

    nvgSave(vg);
    nvgIntersectScissor(vg, th.pad, list_y, w - th.pad * 2.0f, list_h);
    for (int row = 0; row < visible && first_row + row < total; row++) {
        const int idx = first_row + row;
        const float ry = list_y + 8 + row * kRow;
        const bool sel = (idx == s.search_index);
        if (sel) {
            rrect(vg, th.pad + 6, ry, w - th.pad * 2.0f - 12,
                  kRow - 4, 6.0f, th.bg_panel_hi);
        }
        const auto [si, gi] = s.search_results[idx];
        if (si >= lib.systems.size()) continue;
        const auto& sysr = lib.systems[si];
        if (gi >= sysr.games.size()) continue;
        const auto& gr = sysr.games[gi];

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        const std::string label = (gr.favorite ? std::string{"\xe2\x98\x85 "}
                                               : std::string{"   "})
                                + gr.display;
        nvgText(vg, th.pad + 18, ry + (kRow - 4) * 0.5f,
            label.c_str(), nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        const std::string sn{sysr.def->short_name};
        nvgText(vg, w - th.pad - 18, ry + (kRow - 4) * 0.5f,
            sn.c_str(), nullptr);
    }
    nvgRestore(vg);
}

// ---- GAME DETAIL VIEW -----------------------------------------------------

void draw_game_detail(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    if (lib.systems.empty()) { draw_empty(vg, w, h, "No systems", ""); return; }
    const auto& sys = lib.systems[s.system_index];
    if (sys.games.empty())  { draw_empty(vg, w, h, "No games", "");   return; }
    const auto& g = sys.games[s.game_index];
    // When the user opens a game from a virtual carousel tile (Recent /
    // Favorites), `sys.def` is the synthetic SystemDef with no cores.
    // For the core-picker / asset-path / state-slot logic we want the
    // ROM's real origin system. Fall back to sys.def for normal calls
    // so this stays a no-op when the user is in a real system.
    const auto* def = library::is_virtual_system(*sys.def)
        ? library::origin_system_for_rom(g.path)
        : sys.def;
    if (!def) def = sys.def;   // last-resort safety

    // Backdrop behind the detail panels for visual context.
    if (library::config().show_backgrounds) {
        const int handle = backdrop_cache().get_or_load(vg,
            backdrop_path(def->folder_name, g.stem));
        if (handle > 0) {
            blit_cover(vg, handle, 0, 0, w, h, 0.35f);
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, w, h);
            nvgFillColor(vg, nvgRGBAf(th.bg.r, th.bg.g, th.bg.b, 0.55f));
            nvgFill(vg);
        }
    }

    const float content_y = kTopBarH + 16.0f;
    const float content_h = h - content_y - kBottomBarH - 16.0f;

    // Left: cover. Right: core picker.
    const float left_w  = (w - th.pad * 3.0f) * 0.45f;
    const float right_x = th.pad + left_w + th.pad;
    const float right_w = w - right_x - th.pad;

    rrect(vg, th.pad, content_y, left_w, content_h, th.radius, th.bg_panel);
    rrect(vg, right_x, content_y, right_w, content_h, th.radius, th.bg_panel);

    // Cover.
    const std::string cover = scrapers::cover_path(def->folder_name, g.stem);
    const float cx = th.pad + th.pad;
    const float cy = content_y + th.pad;
    const float cw = left_w - th.pad * 2.0f;
    const float ch = content_h - th.pad * 2.0f - 60.0f;
    const int handle = library::config().show_covers
        ? cover_cache().get_or_load(vg, cover)
        : 0;
    if (handle > 0) {
        int iw = 0, ih = 0;
        nvgImageSize(vg, handle, &iw, &ih);
        if (iw > 0 && ih > 0) {
            const float ar_img = (float)iw / (float)ih;
            const float ar_box = cw / ch;
            float dw = cw, dh = ch;
            if (ar_img > ar_box) dh = cw / ar_img;
            else                 dw = ch * ar_img;
            const float dx = cx + (cw - dw) * 0.5f;
            const float dy = cy + (ch - dh) * 0.5f;
            auto pat = nvgImagePattern(vg, dx, dy, dw, dh, 0.f, handle, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, dx, dy, dw, dh, 8.0f);
            nvgFillPaint(vg, pat);
            nvgFill(vg);
        }
    } else {
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, cx + cw * 0.5f, cy + ch * 0.5f, "no cover", nullptr);
    }

    // File ext meta below cover.
    char meta[128];
    std::snprintf(meta, sizeof(meta), ".%s", g.ext.c_str());
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    nvgText(vg, cx, cy + ch + 36, meta, nullptr);

    // Core picker + per-game knob rows. Layout in this order:
    //   - Continue (if a save state exists)
    //   - Shader   (per-game override; cycles via L/R, A clears it)
    //   - Run-ahead (same)
    //   - Core 1..N
    //
    // detail_core_index navigates the whole list; helpers below map
    // the index back to a row kind so input dispatch stays linear.
    const auto* resolved   = library::resolve_core(*def, g.path);
    const auto  per_game   = library::per_game_core_for(g.path);
    const char* per_sys    = library::config().default_core_for(def->folder_name);
    const int   resume_slot = latest_state_slot(sys, g);
    const bool  has_resume  = (resume_slot >= 0);

    // Resolve the active per-game knob values (per-game override
    // beats the Config default; we render whichever wins).
    const auto pg_shader   = library::per_game_shader(g.path);
    const auto pg_runahead = library::per_game_runahead(g.path);
    const std::string eff_shader =
        !pg_shader.empty() ? pg_shader
                           : (library::config().shader_name.empty()
                                ? std::string{"none"}
                                : library::config().shader_name);
    const int eff_runahead =
        pg_runahead >= 0 ? pg_runahead
                         : library::config().runahead_frames;

    auto pretty_shader = [](std::string_view name) -> std::string {
        if (name.empty() || name == "none") return "Off";
        if (name == "scanlines")   return "Scanlines";
        if (name == "crt_simple")  return "CRT (simple)";
        if (name == "lcd_grid")    return "LCD grid";
        if (name == "gb_dmg")      return "Game Boy DMG";
        if (name == "gba_correct") return "GBA correction";
        return std::string{name};
    };

    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, right_x + th.pad, content_y + th.pad,
            has_resume ? "Continue / Core" : "Core", nullptr);

    constexpr float kRow = 56.0f;
    float ry = content_y + th.pad + 40.0f;
    int row = 0;

    if (has_resume) {
        const bool sel = (row == (int)s.detail_core_index);
        if (sel) {
            rrect(vg, right_x + th.pad * 0.5f, ry, right_w - th.pad,
                  kRow - 6, 8.0f, th.bg_panel_hi);
        }
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, right_x + th.pad + 8, ry + (kRow - 6) * 0.5f - 8,
                "Continue", nullptr);

        char hint[48];
        if (resume_slot == 0) std::snprintf(hint, sizeof(hint), "quick slot");
        else                  std::snprintf(hint, sizeof(hint), "slot %d", resume_slot);
        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgText(vg, right_x + th.pad + 8, ry + (kRow - 6) * 0.5f + 12,
                hint, nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.accent);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, right_x + right_w - th.pad,
                ry + (kRow - 6) * 0.5f, "resume", nullptr);

        ry += kRow;
        row++;
    }

    // ---- Per-game knob rows (Shader, Run-ahead) ----------------------
    // Both render as Cycle-style: left/right cycles the value, A
    // clears the per-game override (reverts to the general default).
    // The right-aligned tag shows whether the displayed value is
    // overridden ("per-game") or inherited ("default").
    auto draw_knob_row = [&](const char* label,
                             const std::string& value_label,
                             bool overridden) {
        const bool sel = (row == (int)s.detail_core_index);
        if (sel) {
            rrect(vg, right_x + th.pad * 0.5f, ry, right_w - th.pad,
                  kRow - 6, 8.0f, th.bg_panel_hi);
        }
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, right_x + th.pad + 8,
                ry + (kRow - 6) * 0.5f - 8, label, nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgText(vg, right_x + th.pad + 8,
                ry + (kRow - 6) * 0.5f + 12,
                value_label.c_str(), nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.accent);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, right_x + right_w - th.pad,
                ry + (kRow - 6) * 0.5f,
                overridden ? "per-game" : "default", nullptr);
        ry += kRow;
        row++;
    };

    draw_knob_row("Shader", pretty_shader(eff_shader), !pg_shader.empty());

    {
        char buf[24];
        if (eff_runahead == 0)      std::snprintf(buf, sizeof(buf), "Off");
        else if (eff_runahead == 1) std::snprintf(buf, sizeof(buf), "1 frame");
        else                        std::snprintf(buf, sizeof(buf),
                                        "%d frames", eff_runahead);
        draw_knob_row("Run-ahead", buf, pg_runahead >= 0);
    }

    for (const auto& c : def->cores) {
        const bool sel = (row == (int)s.detail_core_index);
        if (sel) {
            rrect(vg, right_x + th.pad * 0.5f, ry, right_w - th.pad,
                  kRow - 6, 8.0f, th.bg_panel_hi);
        }

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        const std::string name{c.name};
        nvgText(vg, right_x + th.pad + 8, ry + (kRow - 6) * 0.5f - 8,
                name.c_str(), nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        const std::string disp{c.display_name};
        nvgText(vg, right_x + th.pad + 8, ry + (kRow - 6) * 0.5f + 12,
                disp.c_str(), nullptr);

        // Tag column.
        std::string tag;
        if (!per_game.empty() && per_game == name)   tag = "per-game";
        else if (per_sys && per_sys == name)         tag = "system default";
        else if (resolved && resolved->name == name) tag = "active";
        else if (row == 0)                            tag = "built-in default";

        if (!tag.empty()) {
            nvgFontSize(vg, th.label_size);
            nvgFillColor(vg, th.accent);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgText(vg, right_x + right_w - th.pad,
                    ry + (kRow - 6) * 0.5f, tag.c_str(), nullptr);
        }

        ry += kRow;
        row++;
    }

}

// Shoulder-button bitmasks. We accept either the upper shoulder (L/R) or the
// trigger (ZL/ZR) so users with grips that hide one or the other still hit
// the carousel/page step.
constexpr std::uint64_t kBtnLeftShoulder  = HidNpadButton_L | HidNpadButton_ZL;
constexpr std::uint64_t kBtnRightShoulder = HidNpadButton_R | HidNpadButton_ZR;

// Touch pixel thresholds. kDragPx promotes a touch from "potential tap" to
// "swipe" (suppressing tap-to-open on release). kStepPx is one carousel
// step's worth of finger travel.
constexpr float kTouchDragPx = 16.0f;
constexpr float kTouchStepPx = 80.0f;
constexpr int   kTapMaxFrames = 30; // ~0.5s

// Single-frame shoulder step: -1 for left, +1 for right, 0 for neither.
// Updates the hold counters in `s`. Generates one step on initial press and
// then auto-repeats after a 30-frame delay (~0.5s), accelerating past 90
// frames (~1.5s) so a long press feels like spinning a wheel.
int shoulder_step(State& s, std::uint64_t held, std::uint64_t down) {
    const bool L = (held & kBtnLeftShoulder)  != 0;
    const bool R = (held & kBtnRightShoulder) != 0;
    s.hold_l_frames = L ? s.hold_l_frames + 1 : 0;
    s.hold_r_frames = R ? s.hold_r_frames + 1 : 0;

    if (down & kBtnLeftShoulder)  return -1;
    if (down & kBtnRightShoulder) return +1;

    auto repeat = [](int hold) -> bool {
        if (hold <= 30) return false;
        const int interval = (hold < 90) ? 8 : 3;
        return ((hold - 30) % interval) == 0;
    };
    if (L && repeat(s.hold_l_frames)) return -1;
    if (R && repeat(s.hold_r_frames)) return +1;
    return 0;
}

void reset_input_state(State& s) {
    s.hold_l_frames = 0;
    s.hold_r_frames = 0;
    s.touch_active  = false;
    s.touch_was_swipe = false;
    s.touch_swipe_acc = 0.0f;
}

} // namespace

void update(State& s, const Library& lib,
            std::uint64_t held, std::uint64_t down,
            const platform::App::Touch& touch,
            float w, float h) {
    s.frame_counter++;
    if (s.banner_ttl > 0) s.banner_ttl--;

    // Quit confirmation modal intercepts everything while open.
    if (s.quit_confirm_open) {
        reset_input_state(s);
        // Tap-to-press on the Yes/No buttons. Layout matches
        // draw_quit_confirm — keep them in sync.
        if (touch.tap_started && touch.count > 0) {
            constexpr float kCardW = 460.0f;
            constexpr float kCardH = 200.0f;
            constexpr float kBtnW  = 140.0f;
            constexpr float kBtnH  = 56.0f;
            const float cx = (w - kCardW) * 0.5f;
            const float cy = (h - kCardH) * 0.5f;
            const float by = cy + kCardH - 28 - kBtnH;
            const float yes_x = cx + kCardW * 0.25f - kBtnW * 0.5f;
            const float no_x  = cx + kCardW * 0.75f - kBtnW * 0.5f;
            const float tx = touch.points[0].x;
            const float ty = touch.points[0].y;
            auto in_btn = [&](float bx) {
                return tx >= bx && tx < bx + kBtnW &&
                       ty >= by && ty < by + kBtnH;
            };
            if (in_btn(yes_x)) {
                s.quit_confirm_index = 0;
                down |= HidNpadButton_A;
            } else if (in_btn(no_x)) {
                s.quit_confirm_index = 1;
                down |= HidNpadButton_A;
            }
        }
        if (down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
            s.quit_confirm_index = 1 - s.quit_confirm_index;
        }
        if (down & HidNpadButton_B) {
            s.quit_confirm_open = false;
            return;
        }
        if (down & HidNpadButton_A) {
            const bool yes = (s.quit_confirm_index == 0);
            s.quit_confirm_open = false;
            if (yes) s.request_quit = true;
        }
        return;
    }

    // Yes/No before downloading the self-update — same modal pattern as
    // the quit confirmation. Default index is 1 (No) so a stray A doesn't
    // blow away whatever the user was about to do.
    if (s.update_confirm_open) {
        reset_input_state(s);
        if (touch.tap_started && touch.count > 0) {
            constexpr float kCardW = 540.0f;
            constexpr float kCardH = 240.0f;
            constexpr float kBtnW  = 140.0f;
            constexpr float kBtnH  = 56.0f;
            const float cx = (w - kCardW) * 0.5f;
            const float cy = (h - kCardH) * 0.5f;
            const float by = cy + kCardH - 28 - kBtnH;
            const float yes_x = cx + kCardW * 0.25f - kBtnW * 0.5f;
            const float no_x  = cx + kCardW * 0.75f - kBtnW * 0.5f;
            const float tx = touch.points[0].x;
            const float ty = touch.points[0].y;
            auto in_btn = [&](float bx) {
                return tx >= bx && tx < bx + kBtnW &&
                       ty >= by && ty < by + kBtnH;
            };
            if (in_btn(yes_x)) {
                s.update_confirm_index = 0;
                down |= HidNpadButton_A;
            } else if (in_btn(no_x)) {
                s.update_confirm_index = 1;
                down |= HidNpadButton_A;
            }
        }
        if (down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
            s.update_confirm_index = 1 - s.update_confirm_index;
        }
        if (down & HidNpadButton_B) {
            s.update_confirm_open = false;
            return;
        }
        if (down & HidNpadButton_A) {
            const bool yes = (s.update_confirm_index == 0);
            s.update_confirm_open = false;
            if (yes) {
                s.request_install_foyer_update = true;
                s.banner_text = "Downloading foyer update...";
                s.banner_ttl  = 180;
            }
        }
        return;
    }

    // Modal popup intercepts all input while open.
    if (s.popup_open) {
        reset_input_state(s);
        const auto items = popup_items_for(s.view);
        const int n = (int)items.size();

        // Tap on a popup row focuses + activates it. Layout matches
        // draw_popup — keep these constants in sync.
        if (touch.tap_started && touch.count > 0 && n > 0) {
            constexpr float kCardW = 460.0f;
            constexpr float kRow   = 64.0f;
            const float cx = (w - kCardW) * 0.5f;
            const float kCardH = 18.0f * 2 + n * kRow;
            const float cy = (h - kCardH) * 0.5f;
            const float tx = touch.points[0].x;
            const float ty = touch.points[0].y;
            if (tx >= cx && tx < cx + kCardW &&
                ty >= cy + 18 && ty < cy + 18 + n * kRow) {
                const int idx = (int)((ty - (cy + 18)) / kRow);
                if (idx >= 0 && idx < n) {
                    s.popup_index = idx;
                    down |= HidNpadButton_A;
                }
            }
        }

        if (down & HidNpadButton_AnyDown) s.popup_index = (s.popup_index + 1) % n;
        if (down & HidNpadButton_AnyUp)   s.popup_index = (s.popup_index - 1 + n) % n;
        // L/R shoulder paging — popup is short, so a single shoulder press
        // jumps to the first / last item. Consistent gesture with longer
        // lists (Settings content, game list).
        if (n > 0) {
            if (const int sh = shoulder_step(s, held, down)) {
                s.popup_index = (sh > 0) ? n - 1 : 0;
            }
        }
        if (down & HidNpadButton_B)    { s.popup_open = false; return; }
        if (down & HidNpadButton_Plus) { s.popup_open = false; return; }
        if (down & HidNpadButton_A) {
            const int op = items[s.popup_index].op;
            s.popup_open = false;
            switch (op) {
                case PopRescan:
                    s.request_rescan = true;
                    s.banner_text = "Rescanning library...";
                    s.banner_ttl  = 180;
                    break;
                case PopSettings:
                    s.view = View::Settings;
                    s.settings_category = 0;
                    s.settings_row = 0;
                    s.settings_in_content = false;
                    break;
                case PopExit:
                    s.request_quit = true;
                    break;
                case PopBack:
                    if (s.view == View::System || s.view == View::GameDetail)
                        s.view = (s.view == View::GameDetail) ? View::System : View::Home;
                    break;
                case PopToggleFavorite: {
                    if (s.view != View::System) break;
                    if (s.system_index >= lib.systems.size()) break;
                    auto& sys2 = const_cast<library::System&>(lib.systems[s.system_index]);
                    if (s.game_index >= sys2.games.size()) break;
                    auto& g2 = sys2.games[s.game_index];
                    g2.favorite = !g2.favorite;
                    library::set_per_game_favorite(g2.path, g2.favorite);
                    s.banner_text = g2.favorite ? "Added to favorites"
                                                : "Removed from favorites";
                    s.banner_ttl  = 120;
                    break;
                }
                case PopResume: {
                    // Find the most-recently-played game across all
                    // systems and launch it directly.
                    std::size_t best_sys = 0, best_game = 0;
                    std::uint64_t best_t = 0;
                    for (std::size_t si = 0; si < lib.systems.size(); si++) {
                        const auto& sysr = lib.systems[si];
                        for (std::size_t gi = 0; gi < sysr.games.size(); gi++) {
                            const auto t = sysr.games[gi].last_played;
                            if (t > best_t) {
                                best_t = t; best_sys = si; best_game = gi;
                            }
                        }
                    }
                    if (best_t == 0) {
                        s.banner_text = "No recently played games";
                        s.banner_ttl  = 180;
                    } else {
                        s.system_index = best_sys;
                        s.game_index   = best_game;
                        s.request_resume_slot = -1;
                        s.request_launch = true;
                    }
                    break;
                }
                case PopSearch:
                    s.view = View::Search;
                    s.search_dirty = true;
                    break;
            }
        }
        return;
    }

    if (lib.systems.empty()) return;

    if (s.view == View::Home) {
        const auto n = lib.systems.size();
        const int  ni = (int)n;
        auto step = [&](int delta) {
            if (ni <= 0) return;
            int idx = (int)s.system_index + delta;
            idx = ((idx % ni) + ni) % ni;
            s.system_index = (std::size_t)idx;
        };

        // D-pad still steps the carousel one tile at a time without repeat.
        if (down & HidNpadButton_AnyRight)     step(+1);
        else if (down & HidNpadButton_AnyLeft) step(-1);

        // Shoulders: initial press steps once; held > 0.5s auto-repeats and
        // accelerates so a long press spins through systems quickly.
        if (const int sh = shoulder_step(s, held, down)) step(sh);

        // Touch: tap on a tile opens it; horizontal drag steps the carousel
        // continuously; release with momentum adds bonus steps.
        if (touch.tap_started && touch.count > 0) {
            s.touch_active      = true;
            s.touch_was_swipe   = false;
            s.touch_start_x     = touch.points[0].x;
            s.touch_start_y     = touch.points[0].y;
            s.touch_last_x      = touch.points[0].x;
            s.touch_start_frame = s.frame_counter;
            s.touch_swipe_acc   = 0.0f;
        }
        if (s.touch_active && touch.count > 0) {
            const float cx = touch.points[0].x;
            const float dx = cx - s.touch_last_x;
            s.touch_last_x = cx;
            s.touch_swipe_acc += dx;

            if (std::abs(cx - s.touch_start_x) > kTouchDragPx) {
                s.touch_was_swipe = true;
            }
            // Drag-step: every kTouchStepPx of finger travel advances by one.
            // Right-swipe (positive dx) reveals the previous tile, matching
            // the natural "drag the strip" mental model.
            while (s.touch_swipe_acc >= kTouchStepPx) {
                step(-1);
                s.touch_swipe_acc -= kTouchStepPx;
            }
            while (s.touch_swipe_acc <= -kTouchStepPx) {
                step(+1);
                s.touch_swipe_acc += kTouchStepPx;
            }
        }
        if (s.touch_active && touch.count == 0) {
            const int dur = (int)(s.frame_counter - s.touch_start_frame);
            if (!s.touch_was_swipe && dur < kTapMaxFrames) {
                const int hit = home_hit_test(w, h, n, s.system_index,
                                              s.touch_start_x, s.touch_start_y);
                if (hit >= 0) {
                    s.system_index = (std::size_t)hit;
                    s.view         = View::System;
                    s.game_index   = 0;
                }
            } else if (s.touch_was_swipe && dur > 0) {
                // Flick momentum: convert lift velocity into bonus steps so
                // a fast swipe spins past several tiles without re-touching.
                const float total_dx = s.touch_last_x - s.touch_start_x;
                const float velocity = total_dx / (float)dur; // px/frame
                if (std::abs(velocity) > 12.0f) {
                    int extra = (int)(std::abs(velocity) / 6.0f);
                    if (extra > 6) extra = 6;
                    const int sign = (velocity > 0) ? -1 : +1;
                    for (int i = 0; i < extra; i++) step(sign);
                }
            }
            s.touch_active    = false;
            s.touch_was_swipe = false;
            s.touch_swipe_acc = 0.0f;
        }

        if (down & HidNpadButton_A) {
            s.view = View::System;
            s.game_index = 0;
        }
        if (down & HidNpadButton_B) {
            s.quit_confirm_open  = true;
            s.quit_confirm_index = 1; // default to "No"
            return;
        }
        if (down & HidNpadButton_Plus) {
            s.popup_open  = true;
            s.popup_index = 0;
            return;
        }
        if (down & HidNpadButton_Minus) {
            s.view = View::Settings;
            s.settings_category   = 0;
            s.settings_row        = 0;
            s.settings_in_content = false;
        }
    } else if (s.view == View::System) {
        const auto& sys = lib.systems[s.system_index];
        if (!sys.games.empty()) {
            const auto total = sys.games.size();
            if (down & HidNpadButton_AnyDown) {
                if (s.game_index + 1 < total) s.game_index++;
            } else if (down & HidNpadButton_AnyUp) {
                if (s.game_index > 0) s.game_index--;
            } else if (down & HidNpadButton_AnyRight) {
                s.game_index = std::min(s.game_index + 10, total - 1);
            } else if (down & HidNpadButton_AnyLeft) {
                s.game_index = (s.game_index >= 10) ? s.game_index - 10 : 0;
            }

            // Shoulders page through the list a screenful at a time, with
            // the same hold-to-spin curve as the carousel.
            const int page = system_visible_rows(h);
            if (const int sh = shoulder_step(s, held, down)) {
                if (sh > 0) {
                    s.game_index = std::min(s.game_index + (std::size_t)page,
                                            total - 1);
                } else {
                    s.game_index = (s.game_index >= (std::size_t)page)
                        ? s.game_index - (std::size_t)page : 0;
                }
            }

            // Touch: a tap on a row launches that game directly. We track
            // start position and treat any non-trivial drag as a non-tap so
            // accidental drags don't fire a launch.
            if (touch.tap_started && touch.count > 0) {
                s.touch_active      = true;
                s.touch_was_swipe   = false;
                s.touch_start_x     = touch.points[0].x;
                s.touch_start_y     = touch.points[0].y;
                s.touch_last_x      = touch.points[0].x;
                s.touch_start_frame = s.frame_counter;
            }
            if (s.touch_active && touch.count > 0) {
                const float cx = touch.points[0].x;
                const float cy = touch.points[0].y;
                if (std::abs(cx - s.touch_start_x) > kTouchDragPx ||
                    std::abs(cy - s.touch_start_y) > kTouchDragPx) {
                    s.touch_was_swipe = true;
                }
            }
            if (s.touch_active && touch.count == 0) {
                const int dur = (int)(s.frame_counter - s.touch_start_frame);
                if (!s.touch_was_swipe && dur < kTapMaxFrames) {
                    const int row = system_row_hit_test(w, h, sys, s.game_index,
                                                        s.touch_start_x,
                                                        s.touch_start_y);
                    if (row >= 0) {
                        s.game_index     = (std::size_t)row;
                        s.request_launch = true;
                    }
                }
                s.touch_active    = false;
                s.touch_was_swipe = false;
            }
        }

        if (down & HidNpadButton_B) {
            s.view = View::Home;
        }
        if (down & HidNpadButton_Plus) {
            s.popup_open  = true;
            s.popup_index = 0;
            return;
        }
        if ((down & HidNpadButton_A) && !sys.games.empty()) {
            s.request_launch = true;
        }
        if ((down & HidNpadButton_X) && !sys.games.empty()) {
            s.view = View::GameDetail;
            // Seed cursor: prefer Continue when a save state exists,
            // else land on the currently resolved core. For a virtual
            // system (Recent / Favorites) the rom's origin SystemDef
            // owns the core list.
            const auto& g = sys.games[s.game_index];
            const auto* eff_def = library::is_virtual_system(*sys.def)
                ? library::origin_system_for_rom(g.path)
                : sys.def;
            if (!eff_def) eff_def = sys.def;
            const bool has_resume = (latest_state_slot(sys, g) >= 0);
            if (has_resume) {
                s.detail_core_index = 0;
            } else {
                const auto* resolved = library::resolve_core(*eff_def, g.path);
                s.detail_core_index = 0;
                if (resolved) {
                    std::size_t i = 0;
                    for (const auto& c : eff_def->cores) {
                        if (c.name == resolved->name) { s.detail_core_index = i; break; }
                        i++;
                    }
                }
            }
        }
        if (down & HidNpadButton_Y) {
            // Y always uses the scraper picked under Settings → General →
            // Preferred scraper (or general.jsonc on disk).
            switch (foyer::library::config().preferred_scraper) {
                case foyer::library::Config::Scraper::ScreenScraper:
                    s.request_scrape_kind = State::ScrapeKind::ScreenScraper; break;
                case foyer::library::Config::Scraper::SteamGridDB:
                    s.request_scrape_kind = State::ScrapeKind::SteamGridDB;   break;
                case foyer::library::Config::Scraper::Libretro:
                default:
                    s.request_scrape_kind = State::ScrapeKind::Libretro;      break;
            }
        }
    } else if (s.view == View::GameDetail) {
        reset_input_state(s);
        const auto& sys = lib.systems[s.system_index];
        if (sys.games.empty()) {
            s.view = View::System;
            return;
        }
        const auto& g = sys.games[s.game_index];
        // Effective SystemDef: real one for normal systems; the rom's
        // origin SystemDef when the user opened the game from a
        // virtual carousel tile.
        const auto* def = library::is_virtual_system(*sys.def)
            ? library::origin_system_for_rom(g.path)
            : sys.def;
        if (!def || def->cores.empty()) {
            s.view = View::System;
            return;
        }
        const int  resume_slot = latest_state_slot(sys, g);
        const bool has_resume  = (resume_slot >= 0);
        const auto core_count  = def->cores.size();
        // Layout: Continue? + Shader + Run-ahead + cores
        const int  resume_idx   = 0;
        const int  shader_idx   = (has_resume ? 1 : 0);
        const int  runahead_idx = shader_idx + 1;
        const int  cores_start  = runahead_idx + 1;
        const int  row_count    = (int)cores_start + (int)core_count;

        const bool on_resume   = has_resume && s.detail_core_index == resume_idx;
        const bool on_shader   = (int)s.detail_core_index == shader_idx;
        const bool on_runahead = (int)s.detail_core_index == runahead_idx;
        const bool on_core     = (int)s.detail_core_index >= cores_start;
        const auto core_idx    = on_core
                                   ? (s.detail_core_index - cores_start)
                                   : 0;

        if (down & HidNpadButton_AnyDown) {
            if (s.detail_core_index + 1 < (std::size_t)row_count)
                s.detail_core_index++;
        } else if (down & HidNpadButton_AnyUp) {
            if (s.detail_core_index > 0) s.detail_core_index--;
        }

        // L/R: cycle Cycle-row values when focus is on the knob rows;
        // jump to first / last otherwise.
        if (const int sh = shoulder_step(s, held, down); sh != 0) {
            if (on_shader) {
                static const char* kShaderNames[] = {
                    "none", "scanlines", "crt_simple", "lcd_grid",
                    "gb_dmg", "gba_correct",
                };
                constexpr int kCount = (int)(sizeof(kShaderNames) /
                                             sizeof(kShaderNames[0]));
                int cur = 0;
                const std::string current = library::per_game_shader(g.path);
                const std::string ref = current.empty()
                    ? library::config().shader_name : current;
                for (int i = 0; i < kCount; i++) {
                    if (ref == kShaderNames[i]) { cur = i; break; }
                }
                cur = ((cur + sh) % kCount + kCount) % kCount;
                library::set_per_game_shader(g.path, kShaderNames[cur]);
                s.banner_text = std::string{"Per-game shader: "}
                              + kShaderNames[cur];
                s.banner_ttl  = 120;
            } else if (on_runahead) {
                constexpr int kMax = 4;
                const int cur =
                    library::per_game_runahead(g.path) >= 0
                        ? library::per_game_runahead(g.path)
                        : library::config().runahead_frames;
                int next = cur + sh;
                if (next < 0)    next = kMax;
                if (next > kMax) next = 0;
                library::set_per_game_runahead(g.path, next);
                char buf[40];
                std::snprintf(buf, sizeof(buf),
                    next == 0 ? "Per-game run-ahead: off"
                              : (next == 1 ? "Per-game run-ahead: 1 frame"
                                            : "Per-game run-ahead: %d frames"),
                    next);
                s.banner_text = buf;
                s.banner_ttl  = 120;
            } else if (row_count > 0) {
                s.detail_core_index = (sh > 0) ? row_count - 1 : 0;
            }
        }

        if (down & HidNpadButton_B) {
            s.view = View::System;
        }
        if (down & HidNpadButton_A) {
            if (on_resume) {
                s.request_resume_slot = resume_slot;
                s.request_launch      = true;
            } else if (on_shader) {
                library::set_per_game_shader(g.path, "");
                s.banner_text = "Shader override cleared (uses general default)";
                s.banner_ttl  = 180;
            } else if (on_runahead) {
                library::set_per_game_runahead(g.path, -1);
                s.banner_text = "Run-ahead override cleared (uses general default)";
                s.banner_ttl  = 180;
            } else if (on_core) {
                const auto& chosen = def->cores[core_idx];
                library::set_per_game_core(g.path, chosen.name);
                s.banner_text = std::string{"Per-game core set: "}
                              + std::string{chosen.name};
                s.banner_ttl  = 180;
            }
        }
        if (on_core && (down & HidNpadButton_Y)) {
            const auto& chosen = def->cores[core_idx];
            library::set_default_core_for(def->folder_name, chosen.name);
            s.banner_text = std::string{"System default core set: "}
                          + std::string{chosen.name};
            s.banner_ttl  = 180;
        }
        if (on_core && (down & HidNpadButton_X)) {
            library::set_per_game_core(g.path, "");
            s.banner_text = "Per-game override cleared";
            s.banner_ttl  = 180;
        }
    } else if (s.view == View::Settings) {
        // (Don't reset_input_state here — Settings consumes touch taps.)
        using settings::Category;
        const auto rows = settings::build_items((Category)s.settings_category, s);
        const int  row_count = (int)rows.size();

        // Touch handling: tap on a sidebar category switches to it,
        // tap on a content row focuses it and synthesizes A so the
        // existing keyboard dispatch fires the action. Layout constants
        // mirror draw_settings — keep them in sync if either changes.
        if (touch.tap_started && touch.count > 0) {
            constexpr float kSidebarW   = 280.0f;
            constexpr float kSidebarPad = 16.0f;
            constexpr float kSideRowH   = 56.0f;
            constexpr float kCardPad    = 18.0f;
            constexpr float kRowH       = 60.0f;

            const float content_y = kTopBarH + 12.0f;
            const float content_h = h - content_y - kBottomBarH - 12.0f;
            const float tx = touch.points[0].x;
            const float ty = touch.points[0].y;

            // Sidebar zone.
            if (tx >= kSidebarPad && tx < kSidebarPad + kSidebarW) {
                const float side_list_y = content_y + 96;
                const int idx = (int)((ty - side_list_y) / kSideRowH);
                if (idx >= 0 && idx < (int)Category::Count_) {
                    s.settings_category   = idx;
                    s.settings_in_content = false;
                    s.settings_row        = 0;
                }
            }
            // Content card zone.
            else {
                const float card_x = kSidebarPad + kSidebarW + kSidebarPad;
                const float card_y = content_y + 56;
                const float card_w = w - card_x - kSidebarPad;
                const float card_h = content_h - 56;

                if (tx >= card_x && tx < card_x + card_w &&
                    ty >= card_y && ty < card_y + card_h && row_count > 0) {
                    const int visible = std::max(1,
                        (int)((card_h - kCardPad * 2.0f) / kRowH));
                    int first = s.settings_row - visible / 2;
                    if (first < 0) first = 0;
                    if (first + visible > row_count) first = std::max(0, row_count - visible);

                    const int row_in_view = (int)((ty - card_y - kCardPad) / kRowH);
                    if (row_in_view >= 0 && row_in_view < visible &&
                        first + row_in_view < row_count) {
                        const int new_row = first + row_in_view;
                        s.settings_in_content = true;
                        if (new_row == s.settings_row) {
                            // Re-tap on the focused row: act on it.
                            down |= HidNpadButton_A;
                        } else {
                            s.settings_row = new_row;
                        }
                    }
                }
            }
        }

        if (!s.settings_in_content) {
            // Sidebar focus.
            if (down & HidNpadButton_B) {
                s.view = View::Home;
                return;
            }
            if (down & HidNpadButton_AnyDown) {
                s.settings_category = (s.settings_category + 1) % (int)Category::Count_;
                s.settings_row     = 0;
                s.settings_subpage = 0;
            } else if (down & HidNpadButton_AnyUp) {
                s.settings_category = (s.settings_category - 1 + (int)Category::Count_)
                                    % (int)Category::Count_;
                s.settings_row     = 0;
                s.settings_subpage = 0;
            }
            if ((down & (HidNpadButton_A | HidNpadButton_AnyRight)) && row_count > 0) {
                s.settings_in_content = true;
                s.settings_row = 0;
            }
        } else {
            // Content focus.
            if (down & HidNpadButton_B) {
                // From a subpage, B returns to the top-level page of
                // the active category (not all the way out to the
                // sidebar) so users can hop between subpages without
                // losing their place.
                if (s.settings_subpage != 0) {
                    s.settings_subpage = 0;
                    s.settings_row     = 0;
                    return;
                }
                s.settings_in_content = false;
                return;
            }
            // Left d-pad / stick also exits content focus back to
            // the sidebar — natural gesture for "go back to the
            // category list" without reaching for B. From a
            // subpage, Left first backs out to the top-level page
            // (matches the B-back behaviour above).
            if (down & HidNpadButton_AnyLeft) {
                if (s.settings_subpage != 0) {
                    s.settings_subpage = 0;
                    s.settings_row     = 0;
                } else {
                    s.settings_in_content = false;
                }
                return;
            }
            if (down & HidNpadButton_AnyDown && row_count > 0) {
                s.settings_row = (s.settings_row + 1) % row_count;
            } else if (down & HidNpadButton_AnyUp && row_count > 0) {
                s.settings_row = (s.settings_row - 1 + row_count) % row_count;
            }

            // L/R shoulders page through the settings list a screenful at
            // a time, matching the System view's game-list behavior.
            if (row_count > 0) {
                if (const int sh = shoulder_step(s, held, down)) {
                    constexpr float kCardPad = 18.0f;
                    constexpr float kRowH    = 60.0f;
                    const float content_y = kTopBarH + 12.0f;
                    const float content_h = h - content_y - kBottomBarH - 12.0f;
                    const float card_h    = content_h - 56;
                    const int   page = std::max(1,
                        (int)((card_h - kCardPad * 2.0f) / kRowH));
                    int next = s.settings_row + sh * page;
                    if (next < 0) next = 0;
                    if (next >= row_count) next = row_count - 1;
                    s.settings_row = next;
                }
            }

            if (s.settings_row >= row_count) return;
            const auto& it = rows[s.settings_row];

            // Cycle handling: Left/Right rotate the current value.
            if (it.kind == settings::ItemKind::Cycle &&
                (down & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight))) {
                const int delta = (down & HidNpadButton_AnyRight) ? +1 : -1;
                if (it.payload == settings::OpScraper) {
                    int n = ((int)library::config().preferred_scraper + delta + 3) % 3;
                    library::set_preferred_scraper((library::Config::Scraper)n);
                } else if (it.payload == settings::OpEmuSysCore) {
                    // Cycle through the system's cores[] list and persist
                    // the choice. The Cycle item's `data` field carries
                    // the system folder name.
                    if (const auto* sys = library::find_system_by_folder(it.data);
                        sys && !sys->cores.empty()) {
                        const auto count = (int)sys->cores.size();
                        std::string current = std::string{sys->cores.front().name};
                        if (auto* over = library::config().default_core_for(sys->folder_name)) {
                            current = over;
                        }
                        int cur = 0;
                        for (int i = 0; i < count; i++) {
                            if (sys->cores[i].name == current) { cur = i; break; }
                        }
                        const int next = ((cur + delta) % count + count) % count;
                        library::set_default_core_for(sys->folder_name,
                                                      sys->cores[next].name);
                    }
                } else if (it.payload == settings::OpTheme) {
                    const auto themes = list_themes();
                    if (!themes.empty()) {
                        std::size_t cur = 0;
                        const auto& current = library::config().theme_name;
                        for (std::size_t i = 0; i < themes.size(); i++) {
                            if (themes[i] == current) { cur = i; break; }
                        }
                        cur = (delta > 0) ? (cur + 1) % themes.size()
                                          : (cur == 0 ? themes.size() - 1 : cur - 1);
                        library::set_theme_name(themes[cur]);
                        load_theme(themes[cur]);
                        s.request_invalidate_covers = true; // refresh theme_bg
                        s.banner_text = std::string{"Theme: "} + themes[cur];
                        s.banner_ttl  = 120;
                    }
                } else if (it.payload == settings::OpSortMode) {
                    constexpr int kSortCount = 4;
                    int n = ((int)library::config().sort_mode + delta + kSortCount)
                          % kSortCount;
                    library::set_sort_mode((library::Config::SortMode)n);
                    // Trigger a rescan so the new order takes effect.
                    s.request_rescan = true;
                    s.banner_text = "Sort changed - rescanning...";
                    s.banner_ttl  = 180;
                } else if (it.payload == settings::OpShader) {
                    static const char* kShaderNames[] = {
                        "none", "scanlines", "crt_simple", "lcd_grid",
                        "gb_dmg", "gba_correct",
                    };
                    constexpr int kCount = (int)(sizeof(kShaderNames) / sizeof(kShaderNames[0]));
                    int cur = 0;
                    const auto& curr = library::config().shader_name;
                    for (int i = 0; i < kCount; i++) {
                        if (curr == kShaderNames[i]) { cur = i; break; }
                    }
                    cur = ((cur + delta) % kCount + kCount) % kCount;
                    library::set_shader_name(kShaderNames[cur]);
                    s.banner_text = std::string{"Shader: "} + kShaderNames[cur];
                    s.banner_ttl  = 120;
                } else if (it.payload == settings::OpRunahead) {
                    constexpr int kMax = 4;
                    int n = library::config().runahead_frames + delta;
                    if (n < 0)    n = kMax;
                    if (n > kMax) n = 0;
                    library::set_runahead_frames(n);
                    char buf[40];
                    if (n == 0) std::snprintf(buf, sizeof(buf), "Run-ahead: off");
                    else        std::snprintf(buf, sizeof(buf),
                                    n == 1 ? "Run-ahead: %d frame"
                                           : "Run-ahead: %d frames", n);
                    s.banner_text = buf;
                    s.banner_ttl  = 120;
                }
            }

            // A — toggle / action / drill.
            if (down & HidNpadButton_A) {
                switch (it.kind) {
                    case settings::ItemKind::Toggle:
                        settings::toggle_set(it.payload, !settings::toggle_get(it.payload));
                        break;
                    case settings::ItemKind::Action:
                        if (it.payload == settings::OpRescan) {
                            s.request_rescan = true;
                            s.banner_text = "Rescanning library...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInvalidateCovers) {
                            s.request_invalidate_covers = true;
                            s.banner_text = "Cover cache cleared";
                            s.banner_ttl  = 120;
                        } else if (it.payload == settings::OpUpdScrapeAll) {
                            s.banner_text = "Open a system and press Y — bulk scrape next pass";
                            s.banner_ttl  = 240;
                        } else if (it.payload == settings::OpUpdInstallCores) {
                            s.request_install_cores = true;
                            s.install_only_core.clear();
                            s.banner_text = "Fetching cores manifest...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpUpdRefreshManifest) {
                            s.request_refresh_manifest = true;
                            s.banner_text = "Fetching cores manifest...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInstallShaderPresets) {
                            s.request_install_shaders = true;
                            s.banner_text = "Fetching shader manifest...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInstallCheatPacks) {
                            s.request_install_cheats = true;
                            s.install_only_cheat.clear();
                            s.banner_text = "Installing all cheat packs...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInstallBezelPacks) {
                            s.request_install_bezels = true;
                            s.install_only_bezel.clear();
                            s.banner_text = "Installing all bezel packs...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpRefreshCheatsManifest) {
                            s.request_refresh_cheats_manifest = true;
                            s.banner_text = "Fetching cheats manifest...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpRefreshBezelsManifest) {
                            s.request_refresh_bezels_manifest = true;
                            s.banner_text = "Fetching bezels manifest...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInstallSingleCheatPack) {
                            s.request_install_cheats = true;
                            s.install_only_cheat = it.data;
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "Installing cheat pack: %s...", it.data.c_str());
                            s.banner_text = buf;
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpInstallSingleBezelPack) {
                            s.request_install_bezels = true;
                            s.install_only_bezel = it.data;
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "Installing bezel pack: %s...", it.data.c_str());
                            s.banner_text = buf;
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpUpdInstallSingleCore) {
                            s.request_install_cores = true;
                            s.install_only_core    = it.data;
                            s.install_force        = false;
                            s.banner_text = std::string{"Installing "} + it.data + "...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpUpdReinstallSingleCore) {
                            s.request_install_cores = true;
                            s.install_only_core    = it.data;
                            s.install_force        = true;
                            s.banner_text = std::string{"Re-installing "} + it.data + "...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpUpdCheckFoyer) {
                            s.request_check_foyer_update = true;
                            s.banner_text = "Checking for foyer update...";
                            s.banner_ttl  = 180;
                        } else if (it.payload == settings::OpUpdInstallFoyer) {
                            s.update_confirm_open  = true;
                            s.update_confirm_index = 1; // safer default
                        } else if (it.payload == settings::OpUpdCancelJob) {
                            // Hit every job — only the active one
                            // observes the cancel.
                            s.install_job.cancel();
                            s.foyer_job.cancel();
                            s.scrape_job.cancel();
                            s.refresh_job.cancel();
                            s.banner_text = "Cancelling...";
                            s.banner_ttl  = 180;
                        }
                        break;
                    case settings::ItemKind::Drill: {
                        const int op = it.payload;
                        const bool is_account =
                            op == settings::OpAccSsDevId || op == settings::OpAccSsDevPw ||
                            op == settings::OpAccSsUser  || op == settings::OpAccSsPw    ||
                            op == settings::OpAccSgKey   ||
                            op == settings::OpAccRaUser  || op == settings::OpAccRaToken;
                        if (is_account) {
                            const auto field = settings::account_field_for(op);
                            const auto entered = settings::swkbd_prompt(
                                field.guide, field.current);
                            if (!entered.empty() || !field.current.empty()) {
                                scrapers::set_account_field(field.path, entered);
                                scrapers::reload_accounts();
                                s.banner_text = std::string{field.guide} + " saved";
                                s.banner_ttl  = 120;
                            }
                        } else {
                            s.banner_text = "Drill-down view comes in the next pass";
                            s.banner_ttl  = 180;
                        }
                        break;
                    }
                    case settings::ItemKind::Subpage: {
                        // Navigate into the subpage. Reset the row
                        // cursor so we land on the first entry of the
                        // sub-list instead of wherever we were on the
                        // top-level page.
                        s.settings_subpage = it.subpage;
                        s.settings_row     = 0;
                        // Auto-fetch the relevant manifest if it's not
                        // loaded yet — Cores subpage triggers cores
                        // refresh, Bezels subpage triggers bezels, etc.
                        // Single-shot per subpage entry; the user can
                        // still hit "Refresh manifest" to force-reload.
                        switch (it.subpage) {
                            case 2: { // Cores catalog
                                if (!settings::manifest_cache().loaded) {
                                    s.request_refresh_manifest = true;
                                }
                                break;
                            }
                            case 3: { // Bezel packs
                                if (!settings::bezels_manifest_cache().loaded) {
                                    s.request_refresh_bezels_manifest = true;
                                }
                                break;
                            }
                            case 4: { // Cheat packs
                                if (!settings::cheats_manifest_cache().loaded) {
                                    s.request_refresh_cheats_manifest = true;
                                }
                                break;
                            }
                            default: break;
                        }
                        break;
                    }
                    default: break;
                }
            }
        }
    } else if (s.view == View::Search) {
        // First entry into the view (or after the user requests a new
        // query): block on swkbd, recompute the result list, store
        // results into State for draw() to render.
        if (s.search_dirty) {
            s.search_dirty = false;
            const auto entered = settings::swkbd_prompt(
                "Search games by name", s.search_query);
            s.search_query = entered;
            s.search_results.clear();
            s.search_index = 0;
            // Lower-case the query once for case-insensitive substring
            // matching against game.display.
            std::string q = entered;
            for (auto& c : q) {
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            }
            if (!q.empty()) {
                for (std::size_t si = 0; si < lib.systems.size(); si++) {
                    const auto& sysr = lib.systems[si];
                    for (std::size_t gi = 0; gi < sysr.games.size(); gi++) {
                        const auto& gr = sysr.games[gi];
                        std::string disp = gr.display;
                        for (auto& c : disp) {
                            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                        }
                        if (disp.find(q) != std::string::npos) {
                            s.search_results.emplace_back(si, gi);
                        }
                    }
                }
            }
        }

        if (down & HidNpadButton_B) {
            s.view = View::Home;
            return;
        }
        if (down & HidNpadButton_Y) {
            // Re-prompt for a new query.
            s.search_dirty = true;
            return;
        }
        const int n = (int)s.search_results.size();
        if (n > 0) {
            if (down & HidNpadButton_AnyDown && s.search_index + 1 < n)
                s.search_index++;
            if (down & HidNpadButton_AnyUp   && s.search_index > 0)
                s.search_index--;
            if (const int sh = shoulder_step(s, held, down)) {
                const int page = std::max(1, (int)((h - kTopBarH - kBottomBarH - 96) / 48));
                int next = s.search_index + sh * page;
                if (next < 0)  next = 0;
                if (next >= n) next = n - 1;
                s.search_index = next;
            }
            // Tap on a result row maps to a click on that row, mirroring
            // the System view's tap-to-launch behavior.
            if (touch.tap_started && touch.count > 0) {
                const float content_y = kTopBarH + 16.0f;
                const float list_y    = content_y + 64.0f;
                const float list_h    = h - content_y - kBottomBarH - 16.0f - 64.0f;
                constexpr float kRow  = 48.0f;
                const int visible = std::max(1, (int)((list_h - 16) / kRow));
                int first_row     = std::max(0, s.search_index - visible / 2);
                if (first_row + visible > n) first_row = std::max(0, n - visible);
                const float ty = touch.points[0].y;
                const int row_in_view = (int)((ty - list_y - 8) / kRow);
                if (row_in_view >= 0 && row_in_view < visible &&
                    first_row + row_in_view < n) {
                    const int new_idx = first_row + row_in_view;
                    if (new_idx == s.search_index) {
                        down |= HidNpadButton_A;
                    } else {
                        s.search_index = new_idx;
                    }
                }
            }
            if (down & HidNpadButton_A) {
                const auto [si, gi] = s.search_results[s.search_index];
                if (si < lib.systems.size() &&
                    gi < lib.systems[si].games.size()) {
                    s.system_index = si;
                    s.game_index   = gi;
                    s.view         = View::System;
                }
            }
        }
    }

    (void)held;
}

void invalidate_cover_cache(NVGcontext* vg) {
    cover_cache().clear(vg);
    system_logo_cache().clear(vg);
    backdrop_cache().clear(vg);
    theme_bg_cache().clear(vg);
    system_splash_cache().clear(vg);
    meta_cache().clear();
}

void set_manifest_cache(library::CoreManifest manifest) {
    auto& mc = settings::manifest_cache();
    mc.data   = std::move(manifest);
    mc.loaded = true;
}

void set_cheats_manifest_cache(library::CheatManifest manifest) {
    auto& mc = settings::cheats_manifest_cache();
    mc.data   = std::move(manifest);
    mc.loaded = true;
}

void set_bezels_manifest_cache(library::BezelManifest manifest) {
    auto& mc = settings::bezels_manifest_cache();
    mc.data   = std::move(manifest);
    mc.loaded = true;
}

void draw(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, th.bg);
    nvgFill(vg);

    // Optional theme wallpaper. Drawn first so it sits behind every view's
    // panels but on top of the flat-colour backdrop.
    if (!th.background.empty()) {
        const int handle = theme_bg_cache().get_or_load(vg, th.background);
        if (handle > 0) blit_cover(vg, handle, 0, 0, w, h, 1.0f);
    }

    switch (s.view) {
        case View::Home:       draw_home       (vg, w, h, s, lib); break;
        case View::System:     draw_system     (vg, w, h, s, lib); break;
        case View::GameDetail: draw_game_detail(vg, w, h, s, lib); break;
        case View::Settings:   draw_settings   (vg, w, h, s, lib); break;
        case View::Search:     draw_search     (vg, w, h, s, lib); break;
    }

    // Sphaira-style persistent top + bottom bars, drawn after the view so
    // their accent stays on top of any panel that bumped against the edge.
    using namespace foyer::ui::icons;

    std::string title = "foyer";
    std::string clock = library::config().show_clock ? clock_label() : std::string{};
    std::string hint;
    switch (s.view) {
        case View::Home: {
            if (!lib.systems.empty()) {
                const auto& sys = lib.systems[s.system_index];
                char rhs[180];
                const char* gword = (sys.games.size() == 1) ? "game" : "games";
                if (clock.empty()) {
                    std::snprintf(rhs, sizeof(rhs), "%.*s  ·  %zu %s",
                        (int)sys.def->display_name.size(),
                        sys.def->display_name.data(),
                        sys.games.size(), gword);
                } else {
                    std::snprintf(rhs, sizeof(rhs), "%.*s  ·  %zu %s  ·  %s",
                        (int)sys.def->display_name.size(),
                        sys.def->display_name.data(),
                        sys.games.size(), gword, clock.c_str());
                }
                clock = rhs;
            }
            // Home: hint follows whether anything is loaded. With no
            // systems there's nothing to "enter", so drop the DPad/A
            // pair; otherwise show the full carousel action set.
            if (lib.systems.empty()) {
                hint = std::string{Minus} + " settings   "
                     + Plus + " menu   " + B + " quit";
            } else {
                hint = std::string{DPad} + " pick   "
                     + A + " enter   " + Minus + " settings   "
                     + Plus + " menu   " + B + " quit";
            }
            break;
        }
        case View::System: {
            const auto* sys_ptr = !lib.systems.empty()
                ? &lib.systems[s.system_index] : nullptr;
            if (sys_ptr) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "foyer  >  %.*s",
                    (int)sys_ptr->def->display_name.size(),
                    sys_ptr->def->display_name.data());
                title = buf;
                if (!sys_ptr->games.empty()) {
                    char rhs[64];
                    if (clock.empty()) {
                        std::snprintf(rhs, sizeof(rhs), "%zu / %zu",
                            s.game_index + 1, sys_ptr->games.size());
                    } else {
                        std::snprintf(rhs, sizeof(rhs), "%zu / %zu  ·  %s",
                            s.game_index + 1, sys_ptr->games.size(), clock.c_str());
                    }
                    clock = rhs;
                }
            }
            // System: only show actions that apply right now. An empty
            // system has nothing to launch / detail / scrape, so collapse
            // to just B back. Virtual systems (Recent / Favorites) can't
            // be scraped wholesale, so drop the scrape hint there.
            const bool empty_sys  = !sys_ptr || sys_ptr->games.empty();
            const bool can_scrape = sys_ptr && !library::is_virtual_system(*sys_ptr->def);
            if (empty_sys) {
                hint = std::string{Plus} + " menu   " + B + " back";
            } else {
                hint = std::string{DPad} + " pick   "
                     + A + " launch   " + X + " details";
                if (can_scrape) hint += std::string{"   "} + Y + " scrape";
                hint += std::string{"   "} + Plus + " menu   " + B + " back";
            }
            break;
        }
        case View::GameDetail: {
            const auto* sys_ptr = !lib.systems.empty()
                ? &lib.systems[s.system_index] : nullptr;
            const auto* game_ptr = (sys_ptr && !sys_ptr->games.empty())
                ? &sys_ptr->games[s.game_index] : nullptr;
            if (game_ptr) {
                char buf[200];
                std::snprintf(buf, sizeof(buf), "foyer  >  %.*s  >  %s",
                    (int)sys_ptr->def->short_name.size(),
                    sys_ptr->def->short_name.data(),
                    game_ptr->display.c_str());
                title = buf;
            }
            // GameDetail rows in order: Continue (if save exists),
            // Shader knob, Run-ahead knob, Core entries. Each row
            // type has its own button affordances; pick the hint
            // based on which row currently holds focus.
            if (!game_ptr) {
                hint = std::string{B} + " back";
            } else {
                const auto* def = library::is_virtual_system(*sys_ptr->def)
                    ? library::origin_system_for_rom(game_ptr->path)
                    : sys_ptr->def;
                if (!def) def = sys_ptr->def;
                const int  resume_slot = latest_state_slot(*sys_ptr, *game_ptr);
                const bool has_resume  = (resume_slot >= 0);
                const int  shader_idx   = (has_resume ? 1 : 0);
                const int  runahead_idx = shader_idx + 1;
                const int  cores_start  = runahead_idx + 1;
                const int  cur          = (int)s.detail_core_index;

                if (has_resume && cur == 0) {
                    hint = std::string{DPad} + " pick   "
                         + A + " continue   " + B + " back";
                } else if (cur == shader_idx) {
                    hint = std::string{DPad} + " pick   "
                         + Left + Right + " cycle shader   "
                         + A + " clear override   " + B + " back";
                } else if (cur == runahead_idx) {
                    hint = std::string{DPad} + " pick   "
                         + Left + Right + " cycle run-ahead   "
                         + A + " clear override   " + B + " back";
                } else if (cur >= cores_start && !def->cores.empty()) {
                    hint = std::string{DPad} + " pick   "
                         + A + " set per-game   "
                         + Y + " set sys default   "
                         + X + " clear override   " + B + " back";
                } else {
                    hint = std::string{B} + " back";
                }
            }
            break;
        }
        case View::Settings: {
            const auto cat = (s.settings_category < (int)settings::Category::Count_)
                ? settings::kCategories[s.settings_category].label : "";
            char buf[160];
            std::snprintf(buf, sizeof(buf), "foyer  >  Settings  >  %s", cat);
            title = buf;
            // Hint composes from the focused row's kind + (for Action
            // rows) the row's value field, which holds the verb
            // ("run", "fetch", "install", "stop", ...).  This way
            // `A <verb>` always reads as the literal action behind
            // the focus, no fixed inline "A:" prefix in the row text.
            const auto srows = settings::build_items(
                (settings::Category)s.settings_category, s);
            std::string a_hint;
            std::string lr_hint;
            if (s.settings_in_content && s.settings_row >= 0
                && s.settings_row < (int)srows.size()) {
                const auto& it = srows[s.settings_row];
                switch (it.kind) {
                    case settings::ItemKind::Toggle: a_hint = "toggle"; break;
                    case settings::ItemKind::Drill:  a_hint = "edit";   break;
                    case settings::ItemKind::Cycle:
                        a_hint  = "select";
                        lr_hint = "change";
                        break;
                    case settings::ItemKind::Action:
                        a_hint = it.value.empty() ? std::string{"run"} : it.value;
                        break;
                    case settings::ItemKind::Static: break;
                }
            } else {
                a_hint = "enter";   // sidebar focus — A goes into content
            }
            hint = std::string{DPad} + " navigate";
            if (!lr_hint.empty()) hint += std::string{"   "} + Left + Right + " " + lr_hint;
            if (!a_hint.empty())  hint += std::string{"   "} + A + " " + a_hint;
            hint += std::string{"   "} + B + " back";
            break;
        }
        case View::Search: {
            title = "foyer  >  Search";
            // Search: A only does anything when there's a result row to
            // open. With no results (either no query yet, or no matches)
            // the only useful keys are Y to open the keyboard and B to
            // back out.
            if (s.search_results.empty()) {
                hint = std::string{Y} + " new query   " + B + " back";
            } else {
                hint = std::string{DPad} + " navigate   "
                     + A + " open   " + Y + " new query   " + B + " back";
            }
            break;
        }
    }

    draw_topbar   (vg, w,    title.c_str(), clock.c_str());
    draw_bottombar(vg, w, h, hint.c_str());

    // Modal popup floats over everything, including bars.
    draw_popup(vg, w, h, s);
    draw_quit_confirm(vg, w, h, s);
    draw_update_confirm(vg, w, h, s);

    // Banner (e.g., "Scraping NES…  3 / 24"). Drawn last so nothing covers it.
    if (s.banner_ttl > 0 && !s.banner_text.empty()) {
        const float bw = 460.0f;
        const float bh = 56.0f;
        const float bx = (w - bw) * 0.5f;
        const float by = 12.0f;
        const float a  = std::min(1.0f, (float)s.banner_ttl / 30.0f);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, bx, by, bw, bh, 12.0f);
        nvgFillColor(vg, nvgRGBAf(0.05f, 0.06f, 0.08f, 0.85f * a));
        nvgFill(vg);
        nvgFontSize(vg, 22.0f);
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, a));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, w * 0.5f, by + bh * 0.5f, s.banner_text.c_str(), nullptr);
    }
}

} // namespace foyer::browser
