#include "views.hpp"
#include "theme.hpp"
#include "library/system_db.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
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
                + Minus     + " settings   "
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
                + X         + " details   "
                + Y         + " scrape   "
                + B         + " back";
    nvgText(vg, w * 0.5f, h - 12, hint_sys.c_str(), nullptr);
}

// ---- SETTINGS VIEW --------------------------------------------------------

namespace settings_items {
    enum : std::size_t {
        PreferredScraper = 0,
        Theme,
        RomRoot,
        Rescan,
        InvalidateCovers,
        Count,
    };
}

const char* scraper_label(library::Config::Scraper s) {
    switch (s) {
        case library::Config::Scraper::ScreenScraper: return "ScreenScraper";
        case library::Config::Scraper::SteamGridDB:   return "SteamGridDB";
        case library::Config::Scraper::Libretro:
        default:                                       return "libretro-thumbnails";
    }
}

void draw_settings(NVGcontext* vg, float w, float h, const State& s, const Library&) {
    const auto& th = theme();
    draw_topbar(vg, w, "Settings", FOYER_DISPLAY_VERSION);

    const float content_y = 80;
    const float content_h = h - content_y - 48;
    rrect(vg, th.pad, content_y, w - th.pad * 2.0f, content_h, th.radius, th.bg_panel);

    constexpr float kRow = 64.0f;
    float ry = content_y + th.pad;

    auto draw_row = [&](std::size_t idx, const char* label, const char* value, bool actionable) {
        const bool sel = (idx == s.settings_index);
        if (sel) {
            rrect(vg, th.pad + th.pad * 0.5f, ry, w - th.pad * 3.0f,
                  kRow - 8, 8.0f, th.bg_panel_hi);
        }
        nvgFontSize(vg, th.body_size);
        nvgFillColor(vg, sel ? th.text_strong : th.text);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgText(vg, th.pad + th.pad, ry + (kRow - 8) * 0.5f, label, nullptr);

        if (value && value[0]) {
            nvgFontSize(vg, th.body_size);
            nvgFillColor(vg, actionable ? th.accent : th.text_dim);
            nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            nvgText(vg, w - th.pad * 2.0f, ry + (kRow - 8) * 0.5f, value, nullptr);
        }
        ry += kRow;
    };

    const auto& cfg = library::config();
    draw_row(settings_items::PreferredScraper, "Preferred scraper",
             scraper_label(cfg.preferred_scraper), true);
    draw_row(settings_items::Theme, "Theme",
             cfg.theme_name.c_str(), true);
    draw_row(settings_items::RomRoot, "Rom root",
             cfg.rom_root.c_str(), false);
    draw_row(settings_items::Rescan, "Rescan library", "A: run", true);
    draw_row(settings_items::InvalidateCovers, "Invalidate cover cache",
             "A: refresh", true);

    // Footer hint.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    nvgText(vg, th.pad + th.pad, content_y + content_h - th.pad,
            "Edit /foyer/config/accounts.jsonc + general.jsonc for credentials and rom_root.",
            nullptr);

    // Bottom hint bar.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    using namespace foyer::ui::icons;
    const auto hint =
        std::string{DPad}  + " pick   "
                + Left + Right + " adjust   "
                + A         + " run   "
                + B         + " back";
    nvgText(vg, w * 0.5f, h - 12, hint.c_str(), nullptr);
}

// ---- GAME DETAIL VIEW -----------------------------------------------------

void draw_game_detail(NVGcontext* vg, float w, float h, const State& s, const Library& lib) {
    const auto& th = theme();
    if (lib.systems.empty()) { draw_empty(vg, w, h, "No systems", ""); return; }
    const auto& sys = lib.systems[s.system_index];
    if (sys.games.empty())  { draw_empty(vg, w, h, "No games", "");   return; }
    const auto& g = sys.games[s.game_index];

    char header[160];
    std::snprintf(header, sizeof(header), "%s", g.display.c_str());
    char rhs[64];
    std::snprintf(rhs, sizeof(rhs), "%.*s",
        (int)sys.def->display_name.size(), sys.def->display_name.data());
    draw_topbar(vg, w, header, rhs);

    const float content_y = 80;
    const float content_h = h - content_y - 48;

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
    const int handle = cover_cache().get_or_load(vg, cover);
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

    nvgFontSize(vg, th.head_size);
    nvgFillColor(vg, th.text_strong);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, right_x + th.pad, content_y + th.pad, "Core", nullptr);

    constexpr float kRow = 56.0f;
    float ry = content_y + th.pad + 40.0f;
    int row = 0;
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

    // Bottom hint.
    nvgFontSize(vg, th.label_size);
    nvgFillColor(vg, th.text_dim);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    using namespace foyer::ui::icons;
    const auto hint =
        std::string{DPad}  + " pick   "
                + A         + " set per-game   "
                + Y         + " set system default   "
                + X         + " clear override   "
                + B         + " back";
    nvgText(vg, w * 0.5f, h - 12, hint.c_str(), nullptr);
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
        if (down & HidNpadButton_Minus) {
            s.view = View::Settings;
            s.settings_index = 0;
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
        if ((down & HidNpadButton_X) && !sys.games.empty()) {
            s.view = View::GameDetail;
            // Seed cursor on the currently resolved core.
            const auto& g = sys.games[s.game_index];
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
        const auto core_count = sys.def->cores.size();

        if (down & HidNpadButton_Down) {
            if (s.detail_core_index + 1 < core_count) s.detail_core_index++;
        } else if (down & HidNpadButton_Up) {
            if (s.detail_core_index > 0) s.detail_core_index--;
        }

        if (down & HidNpadButton_B) {
            s.view = View::System;
        }
        if (down & HidNpadButton_A) {
            const auto& chosen = sys.def->cores[s.detail_core_index];
            library::set_per_game_core(g.path, chosen.name);
            s.banner_text = std::string{"Per-game core set: "} + std::string{chosen.name};
            s.banner_ttl  = 180;
        }
        if (down & HidNpadButton_Y) {
            const auto& chosen = sys.def->cores[s.detail_core_index];
            library::set_default_core_for(sys.def->folder_name, chosen.name);
            s.banner_text = std::string{"System default core set: "} + std::string{chosen.name};
            s.banner_ttl  = 180;
        }
        if (down & HidNpadButton_X) {
            library::set_per_game_core(g.path, "");
            s.banner_text = "Per-game override cleared";
            s.banner_ttl  = 180;
        }
    } else if (s.view == View::Settings) {
        if (down & HidNpadButton_B) {
            s.view = View::Home;
        }

        if (down & HidNpadButton_Down) {
            if (s.settings_index + 1 < settings_items::Count) s.settings_index++;
        } else if (down & HidNpadButton_Up) {
            if (s.settings_index > 0) s.settings_index--;
        }

        const auto cycle_scraper = [](library::Config::Scraper c, int delta) {
            int n = (int)c + delta;
            if (n < 0) n = 2;
            if (n > 2) n = 0;
            return (library::Config::Scraper)n;
        };

        if (s.settings_index == settings_items::PreferredScraper) {
            if (down & HidNpadButton_Right) {
                library::set_preferred_scraper(
                    cycle_scraper(library::config().preferred_scraper, +1));
            } else if (down & HidNpadButton_Left) {
                library::set_preferred_scraper(
                    cycle_scraper(library::config().preferred_scraper, -1));
            }
        }

        if (s.settings_index == settings_items::Theme &&
            (down & (HidNpadButton_Left | HidNpadButton_Right))) {
            const auto themes = list_themes();
            if (!themes.empty()) {
                std::size_t cur = 0;
                const auto& current = library::config().theme_name;
                for (std::size_t i = 0; i < themes.size(); i++) {
                    if (themes[i] == current) { cur = i; break; }
                }
                if (down & HidNpadButton_Right) {
                    cur = (cur + 1) % themes.size();
                } else {
                    cur = (cur == 0) ? themes.size() - 1 : cur - 1;
                }
                library::set_theme_name(themes[cur]);
                load_theme(themes[cur]);
                s.banner_text = std::string{"Theme: "} + themes[cur];
                s.banner_ttl  = 120;
            }
        }
        if (down & HidNpadButton_A) {
            switch (s.settings_index) {
                case settings_items::Rescan:
                    s.request_rescan = true;
                    s.banner_text = "Rescanning library...";
                    s.banner_ttl  = 180;
                    break;
                case settings_items::InvalidateCovers:
                    s.request_invalidate_covers = true;
                    s.banner_text = "Cover cache cleared";
                    s.banner_ttl  = 120;
                    break;
                default: break;
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
        case View::Home:       draw_home       (vg, w, h, s, lib); break;
        case View::System:     draw_system     (vg, w, h, s, lib); break;
        case View::GameDetail: draw_game_detail(vg, w, h, s, lib); break;
        case View::Settings:   draw_settings   (vg, w, h, s, lib); break;
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
