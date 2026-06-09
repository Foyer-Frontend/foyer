#include "plutonium/emulator_element.hpp"

#include "libretro/audio_sdl.hpp"
#include "libretro/bezel_sdl.hpp"
#include "libretro/cheevos.hpp"
#include "libretro/frontend.hpp"
#include "libretro/input.hpp"
#include "libretro/shader.hpp"
#include "libretro/video_sdl.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/system_db.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "plutonium/session_tracker.hpp"
#include "util/archive.hpp"

#include <SDL2/SDL_image.h>
#include <EGL/egl.h>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <thread>

namespace foyer::player::plut {

namespace {
bool ends_with_ci(std::string_view s, std::string_view suf) {
    if (s.size() < suf.size()) return false;
    for (std::size_t i = 0; i < suf.size(); i++) {
        char a = s[s.size() - suf.size() + i];
        char b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return false;
    }
    return true;
}
}  // namespace

EmulatorElement::EmulatorElement() = default;
EmulatorElement::~EmulatorElement() { Shutdown(); }

bool EmulatorElement::BootGame(const std::string& rom_path,
                               const std::string& back_nro,
                               const std::string& system_folder) {
    m_rom_path          = rom_path;
    m_original_rom_path = rom_path;
    m_back_nro          = back_nro;
    m_system_folder     = system_folder;

    foyer::log::write("[player-plutonium] boot rom=%s back=%s sys=%s\n",
        rom_path.c_str(), back_nro.c_str(), system_folder.c_str());

    SDL_Renderer* sdl_renderer = pu::ui::render::GetMainRenderer();
    if (!sdl_renderer) {
        foyer::log::write("[player-plutonium] no SDL renderer\n");
        return false;
    }

    foyer::libretro::VideoSinkSdl::instance().init(sdl_renderer);
    foyer::libretro::bezel_sdl_init(sdl_renderer);

    // Hand SDL's GL context to the shader pipeline. process_texture
    // runs the chain on this context inside video_sdl::upload (NOT
    // during SDL render), with strict state cleanup before SDL is
    // touched again — that's the rule that keeps Plutonium's menu
    // text rendering correctly while still getting GPU shaders.
    EGLDisplay disp = eglGetCurrentDisplay();
    EGLContext ctx  = eglGetCurrentContext();
    EGLSurface surf = eglGetCurrentSurface(EGL_DRAW);
    if (disp != EGL_NO_DISPLAY && ctx != EGL_NO_CONTEXT) {
        if (foyer::libretro::shader_pipeline().init_borrowed(
                (void*)disp, (void*)ctx, (void*)surf)) {
            foyer::log::write(
                "[player-plutonium] shader pipeline bound to SDL GL\n");
        }
    }

    auto& fe = foyer::libretro::Frontend::instance();
    if (!fe.init()) {
        foyer::log::write("[player-plutonium] frontend init failed\n");
        return false;
    }

    // .zip / .7z extract — mirror the brls / ImGui flows so cached
    // chain-launches keep working.
    if (ends_with_ci(m_rom_path, ".zip") || ends_with_ci(m_rom_path, ".7z")) {
        const std::string& valid = fe.system_info().valid_extensions;
        const auto inner = foyer::util::archive_peek_inner_rom(m_rom_path, valid);
        if (!inner.empty()) {
            const auto slash = inner.rfind('/');
            const std::string inner_base = (slash == std::string::npos)
                ? inner : inner.substr(slash + 1);
            const std::string out = "/foyer/data/extract/" + inner_base;
            ::mkdir("/foyer/data", 0777);
            ::mkdir("/foyer/data/extract", 0777);
            struct stat st{};
            const bool cached = (::stat(out.c_str(), &st) == 0
                                 && S_ISREG(st.st_mode)
                                 && st.st_size > 0);
            if (cached) {
                foyer::log::write("[player-plutonium] reusing cached extract %s\n", out.c_str());
                ::utime(out.c_str(), nullptr);
                m_rom_path = out;
            } else if (foyer::util::archive_extract_inner_rom(m_rom_path, valid, out)) {
                foyer::log::write("[player-plutonium] extracted %s -> %s\n",
                    inner.c_str(), out.c_str());
                m_rom_path = out;
            }
        }
    }

    if (!fe.load_game(m_rom_path)) {
        foyer::log::write("[player-plutonium] load_game(%s) failed\n",
            m_rom_path.c_str());
        return false;
    }
    m_game_ok = true;
    fe.set_sram_basis_path(m_original_rom_path);

    std::string stem = m_original_rom_path;
    if (const auto sl = stem.find_last_of('/'); sl != std::string::npos)
        stem = stem.substr(sl + 1);
    if (const auto dot = stem.find_last_of('.'); dot != std::string::npos)
        stem = stem.substr(0, dot);
    foyer::libretro::bezel_sdl_set_rom_id(m_system_folder, stem);
    foyer::libretro::bezel_sdl_set_rom_path(m_original_rom_path);

    // Per-game aspect mode: pause-menu Display→Aspect writes
    // set_per_game_aspect(rom, mode); apply that on boot so the
    // pick survives a clean exit / re-launch. -1 = no per-game
    // pick → fall through to AspectMode::DisplayCore (the default
    // set in VideoSinkSdl's m_aspect_mode).
    const int saved_aspect =
        foyer::library::per_game_aspect(m_original_rom_path);
    if (saved_aspect >= 0) {
        foyer::libretro::VideoSinkSdl::instance().set_aspect(
            static_cast<foyer::libretro::AspectMode>(saved_aspect));
        foyer::log::write("[player-plutonium] queued aspect=%d\n",
            saved_aspect);
    }

    if (!foyer::libretro::AudioSinkSdl::instance().init((unsigned)fe.sample_rate())) {
        foyer::log::write(
            "[player-plutonium] audio init failed @ %u Hz — silent run\n",
            (unsigned)fe.sample_rate());
    }

    // Resolve the shader preset to apply, in priority order:
    //   1. per-game override (per_game_shader)
    //   2. per-system default (config().default_shader_for)
    //   3. general default (config().shader_name)
    // Any empty/none falls through to the next layer.
    std::string s = foyer::library::per_game_shader(m_original_rom_path);
    if (s.empty()) {
        if (const char* sys_default =
                foyer::library::config().default_shader_for(m_system_folder);
            sys_default && *sys_default) {
            s = sys_default;
        }
    }
    if (s.empty()) {
        // Hardware-family fallback — covers per-system defaults set
        // on megadrive that should also apply to genesis.
        const auto fam = foyer::library::family_for_folder(m_system_folder);
        if (fam != m_system_folder) {
            if (const char* sys_default =
                    foyer::library::config().default_shader_for(fam);
                sys_default && *sys_default) {
                s = sys_default;
            }
        }
    }
    if (s.empty()) {
        s = foyer::library::config().shader_name;
    }
    if (!s.empty() && s != "none") {
        foyer::libretro::shader_pipeline().set_preset(s);
        foyer::log::write("[player-plutonium] queued shader preset=%s\n",
            s.c_str());
    }

    // RetroAchievements — login + hash the loaded rom + fetch the
    // achievement set. No-op (mark_failed inside) when the user has
    // no creds in accounts.jsonc, when the rom hash isn't in the RA
    // database, or when the network is offline. Sidecar persists
    // unlocked/total back to the per-game metadata so the browser
    // can render "X/Y achievements" without hitting the network.
    {
        auto& ch = foyer::libretro::Cheevos::instance();
        ch.set_progress_sidecar(m_system_folder, stem);
        ch.init([this](const foyer::libretro::Cheevos::Unlock& ev) {
            this->PushToast(ev);
        });
        ch.identify_game(m_rom_path);
    }
    return true;
}

void EmulatorElement::Shutdown() {
    if (!m_game_ok) return;
    foyer::libretro::Cheevos::instance().shutdown();
    foyer::libretro::AudioSinkSdl::instance().shutdown();
    foyer::libretro::bezel_sdl_shutdown();
    foyer::libretro::VideoSinkSdl::instance().shutdown();
    auto& fe = foyer::libretro::Frontend::instance();
    fe.unload_game();
    fe.shutdown();
    m_game_ok = false;
    m_pad_inited = false;
}

void EmulatorElement::OnInput(const u64 /*keys_down*/,
                              const u64 /*keys_up*/,
                              const u64 /*keys_held*/,
                              const pu::ui::TouchPoint /*touch_pos*/) {
    if (!m_game_ok) return;
    if (m_paused) return;        // hold the last frame; menu is on top
    // Read libnx HID directly — SDL2's game-controller layer on
    // switch-sdl2 mis-maps the L3/R3 stick clicks (they fire as
    // SDL_CONTROLLER_BUTTON_A on hardware). Plutonium's Renderer
    // owns its own PadState for UI nav; this one is parallel.
    if (!m_pad_inited) {
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&m_pad);
        m_pad_inited = true;
    }
    padUpdate(&m_pad);
    foyer::libretro::poll_input(m_pad);
    foyer::libretro::Frontend::instance().run_frame();
    foyer::libretro::Cheevos::instance().do_frame();
}

void EmulatorElement::OnRender(pu::ui::render::Renderer::Ref& drawer,
                               const pu::i32 /*x*/, const pu::i32 /*y*/) {
    SessionTracker::instance().tick();
    if (!m_game_ok) return;
    foyer::libretro::VideoSinkSdl::instance().draw(
        pu::ui::render::ScreenWidth,
        pu::ui::render::ScreenHeight);
    foyer::libretro::bezel_sdl_draw(
        pu::ui::render::ScreenWidth,
        pu::ui::render::ScreenHeight);

    // Toast overlay — top-right pill that fades the oldest entry
    // out after kVisibleMs and removes after kFadeMs. Each row has
    // the achievement badge image on the left and a two-line title /
    // points block on the right.
    constexpr int kVisibleMs = 4500;
    constexpr int kFadeMs    = 800;
    constexpr int kLifeMs    = kVisibleMs + kFadeMs;
    constexpr pu::i32 kPad   = 16;
    constexpr pu::i32 kRowH  = 112;
    constexpr pu::i32 kBadge = 88;
    constexpr pu::i32 kGap   = 12;
    constexpr pu::i32 kRight = 32;
    constexpr pu::i32 kTop   = 32;
    constexpr pu::i32 kTextW = 480;
    constexpr pu::i32 kRowW  = kPad + kBadge + 16 + kTextW + kPad;

    std::vector<Toast> snapshot;
    {
        std::scoped_lock lk{m_toast_mu};
        // Common case: no toasts pending — bail before the snapshot
        // copy so the steady-state frame loop does zero heap work.
        if (m_toasts.empty()) return;
        // Drop expired entries — and release any SDL_Textures we
        // created for their badges.
        const auto now = std::chrono::steady_clock::now();
        while (!m_toasts.empty()) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_toasts.front().ts).count();
            if (age <= kLifeMs) break;
            if (m_toasts.front().badge) {
                SDL_DestroyTexture(m_toasts.front().badge);
            }
            m_toasts.pop_front();
        }
        snapshot.assign(m_toasts.begin(), m_toasts.end());
    }
    if (snapshot.empty()) return;

    const std::string title_font = pu::ui::MakeDefaultFontName(
        pu::ui::DefaultFontSizes[(int)pu::ui::DefaultFontSize::Large]);
    const std::string sub_font = pu::ui::MakeDefaultFontName(
        pu::ui::DefaultFontSizes[(int)pu::ui::DefaultFontSize::Small]);

    SDL_Renderer* sdl = pu::ui::render::GetMainRenderer();
    const auto now = std::chrono::steady_clock::now();
    pu::i32 y_off = kTop;
    for (const auto& t : snapshot) {
        const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - t.ts).count();
        float alpha = 1.0f;
        if (age_ms > kVisibleMs) {
            alpha = 1.0f - (float)(age_ms - kVisibleMs) / (float)kFadeMs;
            if (alpha < 0.0f) alpha = 0.0f;
        }
        const u8 a = (u8)(alpha * 0xFF);
        if (a == 0) { y_off += kRowH + kGap; continue; }

        const pu::i32 x = pu::ui::render::ScreenWidth - kRight - kRowW;

        // Panel + accent bar.
        drawer->RenderRectangleFill(
            pu::ui::Color{ 0x14, 0x14, 0x18, (u8)(a * 0xE6 / 0xFF) },
            x, y_off, kRowW, kRowH);
        drawer->RenderRectangleFill(
            pu::ui::Color{ 0xF5, 0xB3, 0x42, a },
            x, y_off, 6, kRowH);

        // Badge on the left. If we have a texture, blit it scaled to
        // kBadge×kBadge; otherwise leave a placeholder square so the
        // text layout stays stable while the worker downloads.
        const pu::i32 bx = x + kPad;
        const pu::i32 by = y_off + (kRowH - kBadge) / 2;
        if (t.badge && sdl) {
            SDL_SetTextureAlphaMod(t.badge, a);
            SDL_Rect dst{ bx, by, kBadge, kBadge };
            SDL_RenderCopy(sdl, t.badge, nullptr, &dst);
        } else {
            drawer->RenderRectangleFill(
                pu::ui::Color{ 0x2A, 0x2A, 0x32, a },
                bx, by, kBadge, kBadge);
        }

        // Title (top line) + subtitle (points / description).
        const pu::i32 tx = bx + kBadge + 16;
        pu::i32 tw = 0, th = 0;
        pu::ui::render::GetTextDimensions(title_font, t.title, tw, th);
        const pu::i32 title_y = y_off + (kRowH - th - 30) / 2;
        auto title_tex = pu::ui::render::RenderText(title_font, t.title,
            pu::ui::Color{ 0xF8, 0xF8, 0xFA, a }, kTextW);
        if (title_tex) {
            drawer->RenderTexture(title_tex, tx, title_y);
            pu::ui::render::DeleteTexture(title_tex);
        }
        if (!t.subtitle.empty()) {
            auto sub_tex = pu::ui::render::RenderText(sub_font, t.subtitle,
                pu::ui::Color{ 0xC0, 0xC0, 0xC8, a }, kTextW);
            if (sub_tex) {
                drawer->RenderTexture(sub_tex, tx, title_y + th + 4);
                pu::ui::render::DeleteTexture(sub_tex);
            }
        }
        y_off += kRowH + kGap;
    }
}

namespace {

// Download the achievement badge PNG to memory, decode via SDL_image,
// upload to an SDL_Texture under the main Plutonium renderer. Runs on
// a detached worker so a slow CDN doesn't stall the libretro frame
// loop. Returns nullptr on any failure; the toast keeps the
// placeholder square in that case.
SDL_Texture* download_and_decode_badge(const std::string& url) {
    if (url.empty()) return nullptr;
    SDL_Renderer* sdl = pu::ui::render::GetMainRenderer();
    if (!sdl) return nullptr;
    const auto resp = ::foyer::net::get(url);
    if (resp.code != 200 || resp.body.empty()) {
        foyer::log::write("[ra] badge fetch failed (code=%ld len=%zu) %s\n",
            (long)resp.code, resp.body.size(), url.c_str());
        return nullptr;
    }
    SDL_RWops* rw = SDL_RWFromConstMem(resp.body.data(),
                                       (int)resp.body.size());
    if (!rw) return nullptr;
    SDL_Surface* surf = IMG_LoadPNG_RW(rw);
    SDL_RWclose(rw);
    if (!surf) {
        foyer::log::write("[ra] badge decode failed: %s\n", IMG_GetError());
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(sdl, surf);
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

}  // namespace

void EmulatorElement::PushToast(const foyer::libretro::Cheevos::Unlock& ev) {
    Toast t;
    t.title     = ev.title;
    if (ev.points > 0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d pts   %s",
            ev.points, ev.description.c_str());
        t.subtitle = buf;
    } else {
        t.subtitle = ev.description;
    }
    t.badge_url = ev.badge_url;
    t.ts        = std::chrono::steady_clock::now();
    {
        std::scoped_lock lk{m_toast_mu};
        m_toasts.push_back(std::move(t));
        // Cap the queue — runaway unlock storm shouldn't keep growing.
        while (m_toasts.size() > 6) {
            if (m_toasts.front().badge) {
                SDL_DestroyTexture(m_toasts.front().badge);
            }
            m_toasts.pop_front();
        }
    }
    // Kick a detached worker to grab the badge. By the time it
    // finishes the toast is probably still on screen; if not, the
    // texture lookup below quietly drops the result.
    if (!ev.badge_url.empty()) {
        const std::string url = ev.badge_url;
        const std::string key = ev.title;
        std::thread([this, url, key]() {
            SDL_Texture* tex = download_and_decode_badge(url);
            if (!tex) return;
            std::scoped_lock lk{m_toast_mu};
            bool placed = false;
            for (auto& it : m_toasts) {
                if (it.title == key && !it.badge) {
                    it.badge   = tex;
                    it.fetched = true;
                    placed = true;
                    break;
                }
            }
            if (!placed) SDL_DestroyTexture(tex);
        }).detach();
    }
}

void EmulatorElement::SetAspect(foyer::libretro::AspectMode m) {
    foyer::libretro::VideoSinkSdl::instance().set_aspect(m);
}

}  // namespace foyer::player::plut
