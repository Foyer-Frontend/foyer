#include "views.hpp"
#include "theme.hpp"
#include "launch.hpp"
#include "mtp.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "platform/log.hpp"
#include "scrapers/accounts.hpp"
#include "scrapers/cache.hpp"
#include "ui/icons.hpp"

#include <switch.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
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

// Resolve the per-system splash image. Tries (in order):
//   1. <pack_dir>/systems/<folder>/splash.png  — when a theme pack is active
//   2. /foyer/assets/systems/<folder>-splash.png — SD override
//   3. /foyer/assets/systems/<folder>.jpg        — legacy SD JPG
//   4. romfs:/systems/<folder>-splash.png        — bundled artwork
// PNG used so the parallelogram alpha in the bundled art is preserved.
std::string system_splash_path(std::string_view folder) {
    auto exists = [](const std::string& p) {
        struct stat st{};
        if (::stat(p.c_str(), &st) == 0) return true;
        std::ifstream f{p};
        return (bool)f;
    };
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

void draw_home(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();

    if (lib.systems.empty()) {
        draw_empty(vg, w, h,
            "No systems found",
            "drop roms into /foyer/roms/<system>/ and rescan");
        return;
    }

    // Tile metrics. Selected tile sits in the centre at full size; up to two
    // neighbours each side render smaller and fade out.
    // Portrait tiles matching the strip artwork aspect (~454x1080 ≈ 0.42).
    // All tiles render at the same size, edge-to-edge (no scale variation),
    // matching the ES-DE Art Book Next look.
    constexpr float kTileW = 240.0f;
    constexpr float kTileH = 560.0f;
    constexpr float kGap   = 0.0f;

    const auto idx_centre = (int)s.system_index;
    const auto count      = (int)lib.systems.size();
    const float cy        = h * 0.5f;

    for (int offset = -2; offset <= 2; offset++) {
        if (count <= 0) break;
        // Circular wrap: NES → ... ← saturn so the user always sees two
        // systems on either side of the focus.
        int idx = idx_centre + offset;
        idx = ((idx % count) + count) % count;
        const auto& sys = lib.systems[idx];

        // All tiles share the same size; selection is communicated via the
        // logo overlay (only on the focused tile) and an alpha boost.
        const float tw    = kTileW;
        const float thh   = kTileH;
        const float cx    = w * 0.5f + offset * (kTileW + kGap);
        const float x     = cx - tw * 0.5f;
        const float y     = cy - thh * 0.5f;

        const bool centre = (offset == 0);

        // Tile body: strip artwork only — no card background, no outline.
        // Rounded scissor so corners stay clean.
        nvgSave(vg);
        nvgScissor(vg, x, y, tw, thh);

        const int strip_h = system_splash_cache().get_or_load(vg,
            system_splash_path(sys.def->folder_name));
        if (strip_h > 0) {
            // Overzoom by 1.25x so the parallelogram artwork extends past
            // the tile bounds (the scissor crops the transparent corners,
            // minimizing the visible gap between adjacent tiles).
            constexpr float kZoom = 1.25f;
            const float ex  = (tw  * (kZoom - 1.0f)) * 0.5f;
            const float ey  = (thh * (kZoom - 1.0f)) * 0.5f;
            blit_cover(vg, strip_h, x - ex, y - ey,
                       tw * kZoom, thh * kZoom,
                       centre ? 1.0f : 0.55f);
        }

        // Console logo overlay only on the focused tile, ES-DE style.
        if (centre) {
            const auto logo_path = system_logo_path(sys.def->folder_name);
            const int  logo_h    = system_logo_cache().get_or_load(vg, logo_path);
            if (logo_h > 0) {
                blit_aspect_fit(vg, logo_h,
                    x + tw * 0.10f, y + thh * 0.35f,
                    tw * 0.80f, thh * 0.30f,
                    0.0f, 1.0f);
            }
        }

        nvgRestore(vg);
    }

}

// ---- SYSTEM VIEW (game list + sidebar) ------------------------------------

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
    constexpr float kRow = 44.0f;
    const int visible = (int)((content_h - 16) / kRow);
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

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, th.pad + 18, ry + (kRow - 4) * 0.5f,
            sys.games[idx].display.c_str(), nullptr);

        nvgFontSize(vg, th.label_size);
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, th.pad + list_w - 18, ry + (kRow - 4) * 0.5f,
            sys.games[idx].ext.c_str(), nullptr);
    }
    nvgRestore(vg);

    // ---- sidebar ----
    const auto& g = sys.games[s.game_index];
    nvgSave(vg);
    nvgIntersectScissor(vg, side_x, content_y, side_w, content_h);

    // Box art on top of sidebar (lazy-loaded from /foyer/assets/covers/).
    const std::string cover = scrapers::cover_path(
        sys.def->folder_name, g.stem);
    const float cover_h = 240.0f;
    const float cover_x = side_x + th.pad;
    const float cover_y = content_y + th.pad;
    const float cover_w = side_w - th.pad * 2.0f;

    rrect(vg, cover_x, cover_y, cover_w, cover_h, 8.0f, th.bg_panel_hi);

    const int handle = library::config().show_covers
        ? cover_cache().get_or_load(vg, cover)
        : 0;
    if (handle > 0) {
        // Aspect-fit centred inside cover_h slot.
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
            nvgRoundedRect(vg, dx, dy, dw, dh, 6.0f);
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

    // Title under cover.
    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, side_x + th.pad, cover_y + cover_h + 16,
        g.display.c_str(), nullptr);

    char meta[128];
    std::snprintf(meta, sizeof(meta), "%.*s   .%s",
        (int)sys.def->short_name.size(), sys.def->short_name.data(),
        g.ext.c_str());
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgText(vg, side_x + th.pad, cover_y + cover_h + 50, meta, nullptr);

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
};

std::vector<PopupItem> popup_items_for(View v) {
    switch (v) {
        case View::Home:
            return { {"Rescan Games", PopRescan},
                     {"Settings",     PopSettings},
                     {"Exit",         PopExit} };
        case View::System:
            return { {"Rescan Games", PopRescan},
                     {"Settings",     PopSettings},
                     {"Back",         PopBack} };
        default:
            return { {"Rescan Games", PopRescan},
                     {"Settings",     PopSettings},
                     {"Back",         PopBack} };
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
enum class ItemKind { Cycle, Toggle, Action, Static, Drill };

struct Item {
    ItemKind     kind;
    std::string  label;
    std::string  value;       // displayed right side
    std::string  hint;        // optional dim sub-label below the row
    int          payload = 0; // category-specific opcode
};

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
    OpEmuList,
    OpAccCreds,
    OpUpdScrapeAll, OpUpdInstalledCores,
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

std::vector<Item> build_items(Category cat) {
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
        case Category::Display:
            rows.push_back({ItemKind::Cycle,  "Theme",        cfg.theme_name,
                "Drop palettes into /foyer/config/themes/.",          OpTheme});
            rows.push_back({ItemKind::Toggle, "Show clock",       "", "",     OpShowClock});
            rows.push_back({ItemKind::Toggle, "Show backgrounds", "",
                "Use /foyer/assets/backgrounds/<sys>/<stem>.jpg.",    OpShowBg});
            rows.push_back({ItemKind::Toggle, "Show covers",      "", "",     OpShowCovers});
            break;
        case Category::Audio:
            rows.push_back({ItemKind::Static, "System volume controls live in the Switch home menu",
                "", "Per-core audio settings are exposed in the in-game pause overlay.", 0});
            break;
        case Category::Library:
            rows.push_back({ItemKind::Action, "Rescan library",         "A: run",     "", OpRescan});
            rows.push_back({ItemKind::Action, "Invalidate cover cache", "A: refresh", "", OpInvalidateCovers});
            break;
        case Category::Emulator:
            rows.push_back({ItemKind::Static, "Cores listed below are declared in system_db; install the matching nro under /foyer/cores/.",
                "", "", OpEmuList});
            break;
        case Category::Accounts: {
            const auto& a = scrapers::accounts();
            rows.push_back({ItemKind::Drill, "ScreenScraper dev ID",
                mask_credential(a.screenscraper.devid),
                "A: edit via on-screen keyboard.", OpAccSsDevId});
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
            rows.push_back({ItemKind::Action, "Scrape all systems", "A: run",
                "Walks every system using the preferred scraper.",   OpUpdScrapeAll});
            // Inline list of cores actually present on the SD. Each row is
            // labeled with the core name + nro size; missing cores from the
            // system_db core list are flagged so the user knows what's
            // pending install.
            rows.push_back({ItemKind::Static, "Installed cores", "", "", 0});
            std::vector<std::string> seen;
            if (auto* dir = ::opendir("/foyer/cores")) {
                while (auto* e = ::readdir(dir)) {
                    if (!e->d_name[0] || e->d_name[0] == '.') continue;
                    std::string_view n{e->d_name};
                    if (!n.starts_with("foyer-") || !n.ends_with(".nro")) continue;
                    std::string core_name{n.substr(6, n.size() - 6 - 4)};
                    char path[256];
                    std::snprintf(path, sizeof(path), "/foyer/cores/%.*s",
                        (int)n.size(), n.data());
                    struct stat st{};
                    char rhs[64] = "present";
                    if (::stat(path, &st) == 0) {
                        std::snprintf(rhs, sizeof(rhs), "%lld KB",
                            (long long)(st.st_size / 1024));
                    }
                    rows.push_back({ItemKind::Static, std::string{"  "} + core_name, rhs, "", 0});
                    seen.push_back(std::move(core_name));
                }
                ::closedir(dir);
            }
            // Flag the cores referenced in system_db that aren't on disk yet.
            for (const auto& sys : library::all_systems()) {
                for (const auto& c : sys.cores) {
                    bool installed = false;
                    for (const auto& n : seen) if (n == c.name) { installed = true; break; }
                    if (!installed) {
                        rows.push_back({ItemKind::Static,
                            std::string{"  "} + std::string{c.name}, "missing", "", 0});
                        seen.emplace_back(c.name);
                    }
                }
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

    const auto rows = build_items((Category)s.settings_category);
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

    constexpr float kRowH = 60.0f;
    const float inner_x = card_x + kCardPad;
    const float inner_w = card_w - kCardPad * 2.0f;

    float ry = card_y + kCardPad;
    for (int i = 0; i < (int)rows.size(); i++) {
        const auto& it  = rows[i];
        const bool sel  = s.settings_in_content && (i == s.settings_row);

        if (sel) {
            rrect(vg, inner_x - 4, ry - 2, inner_w + 8, kRowH - 8, 8.0f, th.bg_panel_hi);
        }

        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, inner_x + 8, ry + (kRowH - 12) * 0.5f, it.label.c_str(), nullptr);

        const float vx = inner_x + inner_w - 8;
        const float vy = ry + (kRowH - 12) * 0.5f;

        switch (it.kind) {
            case ItemKind::Toggle: {
                const bool on = toggle_get(it.payload);
                draw_pill_toggle(vg,
                    vx - 56, vy - 14, 52.0f, 28.0f,
                    on, nvgRGBA(0x4C, 0xC2, 0x6F, 0xFF), th.border);
                break;
            }
            case ItemKind::Cycle:
            case ItemKind::Static:
            case ItemKind::Action: {
                nvgFontSize(vg, th.body_size);
                nvgFillColor(vg, it.kind == ItemKind::Action ? th.accent : th.text_dim);
                nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgText(vg, vx, vy, it.value.c_str(), nullptr);
                break;
            }
            case ItemKind::Drill: {
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
            nvgText(vg, inner_x + 8, ry + kRowH - 18, it.hint.c_str(), nullptr);
        }
        ry += kRowH;
    }
}

} // namespace settings

void draw_settings(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    settings::draw_settings(vg, w, h, s, lib);
}

// ---- GAME DETAIL VIEW -----------------------------------------------------

void draw_game_detail(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    if (lib.systems.empty()) { draw_empty(vg, w, h, "No systems", ""); return; }
    const auto& sys = lib.systems[s.system_index];
    if (sys.games.empty())  { draw_empty(vg, w, h, "No games", "");   return; }
    const auto& g = sys.games[s.game_index];

    // Backdrop behind the detail panels for visual context.
    if (library::config().show_backgrounds) {
        const int handle = backdrop_cache().get_or_load(vg,
            backdrop_path(sys.def->folder_name, g.stem));
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
    const std::string cover = scrapers::cover_path(sys.def->folder_name, g.stem);
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

    // Core picker.
    const auto* resolved   = library::resolve_core(*sys.def, g.path);
    const auto  per_game   = library::per_game_core_for(g.path);
    const char* per_sys    = library::config().default_core_for(sys.def->folder_name);
    const int   resume_slot = latest_state_slot(sys, g);
    const bool  has_resume  = (resume_slot >= 0);

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

    for (const auto& c : sys.def->cores) {
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

} // namespace

void update(State& s, const Library& lib, std::uint64_t held, std::uint64_t down) {
    if (s.banner_ttl > 0) s.banner_ttl--;

    // Quit confirmation modal intercepts everything while open.
    if (s.quit_confirm_open) {
        if (down & (HidNpadButton_Left | HidNpadButton_Right)) {
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

    // Modal popup intercepts all input while open.
    if (s.popup_open) {
        const auto items = popup_items_for(s.view);
        const int n = (int)items.size();
        if (down & HidNpadButton_Down) s.popup_index = (s.popup_index + 1) % n;
        if (down & HidNpadButton_Up)   s.popup_index = (s.popup_index - 1 + n) % n;
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
            }
        }
        return;
    }

    if (lib.systems.empty()) return;

    if (s.view == View::Home) {
        const auto n = lib.systems.size();
        if (down & HidNpadButton_Right) {
            s.system_index = (s.system_index + 1) % n;
        } else if (down & HidNpadButton_Left) {
            s.system_index = (s.system_index + n - 1) % n;
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
            if (down & HidNpadButton_Down) {
                if (s.game_index + 1 < sys.games.size()) s.game_index++;
            } else if (down & HidNpadButton_Up) {
                if (s.game_index > 0) s.game_index--;
            } else if (down & HidNpadButton_AnyRight) {
                s.game_index = std::min(s.game_index + 10, sys.games.size() - 1);
            } else if (down & HidNpadButton_AnyLeft) {
                s.game_index = (s.game_index >= 10) ? s.game_index - 10 : 0;
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
            // Seed cursor: prefer Continue when a save state exists, else
            // land on the currently resolved core.
            const auto& g = sys.games[s.game_index];
            const bool has_resume = (latest_state_slot(sys, g) >= 0);
            if (has_resume) {
                s.detail_core_index = 0;
            } else {
                const auto* resolved = library::resolve_core(*sys.def, g.path);
                s.detail_core_index = 0;
                if (resolved) {
                    std::size_t i = 0;
                    for (const auto& c : sys.def->cores) {
                        if (c.name == resolved->name) { s.detail_core_index = i; break; }
                        i++;
                    }
                }
            }
        }
        if (down & HidNpadButton_Y) {
            // Y always uses the scraper picked in /foyer/config/general.jsonc.
            // Settings UI (Phase 8) will let the user change it from foyer
            // itself; for now they can edit the file via MTP.
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
        const auto& sys = lib.systems[s.system_index];
        if (sys.games.empty() || sys.def->cores.empty()) {
            s.view = View::System;
            return;
        }
        const auto& g = sys.games[s.game_index];
        const int  resume_slot = latest_state_slot(sys, g);
        const bool has_resume  = (resume_slot >= 0);
        const auto core_count  = sys.def->cores.size();
        const auto row_count   = core_count + (has_resume ? 1 : 0);
        const bool on_resume   = has_resume && s.detail_core_index == 0;
        const auto core_idx    = has_resume
                                   ? (s.detail_core_index == 0 ? 0
                                                                : s.detail_core_index - 1)
                                   : s.detail_core_index;

        if (down & HidNpadButton_Down) {
            if (s.detail_core_index + 1 < row_count) s.detail_core_index++;
        } else if (down & HidNpadButton_Up) {
            if (s.detail_core_index > 0) s.detail_core_index--;
        }

        if (down & HidNpadButton_B) {
            s.view = View::System;
        }
        if (down & HidNpadButton_A) {
            if (on_resume) {
                s.request_resume_slot = resume_slot;
                s.request_launch      = true;
            } else {
                const auto& chosen = sys.def->cores[core_idx];
                library::set_per_game_core(g.path, chosen.name);
                s.banner_text = std::string{"Per-game core set: "} + std::string{chosen.name};
                s.banner_ttl  = 180;
            }
        }
        if (!on_resume && (down & HidNpadButton_Y)) {
            const auto& chosen = sys.def->cores[core_idx];
            library::set_default_core_for(sys.def->folder_name, chosen.name);
            s.banner_text = std::string{"System default core set: "} + std::string{chosen.name};
            s.banner_ttl  = 180;
        }
        if (!on_resume && (down & HidNpadButton_X)) {
            library::set_per_game_core(g.path, "");
            s.banner_text = "Per-game override cleared";
            s.banner_ttl  = 180;
        }
    } else if (s.view == View::Settings) {
        using settings::Category;
        const auto rows = settings::build_items((Category)s.settings_category);
        const int  row_count = (int)rows.size();

        if (!s.settings_in_content) {
            // Sidebar focus.
            if (down & HidNpadButton_B) {
                s.view = View::Home;
                return;
            }
            if (down & HidNpadButton_Down) {
                s.settings_category = (s.settings_category + 1) % (int)Category::Count_;
                s.settings_row = 0;
            } else if (down & HidNpadButton_Up) {
                s.settings_category = (s.settings_category - 1 + (int)Category::Count_)
                                    % (int)Category::Count_;
                s.settings_row = 0;
            }
            if ((down & (HidNpadButton_A | HidNpadButton_Right)) && row_count > 0) {
                s.settings_in_content = true;
                s.settings_row = 0;
            }
        } else {
            // Content focus.
            if (down & HidNpadButton_B) {
                s.settings_in_content = false;
                return;
            }
            if (down & HidNpadButton_Down && row_count > 0) {
                s.settings_row = (s.settings_row + 1) % row_count;
            } else if (down & HidNpadButton_Up && row_count > 0) {
                s.settings_row = (s.settings_row - 1 + row_count) % row_count;
            }

            if (s.settings_row >= row_count) return;
            const auto& it = rows[s.settings_row];

            // Cycle handling: Left/Right rotate the current value.
            if (it.kind == settings::ItemKind::Cycle &&
                (down & (HidNpadButton_Left | HidNpadButton_Right))) {
                const int delta = (down & HidNpadButton_Right) ? +1 : -1;
                if (it.payload == settings::OpScraper) {
                    int n = ((int)library::config().preferred_scraper + delta + 3) % 3;
                    library::set_preferred_scraper((library::Config::Scraper)n);
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
                    default: break;
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
                if (clock.empty()) {
                    std::snprintf(rhs, sizeof(rhs), "%.*s  ·  %zu games",
                        (int)sys.def->display_name.size(),
                        sys.def->display_name.data(),
                        sys.games.size());
                } else {
                    std::snprintf(rhs, sizeof(rhs), "%.*s  ·  %zu games  ·  %s",
                        (int)sys.def->display_name.size(),
                        sys.def->display_name.data(),
                        sys.games.size(), clock.c_str());
                }
                clock = rhs;
            }
            hint = std::string{DPad} + " pick   "
                 + A + " enter   " + Minus + " settings   "
                 + Plus + " menu   " + B + " quit";
            break;
        }
        case View::System: {
            if (!lib.systems.empty()) {
                const auto& sys = lib.systems[s.system_index];
                char buf[160];
                std::snprintf(buf, sizeof(buf), "foyer  >  %.*s",
                    (int)sys.def->display_name.size(),
                    sys.def->display_name.data());
                title = buf;
                if (!sys.games.empty()) {
                    char rhs[64];
                    if (clock.empty()) {
                        std::snprintf(rhs, sizeof(rhs), "%zu / %zu",
                            s.game_index + 1, sys.games.size());
                    } else {
                        std::snprintf(rhs, sizeof(rhs), "%zu / %zu  ·  %s",
                            s.game_index + 1, sys.games.size(), clock.c_str());
                    }
                    clock = rhs;
                }
            }
            hint = std::string{DPad} + " pick   "
                 + A + " launch   " + X + " details   "
                 + Y + " scrape   " + B + " back";
            break;
        }
        case View::GameDetail: {
            if (!lib.systems.empty()) {
                const auto& sys = lib.systems[s.system_index];
                if (!sys.games.empty()) {
                    char buf[200];
                    std::snprintf(buf, sizeof(buf), "foyer  >  %.*s  >  %s",
                        (int)sys.def->short_name.size(),
                        sys.def->short_name.data(),
                        sys.games[s.game_index].display.c_str());
                    title = buf;
                }
            }
            hint = std::string{DPad} + " pick   "
                 + A + " set per-game   " + Y + " set sys default   "
                 + X + " clear override   " + B + " back";
            break;
        }
        case View::Settings: {
            const auto cat = (s.settings_category < (int)settings::Category::Count_)
                ? settings::kCategories[s.settings_category].label : "";
            char buf[160];
            std::snprintf(buf, sizeof(buf), "foyer  >  Settings  >  %s", cat);
            title = buf;
            hint  = std::string{DPad} + " navigate   "
                  + Left + Right + " adjust   "
                  + A + " select   " + B + " back";
            break;
        }
    }

    draw_topbar   (vg, w,    title.c_str(), clock.c_str());
    draw_bottombar(vg, w, h, hint.c_str());

    // Modal popup floats over everything, including bars.
    draw_popup(vg, w, h, s);
    draw_quit_confirm(vg, w, h, s);

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
