#pragma once

#include <string>
#include <string_view>

namespace foyer::library {

struct SystemDef;

// Per-rom overrides keyed by absolute SD path. Persisted at
// /foyer/config/per_game.jsonc.
//
// Schema:
//   {
//     "/foyer/roms/nes/Tricky.nes": { "core": "nestopia" }
//   }

// Lookup the user's preferred core for `rom_path`. Returns "" on miss.
std::string per_game_core_for(std::string_view rom_path);

// Persist a new core preference for the given rom. Pass empty string to
// clear.
void set_per_game_core(std::string_view rom_path, std::string_view core_name);

// Resolve which core to use for a rom: per-game override > general
// default_core_per_system > system_db default. Always returns a non-null
// CoreDef when the system has at least one configured core; nullptr only
// for unsupported systems.
struct CoreDef;
const CoreDef* resolve_core(const SystemDef& sys, std::string_view rom_path);

} // namespace foyer::library
