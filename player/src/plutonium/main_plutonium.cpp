// foyer player — Plutonium render shell entry.
//
// argv contract: same as main_brls.cpp / main_imgui.cpp.
//   argv[0]  nro path on sd
//   argv[1]  rom path (sdmc: prefix tolerated)
//   argv[2]  back nro path — used by Quit chain-launch (Phase P4)

#include "plutonium/main_application.hpp"
#include "platform/log.hpp"

#include <pu/Plutonium>
#include <switch.h>

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
    foyer::log::write("[player-plutonium] argc=%d\n", argc);

    // Disable SDL2's draw batching + state cache so our raw-GL
    // shader pass doesn't leak state into the next SDL_RenderCopy
    // (the menu text textures were rendered with our present
    // fragment shader bound, coming out as black rectangles).
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "0");

    auto renderer_opts = pu::ui::render::RendererInitOptions(
        SDL_INIT_EVERYTHING,
        pu::ui::render::RendererHardwareFlags);
    renderer_opts.UseImage(pu::ui::render::ImgAllFlags);
    renderer_opts.SetPlServiceType(PlServiceType_User);
    renderer_opts.AddDefaultAllSharedFonts();
    renderer_opts.SetInputPlayerCount(1);
    renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);

    auto renderer = pu::ui::render::Renderer::New(renderer_opts);
    auto main_app = foyer::player::plut::MainApplication::New(renderer);

    if (argc >= 2) {
        const std::string rom = normalise_argv_path(argv[1]);
        const std::string back = (argc >= 3)
            ? normalise_argv_path(argv[2]) : std::string{};
        main_app->SetBootArgs(rom, back, derive_system_folder(rom));
    } else {
        // Emulator-test autoboot: hbloader passes argv on hardware,
        // but PC Switch emulators can't hand args to an NRO. When
        // launched bare, fall back to a rom path read from
        // /foyer/data/test_autoboot.txt (single line). Lets CI / a
        // desktop emulator exercise every player without the foyer
        // chain-launch. Absent file = unchanged behaviour.
        if (auto* f = std::fopen("/foyer/data/test_autoboot.txt", "rb")) {
            char buf[512] = {0};
            const auto n = std::fread(buf, 1, sizeof(buf) - 1, f);
            std::fclose(f);
            std::string rom(buf, n);
            while (!rom.empty()
                   && (rom.back() == '\n' || rom.back() == '\r'
                       || rom.back() == ' ')) {
                rom.pop_back();
            }
            if (!rom.empty()) {
                foyer::log::write(
                    "[player-plutonium] autoboot (test) rom=%s\n",
                    rom.c_str());
                main_app->SetBootArgs(normalise_argv_path(rom), {},
                    derive_system_folder(rom));
            }
        }
    }

    if (R_FAILED(main_app->Load())) {
        foyer::log::write("[player-plutonium] Load() failed\n");
        return 1;
    }
    main_app->Show();
    foyer::log::write("[player-plutonium] exit\n");
    return 0;
}
