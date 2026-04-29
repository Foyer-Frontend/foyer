#include "views.hpp"
#include "theme.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "scrapers/cache.hpp"
#include "ui/icons.hpp"

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>
#include <unordered_map>

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

        struct stat st{};
        if (::stat(path.c_str(), &st) != 0) {
            images[path] = 0;
            return 0;
        }
        const int img = nvgCreateImage(vg, path.c_str(), 0);
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

void draw_topbar(NVGcontext* vg, float w, const char* left, const char* right) {
    const auto& th = theme();
    nvgFontSize(vg, th.head_size);

    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, th.pad, 36, left, nullptr);

    if (right && right[0]) {
        nvgFillColor(vg, th.text_dim);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, w - th.pad, 36, right, nullptr);
    }

    // Underline.
    nvgBeginPath(vg);
    nvgRect(vg, th.pad, 64, w - th.pad * 2.0f, 2.0f);
    nvgFillColor(vg, th.border);
    nvgFill(vg);
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

    draw_topbar(vg, w, "foyer", "");

    if (lib.systems.empty()) {
        draw_empty(vg, w, h,
            "No systems found",
            "drop roms into /foyer/roms/<system>/ and rescan");
        return;
    }

    // Tile metrics. Selected tile sits in the centre at full size; up to two
    // neighbours each side render smaller and fade out.
    constexpr float kTileW = 360.0f;
    constexpr float kTileH = 220.0f;
    constexpr float kGap   = 32.0f;

    const auto idx_centre = (int)s.system_index;
    const auto count      = (int)lib.systems.size();
    const float cy        = h * 0.5f;

    for (int offset = -2; offset <= 2; offset++) {
        const int idx = idx_centre + offset;
        if (idx < 0 || idx >= count) continue;
        const auto& sys = lib.systems[idx];

        const float scale = (offset == 0) ? 1.0f : (std::abs(offset) == 1 ? 0.78f : 0.55f);
        const float tw    = kTileW * scale;
        const float thh   = kTileH * scale;
        const float cx    = w * 0.5f + offset * (kTileW + kGap) * 0.85f;
        const float x     = cx - tw * 0.5f;
        const float y     = cy - thh * 0.5f;

        const bool centre = (offset == 0);
        rrect(vg, x, y, tw, thh, th.radius,
            centre ? th.bg_panel_hi : th.bg_panel);
        if (centre) {
            rrect_outline(vg, x, y, tw, thh, th.radius, th.accent, 3.0f);
        }

        nvgSave(vg);
        nvgIntersectScissor(vg, x, y, tw, thh);

        // Short-name big.
        nvgFontSize(vg, th.title_size * scale);
        nvgFillColor(vg, centre ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, x + tw * 0.5f, y + thh * 0.5f - 18 * scale,
            std::string{sys.def->short_name}.c_str(), nullptr);

        // Long name below.
        nvgFontSize(vg, th.body_size * scale);
        nvgFillColor(vg, th.text_dim);
        nvgText(vg, x + tw * 0.5f, y + thh * 0.5f + 22 * scale,
            std::string{sys.def->display_name}.c_str(), nullptr);

        // Game count badge bottom-right.
        if (centre) {
            char badge[32];
            std::snprintf(badge, sizeof(badge), "%zu games", sys.games.size());
            nvgFontSize(vg, th.label_size);
            nvgFillColor(vg, th.accent);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
            nvgText(vg, x + tw - th.pad * 0.5f, y + thh - th.pad * 0.5f, badge, nullptr);
        }
        nvgRestore(vg);
    }

    // Bottom hint bar.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    using namespace foyer::ui::icons;
    const auto hint_home =
        std::string{DPad}  + " pick system   "
                + A         + " enter   "
                + Plus      + " exit";
    nvgText(vg, w * 0.5f, h - 12, hint_home.c_str(), nullptr);
}

// ---- SYSTEM VIEW (game list + sidebar) ------------------------------------

void draw_system(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    if (lib.systems.empty()) {
        draw_empty(vg, w, h, "No systems", "");
        return;
    }
    const auto& sys = lib.systems[s.system_index];

    char header[160];
    std::snprintf(header, sizeof(header), "%.*s",
        (int)sys.def->display_name.size(), sys.def->display_name.data());
    char rhs[64];
    std::snprintf(rhs, sizeof(rhs), "%zu / %zu",
        (sys.games.empty() ? 0 : (s.game_index + 1)),
        sys.games.size());
    draw_topbar(vg, w, header, rhs);

    // Layout: left list 60%, right sidebar 40%.
    const float content_y  = 80;
    const float content_h  = h - content_y - 48;
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

    const int handle = cover_cache().get_or_load(vg, cover);
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

    // Bottom hint.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    using namespace foyer::ui::icons;
    const auto hint_sys =
        std::string{DPad}  + " pick   "
                + A         + " launch   "
                + Y         + " scrape   "
                + B         + " back";
    nvgText(vg, w * 0.5f, h - 12, hint_sys.c_str(), nullptr);
}

} // namespace

void update(State& s, const Library& lib, std::uint64_t held, std::uint64_t down) {
    if (s.banner_ttl > 0) s.banner_ttl--;
    if (lib.systems.empty()) return;

    if (s.view == View::Home) {
        if (down & HidNpadButton_Right) {
            if (s.system_index + 1 < lib.systems.size()) s.system_index++;
        } else if (down & HidNpadButton_Left) {
            if (s.system_index > 0) s.system_index--;
        }

        if (down & HidNpadButton_A) {
            s.view = View::System;
            s.game_index = 0;
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
        if ((down & HidNpadButton_A) && !sys.games.empty()) {
            s.request_launch = true;
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
    }

    (void)held;
}

void invalidate_cover_cache(NVGcontext* vg) {
    cover_cache().clear(vg);
}

void draw(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, th.bg);
    nvgFill(vg);

    switch (s.view) {
        case View::Home:   draw_home  (vg, w, h, s, lib); break;
        case View::System: draw_system(vg, w, h, s, lib); break;
    }

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
