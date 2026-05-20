#include "bezel_sdl.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "platform/log.hpp"

#include <SDL2/SDL_image.h>

#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace foyer::libretro {
namespace {

std::string   g_folder;
std::string   g_stem;
std::string   g_rom_path;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture*  g_tex      = nullptr;
bool          g_resolved = false;
std::string   g_resolved_path;

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string resolve_path() {
    // Per-game bezel-visibility override beats Config::show_bezels.
    // -1 = inherit; 0 = off; 1 = on. Pause-menu toggle writes
    // per_game_show_bezel so changes don't bleed into other roms.
    const int per_game = !g_rom_path.empty()
        ? foyer::library::per_game_show_bezel(g_rom_path) : -1;
    bool enabled;
    if (per_game >= 0) {
        enabled = (per_game != 0);
    } else {
        enabled = foyer::library::config().show_bezels;
    }
    foyer::log::write(
        "[bezel_sdl] resolve folder=%s stem=%s show_bezels=%d (per_game=%d)\n",
        g_folder.c_str(), g_stem.c_str(), (int)enabled, per_game);
    if (!enabled) return {};
    char buf[512];

    if (!g_folder.empty() && !g_stem.empty()) {
        const std::string bundle_dir =
            "/foyer/assets/system/" + g_folder + "/" + g_stem + "/";
        const char* bundle_candidates[] = {
            "bezel-16-9(wor).png", "bezel-16-9(us).png",
            "bezel-16-9(eu).png",  "bezel-16-9(jp).png",
            "bezel-16-9.png",
            "bezel(wor).png",      "bezel(us).png",
            "bezel(eu).png",       "bezel(jp).png",
            "bezel.png",
        };
        for (const char* name : bundle_candidates) {
            const std::string path = bundle_dir + name;
            if (exists(path)) {
                foyer::log::write("[bezel_sdl] using bundle %s\n",
                    path.c_str());
                return path;
            }
        }
    }
    if (!g_folder.empty() && !g_stem.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/content/bezels/%s/%s.png",
            g_folder.c_str(), g_stem.c_str());
        if (exists(buf)) return std::string{buf};
    }
    // Per-system default-bezel override from config — lets the user
    // pick any installed bezel PNG for this system from
    // PerSystemActivity, decoupling the asset filename from the
    // system folder key the bezel pack was installed under.
    if (!g_folder.empty()) {
        if (const char* name =
                foyer::library::config().default_bezel_for(g_folder);
            name && *name) {
            std::snprintf(buf, sizeof(buf),
                "/foyer/content/bezels/%s.png", name);
            if (exists(buf)) return std::string{buf};
        }
    }
    if (!g_folder.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/content/bezels/%s.png", g_folder.c_str());
        if (exists(buf)) return std::string{buf};
    }
    return {};
}

void ensure_loaded() {
    if (g_resolved) return;
    g_resolved      = true;
    g_resolved_path = resolve_path();
    if (g_resolved_path.empty() || !g_renderer) return;

    SDL_Surface* surf = IMG_Load(g_resolved_path.c_str());
    if (!surf) {
        foyer::log::write("[bezel_sdl] IMG_Load failed for %s (%s)\n",
            g_resolved_path.c_str(), IMG_GetError());
        g_resolved_path.clear();
        return;
    }
    g_tex = SDL_CreateTextureFromSurface(g_renderer, surf);
    SDL_FreeSurface(surf);
    if (!g_tex) {
        foyer::log::write("[bezel_sdl] CreateTexture failed (%s)\n",
            SDL_GetError());
        g_resolved_path.clear();
        return;
    }
    SDL_SetTextureBlendMode(g_tex, SDL_BLENDMODE_BLEND);
    foyer::log::write("[bezel_sdl] uploaded %s\n", g_resolved_path.c_str());
}

}  // namespace

void bezel_sdl_set_rom_id(const std::string& system_folder,
                          const std::string& rom_stem) {
    foyer::log::write("[bezel_sdl] set folder=\"%s\" stem=\"%s\"\n",
        system_folder.c_str(), rom_stem.c_str());
    g_folder = system_folder;
    g_stem   = rom_stem;
    bezel_sdl_invalidate();
}

void bezel_sdl_set_rom_path(const std::string& rom_path) {
    g_rom_path = rom_path;
    bezel_sdl_invalidate();
}

bool bezel_sdl_init(SDL_Renderer* renderer) {
    g_renderer = renderer;
    return true;
}

void bezel_sdl_draw(int screen_w, int screen_h) {
    ensure_loaded();
    if (!g_tex || !g_renderer) return;
    SDL_Rect dst{ 0, 0, screen_w, screen_h };
    SDL_RenderCopy(g_renderer, g_tex, nullptr, &dst);
}

void bezel_sdl_invalidate() {
    if (g_tex) {
        SDL_DestroyTexture(g_tex);
        g_tex = nullptr;
    }
    g_resolved = false;
    g_resolved_path.clear();
}

void bezel_sdl_shutdown() {
    bezel_sdl_invalidate();
    g_renderer = nullptr;
}

}  // namespace foyer::libretro
