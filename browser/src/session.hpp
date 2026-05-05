#pragma once

#include "views.hpp"

namespace foyer::browser {

// One-shot view-state stash written right before envSetNextLoad
// fires and consumed on the next foyer boot — gives the user "back
// to where I was" continuity when returning from a libretro core
// or external standalone.
//
// Cold launch (hbmenu) lands on Home view as expected. Only an
// explicit "foyer-resume" argv token from the player's chain-back
// triggers the restore — that flag is the unambiguous "we're
// returning from a core" signal.

// Persist the bits worth restoring. Runs synchronously; small JSON
// blob at /foyer/data/session.json. Safe to call from any view.
void save_session(const State& s);

// Read the session file (if any) and apply the stashed view +
// indices to `s`. Always deletes the file after read so a stale
// blob doesn't pollute a future cold launch. Caller is expected
// to have checked argv for the resume marker first; if `restore`
// is false this becomes a pure cleanup (file removed, state
// untouched).
void load_and_consume_session(State& s, bool restore);

// Convenience: returns true if argv contains the "foyer-resume"
// token the player's chain-back inserts. Cold launches from
// hbmenu won't have it.
bool argv_has_resume_marker(int argc, char** argv);

} // namespace foyer::browser
