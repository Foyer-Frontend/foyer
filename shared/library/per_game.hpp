#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace foyer::library {

struct SystemDef;
struct CoreDef;
struct Game;

// Per-rom user state keyed by absolute SD path. Persisted at
// /foyer/config/per_game.jsonc.
//
// Schema:
//   {
//     "/foyer/roms/nes/Tricky.nes": {
//       "core":        "nestopia",   // optional
//       "favorite":    true,         // optional
//       "last_played": 1715000000,   // optional, unix epoch seconds
//       "playtime":    12345         // optional, total seconds played
//     }
//   }

// Lookup the user's preferred core for `rom_path`. Returns "" on miss.
std::string per_game_core_for(std::string_view rom_path);

// Persist a new core preference for the given rom. Pass empty string to
// clear.
void set_per_game_core(std::string_view rom_path, std::string_view core_name);

// Favorites — surfaced in the home-view "Favorites" virtual system and
// in the System view filter cycle. Toggled by the user via X.
bool per_game_favorite(std::string_view rom_path);
void set_per_game_favorite(std::string_view rom_path, bool favorite);

// Launch tracking — last_played is the unix epoch seconds at which the
// user last hit Launch on this rom. Drives the "Recent" virtual system
// and the resume-last-game popup item.
std::uint64_t per_game_last_played(std::string_view rom_path);
void          mark_per_game_played(std::string_view rom_path);

// Total seconds played per rom. Browser doesn't update this — the
// player will write it back on a clean exit (TODO). Available now so
// the sort-by-playtime UI is wired end-to-end already.
std::uint64_t per_game_playtime(std::string_view rom_path);
void          add_per_game_playtime(std::string_view rom_path, std::uint64_t seconds);

// Per-game shader override (built-in name or *.glsl stem). Empty
// string means "fall back to Config::shader_name". Resolution order
// at launch time is per-rom > general default > "none".
std::string per_game_shader(std::string_view rom_path);
void        set_per_game_shader(std::string_view rom_path, std::string_view shader_name);

// Resolve which core to use for a rom: per-game override > general
// default_core_per_system > system_db default. Always returns a non-null
// CoreDef when the system has at least one configured core; nullptr only
// for unsupported systems. If `sys` is a virtual system (Recent /
// Favorites tile), recovers the rom's origin system from its path and
// recurses into that.
const CoreDef* resolve_core(const SystemDef& sys, std::string_view rom_path);

// Recover the rom's origin SystemDef purely from its path. Used by code
// paths that need the real system when the caller has only a virtual
// one (carousel-Recent / -Favorites tiles share Game objects but the
// surrounding System points at the virtual SystemDef).
const SystemDef* origin_system_for_rom(std::string_view rom_path);

// Hydrate a freshly-scanned Game struct with the favorite / last_played
// / playtime fields from the per_game store. Call once per game in
// scan_library so the in-memory Library snapshot reflects user state
// without further per-call lookups.
void apply_per_game_state(Game& g);

} // namespace foyer::library
