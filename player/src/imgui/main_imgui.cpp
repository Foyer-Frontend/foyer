// foyer player — ImGui render shell (Phase ImGui-2).
//
// Boots the libretro core through emulator_loop + draws an FPS
// overlay via ImGui. Pause modal + picker overlays land in Phase 4.
//
// argv contract: same as main_brls.cpp.
//   argv[0]  nro path on sd
//   argv[1]  rom path (sdmc: prefix tolerated, stripped)
//   argv[2]  back nro path (sdmc:) — used by Quit chain-launch
//   argv[3+] reserved

#include "imgui/emulator_loop.hpp"
#include "imgui/gl_context.hpp"
#include "imgui/imgui_switch_input.hpp"
#include "imgui/imgui_theme.hpp"
#include "imgui/modals.hpp"

#include "libretro/audio.hpp"
#include "platform/log.hpp"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLES3/gl3.h>
#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

extern "C" {
void userAppInit(void) {
    Result rc = romfsInit();
    if (R_FAILED(rc)) diagAbortWithResult(rc);
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    setsysInitialize();
    appletInitializeGamePlayRecording();
    appletSetGamePlayRecordingState(false);
}

void userAppExit(void) {
    setsysExit();
    nifmExit();
    socketExit();
    romfsExit();
}
}

namespace {

std::string normalise_argv_path(std::string_view in) {
    if (in.starts_with("\"") && in.ends_with("\"")) {
        in = in.substr(1, in.size() - 2);
    }
    if (in.starts_with("sdmc:")) {
        in = in.substr(5);
    }
    return std::string{in};
}

std::string derive_system_folder(const std::string& rom_path) {
    // /foyer/roms/<sys>/<file> — parent of parent dir is <sys>.
    const auto slash = rom_path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return {};
    const auto upper = rom_path.substr(0, slash);
    const auto up_slash = upper.find_last_of('/');
    return (up_slash == std::string::npos)
        ? upper : upper.substr(up_slash + 1);
}

}  // namespace

int main(int argc, char** argv) {
    foyer::log::init_file();
    foyer::log::write("[player-imgui] argc=%d\n", argc);

    using namespace foyer::player::imgui_shell;

    GlContext gl{};
    if (!gl_context_init(gl)) {
        foyer::log::write("[player-imgui] GL bringup failed\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io_init = ImGui::GetIO();
    io_init.IniFilename = nullptr;

    // Default ImGui font at 18 px (readable from couch distance on
    // 720p / 1080p), with FontAwesome 6 Free Solid merged on top so
    // ICON_FA_* literals from imgui/icons.hpp render as glyphs.
    {
        ImFontConfig base_cfg;
        base_cfg.SizePixels = 18.0f;
        io_init.Fonts->AddFontDefault(&base_cfg);

        ImFontConfig fa_cfg;
        fa_cfg.MergeMode        = true;
        fa_cfg.PixelSnapH       = true;
        fa_cfg.GlyphMinAdvanceX = 18.0f;
        static const ImWchar fa_ranges[] = { 0xf000, 0xf8ff, 0 };
        io_init.Fonts->AddFontFromFileTTF(
            "romfs:/fonts/fa-solid-900.ttf",
            18.0f, &fa_cfg, fa_ranges);
    }

    ImGui_ImplOpenGL3_Init("#version 300 es");
    input_init();
    theme_apply();
    u64 last_theme_check_ns = armGetSystemTick();
    constexpr u64 kThemeCheckPeriodNs = 1'000'000'000ull;

    bool game_running = false;
    if (argc >= 2) {
        const std::string rom_path = normalise_argv_path(argv[1]);
        const std::string back_nro =
            (argc >= 3) ? normalise_argv_path(argv[2]) : std::string{};
        const std::string sys_folder = derive_system_folder(rom_path);

        if (foyer::player::emulator::start(gl, rom_path, back_nro, sys_folder)) {
            game_running = true;
        } else {
            foyer::log::write("[player-imgui] emulator start failed\n");
        }
    } else {
        foyer::log::write("[player-imgui] no rom path in argv — idle\n");
    }

    bool quit = false;
    while (appletMainLoop() && !quit) {
        gl_context_tick(gl);
        input_new_frame();
        // + (Plus / Start) is reserved for libretro Start; the
        // pause modal in Phase 4 uses L3+R3. The "no rom path"
        // idle screen still wants + to exit.
        if (!game_running && input_pressed_plus()) quit = true;

        const u64 now_ns = armGetSystemTick();
        if (now_ns - last_theme_check_ns > kThemeCheckPeriodNs) {
            theme_apply_if_changed();
            last_theme_check_ns = now_ns;
        }

        // Advance the core BEFORE draw so the freshest frame is on
        // the GL texture by the time we blit it.
        if (game_running) {
            if (foyer::player::emulator::tick()) quit = true;
        }

        // Clear + draw the game frame + bezel.
        if (game_running) {
            foyer::player::emulator::draw((float)gl.fb_w, (float)gl.fb_h);
        } else {
            glViewport(0, 0, gl.fb_w, gl.fb_h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // ImGui overlay on top.
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)gl.fb_w, (float)gl.fb_h);
        io.DeltaTime   = 1.0f / 60.0f;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        if (!game_running) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2((float)gl.fb_w, (float)gl.fb_h));
            ImGui::Begin("foyer", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("foyer player (ImGui) — Phase 2");
            ImGui::Text("No rom path in argv. + to exit.");
            ImGui::End();
        } else {
            ImGui::SetNextWindowPos(ImVec2(8, 8));
            ImGui::SetNextWindowBgAlpha(0.30f);
            ImGui::Begin("hud", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoInputs);
            ImGui::Text("FPS %.0f", io.Framerate);
            ImGui::End();
        }
        modals_draw();
        if (modals_quit_requested()) quit = true;
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Drive the audren pump on the main thread alongside the
        // libretro audio callback (which fills the queue from the
        // run_frame thread context). AudioSink::pump pulls from the
        // queue and writes into the audio buffers.
        foyer::libretro::AudioSink::instance().pump();

        gl_context_swap(gl);
    }

    if (game_running) foyer::player::emulator::shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    gl_context_shutdown(gl);

    // Chain-launch back to browser if we have a back nro path.
    if (quit && argc >= 3) {
        const std::string back = normalise_argv_path(argv[2]);
        if (!back.empty()) {
            const std::string sdmc_back = "sdmc:" + back;
            char a[512];
            std::snprintf(a, sizeof(a), "\"%s\"", sdmc_back.c_str());
            envSetNextLoad(sdmc_back.c_str(), a);
        }
    }
    foyer::log::write("[player-imgui] exit\n");
    return 0;
}
