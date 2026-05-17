#include "imgui/imgui_switch_input.hpp"

#include <imgui.h>
#include <switch.h>

namespace foyer::player::imgui_shell {

namespace {

PadState   g_pad{};
HidTouchScreenState g_touch{};
bool       g_inited = false;
bool       g_plus_pressed_this_frame = false;

struct Map {
    HidNpadButton hos;
    ImGuiKey      ig;
};

const Map kButtons[] = {
    { HidNpadButton_A,            ImGuiKey_GamepadFaceRight },
    { HidNpadButton_B,            ImGuiKey_GamepadFaceDown  },
    { HidNpadButton_X,            ImGuiKey_GamepadFaceUp    },
    { HidNpadButton_Y,            ImGuiKey_GamepadFaceLeft  },
    { HidNpadButton_L,            ImGuiKey_GamepadL1        },
    { HidNpadButton_R,            ImGuiKey_GamepadR1        },
    { HidNpadButton_ZL,           ImGuiKey_GamepadL2        },
    { HidNpadButton_ZR,           ImGuiKey_GamepadR2        },
    { HidNpadButton_StickL,       ImGuiKey_GamepadL3        },
    { HidNpadButton_StickR,       ImGuiKey_GamepadR3        },
    { HidNpadButton_Plus,         ImGuiKey_GamepadStart     },
    { HidNpadButton_Minus,        ImGuiKey_GamepadBack      },
    { HidNpadButton_Left,         ImGuiKey_GamepadDpadLeft  },
    { HidNpadButton_Right,        ImGuiKey_GamepadDpadRight },
    { HidNpadButton_Up,           ImGuiKey_GamepadDpadUp    },
    { HidNpadButton_Down,         ImGuiKey_GamepadDpadDown  },
};

}  // namespace

void input_init() {
    if (g_inited) return;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
    hidInitializeTouchScreen();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    g_inited = true;
}

void input_new_frame() {
    if (!g_inited) return;
    g_plus_pressed_this_frame = false;

    padUpdate(&g_pad);
    const u64 down  = padGetButtonsDown(&g_pad);
    const u64 up    = padGetButtonsUp(&g_pad);
    if (down & HidNpadButton_Plus) g_plus_pressed_this_frame = true;

    ImGuiIO& io = ImGui::GetIO();
    for (const auto& m : kButtons) {
        if (down & m.hos) io.AddKeyEvent(m.ig, true);
        if (up   & m.hos) io.AddKeyEvent(m.ig, false);
    }

    // Touch -> mouse. ImGui only supports one cursor; the first
    // active point wins. Phase 4 may swap in multi-touch via
    // io.AddMouseSourceEvent when modals add slider drags.
    if (R_SUCCEEDED(hidGetTouchScreenStates(&g_touch, 1))
            && g_touch.count > 0) {
        const auto& t = g_touch.touches[0];
        io.AddMousePosEvent(static_cast<float>(t.x),
                            static_cast<float>(t.y));
        io.AddMouseButtonEvent(0, true);
    } else {
        io.AddMouseButtonEvent(0, false);
    }
}

bool input_pressed_plus() { return g_plus_pressed_this_frame; }

::PadState* input_pad() { return g_inited ? &g_pad : nullptr; }

}  // namespace foyer::player::imgui_shell
