#pragma once

namespace foyer::browser {

// Roms-over-USB. Spins up libhaze with a single FileSystemProxy whose root is
// pinned to the user's configured rom_root, so the host computer sees only
// /foyer/roms/* — never the rest of the SD card.
//
// stop() is safe to call when the server isn't running. Calling start()
// twice is a no-op (returns true).

bool mtp_start();
void mtp_stop();
bool mtp_running();

} // namespace foyer::browser
