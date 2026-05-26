#include "bezel_sdl.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/system_db.hpp"
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
    const bool force_default = !g_folder.empty()
        && foyer::library::config().is_force_default_bezel_for(g_folder);
    foyer::log::write(
        "[bezel_sdl] resolve folder=%s stem=%s show_bezels=%d (per_game=%d force_default=%d)\n",
        g_folder.c_str(), g_stem.c_str(), (int)enabled, per_game,
        (int)force_default);
    if (!enabled) return {};

    // Per-game explicit pick beats every other resolver path. Set by
    // the per-game default-bezel picker on PerGameActivity. Caller
    // wrote an absolute file path; we just stat-check and return it.
    if (!g_rom_path.empty()) {
        const auto choice =
            foyer::library::per_game_bezel_choice(g_rom_path);
        if (!choice.empty() && exists(choice)) {
            foyer::log::write("[bezel_sdl] per-game pick %s\n",
                choice.c_str());
            return choice;
        }
    }

    char buf[512];

    // Per-system "force default bezel" opt-in skips the per-rom
    // candidate paths (#1 bundled + #2 installed) and jumps straight
    // to the per-system selector (#3) / legacy <folder>.png (#4).
    // Lets a user say "always use my picked bezel for this system,
    // ignore whichever per-rom art might be sitting on disk".
    if (!force_default) {
        if (!g_folder.empty() && !g_stem.empty()) {
            // Order = priority. Region-tagged SS scrapes win first
            // (`bezel-16-9(<region>).png`), then bare SS bezel.png,
            // then per-game downloads from external sources
            // (bezel-bezelproject.png, bezel-estefan.png). Lets users
            // keep multiple bezels on disk for the same rom and pick
            // between them by deleting the higher-priority files.
            const char* bundle_candidates[] = {
                "bezel-16-9(wor).png", "bezel-16-9(us).png",
                "bezel-16-9(eu).png",  "bezel-16-9(jp).png",
                "bezel-16-9.png",
                "bezel(wor).png",      "bezel(us).png",
                "bezel(eu).png",       "bezel(jp).png",
                "bezel.png",
                "bezel-bezelproject.png",
                "bezel-estefan.png",
            };
            // Try the folder-specific dir first, then the
            // hardware-family dir — so a genesis rom finds its
            // bezel even when the scrape lives under megadrive/.
            const auto fam = foyer::library::family_for_folder(g_folder);
            const std::string bundle_dirs[] = {
                "/foyer/assets/system/" + g_folder + "/" + g_stem + "/",
                (fam != g_folder
                    ? "/foyer/assets/system/" + std::string{fam} + "/"
                          + g_stem + "/"
                    : std::string{}),
            };
            for (const auto& bundle_dir : bundle_dirs) {
                if (bundle_dir.empty()) continue;
                for (const char* name : bundle_candidates) {
                    const std::string path = bundle_dir + name;
                    if (exists(path)) {
                        foyer::log::write("[bezel_sdl] using bundle %s\n",
                            path.c_str());
                        return path;
                    }
                }
            }
        }
        if (!g_folder.empty() && !g_stem.empty()) {
            std::snprintf(buf, sizeof(buf),
                "/foyer/content/bezels/%s/%s.png",
                g_folder.c_str(), g_stem.c_str());
            if (exists(buf)) return std::string{buf};
            // Family fallback for the per-rom installed path —
            // covers genesis roms when only megadrive/<stem>.png
            // exists (and vice versa).
            const auto fam = foyer::library::family_for_folder(g_folder);
            if (fam != g_folder) {
                std::snprintf(buf, sizeof(buf),
                    "/foyer/content/bezels/%.*s/%s.png",
                    (int)fam.size(), fam.data(), g_stem.c_str());
                if (exists(buf)) return std::string{buf};
            }
        }
    }
    // Per-system default-bezel override from config — lets the user
    // pick any installed bezel PNG for this system from
    // PerSystemActivity, decoupling the asset filename from the
    // system folder key the bezel pack was installed under.
    //
    // Each lookup falls back to the hardware-family slug
    // (foyer::library::family_for_folder) when the folder-specific
    // path doesn't exist, so a `megadrive-bezelproject` install also
    // covers genesis roms (and vice versa).
    auto try_per_system = [&](std::string_view key) -> std::string {
        if (key.empty()) return {};
        if (const char* name =
                foyer::library::config().default_bezel_for(key);
            name && *name) {
            std::snprintf(buf, sizeof(buf),
                "/foyer/content/bezels/%s.png", name);
            if (exists(buf)) return std::string{buf};
        }
        std::snprintf(buf, sizeof(buf),
            "/foyer/content/bezels/%.*s.png",
            (int)key.size(), key.data());
        if (exists(buf)) return std::string{buf};
        return {};
    };
    if (!g_folder.empty()) {
        if (auto p = try_per_system(g_folder); !p.empty()) return p;
        const auto fam = foyer::library::family_for_folder(g_folder);
        if (fam != g_folder) {
            if (auto p = try_per_system(fam); !p.empty()) return p;
        }
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
