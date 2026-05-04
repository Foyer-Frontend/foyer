#include "scanner.hpp"
#include "config.hpp"
#include "per_game.hpp"
#include "system_db.hpp"
#include "platform/log.hpp"
#include "util/archive.hpp"

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

bool is_archive_ext(std::string_view ext) {
    return ext == "zip" || ext == "7z";
}

// Loose variant for the synthetic Unknown system — accepts any file
// with any extension so the user can see what's actually inside an
// unrecognised /foyer/roms/<folder>/. We don't peek archives here
// (we don't know what to look for inside them) and we cap the
// per-folder count at 200 so a misplaced 5,000-file dump doesn't
// blow up the carousel. Symlinks (DT_LNK) are not followed.
void scan_dir_loose(const std::string& path,
                    std::vector<Game>& out,
                    bool recurse,
                    std::size_t soft_cap) {
    if (out.size() >= soft_cap) return;
    auto* dir = ::opendir(path.c_str());
    if (!dir) return;
    while (auto* g = ::readdir(dir)) {
        if (out.size() >= soft_cap) break;
        if (g->d_name[0] == '.') continue;
        const std::string full = path + "/" + g->d_name;
        if (g->d_type == DT_DIR) {
            if (recurse) scan_dir_loose(full, out, true, soft_cap);
            continue;
        }
        if (g->d_type != DT_REG) continue;
        const auto ext = ext_of(g->d_name);
        if (ext.empty()) continue;

        Game game;
        game.path     = full;
        game.filename = g->d_name;
        game.stem     = stem_of(g->d_name);
        game.ext      = ext;
        game.display  = game.stem;
        apply_per_game_state(game);
        out.emplace_back(std::move(game));
    }
    ::closedir(dir);
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

        bool accept = ext_in_pipe_list(ext, def.extensions);
        if (!accept && is_archive_ext(ext)) {
            // Peek the archive: only accept if it carries a rom whose
            // inner extension matches this system's known list.
            auto inner = foyer::util::archive_peek_inner_rom(full, def.extensions);
            accept = !inner.empty();
        }
        if (!accept) continue;

        Game game;
        game.path     = full;
        game.filename = g->d_name;
        game.stem     = stem_of(g->d_name);
        game.ext      = ext;
        game.display  = game.stem;
        // Hydrate favorite + last_played from the persisted store so
        // browser-side filters work without per-call lookups.
        apply_per_game_state(game);
        out.emplace_back(std::move(game));
    }
    ::closedir(dir);
}

void sort_games(std::vector<Game>& games, Config::SortMode mode) {
    auto by_name = [](const Game& a, const Game& b) {
        return a.stem < b.stem;
    };
    switch (mode) {
        case Config::SortMode::Recent:
            std::sort(games.begin(), games.end(),
                [&](const Game& a, const Game& b) {
                    if (a.last_played != b.last_played)
                        return a.last_played > b.last_played;
                    return by_name(a, b);
                });
            break;
        case Config::SortMode::Playtime:
            std::sort(games.begin(), games.end(),
                [&](const Game& a, const Game& b) {
                    // playtime not in Game today — fall back to name
                    // ordering. Playtime sort wires up properly when
                    // the player writes playtime back to per_game.
                    return by_name(a, b);
                });
            break;
        case Config::SortMode::Favorites:
            std::sort(games.begin(), games.end(),
                [&](const Game& a, const Game& b) {
                    if (a.favorite != b.favorite) return a.favorite;
                    return by_name(a, b);
                });
            break;
        case Config::SortMode::Name:
        default:
            std::sort(games.begin(), games.end(), by_name);
            break;
    }
}

} // namespace

std::vector<System> scan_library(const ScanOptions& opts) {
    std::vector<System> out;
    auto* root = ::opendir(opts.rom_root.c_str());
    if (!root) {
        foyer::log::write("[scan] root '%s' missing\n", opts.rom_root.c_str());
        return out;
    }

    // Folders that don't match any known system get pooled into the
    // synthetic Unknown tile after the main scan, so the user at least
    // sees the rom data exists.
    System unknown;
    unknown.def       = &kVirtualUnknownDef;
    unknown.root_path = opts.rom_root;

    while (auto* d = ::readdir(root)) {
        if (d->d_name[0] == '.' || d->d_type != DT_DIR) continue;

        const auto* def = find_system_by_folder(d->d_name);
        if (!def) {
            // Unrecognised folder. Pull its files into the Unknown
            // bucket — capped at 200 so a misplaced dump doesn't
            // dominate the carousel.
            const auto sub = opts.rom_root + "/" + d->d_name;
            scan_dir_loose(sub, unknown.games, opts.recurse, /*soft_cap=*/200);
            continue;
        }

        System sys;
        sys.def       = def;
        sys.root_path = opts.rom_root + "/" + d->d_name;

        scan_dir_into(sys.root_path, *def, sys.games, opts.recurse);

        // Apply the user's selected sort order. config().sort_mode is
        // a per-browser preference; cycling it in Settings -> Library
        // and rescanning is enough to re-order without restarting.
        sort_games(sys.games, config().sort_mode);

        if (!sys.games.empty()) {
            out.emplace_back(std::move(sys));
        }
    }
    ::closedir(root);

    if (!unknown.games.empty()) {
        sort_games(unknown.games, Config::SortMode::Name);
        out.emplace_back(std::move(unknown));
    }

    // Synthesise the virtual "Recent" + "Favorites" carousel tiles by
    // pooling games across every real system. Inserted in reverse order
    // so Recent ends up at index 0 and Favorites at index 1 (when both
    // have content). Empty virtuals are simply skipped — no point in
    // showing a Favorites tile if the user hasn't favorited anything.
    auto add_virtual = [&](const SystemDef& def, auto&& filter, auto&& sort_fn,
                           std::size_t cap) {
        System v;
        v.def       = &def;
        v.root_path = "(virtual)";
        for (auto& real : out) {
            for (auto& g : real.games) {
                if (filter(g)) v.games.push_back(g);
            }
        }
        if (v.games.empty()) return;
        sort_fn(v.games);
        if (cap > 0 && v.games.size() > cap) v.games.resize(cap);
        out.insert(out.begin(), std::move(v));
    };
    add_virtual(kVirtualFavoritesDef,
        [](const Game& g) { return g.favorite; },
        [](std::vector<Game>& v) {
            std::sort(v.begin(), v.end(),
                [](const Game& a, const Game& b) { return a.stem < b.stem; });
        },
        /*cap=*/0);
    add_virtual(kVirtualRecentDef,
        [](const Game& g) { return g.last_played > 0; },
        [](std::vector<Game>& v) {
            std::sort(v.begin(), v.end(),
                [](const Game& a, const Game& b) {
                    return a.last_played > b.last_played;
                });
        },
        /*cap=*/100);

    foyer::log::write("[scan] %zu system(s) populated\n", out.size());
    for (const auto& s : out) {
        foyer::log::write("[scan]   %-13.*s  %3zu games\n",
            (int)s.def->folder_name.size(), s.def->folder_name.data(),
            s.games.size());
    }
    return out;
}

} // namespace foyer::library
