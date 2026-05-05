#include "bezel.hpp"
#include "library/config.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace foyer::libretro {
namespace {

std::string g_folder;
std::string g_stem;
int         g_handle  = 0;
bool        g_resolved = false;
std::string g_resolved_path;

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Picks the PNG path to use; per-rom wins, per-system is the
// fallback. Returns "" when bezels are turned off OR when no
// system-specific art is configured — the caller short-circuits to
// a no-op draw (i.e. plain emulator output) in either case.
//
// History: v0.2.x shipped a `/foyer/bezels/default.png` catch-all
// "tv frame" so every game got *some* art out of the box. Users
// found it ugly and there was no way to opt out per-system, so the
// fallback was dropped — picking an art for a system is now an
// explicit Settings → Emulator → Bezel per system action.
std::string resolve_path() {
    if (!foyer::library::config().show_bezels) return {};
    char buf[512];
    if (!g_folder.empty() && !g_stem.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/bezels/%s/%s.png", g_folder.c_str(), g_stem.c_str());
        if (exists(buf)) return std::string{buf};
    }
    if (!g_folder.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/bezels/%s.png", g_folder.c_str());
        if (exists(buf)) return std::string{buf};
    }
    return {};
}

void ensure_loaded(NVGcontext* vg) {
    if (g_resolved) return;
    g_resolved      = true;
    g_resolved_path = resolve_path();
    if (g_resolved_path.empty()) return;
    g_handle = nvgCreateImage(vg, g_resolved_path.c_str(),
                              NVG_IMAGE_GENERATE_MIPMAPS);
    if (g_handle <= 0) {
        foyer::log::write("[bezel] failed to load %s\n", g_resolved_path.c_str());
        g_resolved_path.clear();
    } else {
        foyer::log::write("[bezel] using %s\n", g_resolved_path.c_str());
    }
}

} // namespace

void set_bezel_rom_id(const std::string& system_folder, const std::string& rom_stem) {
    g_folder    = system_folder;
    g_stem      = rom_stem;
    g_resolved  = false;
    g_handle    = 0;
    g_resolved_path.clear();
}

void draw_bezel(NVGcontext* vg, float w, float h) {
    ensure_loaded(vg);
    if (g_handle <= 0) return;

    auto pat = nvgImagePattern(vg, 0.0f, 0.0f, w, h, 0.0f, g_handle, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, w, h);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

void invalidate_bezel(NVGcontext* vg) {
    if (g_handle > 0) nvgDeleteImage(vg, g_handle);
    g_handle = 0;
    g_resolved = false;
    g_resolved_path.clear();
}

} // namespace foyer::libretro
