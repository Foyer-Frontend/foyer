#include "input_sdl.hpp"
#include "frontend.hpp"
#include "platform/log.hpp"
#include "libretro.h"

#include <SDL2/SDL.h>

namespace foyer::libretro {

namespace {

SDL_GameController* g_pad = nullptr;

void try_open() {
    if (g_pad) return;
    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++) {
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) {
                foyer::log::write("[input_sdl] opened pad %d: %s\n",
                    i, SDL_GameControllerName(g_pad));
                return;
            }
        }
    }
}

}  // namespace

void input_sdl_init() {
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    try_open();
}

void input_sdl_shutdown() {
    if (g_pad) {
        SDL_GameControllerClose(g_pad);
        g_pad = nullptr;
    }
}

void input_sdl_poll() {
    // SDL_GameController state is event-driven; the caller
    // (EmulatorElement::OnInput) is expected to pump events
    // separately. Here we just read the latest state.
    if (!g_pad) {
        try_open();
        if (!g_pad) return;
    }
    auto get = [&](SDL_GameControllerButton b) {
        return SDL_GameControllerGetButton(g_pad, b) ? 1u : 0u;
    };

    auto& in = Frontend::instance().input();
    std::uint16_t btn = 0;
    auto setbit = [&](unsigned id, unsigned on) {
        if (on) btn |= (std::uint16_t)(1u << id);
    };

    // SDL_GameController uses Xbox-convention button names. On the
    // Switch port:
    //   SDL_CONTROLLER_BUTTON_A    -> physical Switch B (bottom)
    //   SDL_CONTROLLER_BUTTON_B    -> physical Switch A (right)
    //   SDL_CONTROLLER_BUTTON_X    -> physical Switch Y (left)
    //   SDL_CONTROLLER_BUTTON_Y    -> physical Switch X (top)
    // libretro JOYPAD_A is the right-hand confirm button (Switch A),
    // JOYPAD_B is bottom (Switch B). Map by physical position.
    setbit(RETRO_DEVICE_ID_JOYPAD_A,      get(SDL_CONTROLLER_BUTTON_B));
    setbit(RETRO_DEVICE_ID_JOYPAD_B,      get(SDL_CONTROLLER_BUTTON_A));
    setbit(RETRO_DEVICE_ID_JOYPAD_X,      get(SDL_CONTROLLER_BUTTON_Y));
    setbit(RETRO_DEVICE_ID_JOYPAD_Y,      get(SDL_CONTROLLER_BUTTON_X));
    setbit(RETRO_DEVICE_ID_JOYPAD_SELECT, get(SDL_CONTROLLER_BUTTON_BACK));
    setbit(RETRO_DEVICE_ID_JOYPAD_START,  get(SDL_CONTROLLER_BUTTON_START));
    setbit(RETRO_DEVICE_ID_JOYPAD_L,      get(SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
    setbit(RETRO_DEVICE_ID_JOYPAD_R,      get(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
    setbit(RETRO_DEVICE_ID_JOYPAD_L3,     get(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    setbit(RETRO_DEVICE_ID_JOYPAD_R3,     get(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    // D-pad bits — physical D-pad OR left stick deflection beyond a
    // dead-zone. RetroArch uses ~16 % of full range; matches the
    // libnx HidNpadButton_StickL* bits the brls / ImGui paths
    // synthesised. Without this, players on a Pro Controller or
    // Joy-Con grip with only the analog stick can't navigate
    // 2D platformers.
    const Sint16 lx_raw = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 ly_raw = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
    constexpr Sint16 kDeadzone = 0x4000;     // ~50 % of Sint16 max
    const unsigned stick_up    = (ly_raw < -kDeadzone) ? 1u : 0u;
    const unsigned stick_down  = (ly_raw >  kDeadzone) ? 1u : 0u;
    const unsigned stick_left  = (lx_raw < -kDeadzone) ? 1u : 0u;
    const unsigned stick_right = (lx_raw >  kDeadzone) ? 1u : 0u;
    setbit(RETRO_DEVICE_ID_JOYPAD_UP,
        get(SDL_CONTROLLER_BUTTON_DPAD_UP)    | stick_up);
    setbit(RETRO_DEVICE_ID_JOYPAD_DOWN,
        get(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  | stick_down);
    setbit(RETRO_DEVICE_ID_JOYPAD_LEFT,
        get(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  | stick_left);
    setbit(RETRO_DEVICE_ID_JOYPAD_RIGHT,
        get(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) | stick_right);

    // ZL / ZR are reported as axis triggers in SDL; libretro spec has
    // them as JOYPAD_L2 / R2 button bits, so threshold at half-range.
    const Sint16 lt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    const Sint16 rt = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    setbit(RETRO_DEVICE_ID_JOYPAD_L2,     lt > 0x4000 ? 1 : 0);
    setbit(RETRO_DEVICE_ID_JOYPAD_R2,     rt > 0x4000 ? 1 : 0);

    in.buttons = btn;

    const Sint16 rx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX);
    const Sint16 ry = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY);
    in.axes[0] = lx_raw;
    in.axes[1] = ly_raw;
    in.axes[2] = rx;
    in.axes[3] = ry;
}

}  // namespace foyer::libretro
