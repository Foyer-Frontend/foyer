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
// player's session_tracker writes it back on a clean exit (landed
// in 0.7.15). Drives the sort-by-playtime UI.
std::uint64_t per_game_playtime(std::string_view rom_path);
void          add_per_game_playtime(std::string_view rom_path, std::uint64_t seconds);
// Reset both playtime AND last_played to zero — used by the System
// view's "Clear playtime" bulk action.
void          clear_per_game_playtime(std::string_view rom_path);

// Per-game shader override (built-in name or *.glsl stem). Empty
// string means "fall back to Config::shader_name". Resolution order
// at launch time is per-rom > general default > "none".
std::string per_game_shader(std::string_view rom_path);
void        set_per_game_shader(std::string_view rom_path, std::string_view shader_name);

// Per-game bezel pick. Empty = "(auto)" — resolver walks the normal
// per-rom → per-system → legacy fallback chain. Non-empty = absolute
// file path of the bezel PNG to render (either a game-specific file
// under /foyer/assets/system/<sys>/<stem>/ or an installed
// per-system pack at /foyer/content/bezels/<name>.png). Lets the
// user fix a specific bezel choice for one rom without touching
// the per-system default.
std::string per_game_bezel_choice(std::string_view rom_path);
void        set_per_game_bezel_choice(std::string_view rom_path,
                                      std::string_view abs_path);

// Per-game run-ahead override. -1 means "fall back to
// Config::runahead_frames". 0..4 sets the explicit lookahead count
// for this rom (some games tolerate higher values without artifacts;
// others crash at K>=2 and need a per-rom cap).
int  per_game_runahead(std::string_view rom_path);
void set_per_game_runahead(std::string_view rom_path, int frames);

// Per-game bezel visibility override. -1 means "fall back to
// Config::show_bezels". 0 = off, 1 = on. Lets the pause-menu
// "Show bezels" toggle scope its effect to the current rom
// instead of writing the global default — change on gambatte
// doesn't bleed into swanstation's games.
int  per_game_show_bezel(std::string_view rom_path);
void set_per_game_show_bezel(std::string_view rom_path, int tri_state);

// Per-game aspect-mode override. -1 means "no per-game pick yet"
// (defaults to AspectMode::DisplayCore at boot). 0..6 maps to the
// AspectMode enum values in libretro/aspect.hpp (kept as a plain
// int here to avoid the libretro include in shared/library/).
// Pause-menu Display→Aspect writes this so the choice survives a
// clean exit and re-launch instead of resetting per session.
int  per_game_aspect(std::string_view rom_path);
void set_per_game_aspect(std::string_view rom_path, int aspect_mode);

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
