#pragma once

#include <switch.h>

namespace foyer::libretro {

// Maps the Switch pad state into Frontend::InputState before each retro_run.
// The frontend then reflects that back to the core via input_state_cb.
void poll_input(PadState& pad);

} // namespace foyer::libretro
