#pragma once

#include <string>

namespace foyer::library { struct Game; struct System; }

namespace foyer::browser {

// Locates /foyer/cores/foyer-<core>.nro for the given system and queues it
// via libnx envSetNextLoad with the rom path as argv[1]. Caller must exit
// the process to actually trigger the chain-launch.
//
// Returns true if envSetNextLoad succeeded. False if the player nro is
// missing on disk (caller should surface an "install core" prompt — Phase
// 5 will hook it up).
bool launch_game(const library::System& sys, const library::Game& game);

} // namespace foyer::browser
