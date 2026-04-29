#include "input.hpp"
#include "frontend.hpp"
#include "libretro.h"

namespace foyer::libretro {

void poll_input(PadState& pad) {
    auto& in = Frontend::instance().input();

    const auto held = padGetButtons(&pad);
    std::uint16_t b = 0;

    auto set = [&](std::uint64_t bit, unsigned id) {
        if (held & bit) b |= (std::uint16_t)(1u << id);
    };

    // Standard libretro joypad layout. Switch buttons mapped to a
    // SNES/NES-style frontend (B/A swap relative to Nintendo's physical
    // layout — matches RetroArch defaults).
    set(HidNpadButton_B,         RETRO_DEVICE_ID_JOYPAD_B);
    set(HidNpadButton_Y,         RETRO_DEVICE_ID_JOYPAD_Y);
    set(HidNpadButton_Minus,     RETRO_DEVICE_ID_JOYPAD_SELECT);
    set(HidNpadButton_Plus,      RETRO_DEVICE_ID_JOYPAD_START);
    set(HidNpadButton_Up | HidNpadButton_StickLUp,    RETRO_DEVICE_ID_JOYPAD_UP);
    set(HidNpadButton_Down | HidNpadButton_StickLDown,  RETRO_DEVICE_ID_JOYPAD_DOWN);
    set(HidNpadButton_Left | HidNpadButton_StickLLeft, RETRO_DEVICE_ID_JOYPAD_LEFT);
    set(HidNpadButton_Right | HidNpadButton_StickLRight, RETRO_DEVICE_ID_JOYPAD_RIGHT);
    set(HidNpadButton_A,         RETRO_DEVICE_ID_JOYPAD_A);
    set(HidNpadButton_X,         RETRO_DEVICE_ID_JOYPAD_X);
    set(HidNpadButton_L,         RETRO_DEVICE_ID_JOYPAD_L);
    set(HidNpadButton_R,         RETRO_DEVICE_ID_JOYPAD_R);
    set(HidNpadButton_ZL,        RETRO_DEVICE_ID_JOYPAD_L2);
    set(HidNpadButton_ZR,        RETRO_DEVICE_ID_JOYPAD_R2);
    set(HidNpadButton_StickL,    RETRO_DEVICE_ID_JOYPAD_L3);
    set(HidNpadButton_StickR,    RETRO_DEVICE_ID_JOYPAD_R3);

    in.buttons = b;

    // Analog sticks: scale -32768..32767 → libretro range.
    const auto stickL = padGetStickPos(&pad, 0);
    const auto stickR = padGetStickPos(&pad, 1);
    in.axes[0] = (std::int16_t)stickL.x;
    in.axes[1] = (std::int16_t)-stickL.y; // Switch up=+, libretro up=-
    in.axes[2] = (std::int16_t)stickR.x;
    in.axes[3] = (std::int16_t)-stickR.y;
}

} // namespace foyer::libretro
