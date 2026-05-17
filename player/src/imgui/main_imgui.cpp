// foyer player — ImGui render shell (Phase ImGui-1).
//
// One-window-loop entry. libretro is NOT wired yet (Phase 2 hooks
// Frontend::run_frame in via emulator_loop.cpp). Right now we boot,
// stand up EGL/GLES3, init ImGui, draw "Hello foyer", exit on +.
//
// Switch boot still needs the userAppInit/userAppExit boilerplate
// from libnx (services init, romfs). brls's switch_wrapper.c brought
// that with it; without brls we have to provide our own.

#include "imgui/gl_context.hpp"
#include "imgui/imgui_switch_input.hpp"
#include "imgui/imgui_theme.hpp"

#include "platform/log.hpp"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLES3/gl3.h>
#include <switch.h>

#include <cstdio>
#include <cstdlib>

extern "C" {
// libnx hooks. Match the minimal "graphics + romfs + nifm + setsys"
// service set; mirror what borealis's switch_wrapper.c did so the
// rest of foyer_shared keeps working.
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

using namespace foyer::player::imgui_shell;

bool g_should_quit = false;

void draw_hello(int fb_w, int fb_h) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)fb_w, (float)fb_h));
    ImGui::Begin("foyer", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    const char* msg = "Hello foyer — ImGui Phase 1\n+ to exit";
    const auto sz = ImGui::CalcTextSize(msg);
    ImGui::SetCursorPos(ImVec2(((float)fb_w - sz.x) * 0.5f,
                               ((float)fb_h - sz.y) * 0.5f));
    ImGui::Text("%s", msg);
    ImGui::End();
}

}  // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    foyer::log::write("[player-imgui] boot\n");

    GlContext gl{};
    if (!gl_context_init(gl)) {
        foyer::log::write("[player-imgui] GL bringup failed; aborting\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;        // no imgui.ini on the SD card
    ImGui_ImplOpenGL3_Init("#version 300 es");
    input_init();
    theme_apply();

    // 1 Hz theme re-poll alongside the main loop; matches the brls
    // theme_watcher cadence in the browser binary.
    u64 last_theme_check_ns = armGetSystemTick();
    constexpr u64 kThemeCheckPeriodNs = 1'000'000'000ull;

    while (appletMainLoop() && !g_should_quit) {
        input_new_frame();
        if (input_pressed_plus()) g_should_quit = true;

        const u64 now_ns = armGetSystemTick();
        if (now_ns - last_theme_check_ns > kThemeCheckPeriodNs) {
            theme_apply_if_changed();
            last_theme_check_ns = now_ns;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)gl.fb_w, (float)gl.fb_h);
        io.DeltaTime   = 1.0f / 60.0f;            // Phase 2 reads actual delta

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        draw_hello(gl.fb_w, gl.fb_h);

        ImGui::Render();
        glViewport(0, 0, gl.fb_w, gl.fb_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        gl_context_swap(gl);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    gl_context_shutdown(gl);
    foyer::log::write("[player-imgui] exit\n");
    return 0;
}
