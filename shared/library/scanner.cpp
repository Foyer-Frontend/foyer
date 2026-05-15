#include "scanner.hpp"
#include "config.hpp"
#include "library_cache.hpp"
#include "per_game.hpp"
#include "switch_titles.hpp"
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

// Drop systems whose game list is empty, when the user has toggled
// "Hide empty systems" on in Settings → Library. Applied first so
// the subsequent sort operates on the visible subset only. Virtual
// systems (Recent / Favorites) keep their special handling — those
// don't go through this list.
void apply_hide_empty(std::vector<System>& systems) {
    if (!config().hide_empty_systems) return;
    systems.erase(
        std::remove_if(systems.begin(), systems.end(),
            [](const System& s){ return s.games.empty(); }),
        systems.end());
}

// Reorder real systems according to Config::system_sort_mode. Used by
// both the cache-hit fast path and the full-scan path, so a switch in
// system_sort_mode without other library changes still re-orders on
// the next scan_library() call.
void apply_system_sort(std::vector<System>& systems) {
    const auto mode = config().system_sort_mode;
    if (mode == Config::SystemSortMode::ScannerOrder) return;

    auto folder_lc = [](std::string_view s) {
        std::string r{s};
        for (auto& c : r) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return r;
    };
    switch (mode) {
        case Config::SystemSortMode::Alphabetical:
            std::sort(systems.begin(), systems.end(),
                [&](const System& a, const System& b) {
                    return folder_lc(a.def ? a.def->short_name : "") <
                           folder_lc(b.def ? b.def->short_name : "");
                });
            break;
        case Config::SystemSortMode::GameCount:
            std::sort(systems.begin(), systems.end(),
                [&](const System& a, const System& b) {
                    if (a.games.size() != b.games.size())
                        return a.games.size() > b.games.size();
                    return folder_lc(a.def ? a.def->short_name : "") <
                           folder_lc(b.def ? b.def->short_name : "");
                });
            break;
        case Config::SystemSortMode::Custom: {
            const auto& order = config().system_custom_order;
            auto rank = [&](const System& s) -> int {
                if (!s.def) return (int)order.size() + 1;
                for (std::size_t i = 0; i < order.size(); i++)
                    if (s.def->folder_name == order[i]) return (int)i;
                return (int)order.size() + 1;
            };
            std::stable_sort(systems.begin(), systems.end(),
                [&](const System& a, const System& b) {
                    const int ra = rank(a), rb = rank(b);
                    if (ra != rb) return ra < rb;
                    return folder_lc(a.def ? a.def->short_name : "") <
                           folder_lc(b.def ? b.def->short_name : "");
                });
            break;
        }
        default: break;
    }
}

} // namespace

std::vector<System> scan_library(const ScanOptions& opts) {
    constexpr const char* kCachePath = "/foyer/data/cache/library.cache.json";

    // Cache fast-path. Only honoured when the caller didn't ask for
    // a forced rescan AND when the cache load validates against
    // current rom-root + per-folder mtimes. Virtual systems
    // (Recent / Favorites / Unknown) are NOT in the cache; they're
    // synthesized below from the cached real-system games.
    if (!opts.force_rescan) {
        if (auto cached = load_library_cache(kCachePath, opts.rom_root)) {
            // Re-apply per-game state (favorites / last_played /
            // playtime) — these can change between runs without
            // touching SD mtimes, so the cache stores a stale
            // snapshot. Refresh from per_game.jsonc on every load.
            for (auto& s : *cached) {
                for (auto& g : s.games) {
                    apply_per_game_state(g);
                }
                sort_games(s.games, config().sort_mode);
            }
            // System ordering is a config knob (alphabetical / game
            // count / custom / scanner default) — re-apply on every
            // load so a switch in system_sort_mode shows up without
            // needing to bust the library cache.
            apply_hide_empty(*cached);
            apply_system_sort(*cached);
            // Recreate virtual carousel tiles from the loaded games.
            // IMPORTANT: walk only REAL systems — once we insert
            // Favorites at out_real.begin(), the next add_virtual
            // (Recent / AllGames) would otherwise iterate the
            // freshly-inserted virtual and duplicate every game that
            // matches more than one filter (e.g. a favourited recent
            // game ended up listed twice in Recent + Favorites + Recent
            // again). is_virtual_system() skips them cleanly.
            auto& out_real = *cached;
            auto add_virtual = [&](const SystemDef& def, auto&& filter,
                                   auto&& sort_fn, std::size_t cap) {
                System v;
                v.def       = &def;
                v.root_path = "(virtual)";
                for (auto& real : out_real) {
                    if (real.def && is_virtual_system(*real.def)) continue;
                    for (auto& g : real.games) {
                        if (filter(g)) v.games.push_back(g);
                    }
                }
                if (v.games.empty()) return;
                sort_fn(v.games);
                if (cap > 0 && v.games.size() > cap) v.games.resize(cap);
                out_real.insert(out_real.begin(), std::move(v));
            };
            // Favorites virtual carousel restored in 0.5.24 — the
            // standalone row didn't read well per user feedback.
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
            add_virtual(kVirtualAllGamesDef,
                [](const Game&) { return true; },
                [](std::vector<Game>& v) {
                    std::sort(v.begin(), v.end(),
                        [](const Game& a, const Game& b) { return a.stem < b.stem; });
                },
                /*cap=*/0);
            // Always-present Switch virtual — populated from
            // libnx's nsListApplicationRecord at boot.
            {
                System v;
                v.def       = &kVirtualSwitchDef;
                v.root_path = "(virtual)";
                for (const auto& t : ::foyer::library::switch_titles()) {
                    Game g{};
                    g.path      = switch_path_for(t.application_id);
                    g.stem      = t.name.empty()
                        ? std::string{"Untitled"} : t.name;
                    g.display   = g.stem;
                    g.box_art   = t.icon_path;
                    v.games.push_back(std::move(g));
                }
                out_real.insert(out_real.begin(), std::move(v));
            }
            return std::move(*cached);
        }
    }

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

        // Emit the system regardless of whether it has roms — the
        // post-scan apply_hide_empty() decides whether to surface
        // empty entries based on Config::hide_empty_systems. v0.4.0
        // shipped an unconditional `if (!sys.games.empty())` guard
        // here that defeated the toggle entirely; v0.4.x restores
        // the user's choice.
        out.emplace_back(std::move(sys));
    }
    ::closedir(root);

    if (!unknown.games.empty()) {
        sort_games(unknown.games, Config::SortMode::Name);
        out.emplace_back(std::move(unknown));
    }

    apply_hide_empty(out);
    apply_system_sort(out);

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
    add_virtual(kVirtualAllGamesDef,
        [](const Game&) { return true; },
        [](std::vector<Game>& v) {
            std::sort(v.begin(), v.end(),
                [](const Game& a, const Game& b) { return a.stem < b.stem; });
        },
        /*cap=*/0);

    // Switch-titles virtual — populated from
    // foyer::library::switch_titles() (loaded in main.cpp boot
    // path). Each installed Switch app becomes a Game whose
    // `path` is "switch://<hex>"; launch.cpp peels the hex back
    // out and hands it to appletRequestLaunchApplication.
    {
        System v;
        v.def       = &kVirtualSwitchDef;
        v.root_path = "(virtual)";
        for (const auto& t : ::foyer::library::switch_titles()) {
            Game g{};
            g.path      = switch_path_for(t.application_id);
            g.stem      = t.name.empty()
                ? std::string{"Untitled"} : t.name;
            g.display   = g.stem;
            g.box_art   = t.icon_path;
            v.games.push_back(std::move(g));
        }
        out.insert(out.begin(), std::move(v));
    }

    foyer::log::write("[scan] %zu system(s) populated\n", out.size());
    for (const auto& s : out) {
        foyer::log::write("[scan]   %-13.*s  %3zu games\n",
            (int)s.def->folder_name.size(), s.def->folder_name.data(),
            s.games.size());
    }

    // Persist the freshly-built snapshot. Next boot's scan_library
    // call returns the cached vector unless rom-root or per-system
    // mtimes invalidate. Virtual tiles aren't written — they're
    // re-synthesized on cache load.
    save_library_cache(kCachePath, out, opts.rom_root);

    return out;
}

} // namespace foyer::library
