#include "imgui/emulator_loop.hpp"
#include "imgui/imgui_switch_input.hpp"

#include "libretro/audio.hpp"
#include "libretro/bezel_gl.hpp"
#include "libretro/frontend.hpp"
#include "libretro/input.hpp"
#include "libretro/video_gl.hpp"
#include "platform/log.hpp"
#include "util/archive.hpp"

#include <GLES3/gl3.h>
#include <switch.h>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <string>

namespace foyer::player::emulator {

namespace {

std::string g_rom_path;
std::string g_original_rom_path;
std::string g_back_nro;
std::string g_system_folder;
bool        g_game_ok       = false;
bool        g_pause_combo   = false;
bool        g_exit_request  = false;

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

bool start(const std::string& rom_path,
           const std::string& back_nro,
           const std::string& system_folder) {
    g_rom_path          = rom_path;
    g_original_rom_path = rom_path;
    g_back_nro          = back_nro;
    g_system_folder     = system_folder;

    foyer::log::write("[player-imgui] boot rom=%s back=%s sys=%s\n",
        rom_path.c_str(), back_nro.c_str(), system_folder.c_str());

    if (!foyer::libretro::VideoSinkGl::instance().init()) {
        foyer::log::write("[player-imgui] video_gl init failed\n");
        return false;
    }
    if (!foyer::libretro::bezel_gl_init()) {
        foyer::log::write("[player-imgui] bezel_gl init failed (non-fatal)\n");
    }

    auto& fe = foyer::libretro::Frontend::instance();
    if (!fe.init()) {
        foyer::log::write("[player-imgui] frontend init failed\n");
        return false;
    }

    // .zip / .7z extract — mirror emulator_activity.cpp's logic so
    // chain-launches with archived roms keep working.
    if (ends_with_ci(g_rom_path, ".zip") || ends_with_ci(g_rom_path, ".7z")) {
        const std::string& valid = fe.system_info().valid_extensions;
        const auto inner = foyer::util::archive_peek_inner_rom(g_rom_path, valid);
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
                foyer::log::write("[player-imgui] reusing cached extract %s\n", out.c_str());
                ::utime(out.c_str(), nullptr);
                g_rom_path = out;
            } else if (foyer::util::archive_extract_inner_rom(g_rom_path, valid, out)) {
                foyer::log::write("[player-imgui] extracted %s -> %s\n", inner.c_str(), out.c_str());
                g_rom_path = out;
            }
        }
    }

    if (!fe.load_game(g_rom_path)) {
        foyer::log::write("[player-imgui] load_game(%s) failed\n", g_rom_path.c_str());
        return false;
    }
    g_game_ok = true;

    fe.set_sram_basis_path(g_original_rom_path);

    std::string stem = g_original_rom_path;
    if (const auto sl = stem.find_last_of('/'); sl != std::string::npos)
        stem = stem.substr(sl + 1);
    if (const auto dot = stem.find_last_of('.'); dot != std::string::npos)
        stem = stem.substr(0, dot);
    foyer::libretro::bezel_gl_set_rom_id(g_system_folder, stem);

    if (!foyer::libretro::AudioSink::instance().init((unsigned)fe.sample_rate())) {
        foyer::log::write("[player-imgui] audio init failed @ %u Hz — silent run\n",
            (unsigned)fe.sample_rate());
    }
    return true;
}

bool tick() {
    if (!g_game_ok) return false;

    // Share the PadState owned by imgui_switch_input — it's already
    // padUpdate'd this frame (the ImGui shell polls it before
    // tick()), so we just read out of the same struct. Two
    // independent padInitializeDefault sites was the reason
    // controls didn't reach the core in v0.6.125: libnx silently
    // routed events to the most-recently-initialised PadState
    // (imgui_switch_input's) while libretro::poll_input was reading
    // a different one.
    PadState* pad = foyer::player::imgui_shell::input_pad();
    if (!pad) {
        return g_exit_request;
    }
    const u64 held = padGetButtons(pad);

    const bool combo = (held & HidNpadButton_StickL)
                    && (held & HidNpadButton_StickR);
    if (combo && !g_pause_combo) {
        foyer::log::write("[player-imgui] L3+R3 (pause TBD in Phase 4)\n");
    }
    g_pause_combo = combo;

    foyer::libretro::poll_input(*pad);

    foyer::libretro::Frontend::instance().run_frame();
    return g_exit_request;
}

void draw(float screen_w, float screen_h) {
    glViewport(0, 0, (GLsizei)screen_w, (GLsizei)screen_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    foyer::libretro::VideoSinkGl::instance().draw(screen_w, screen_h);
    foyer::libretro::bezel_gl_draw(screen_w, screen_h);
}

void shutdown() {
    foyer::libretro::AudioSink::instance().shutdown();
    foyer::libretro::bezel_gl_shutdown();
    foyer::libretro::VideoSinkGl::instance().shutdown();
    auto& fe = foyer::libretro::Frontend::instance();
    if (g_game_ok) fe.unload_game();
    fe.shutdown();
    g_game_ok = false;
}

}  // namespace foyer::player::emulator
