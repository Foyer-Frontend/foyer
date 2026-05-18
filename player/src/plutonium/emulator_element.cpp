#include "plutonium/emulator_element.hpp"

#include "libretro/audio_sdl.hpp"
#include "libretro/bezel_sdl.hpp"
#include "libretro/cheevos.hpp"
#include "libretro/frontend.hpp"
#include "libretro/input.hpp"
#include "libretro/shader.hpp"
#include "libretro/video_sdl.hpp"
#include "library/config.hpp"
#include "platform/log.hpp"
#include "util/archive.hpp"

#include <EGL/egl.h>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

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

    if (!foyer::libretro::AudioSinkSdl::instance().init((unsigned)fe.sample_rate())) {
        foyer::log::write(
            "[player-plutonium] audio init failed @ %u Hz — silent run\n",
            (unsigned)fe.sample_rate());
    }

    // Apply the persisted shader preset from config so a choice
    // made under brls/ImGui survives the shell switch.
    const auto& s = foyer::library::config().shader_name;
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
        ch.init([this](const std::string& title) {
            foyer::log::write("[ra] unlocked: %s\n", title.c_str());
            this->PushToast(title);
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
    if (!m_game_ok) return;
    foyer::libretro::VideoSinkSdl::instance().draw(
        pu::ui::render::ScreenWidth,
        pu::ui::render::ScreenHeight);
    foyer::libretro::bezel_sdl_draw(
        pu::ui::render::ScreenWidth,
        pu::ui::render::ScreenHeight);

    // Toast overlay — top-right pill that fades the oldest entry
    // out after kVisibleMs and removes after kFadeMs. Stack down
    // when multiple unlocks fire quickly.
    constexpr int kVisibleMs = 3500;
    constexpr int kFadeMs    = 600;
    constexpr int kLifeMs    = kVisibleMs + kFadeMs;
    constexpr pu::i32 kPad = 18;
    constexpr pu::i32 kRowH = 64;
    constexpr pu::i32 kGap  = 8;
    constexpr pu::i32 kRight = 32;
    constexpr pu::i32 kTop   = 32;

    std::vector<Toast> snapshot;
    {
        std::scoped_lock lk{m_toast_mu};
        // Drop expired entries
        const auto now = std::chrono::steady_clock::now();
        while (!m_toasts.empty()) {
            const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_toasts.front().ts).count();
            if (age > kLifeMs) m_toasts.pop_front();
            else break;
        }
        snapshot.assign(m_toasts.begin(), m_toasts.end());
    }
    if (snapshot.empty()) return;

    const std::string font_name = pu::ui::MakeDefaultFontName(
        pu::ui::DefaultFontSizes[(int)pu::ui::DefaultFontSize::Medium]);
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

        pu::i32 tw = 0, th = 0;
        pu::ui::render::GetTextDimensions(font_name, t.msg, tw, th);
        const pu::i32 w = tw + kPad * 2;
        const pu::i32 x = pu::ui::render::ScreenWidth - kRight - w;

        // Panel + accent bar.
        drawer->RenderRectangleFill(
            pu::ui::Color{ 0x18, 0x18, 0x1A, (u8)(a * 0xE6 / 0xFF) },
            x, y_off, w, kRowH);
        drawer->RenderRectangleFill(
            pu::ui::Color{ 0xF5, 0xB3, 0x42, a },
            x, y_off, 4, kRowH);

        auto txt = pu::ui::render::RenderText(font_name, t.msg,
            pu::ui::Color{ 0xF8, 0xF8, 0xFA, a });
        if (txt) {
            const pu::i32 tx = x + kPad;
            const pu::i32 ty = y_off + (kRowH - th) / 2;
            drawer->RenderTexture(txt, tx, ty);
            pu::ui::render::DeleteTexture(txt);
        }
        y_off += kRowH + kGap;
    }
}

void EmulatorElement::PushToast(const std::string& msg) {
    std::scoped_lock lk{m_toast_mu};
    m_toasts.push_back({ msg, std::chrono::steady_clock::now() });
    // Cap the queue — runaway unlock storm shouldn't keep growing.
    while (m_toasts.size() > 6) m_toasts.pop_front();
}

void EmulatorElement::SetAspect(foyer::libretro::AspectMode m) {
    foyer::libretro::VideoSinkSdl::instance().set_aspect(m);
}

}  // namespace foyer::player::plut
