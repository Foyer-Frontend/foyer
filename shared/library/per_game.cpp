#include "per_game.hpp"
#include "system_db.hpp"
#include "config.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kPath = "/foyer/config/per_game.jsonc";

std::mutex                                      g_mutex;
std::unordered_map<std::string, std::string>    g_overrides;
std::atomic<bool>                               g_loaded{false};

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
    g_overrides.clear();

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
            auto* core = yyjson_obj_get(v, "core");
            if (core && yyjson_is_str(core)) {
                g_overrides.emplace(yyjson_get_str(k), yyjson_get_str(core));
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
    out << "// foyer per-rom overrides. Keys are absolute SD paths.\n";
    out << "{\n";
    bool first = true;
    for (const auto& [path, core] : g_overrides) {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << path << "\": { \"core\": \"" << core << "\" }";
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

} // namespace

std::string per_game_core_for(std::string_view rom_path) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    auto it = g_overrides.find(std::string{rom_path});
    return (it == g_overrides.end()) ? std::string{} : it->second;
}

void set_per_game_core(std::string_view rom_path, std::string_view core_name) {
    ensure_loaded();
    std::scoped_lock lk{g_mutex};
    if (core_name.empty()) {
        g_overrides.erase(std::string{rom_path});
    } else {
        g_overrides[std::string{rom_path}] = std::string{core_name};
    }
    save_locked();
}

const CoreDef* resolve_core(const SystemDef& sys, std::string_view rom_path) {
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
