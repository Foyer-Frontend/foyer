#include "scanner.hpp"
#include "system_db.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace foyer::library {
namespace {

std::string lower(std::string_view in) {
    std::string out{in};
    for (auto& c : out) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }
    return out;
}

std::string ext_of(std::string_view name) {
    const auto pos = name.find_last_of('.');
    if (pos == std::string_view::npos) return {};
    return lower(name.substr(pos + 1));
}

std::string stem_of(std::string_view name) {
    const auto pos = name.find_last_of('.');
    return std::string{(pos == std::string_view::npos) ? name : name.substr(0, pos)};
}

bool ext_in_pipe_list(std::string_view ext, std::string_view list) {
    // Walk pipe-separated list; case-insensitive.
    std::size_t cursor = 0;
    while (cursor < list.size()) {
        const auto bar = list.find('|', cursor);
        const auto seg = list.substr(cursor,
            (bar == std::string_view::npos) ? list.size() - cursor : bar - cursor);
        if (seg.size() == ext.size()) {
            bool eq = true;
            for (std::size_t i = 0; i < seg.size(); i++) {
                char a = seg[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                char b = ext[i];
                if (a != b) { eq = false; break; }
            }
            if (eq) return true;
        }
        if (bar == std::string_view::npos) break;
        cursor = bar + 1;
    }
    return false;
}

void scan_dir_into(const std::string& path,
                   const SystemDef& def,
                   std::vector<Game>& out,
                   bool recurse) {
    auto* dir = ::opendir(path.c_str());
    if (!dir) return;
    while (auto* g = ::readdir(dir)) {
        if (g->d_name[0] == '.') continue;
        const std::string full = path + "/" + g->d_name;
        if (g->d_type == DT_DIR) {
            if (recurse) scan_dir_into(full, def, out, true);
            continue;
        }
        if (g->d_type != DT_REG) continue;
        const auto ext = ext_of(g->d_name);
        if (ext.empty()) continue;
        if (!ext_in_pipe_list(ext, def.extensions)) continue;

        Game game;
        game.path     = full;
        game.filename = g->d_name;
        game.stem     = stem_of(g->d_name);
        game.ext      = ext;
        game.display  = game.stem;
        out.emplace_back(std::move(game));
    }
    ::closedir(dir);
}

} // namespace

std::vector<System> scan_library(const ScanOptions& opts) {
    std::vector<System> out;
    auto* root = ::opendir(opts.rom_root.c_str());
    if (!root) {
        foyer::log::write("[scan] root '%s' missing\n", opts.rom_root.c_str());
        return out;
    }

    while (auto* d = ::readdir(root)) {
        if (d->d_name[0] == '.' || d->d_type != DT_DIR) continue;

        const auto* def = find_system_by_folder(d->d_name);
        if (!def) {
            // Folder name doesn't match any known system. Could be a user
            // collection — ignore for now (Phase 8 may surface it as an
            // "Unknown" system in the carousel).
            continue;
        }

        System sys;
        sys.def       = def;
        sys.root_path = opts.rom_root + "/" + d->d_name;

        scan_dir_into(sys.root_path, *def, sys.games, opts.recurse);

        // Stable alphabetical order until the user picks a sort.
        std::sort(sys.games.begin(), sys.games.end(),
            [](const Game& a, const Game& b) { return a.stem < b.stem; });

        if (!sys.games.empty()) {
            out.emplace_back(std::move(sys));
        }
    }
    ::closedir(root);

    foyer::log::write("[scan] %zu system(s) populated\n", out.size());
    for (const auto& s : out) {
        foyer::log::write("[scan]   %-13.*s  %3zu games\n",
            (int)s.def->folder_name.size(), s.def->folder_name.data(),
            s.games.size());
    }
    return out;
}

} // namespace foyer::library
