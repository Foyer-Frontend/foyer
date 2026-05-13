#include "config.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kPath = "/foyer/data/config/general.jsonc";

std::mutex          g_mutex;
Config              g_config{};
std::atomic<bool>   g_loaded{false};

// Mirror of the JSONC strip in scrapers/accounts.cpp — keep both modules
// independent so they don't have to share a tiny utility header just yet.
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

const char* scraper_to_str(Config::Scraper s) {
    switch (s) {
        case Config::Scraper::ScreenScraper: return "screenscraper";
        case Config::Scraper::SteamGridDB:   return "steamgriddb";
        case Config::Scraper::Libretro:
        default:                              return "libretro";
    }
}

Config::Scraper str_to_scraper(const char* s) {
    if (!s) return Config::Scraper::Libretro;
    if (!std::strcmp(s, "screenscraper")) return Config::Scraper::ScreenScraper;
    if (!std::strcmp(s, "steamgriddb"))   return Config::Scraper::SteamGridDB;
    return Config::Scraper::Libretro;
}

const char* sort_to_str(Config::SortMode m) {
    switch (m) {
        case Config::SortMode::Recent:    return "recent";
        case Config::SortMode::Playtime:  return "playtime";
        case Config::SortMode::Favorites: return "favorites";
        case Config::SortMode::Name:      return "name";
    }
    return "name";
}

Config::SortMode str_to_sort(const char* s) {
    if (!s) return Config::SortMode::Name;
    if (!std::strcmp(s, "recent"))    return Config::SortMode::Recent;
    if (!std::strcmp(s, "playtime"))  return Config::SortMode::Playtime;
    if (!std::strcmp(s, "favorites")) return Config::SortMode::Favorites;
    return Config::SortMode::Name;
}

const char* sys_sort_to_str(Config::SystemSortMode m) {
    switch (m) {
        case Config::SystemSortMode::ScannerOrder: return "scanner";
        case Config::SystemSortMode::Alphabetical: return "alphabetical";
        case Config::SystemSortMode::GameCount:    return "game_count";
        case Config::SystemSortMode::Custom:       return "custom";
    }
    return "scanner";
}

Config::SystemSortMode str_to_sys_sort(const char* s) {
    if (!s) return Config::SystemSortMode::ScannerOrder;
    if (!std::strcmp(s, "alphabetical")) return Config::SystemSortMode::Alphabetical;
    if (!std::strcmp(s, "game_count"))   return Config::SystemSortMode::GameCount;
    if (!std::strcmp(s, "custom"))       return Config::SystemSortMode::Custom;
    return Config::SystemSortMode::ScannerOrder;
}

void write_locked() {
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[config] could not write %s\n", kPath);
        return;
    }
    out << "// foyer browser preferences.\n";
    out << "{\n";
    auto bstr = [](bool v) { return v ? "true" : "false"; };
    out << "    \"preferred_scraper\": \""
        << scraper_to_str(g_config.preferred_scraper) << "\",\n";
    out << "    \"rom_root\":          \"" << g_config.rom_root << "\",\n";
    out << "    \"theme\":             \"" << g_config.theme_name << "\",\n";
    out << "    \"theme_color\":       \"" << g_config.theme_color << "\",\n";
    out << "    \"sort_mode\":         \"" << sort_to_str(g_config.sort_mode) << "\",\n";
    out << "    \"system_sort_mode\":  \""
        << sys_sort_to_str(g_config.system_sort_mode) << "\",\n";
    out << "    \"system_custom_order\": [";
    {
        bool sf = true;
        for (const auto& f : g_config.system_custom_order) {
            if (!sf) out << ",";
            sf = false;
            out << "\n        \"" << f << "\"";
        }
        out << (g_config.system_custom_order.empty() ? "],\n" : "\n    ],\n");
    }
    out << "    \"shader\":            \"" << g_config.shader_name << "\",\n";
    out << "    \"runahead_frames\":   " << g_config.runahead_frames << ",\n";
    out << "    \"scan_subfolders\":   " << bstr(g_config.scan_subfolders) << ",\n";
    out << "    \"show_clock\":        " << bstr(g_config.show_clock) << ",\n";
    out << "    \"show_backgrounds\":  " << bstr(g_config.show_backgrounds) << ",\n";
    out << "    \"show_covers\":       " << bstr(g_config.show_covers) << ",\n";
    out << "    \"show_bezels\":       " << bstr(g_config.show_bezels) << ",\n";
    out << "    \"rounded_tiles\":     " << bstr(g_config.rounded_tiles) << ",\n";
    out << "    \"action_row_dock\":   " << bstr(g_config.action_row_dock) << ",\n";
    out << "    \"hide_empty_systems\":" << bstr(g_config.hide_empty_systems) << ",\n";
    out << "    \"language\":          \"" << g_config.language << "\",\n";
    out << "    \"theme_override\":    \"" << g_config.theme_override << "\",\n";
    out << "    \"update_check_on_boot\": "
        << (g_config.update_check_on_boot ? "true" : "false") << ",\n";
    out << "    \"region\":           \"" << g_config.region << "\",\n";
    out << "    \"scrub_extracted_enabled\": "
        << (g_config.scrub_extracted_enabled ? "true" : "false") << ",\n";
    out << "    \"scrub_extracted_days\":    "
        << g_config.scrub_extracted_days << ",\n";
    out << "    \"mtp_autostart\":     " << bstr(g_config.mtp_autostart) << ",\n";
    out << "    \"mtp_expose_roms\":   " << bstr(g_config.mtp_expose_roms) << ",\n";
    out << "    \"mtp_expose_logs\":   " << bstr(g_config.mtp_expose_logs) << ",\n";
    out << "    \"debug_log\":         " << bstr(g_config.debug_log) << ",\n";
    out << "    \"cores_manifest_url\": \"" << g_config.cores_manifest_url << "\",\n";
    out << "    \"foyer_manifest_url\": \"" << g_config.foyer_manifest_url << "\",\n";
    out << "    \"external_eshop_nro\":     \"" << g_config.external_eshop_nro << "\",\n";
    out << "    \"external_eshop_nro_alt\": \"" << g_config.external_eshop_nro_alt << "\",\n";
    out << "    \"external_album_nro\":     \"" << g_config.external_album_nro << "\",\n";
    out << "    \"default_core_per_system\": {";
    bool first = true;
    for (const auto& [folder, core] : g_config.default_core_per_system) {
        if (!first) out << ",";
        first = false;
        out << "\n        \"" << folder << "\": \"" << core << "\"";
    }
    out << (g_config.default_core_per_system.empty() ? "},\n" : "\n    },\n");

    out << "    \"external_cores\": {";
    first = true;
    for (const auto& [folder, nro] : g_config.external_cores) {
        if (!first) out << ",";
        first = false;
        out << "\n        \"" << folder << "\": \"" << nro << "\"";
    }
    out << (g_config.external_cores.empty() ? "}\n" : "\n    }\n");

    out << "}\n";
    foyer::log::write("[config] saved %s\n", kPath);
}

void load_locked() {
    g_config = Config{};

    std::ifstream in{kPath};
    if (!in) {
        write_locked();
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto stripped = strip_comments(ss.str());

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[config] failed to parse %s\n", kPath);
        return;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (auto* v = yyjson_obj_get(root, "preferred_scraper");
        v && yyjson_is_str(v)) {
        g_config.preferred_scraper = str_to_scraper(yyjson_get_str(v));
    }
    if (auto* v = yyjson_obj_get(root, "rom_root");
        v && yyjson_is_str(v)) {
        g_config.rom_root = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "theme");
        v && yyjson_is_str(v)) {
        g_config.theme_name = yyjson_get_str(v);
        // 0.5.18 migration: any saved theme name we no longer ship
        // (snow / dark / light / midnight / forest / hos) gets
        // promoted to alekfull-nx so the user lands on the real
        // bundled theme instead of a fall-through to defaults. Their
        // explicit /foyer/themes/<name>/ pack picks (anything not in
        // the obsolete list) are preserved.
        const std::string& tn = g_config.theme_name;
        if (tn == "snow" || tn == "dark"     || tn == "light"
            || tn == "midnight" || tn == "forest" || tn == "hos"
            || tn.empty()) {
            g_config.theme_name = "alekfull-nx";
        }
    }
    if (auto* v = yyjson_obj_get(root, "theme_color");
        v && yyjson_is_str(v)) {
        g_config.theme_color = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "sort_mode");
        v && yyjson_is_str(v)) {
        g_config.sort_mode = str_to_sort(yyjson_get_str(v));
    }
    if (auto* v = yyjson_obj_get(root, "system_sort_mode");
        v && yyjson_is_str(v)) {
        g_config.system_sort_mode = str_to_sys_sort(yyjson_get_str(v));
    }
    if (auto* arr = yyjson_obj_get(root, "system_custom_order");
        arr && yyjson_is_arr(arr)) {
        g_config.system_custom_order.clear();
        std::size_t i, n; yyjson_val* item;
        yyjson_arr_foreach(arr, i, n, item) {
            if (yyjson_is_str(item))
                g_config.system_custom_order.emplace_back(yyjson_get_str(item));
        }
    }
    if (auto* v = yyjson_obj_get(root, "shader");
        v && yyjson_is_str(v)) {
        g_config.shader_name = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "runahead_frames");
        v && yyjson_is_int(v)) {
        int n = (int)yyjson_get_int(v);
        if (n < 0) n = 0;
        if (n > 4) n = 4;
        g_config.runahead_frames = n;
    }
    auto load_bool = [&](const char* key, bool& out) {
        if (auto* v = yyjson_obj_get(root, key); v && yyjson_is_bool(v)) {
            out = yyjson_get_bool(v);
        }
    };
    load_bool("scan_subfolders",  g_config.scan_subfolders);
    load_bool("show_clock",       g_config.show_clock);
    load_bool("show_backgrounds", g_config.show_backgrounds);
    load_bool("show_covers",      g_config.show_covers);
    load_bool("show_bezels",      g_config.show_bezels);
    load_bool("rounded_tiles",    g_config.rounded_tiles);
    load_bool("action_row_dock",  g_config.action_row_dock);
    load_bool("hide_empty_systems", g_config.hide_empty_systems);
    if (auto* v = yyjson_obj_get(root, "theme_override");
        v && yyjson_is_str(v)) {
        g_config.theme_override = yyjson_get_str(v);
    }

    if (auto* v = yyjson_obj_get(root, "update_check_on_boot");
        v && yyjson_is_bool(v)) {
        g_config.update_check_on_boot = yyjson_get_bool(v);
    }

    if (auto* v = yyjson_obj_get(root, "region");
        v && yyjson_is_str(v)) {
        g_config.region = yyjson_get_str(v);
    }

    if (auto* v = yyjson_obj_get(root, "scrub_extracted_enabled");
        v && yyjson_is_bool(v)) {
        g_config.scrub_extracted_enabled = yyjson_get_bool(v);
    }
    if (auto* v = yyjson_obj_get(root, "scrub_extracted_days");
        v && yyjson_is_int(v)) {
        g_config.scrub_extracted_days = (int)yyjson_get_int(v);
    }

    if (auto* v = yyjson_obj_get(root, "language");
        v && yyjson_is_str(v)) {
        g_config.language = yyjson_get_str(v);
    }
    load_bool("mtp_autostart",    g_config.mtp_autostart);
    load_bool("mtp_expose_roms",  g_config.mtp_expose_roms);
    load_bool("mtp_expose_logs",  g_config.mtp_expose_logs);
    load_bool("debug_log",        g_config.debug_log);
    if (auto* v = yyjson_obj_get(root, "cores_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.cores_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "foyer_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.foyer_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "external_eshop_nro");
        v && yyjson_is_str(v)) {
        g_config.external_eshop_nro = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "external_eshop_nro_alt");
        v && yyjson_is_str(v)) {
        g_config.external_eshop_nro_alt = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "external_album_nro");
        v && yyjson_is_str(v)) {
        g_config.external_album_nro = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "cheats_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.cheats_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "bezels_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.bezels_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "foyer_assets_url");
        v && yyjson_is_str(v)) {
        g_config.foyer_assets_url = yyjson_get_str(v);
    }
    if (auto* obj = yyjson_obj_get(root, "default_core_per_system");
        obj && yyjson_is_obj(obj)) {
        std::size_t i, max; yyjson_val *k, *v;
        yyjson_obj_foreach(obj, i, max, k, v) {
            if (yyjson_is_str(k) && yyjson_is_str(v)) {
                g_config.default_core_per_system.push_back(
                    { yyjson_get_str(k), yyjson_get_str(v) });
            }
        }
    }
    if (auto* obj = yyjson_obj_get(root, "external_cores");
        obj && yyjson_is_obj(obj)) {
        std::size_t i, max; yyjson_val *k, *v;
        yyjson_obj_foreach(obj, i, max, k, v) {
            if (yyjson_is_str(k) && yyjson_is_str(v)) {
                g_config.external_cores.push_back(
                    { yyjson_get_str(k), yyjson_get_str(v) });
            }
        }
    }
    // Seed defaults for the canonical "external core" systems so a
    // first-run user with PPSSPP / Dolphin already on their SD just
    // works without editing JSON. We push only if the user hasn't
    // explicitly mapped that folder yet — overrides win.
    auto seed_default = [](std::string_view folder, std::string_view nro) {
        for (const auto& e : g_config.external_cores) {
            if (e.folder == folder) return;
        }
        g_config.external_cores.push_back(
            { std::string{folder}, std::string{nro} });
    };
    seed_default("psp", "/switch/PPSSPP/PPSSPP.nro");
    seed_default("gc",  "/switch/dolphin-emu/dolphin-emu.nro");
    yyjson_doc_free(doc);

    foyer::log::write("[config] preferred_scraper=%s rom_root=%s overrides=%zu\n",
        scraper_to_str(g_config.preferred_scraper),
        g_config.rom_root.c_str(),
        g_config.default_core_per_system.size());
}

} // namespace

const Config& config() {
    if (!g_loaded.load()) {
        std::scoped_lock lk{g_mutex};
        if (!g_loaded.load()) {
            load_locked();
            g_loaded = true;
        }
    }
    return g_config;
}

void reload_config() {
    std::scoped_lock lk{g_mutex};
    load_locked();
    g_loaded = true;
}

void save_config() {
    std::scoped_lock lk{g_mutex};
    write_locked();
}

void set_preferred_scraper(Config::Scraper s) {
    std::scoped_lock lk{g_mutex};
    g_config.preferred_scraper = s;
    write_locked();
}

void set_theme_name(std::string_view name) {
    std::scoped_lock lk{g_mutex};
    g_config.theme_name = std::string{name};
    write_locked();
}

void set_theme_color(std::string_view color) {
    std::scoped_lock lk{g_mutex};
    g_config.theme_color = std::string{color};
    write_locked();
}

void set_language(std::string_view code) {
    std::scoped_lock lk{g_mutex};
    g_config.language = std::string{code};
    write_locked();
}

void set_theme_override(std::string_view value) {
    std::scoped_lock lk{g_mutex};
    g_config.theme_override = std::string{value};
    write_locked();
}

void set_region(std::string_view value) {
    std::scoped_lock lk{g_mutex};
    g_config.region = std::string{value};
    write_locked();
}

void set_update_check_on_boot(bool enabled) {
    std::scoped_lock lk{g_mutex};
    g_config.update_check_on_boot = enabled;
    write_locked();
}

void set_scrub_extracted_enabled(bool enabled) {
    std::scoped_lock lk{g_mutex};
    g_config.scrub_extracted_enabled = enabled;
    write_locked();
}

void set_scrub_extracted_days(int days) {
    std::scoped_lock lk{g_mutex};
    g_config.scrub_extracted_days = days < 1 ? 1 : days;
    write_locked();
}

void set_system_sort_mode(Config::SystemSortMode mode) {
    std::scoped_lock lk{g_mutex};
    g_config.system_sort_mode = mode;
    write_locked();
}

void set_system_custom_order(std::vector<std::string> order) {
    std::scoped_lock lk{g_mutex};
    g_config.system_custom_order = std::move(order);
    write_locked();
}

void set_sort_mode(Config::SortMode mode) {
    std::scoped_lock lk{g_mutex};
    g_config.sort_mode = mode;
    write_locked();
}

void set_shader_name(std::string_view name) {
    std::scoped_lock lk{g_mutex};
    g_config.shader_name = std::string{name};
    write_locked();
}

void set_runahead_frames(int frames) {
    if (frames < 0) frames = 0;
    if (frames > 4) frames = 4;
    std::scoped_lock lk{g_mutex};
    g_config.runahead_frames = frames;
    write_locked();
}

void set_bool(std::string_view key, bool value) {
    std::scoped_lock lk{g_mutex};
    if      (key == "scan_subfolders")  g_config.scan_subfolders  = value;
    else if (key == "show_clock")       g_config.show_clock       = value;
    else if (key == "show_backgrounds") g_config.show_backgrounds = value;
    else if (key == "show_covers")      g_config.show_covers      = value;
    else if (key == "show_bezels")      g_config.show_bezels      = value;
    else if (key == "rounded_tiles")    g_config.rounded_tiles    = value;
    else if (key == "action_row_dock")  g_config.action_row_dock  = value;
    else if (key == "hide_empty_systems") g_config.hide_empty_systems = value;
    else if (key == "mtp_autostart")    g_config.mtp_autostart    = value;
    else if (key == "mtp_expose_roms")  g_config.mtp_expose_roms  = value;
    else if (key == "mtp_expose_logs")  g_config.mtp_expose_logs  = value;
    else if (key == "debug_log")        g_config.debug_log        = value;
    else return;
    write_locked();
}

void set_default_core_for(std::string_view folder, std::string_view core_name) {
    std::scoped_lock lk{g_mutex};
    for (auto& e : g_config.default_core_per_system) {
        if (e.folder == folder) {
            if (core_name.empty()) {
                e = g_config.default_core_per_system.back();
                g_config.default_core_per_system.pop_back();
            } else {
                e.core = std::string{core_name};
            }
            write_locked();
            return;
        }
    }
    if (!core_name.empty()) {
        g_config.default_core_per_system.push_back(
            { std::string{folder}, std::string{core_name} });
        write_locked();
    }
}

const char* Config::default_core_for(std::string_view folder) const {
    for (const auto& e : default_core_per_system) {
        if (e.folder == folder) return e.core.c_str();
    }
    return nullptr;
}

std::string Config::external_core_for(std::string_view folder) const {
    for (const auto& e : external_cores) {
        if (e.folder == folder) return e.nro_path;
    }
    return {};
}

} // namespace foyer::library
