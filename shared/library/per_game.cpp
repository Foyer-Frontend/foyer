#include "per_game.hpp"
#include "scanner.hpp"
#include "system_db.hpp"
#include "config.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kPath = "/foyer/data/config/per_game.jsonc";

struct Entry {
    std::string   core;
    std::string   shader;
    bool          favorite    = false;
    std::uint64_t last_played = 0;
    std::uint64_t playtime    = 0;
    // -1 means "inherit Config::runahead_frames" (default). 0..4 are
    // explicit per-rom overrides — saved when the user touches the
    // value via the GameDetail or pause-overlay knob.
    int           runahead    = -1;
    // -1 = inherit Config::show_bezels (default), 0 = off, 1 = on.
    // Toggled by the pause menu's "Show bezels" item so changes
    // don't bleed across games on other cores.
    int           show_bezel  = -1;
    // -1 = no per-game pick (defaults to AspectMode::DisplayCore
    // at boot). 0..6 = the AspectMode enum values from
    // libretro/aspect.hpp. Persisted from the pause menu's
    // Display→Aspect picker.
    int           aspect      = -1;
    // Empty = "(auto)" — bezel_sdl walks the normal fallback chain.
    // Non-empty = absolute file path the resolver renders directly,
    // skipping the chain. Written by the per-game default-bezel
    // picker on the rom's PerGameActivity page.
    std::string   bezel_choice;
};

std::mutex                                 g_mutex;
std::unordered_map<std::string, Entry>     g_entries;
std::atomic<bool>                          g_loaded{false};

bool entry_is_default(const Entry& e) {
    return e.core.empty() && e.shader.empty() && !e.favorite
        && e.last_played == 0 && e.playtime == 0 && e.runahead < 0
        && e.show_bezel < 0 && e.aspect < 0 && e.bezel_choice.empty();
}

std::string strip_comments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_str = false, escape = false;
    for (std::size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (in_str) {
            out.push_back(c);
            if (escape)         escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') i++;
            if (i < in.size()) out.push_back(in[i]);
            continue;
        }
        out.push_back(c);
    }
    return out;
}

void load_locked() {
    g_entries.clear();

    std::ifstream in{kPath};
    if (!in) return;

    std::stringstream ss;
    ss << in.rdbuf();
    const auto stripped = strip_comments(ss.str());

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[per_game] parse error in %s\n", kPath);
        return;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        std::size_t i, max; yyjson_val *k, *v;
        yyjson_obj_foreach(root, i, max, k, v) {
            if (!yyjson_is_str(k) || !yyjson_is_obj(v)) continue;
            Entry e;
            if (auto* x = yyjson_obj_get(v, "core");
                x && yyjson_is_str(x))
                e.core = yyjson_get_str(x);
            if (auto* x = yyjson_obj_get(v, "shader");
                x && yyjson_is_str(x))
                e.shader = yyjson_get_str(x);
            if (auto* x = yyjson_obj_get(v, "favorite");
                x && yyjson_is_bool(x))
                e.favorite = yyjson_get_bool(x);
            if (auto* x = yyjson_obj_get(v, "last_played");
                x && (yyjson_is_uint(x) || yyjson_is_int(x)))
                e.last_played = (std::uint64_t)yyjson_get_uint(x);
            if (auto* x = yyjson_obj_get(v, "playtime");
                x && (yyjson_is_uint(x) || yyjson_is_int(x)))
                e.playtime = (std::uint64_t)yyjson_get_uint(x);
            if (auto* x = yyjson_obj_get(v, "runahead");
                x && yyjson_is_int(x)) {
                int n = (int)yyjson_get_int(x);
                if (n < 0) n = -1;
                if (n > 4) n = 4;
                e.runahead = n;
            }
            if (auto* x = yyjson_obj_get(v, "show_bezel");
                x && yyjson_is_int(x)) {
                int n = (int)yyjson_get_int(x);
                if (n < -1) n = -1;
                if (n > 1)  n = 1;
                e.show_bezel = n;
            }
            if (auto* x = yyjson_obj_get(v, "aspect");
                x && yyjson_is_int(x)) {
                int n = (int)yyjson_get_int(x);
                if (n < -1) n = -1;
                if (n > 6)  n = 6;
                e.aspect = n;
            }
            if (auto* x = yyjson_obj_get(v, "bezel_choice");
                x && yyjson_is_str(x)) {
                e.bezel_choice = yyjson_get_str(x);
            }
            if (!entry_is_default(e)) {
                g_entries.emplace(yyjson_get_str(k), std::move(e));
            }
        }
    }
    yyjson_doc_free(doc);
}

void save_locked() {
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[per_game] could not write %s\n", kPath);
        return;
    }
    out << "// foyer per-rom state. Keys are absolute SD paths.\n";
    out << "{\n";
    bool first = true;
    for (const auto& [path, e] : g_entries) {
        if (entry_is_default(e)) continue;
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << path << "\": {";
        bool need_comma = false;
        if (!e.core.empty()) {
            out << " \"core\": \"" << e.core << "\"";
            need_comma = true;
        }
        if (!e.shader.empty()) {
            if (need_comma) out << ",";
            out << " \"shader\": \"" << e.shader << "\"";
            need_comma = true;
        }
        if (e.favorite) {
            if (need_comma) out << ",";
            out << " \"favorite\": true";
            need_comma = true;
        }
        if (e.last_played) {
            if (need_comma) out << ",";
            out << " \"last_played\": " << e.last_played;
            need_comma = true;
        }
        if (e.playtime) {
            if (need_comma) out << ",";
            out << " \"playtime\": " << e.playtime;
            need_comma = true;
        }
        if (e.runahead >= 0) {
            if (need_comma) out << ",";
            out << " \"runahead\": " << e.runahead;
            need_comma = true;
        }
        if (e.show_bezel >= 0) {
            if (need_comma) out << ",";
            out << " \"show_bezel\": " << e.show_bezel;
            need_comma = true;
        }
        if (e.aspect >= 0) {
            if (need_comma) out << ",";
            out << " \"aspect\": " << e.aspect;
            need_comma = true;
        }
        if (!e.bezel_choice.empty()) {
            if (need_comma) out << ",";
            out << " \"bezel_choice\": \"" << e.bezel_choice << "\"";
            need_comma = true;
        }
        out << " }";
    }
    out << "\n}\n";
}

void ensure_loaded() {
    if (g_loaded.load()) return;
    std::scoped_lock lk{g_mutex};
    if (g_loaded.load()) return;
    load_locked();
    g_loaded = true;
}

// Lookup (read-only) under the global lock. Caller must call
// ensure_loaded() FIRST, before taking the lock — ensure_loaded
// takes its own lock briefly and std::mutex isn't recursive.
const Entry* find_entry_locked(std::string_view rom_path) {
    auto it = g_entries.find(std::string{rom_path});
    return (it == g_entries.end()) ? nullptr : &it->second;
}

// Mutate one field of an entry, then persist. Lock is taken inside
// (caller must NOT hold it).
template <typename Mutator>
void mutate(std::string_view rom_path, Mutator&& mut) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto& e = g_entries[std::string{rom_path}];
    mut(e);
    if (entry_is_default(e)) {
        g_entries.erase(std::string{rom_path});
    }
    save_locked();
}

} // namespace

std::string per_game_core_for(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->core : std::string{};
}

void set_per_game_core(std::string_view rom_path, std::string_view core_name) {
    mutate(rom_path, [&](Entry& e) { e.core = std::string{core_name}; });
}

bool per_game_favorite(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->favorite : false;
}

void set_per_game_favorite(std::string_view rom_path, bool favorite) {
    mutate(rom_path, [&](Entry& e) { e.favorite = favorite; });
}

std::uint64_t per_game_last_played(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->last_played : 0;
}

void mark_per_game_played(std::string_view rom_path) {
    const auto now = (std::uint64_t)std::time(nullptr);
    mutate(rom_path, [&](Entry& e) { e.last_played = now; });
}

std::uint64_t per_game_playtime(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->playtime : 0;
}

void add_per_game_playtime(std::string_view rom_path, std::uint64_t seconds) {
    mutate(rom_path, [&](Entry& e) { e.playtime += seconds; });
}

void clear_per_game_playtime(std::string_view rom_path) {
    mutate(rom_path, [&](Entry& e) {
        e.playtime    = 0;
        e.last_played = 0;
    });
}

std::string per_game_shader(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->shader : std::string{};
}

void set_per_game_shader(std::string_view rom_path, std::string_view shader_name) {
    mutate(rom_path, [&](Entry& e) { e.shader = std::string{shader_name}; });
}

int per_game_runahead(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->runahead : -1;
}

void set_per_game_runahead(std::string_view rom_path, int frames) {
    if (frames < -1) frames = -1;
    if (frames > 4)  frames = 4;
    mutate(rom_path, [&](Entry& e) { e.runahead = frames; });
}

int per_game_show_bezel(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->show_bezel : -1;
}

void set_per_game_show_bezel(std::string_view rom_path, int tri_state) {
    if (tri_state < -1) tri_state = -1;
    if (tri_state > 1)  tri_state = 1;
    mutate(rom_path, [&](Entry& e) { e.show_bezel = tri_state; });
}

std::string per_game_bezel_choice(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->bezel_choice : std::string{};
}

void set_per_game_bezel_choice(std::string_view rom_path,
                               std::string_view abs_path) {
    mutate(rom_path, [&](Entry& e) {
        e.bezel_choice = std::string{abs_path};
    });
}

int per_game_aspect(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(rom_path);
    return e ? e->aspect : -1;
}

void set_per_game_aspect(std::string_view rom_path, int aspect_mode) {
    if (aspect_mode < -1) aspect_mode = -1;
    if (aspect_mode > 6)  aspect_mode = 6;
    mutate(rom_path, [&](Entry& e) { e.aspect = aspect_mode; });
}

void apply_per_game_state(Game& g) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto* e = find_entry_locked(g.path);
    if (!e) return;
    g.favorite    = e->favorite;
    g.last_played = e->last_played;
}

const SystemDef* origin_system_for_rom(std::string_view rom_path) {
    constexpr std::string_view kPrefix = "/foyer/roms/";
    std::string_view p = rom_path;
    if (!p.starts_with(kPrefix)) return nullptr;
    p.remove_prefix(kPrefix.size());
    const auto slash = p.find('/');
    if (slash == std::string_view::npos) return nullptr;
    return find_system_by_folder(p.substr(0, slash));
}

const CoreDef* resolve_core(const SystemDef& sys, std::string_view rom_path) {
    // When called for a virtual system (Recent / Favorites tile), the
    // sys here has no cores of its own. Recover the rom's origin
    // system from its path and recurse.
    if (is_virtual_system(sys)) {
        // path layout: /foyer/roms/<folder>/...
        std::string_view p = rom_path;
        constexpr std::string_view kPrefix = "/foyer/roms/";
        if (p.starts_with(kPrefix)) p.remove_prefix(kPrefix.size());
        const auto slash = p.find('/');
        if (slash == std::string_view::npos) return nullptr;
        const auto folder = p.substr(0, slash);
        if (auto* real = find_system_by_folder(folder)) {
            return resolve_core(*real, rom_path);
        }
        return nullptr;
    }

    if (sys.cores.empty()) return nullptr;

    // 1. per-rom override.
    if (const auto pg = per_game_core_for(rom_path); !pg.empty()) {
        if (auto* c = find_core_in_system(sys, pg)) return c;
    }

    // 2. general.jsonc default_core_per_system[<folder>] override.
    if (auto* general = config().default_core_for(sys.folder_name);
        general && *general) {
        if (auto* c = find_core_in_system(sys, general)) return c;
    }

    // 3. system_db default.
    return default_core(sys);
}

} // namespace foyer::library
