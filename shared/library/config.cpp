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

void write_locked() {
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[config] could not write %s\n", kPath);
        return;
    }
    out << "// foyer browser preferences.\n";
    out << "{\n";
    out << "    \"preferred_scraper\": \""
        << scraper_to_str(g_config.preferred_scraper) << "\",\n";
    out << "    \"rom_root\":          \"" << g_config.rom_root << "\",\n";
    out << "    \"default_core_per_system\": {";
    bool first = true;
    for (const auto& [folder, core] : g_config.default_core_per_system) {
        if (!first) out << ",";
        first = false;
        out << "\n        \"" << folder << "\": \"" << core << "\"";
    }
    out << (g_config.default_core_per_system.empty() ? "}\n" : "\n    }\n");
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

} // namespace foyer::library
