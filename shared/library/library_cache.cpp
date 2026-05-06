#include "library_cache.hpp"
#include "system_db.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

#include <yyjson.h>

namespace foyer::library {
namespace {

std::time_t mtime_of(std::string_view path) {
    struct stat st{};
    if (::stat(std::string{path}.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

// One write helper that emits the games array for a given System.
// Kept terse — the cache is foyer-internal so we don't preserve
// every field, only the ones that actually matter for the next
// boot's render path.
void emit_games(std::ostream& out, const std::vector<Game>& games) {
    out << "[";
    bool first = true;
    for (const auto& g : games) {
        if (!first) out << ",";
        first = false;
        // Escape backslashes + quotes in the path/filename strings.
        // ROM files often contain quotes / unicode (no-intro names),
        // so be defensive.
        auto emit_str = [&](std::string_view s) {
            out << "\"";
            for (char c : s) {
                if (c == '\\' || c == '"') out << '\\';
                if (c == '\n')      { out << "\\n"; continue; }
                if (c == '\r')      { out << "\\r"; continue; }
                if (c == '\t')      { out << "\\t"; continue; }
                out << c;
            }
            out << "\"";
        };
        out << "{\"p\":";
        emit_str(g.path);
        out << ",\"n\":";
        emit_str(g.filename);
        out << ",\"s\":";
        emit_str(g.stem);
        out << ",\"e\":";
        emit_str(g.ext);
        out << ",\"d\":";
        emit_str(g.display);
        if (g.favorite)    out << ",\"f\":1";
        if (g.last_played) out << ",\"l\":" << g.last_played;
        out << "}";
    }
    out << "]";
}

// List the top-level system folders currently present under
// rom_root. Used to detect "user added a new system folder" — if
// a name shows up that the cache didn't capture, we invalidate.
std::set<std::string> list_top_folders(std::string_view rom_root) {
    std::set<std::string> out;
    auto* dir = ::opendir(std::string{rom_root}.c_str());
    if (!dir) return out;
    while (auto* e = ::readdir(dir)) {
        if (!e->d_name[0] || e->d_name[0] == '.') continue;
        if (e->d_type != DT_DIR) continue;
        out.emplace(e->d_name);
    }
    ::closedir(dir);
    return out;
}

} // namespace

bool save_library_cache(std::string_view path,
                        const std::vector<System>& systems,
                        std::string_view rom_root) {
    std::ofstream out{std::string{path}, std::ios::trunc};
    if (!out) {
        foyer::log::write("[lib_cache] open(%.*s) for write failed\n",
            (int)path.size(), path.data());
        return false;
    }

    // Snapshot every top-level folder we see today, even empty ones.
    // The loader compares against this set to detect "user added /
    // removed a system folder". Without it, an empty folder like
    // /foyer/roms/nds/ (created before any roms land there) makes the
    // loader think a new system appeared every boot, causing a full
    // rescan even when the populated systems are unchanged.
    const auto seen = list_top_folders(rom_root);

    out << "{";
    // v3: scanner now emits empty real systems too — apply_hide_empty
    // post-scan respects Config::hide_empty_systems. Pre-v3 caches
    // were saved without the empty entries so the toggle had nothing
    // to surface; bumping forces a one-time rescan on upgrade.
    out << "\"v\":3,";
    out << "\"rom_root\":\"" << rom_root << "\",";
    out << "\"rom_root_mtime\":" << mtime_of(rom_root) << ",";
    out << "\"seen_folders\":[";
    bool sf_first = true;
    for (const auto& f : seen) {
        if (!sf_first) out << ",";
        sf_first = false;
        out << "\"" << f << "\"";
    }
    out << "],";
    out << "\"systems\":[";
    bool first = true;
    for (const auto& s : systems) {
        if (!s.def) continue;
        // Skip virtual systems — their game list is reconstructed
        // from real systems' games + per_game state at scan time.
        if (is_virtual_system(*s.def)) continue;
        if (!first) out << ",";
        first = false;
        out << "{";
        out << "\"folder\":\"" << s.def->folder_name << "\",";
        out << "\"root\":\""   << s.root_path        << "\",";
        out << "\"mtime\":"    << mtime_of(s.root_path) << ",";
        out << "\"games\":";
        emit_games(out, s.games);
        out << "}";
    }
    out << "]";
    out << "}\n";
    foyer::log::write("[lib_cache] wrote %.*s (%zu systems)\n",
        (int)path.size(), path.data(), systems.size());
    return true;
}

std::optional<std::vector<System>>
load_library_cache(std::string_view path,
                   std::string_view rom_root) {
    std::ifstream in{std::string{path}};
    if (!in) return std::nullopt;
    std::stringstream ss; ss << in.rdbuf();
    const auto txt = ss.str();
    if (txt.empty()) return std::nullopt;

    auto* doc = yyjson_read(txt.data(), txt.size(), 0);
    if (!doc) {
        foyer::log::write("[lib_cache] parse error at %.*s\n",
            (int)path.size(), path.data());
        return std::nullopt;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return std::nullopt; }

    // Schema version guard. Bump when the on-disk shape changes.
    // v2 added `seen_folders` so the loader can distinguish "no-op
    // empty folder still there" from "user added a real new system".
    if (auto* v = yyjson_obj_get(root, "v");
        !v || !yyjson_is_int(v) || yyjson_get_int(v) != 3) {
        yyjson_doc_free(doc);
        return std::nullopt;
    }
    if (auto* rr = yyjson_obj_get(root, "rom_root");
        !rr || !yyjson_is_str(rr)
            || std::string{yyjson_get_str(rr)} != std::string{rom_root}) {
        // User changed rom_root — cache is meaningless against the new path.
        yyjson_doc_free(doc);
        return std::nullopt;
    }
    if (auto* rrt = yyjson_obj_get(root, "rom_root_mtime");
        rrt && yyjson_is_int(rrt)) {
        const std::time_t cached_root_mtime = (std::time_t)yyjson_get_int(rrt);
        if (mtime_of(rom_root) > cached_root_mtime) {
            // New / removed system folder under rom_root.
            yyjson_doc_free(doc);
            return std::nullopt;
        }
    }

    std::vector<System> out;
    std::set<std::string> cached_folders;

    auto* arr = yyjson_obj_get(root, "systems");
    if (arr && yyjson_is_arr(arr)) {
        std::size_t i, max; yyjson_val* item;
        yyjson_arr_foreach(arr, i, max, item) {
            if (!yyjson_is_obj(item)) continue;

            auto get_str = [&](const char* k) -> std::string {
                auto* v = yyjson_obj_get(item, k);
                return (v && yyjson_is_str(v)) ? std::string{yyjson_get_str(v)} : std::string{};
            };
            auto get_int = [&](const char* k) -> std::int64_t {
                auto* v = yyjson_obj_get(item, k);
                return (v && yyjson_is_int(v)) ? yyjson_get_int(v) : 0;
            };

            const auto folder = get_str("folder");
            const auto root_path = get_str("root");
            const auto mtime = (std::time_t)get_int("mtime");

            const auto* def = find_system_by_folder(folder);
            if (!def) continue; // system_db changed — drop unknown folder

            // Per-folder freshness check.
            if (mtime_of(root_path) > mtime) {
                yyjson_doc_free(doc);
                return std::nullopt;
            }

            System sys;
            sys.def       = def;
            sys.root_path = root_path;

            if (auto* gs = yyjson_obj_get(item, "games");
                gs && yyjson_is_arr(gs)) {
                std::size_t j, jmax; yyjson_val* gv;
                yyjson_arr_foreach(gs, j, jmax, gv) {
                    if (!yyjson_is_obj(gv)) continue;
                    Game g;
                    auto pull_str = [&](const char* k, std::string& dst) {
                        auto* v = yyjson_obj_get(gv, k);
                        if (v && yyjson_is_str(v)) dst = yyjson_get_str(v);
                    };
                    pull_str("p", g.path);
                    pull_str("n", g.filename);
                    pull_str("s", g.stem);
                    pull_str("e", g.ext);
                    pull_str("d", g.display);
                    if (auto* fv = yyjson_obj_get(gv, "f");
                        fv && yyjson_is_int(fv) && yyjson_get_int(fv))
                        g.favorite = true;
                    if (auto* lv = yyjson_obj_get(gv, "l");
                        lv && (yyjson_is_int(lv) || yyjson_is_uint(lv)))
                        g.last_played = (std::uint64_t)yyjson_get_uint(lv);
                    sys.games.push_back(std::move(g));
                }
            }

            out.push_back(std::move(sys));
            cached_folders.emplace(folder);
        }
    }
    // Pull the `seen_folders` set written by the saver — every
    // top-level rom_root entry we observed at save time, populated or
    // empty. Comparing today's set against this lets us detect both
    // additions (new system to scan) and removals without false-
    // positives from empty folders that haven't gained roms yet.
    std::set<std::string> seen_at_write;
    if (auto* sf = yyjson_obj_get(root, "seen_folders");
        sf && yyjson_is_arr(sf)) {
        std::size_t i, n;
        yyjson_val* item;
        yyjson_arr_foreach(sf, i, n, item) {
            if (yyjson_is_str(item)) seen_at_write.emplace(yyjson_get_str(item));
        }
    }
    yyjson_doc_free(doc);

    const auto today = list_top_folders(rom_root);
    if (today != seen_at_write) {
        // Symmetric difference would tell us add vs. remove; for the
        // log just note the count so the user can grep.
        foyer::log::write(
            "[lib_cache] folder set changed (was %zu, now %zu) - rescanning\n",
            seen_at_write.size(), today.size());
        return std::nullopt;
    }

    foyer::log::write("[lib_cache] hit (%zu systems)\n", out.size());
    return out;
}

} // namespace foyer::library
