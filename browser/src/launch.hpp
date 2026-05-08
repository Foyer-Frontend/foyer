#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace foyer::library { struct Game; struct System; }

namespace foyer::browser {

// Locates /foyer/cores/foyer-<core>.nro for the given system and queues it
// via libnx envSetNextLoad with the rom path as argv[1]. Caller must exit
// the process to actually trigger the chain-launch.
//
// Returns true if envSetNextLoad succeeded. False if the player nro is
// missing on disk (caller should surface an "install core" prompt — Phase
// 5 will hook it up).
// `resume_slot` >= 0 asks the player to load that save-state slot right after
// the rom finishes booting. Pass -1 (default) to launch fresh.
bool launch_game(const library::System& sys, const library::Game& game,
                 int resume_slot = -1);

// Scans /foyer/states/<sys>/<stem>.<slot>.state for the rom and returns the
// slot index whose file has the most recent mtime, or -1 if none exist.
int latest_state_slot(const library::System& sys, const library::Game& game);

// Chain-launch an arbitrary NRO on the SD. Walks each candidate path
// in order (skipping empty / nonexistent ones) and queues the first
// match through envSetNextLoad. Caller exits the process to actually
// trigger the load. Returns the path that was queued, empty string if
// no candidate resolved.
std::string queue_external_nro(std::initializer_list<std::string_view> candidates);

} // namespace foyer::browser
