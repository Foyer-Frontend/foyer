#include "cheats.hpp"
#include "platform/log.hpp"
#include "libretro.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>

extern "C" void retro_cheat_reset(void);
extern "C" void retro_cheat_set(unsigned index, bool enabled, const char* code);

namespace foyer::libretro {
namespace {

constexpr const char* kCheatsRoot = "/foyer/cheats";

std::string cht_path(std::string_view system_folder, std::string_view rom_stem) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s/%.*s/%.*s.cht",
        kCheatsRoot,
        (int)system_folder.size(), system_folder.data(),
        (int)rom_stem.size(),      rom_stem.data());
    return std::string{buf};
}

// Trim whitespace + surrounding quotes from a value. RetroArch writes
// quoted strings; some 3rd-party generators strip them. Accept both.
std::string trim_value(std::string s) {
    auto issp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back()))  s.pop_back();
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

bool to_bool(const std::string& s) {
    return s == "true" || s == "1" || s == "TRUE" || s == "True";
}

} // namespace

std::vector<Cheat> load_cheats_for(std::string_view system_folder,
                                   std::string_view rom_stem) {
    const auto path = cht_path(system_folder, rom_stem);
    std::ifstream in{path};
    if (!in) return {};

    // Two passes: collect every "cheatN_<key> = value" into a flat map,
    // then materialise into the struct list. Lets us tolerate fields
    // appearing in any order (some authoring tools shuffle them).
    std::unordered_map<std::string, std::string> kv;
    std::string line;
    int max_idx = -1;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim_value(line.substr(0, eq));
        std::string val = trim_value(line.substr(eq + 1));
        if (key == "cheats") {
            try { max_idx = std::max(max_idx, std::stoi(val) - 1); } catch (...) {}
            continue;
        }
        if (key.rfind("cheat", 0) == 0) {
            // "cheatN_field" — track max N seen.
            const auto us = key.find('_');
            if (us != std::string::npos) {
                try {
                    int n = std::stoi(key.substr(5, us - 5));
                    max_idx = std::max(max_idx, n);
                } catch (...) {}
            }
        }
        kv[std::move(key)] = std::move(val);
    }

    std::vector<Cheat> out;
    for (int i = 0; i <= max_idx; i++) {
        char k_desc[64], k_code[64], k_en[64];
        std::snprintf(k_desc, sizeof(k_desc), "cheat%d_desc", i);
        std::snprintf(k_code, sizeof(k_code), "cheat%d_code", i);
        std::snprintf(k_en,   sizeof(k_en),   "cheat%d_enable", i);
        Cheat c;
        if (auto it = kv.find(k_desc); it != kv.end()) c.desc = it->second;
        if (auto it = kv.find(k_code); it != kv.end()) c.code = it->second;
        if (auto it = kv.find(k_en);   it != kv.end()) c.enabled = to_bool(it->second);
        if (c.code.empty()) continue; // skip incomplete entries
        if (c.desc.empty()) c.desc = "Cheat " + std::to_string(i + 1);
        out.push_back(std::move(c));
    }
    foyer::log::write("[cheats] %s -> %zu entries\n", path.c_str(), out.size());
    return out;
}

void save_cheats_for(std::string_view system_folder,
                     std::string_view rom_stem,
                     const std::vector<Cheat>& cheats) {
    const auto path = cht_path(system_folder, rom_stem);
    std::ifstream in{path};
    if (!in) {
        // No source file to update — write a fresh minimal one. This
        // also covers the case where a future overlay UI lets the user
        // create cheats from scratch.
        ::mkdir(kCheatsRoot, 0755);
        char dir[512];
        std::snprintf(dir, sizeof(dir), "%s/%.*s",
            kCheatsRoot, (int)system_folder.size(), system_folder.data());
        ::mkdir(dir, 0755);
        std::ofstream out{path, std::ios::trunc};
        if (!out) return;
        out << "cheats = " << cheats.size() << "\n";
        for (std::size_t i = 0; i < cheats.size(); i++) {
            out << "cheat" << i << "_desc = \"" << cheats[i].desc << "\"\n";
            out << "cheat" << i << "_code = \"" << cheats[i].code << "\"\n";
            out << "cheat" << i << "_enable = \""
                << (cheats[i].enabled ? "true" : "false") << "\"\n";
        }
        return;
    }

    // Preserve the existing file format — just rewrite the _enable
    // lines for indices that match. Any non-cheat content (top-level
    // comments, custom keys) survives the round-trip.
    std::stringstream ss;
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = trim_value(line.substr(0, eq));
            if (key.rfind("cheat", 0) == 0 && key.find("_enable") != std::string::npos) {
                // Pull the cheat index out: "cheatN_enable"
                try {
                    const auto us = key.find('_');
                    int n = std::stoi(key.substr(5, us - 5));
                    if (n >= 0 && (std::size_t)n < cheats.size()) {
                        ss << "cheat" << n << "_enable = \""
                           << (cheats[n].enabled ? "true" : "false") << "\"\n";
                        continue;
                    }
                } catch (...) {}
            }
        }
        ss << line << "\n";
    }
    in.close();

    std::ofstream out{path, std::ios::trunc};
    if (!out) {
        foyer::log::write("[cheats] could not write %s\n", path.c_str());
        return;
    }
    out << ss.str();
}

void apply_cheats_to_core(const std::vector<Cheat>& cheats) {
    retro_cheat_reset();
    for (std::size_t i = 0; i < cheats.size(); i++) {
        if (!cheats[i].enabled) continue;
        retro_cheat_set((unsigned)i, true, cheats[i].code.c_str());
    }
    foyer::log::write("[cheats] applied %zu enabled\n",
        (std::size_t)std::count_if(cheats.begin(), cheats.end(),
            [](const Cheat& c) { return c.enabled; }));
}

} // namespace foyer::libretro
