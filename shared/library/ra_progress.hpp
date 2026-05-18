#pragma once
//
// shared/library/ra_progress — pre-play RA achievement progress for
// the browser's GameActivity. Computes the rcheevos hash of a rom,
// resolves it to a RA game id, then fetches the user's current
// progress for that game and writes the result into the per-rom
// metadata sidecar (game_meta.{cheevos_total, cheevos_unlocked}).
//
// Called from a worker thread on the install queue; result is read
// back by GameActivity via load_meta() on completion.
//
// Requires:
//   - accounts.retroachievements.user      (RA username)
//   - accounts.retroachievements.web_api_key (REST stats key)
// When either is empty, fetch_progress() bails immediately.

#include <string>
#include <string_view>

namespace foyer::library {

// Synchronously: hash the rom, hit RA REST, write the sidecar.
// Returns true if the sidecar was updated (achievement counts now
// populated), false otherwise (creds missing, hash failed, network
// error, RA didn't know the rom).
//
// `system_folder` and `rom_stem` are used to read/write the same
// sidecar the player updates after each unlock — sources of truth
// stay aligned across browser + player.
bool fetch_progress(std::string_view system_folder,
                    std::string_view rom_stem,
                    std::string_view rom_path);

}  // namespace foyer::library
