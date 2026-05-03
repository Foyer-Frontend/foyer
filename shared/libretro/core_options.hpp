#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "libretro.h"

namespace foyer::libretro {

// One libretro core option: key, current value, the rest of the metadata
// the pause overlay needs to render a picker.
struct CoreOption {
    std::string              key;
    std::string              desc;          // human label, e.g. "Palette"
    std::vector<std::string> choices;       // possible values
    std::string              value;         // current value
    std::string              default_value; // value the core declared as default
};

// Stores the option list a core registered via SET_VARIABLES /
// SET_CORE_OPTIONS / SET_CORE_OPTIONS_V2 and persists user overrides.
//
// Two layers, applied bottom-up at load time:
//   1. /foyer/config/cores/<core>.jsonc  — per-core defaults shared by
//      every rom this core runs (think "tweaks I always want for
//      snes9x").
//   2. /foyer/config/cores/per_game/<rom_basename>__<core>.jsonc —
//      per-rom overrides written when the user changes a value while
//      this rom is loaded. Only applied when set_rom_path() was
//      called before ingest.
//
// Hot-state lives in a singleton so the libretro env_cb can reach it
// without threading an instance pointer.
class CoreOptions {
public:
    static CoreOptions& instance();

    // Tell us which core's option file to read/write. Call before ingesting.
    void set_core_name(std::string_view name);

    // Tell us which rom is loaded so per-rom overrides can layer on
    // top of per-core defaults. Pass an empty path to revert to
    // per-core-only mode (useful for system-level browsing).
    void set_rom_path(std::string_view rom_path);

    // Legacy SET_VARIABLES (retro_variable[]). The "value" field is a
    // "Description; choice|choice|choice" string with the first choice as
    // the default.
    void ingest_legacy(const struct retro_variable* vars);

    // SET_CORE_OPTIONS — array of retro_core_option_definition terminated
    // by a NULL key.
    void ingest_v1(const struct retro_core_option_definition* defs);

    // SET_CORE_OPTIONS_V2 — defs plus optional groups. We flatten and ignore
    // categories for now.
    void ingest_v2(const struct retro_core_options_v2* opts);

    // RETRO_ENVIRONMENT_GET_VARIABLE.
    const char* get(std::string_view key) const;

    // True if any value has changed since the last call. Resets on read.
    bool consume_dirty();

    // Updated by the pause overlay's option picker. Persists immediately.
    void set(std::string_view key, std::string_view value);

    // Read-only view for the overlay UI.
    const std::vector<CoreOption>& options() const { return m_opts; }

private:
    void load_overrides_from_disk();
    void load_overrides_from_file(const std::string& path);
    void save_to_disk() const;

    std::string per_core_path() const;
    std::string per_game_path() const;

    std::string             m_core_name;
    std::string             m_rom_path;     // empty = no per-rom layer
    std::vector<CoreOption> m_opts;
    bool                    m_dirty = false;
};

} // namespace foyer::libretro
