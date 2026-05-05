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

constexpr const char* kPath = "/foyer/config/general.jsonc";

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
    out << "    \"sort_mode\":         \"" << sort_to_str(g_config.sort_mode) << "\",\n";
    out << "    \"shader\":            \"" << g_config.shader_name << "\",\n";
    out << "    \"runahead_frames\":   " << g_config.runahead_frames << ",\n";
    out << "    \"scan_subfolders\":   " << bstr(g_config.scan_subfolders) << ",\n";
    out << "    \"show_clock\":        " << bstr(g_config.show_clock) << ",\n";
    out << "    \"show_backgrounds\":  " << bstr(g_config.show_backgrounds) << ",\n";
    out << "    \"show_covers\":       " << bstr(g_config.show_covers) << ",\n";
    out << "    \"mtp_autostart\":     " << bstr(g_config.mtp_autostart) << ",\n";
    out << "    \"debug_log\":         " << bstr(g_config.debug_log) << ",\n";
    out << "    \"cores_manifest_url\": \"" << g_config.cores_manifest_url << "\",\n";
    out << "    \"foyer_manifest_url\": \"" << g_config.foyer_manifest_url << "\",\n";
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
    }
    if (auto* v = yyjson_obj_get(root, "sort_mode");
        v && yyjson_is_str(v)) {
        g_config.sort_mode = str_to_sort(yyjson_get_str(v));
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
    load_bool("mtp_autostart",    g_config.mtp_autostart);
    load_bool("debug_log",        g_config.debug_log);
    if (auto* v = yyjson_obj_get(root, "cores_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.cores_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "foyer_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.foyer_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "cheats_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.cheats_manifest_url = yyjson_get_str(v);
    }
    if (auto* v = yyjson_obj_get(root, "bezels_manifest_url");
        v && yyjson_is_str(v)) {
        g_config.bezels_manifest_url = yyjson_get_str(v);
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
    else if (key == "mtp_autostart")    g_config.mtp_autostart    = value;
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
