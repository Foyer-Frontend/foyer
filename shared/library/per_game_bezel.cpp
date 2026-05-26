#include "per_game_bezel.hpp"

#include "library/system_db.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "scrapers/cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <set>
#include <string>
#include <sys/stat.h>

namespace foyer::library {
namespace {

// foyer system slug → "<repo>|<inner-dir>" inside that repo. Inner
// dir names vary per repo and aren't derivable from the repo name —
// NES uses "NES", N64 uses "N64", PSX uses "Sony - PlayStation".
// Mirror the table in foyer-bezels/.github/workflows/build-bezels.yml.
struct BPMap { const char* slug; const char* repo; const char* inner; };
constexpr BPMap kBezelProject[] = {
    {"nes",            "bezelproject-NES",            "NES"},
    {"famicom",        "bezelproject-Famicom",        "Famicom"},
    {"snes",           "bezelproject-SNES",           "SNES"},
    {"sfc",            "bezelproject-SFC",            "SFC"},
    {"gb",             "bezelproject-GB",             "GB"},
    {"gbc",            "bezelproject-GBC",            "GBC"},
    {"gba",            "bezelproject-GBA",            "GBA"},
    {"n64",            "bezelproject-N64",            "N64"},
    {"gc",             "bezelproject-GC",             "GC"},
    {"nds",            "bezelproject-NDS",            "NDS"},
    {"psx",            "bezelproject-PSX",            "Sony - PlayStation"},
    {"psp",            "bezelproject-PSP",            "PSP"},
    {"saturn",         "bezelproject-Saturn",         "Saturn"},
    {"dc",             "bezelproject-Dreamcast",      "Dreamcast"},
    {"segacd",         "bezelproject-SegaCD",         "SegaCD"},
    {"megadrive",      "bezelproject-MegaDrive",      "MegaDrive"},
    {"genesis",        "bezelproject-MegaDrive",      "MegaDrive"},
    {"mastersystem",   "bezelproject-MasterSystem",   "MasterSystem"},
    {"gamegear",       "bezelproject-GameGear",       "GameGear"},
    {"pcengine",       "bezelproject-PCEngine",       "PCEngine"},
    {"pcenginecd",     "bezelproject-PCE-CD",         "PCE-CD"},
    {"pcfx",           "bezelproject-PCFX",           "PCFX"},
    {"supergrafx",     "bezelproject-SuperGrafx",     "SuperGrafx"},
    {"atari2600",      "bezelproject-Atari2600",      "Atari2600"},
    {"atari5200",      "bezelproject-Atari5200",      "Atari5200"},
    {"atari7800",      "bezelproject-Atari7800",      "Atari7800"},
    {"atari800",       "bezelproject-Atari800",       "Atari800"},
    {"atarijaguar",    "bezelproject-AtariJaguar",    "AtariJaguar"},
    {"atarilynx",      "bezelproject-AtariLynx",      "AtariLynx"},
    {"c64",            "bezelproject-C64",            "C64"},
    {"amstradcpc",     "bezelproject-AmstradCPC",     "AmstradCPC"},
    {"intellivision",  "bezelproject-Intellivision",  "Intellivision"},
    {"opera",          "bezelproject-3DO",            "3DO"},
    {"virtualboy",     "bezelproject-VirtualBoy",     "VirtualBoy"},
    {"ngp",            "bezelproject-NGP",            "NGP"},
    {"ngpc",           "bezelproject-NGPC",           "NGPC"},
};

const BPMap* find_bp(std::string_view folder) {
    for (const auto& e : kBezelProject) {
        if (folder == e.slug) return &e;
    }
    // Family fallback — covers e.g. genesis -> bezelproject-MegaDrive.
    const auto fam = foyer::library::family_for_folder(folder);
    if (fam != folder) {
        for (const auto& e : kBezelProject) {
            if (fam == e.slug) return &e;
        }
    }
    return nullptr;
}

// Strip any "(...)" parenthesised tag from `stem` — region, language,
// dump-status, etc.
std::string strip_parens(std::string_view stem) {
    std::string out;
    out.reserve(stem.size());
    int depth = 0;
    for (char c : stem) {
        if (c == '(') depth++;
        else if (c == ')') { if (depth > 0) depth--; continue; }
        if (depth == 0) out.push_back(c);
    }
    // Collapse leftover double spaces + trim.
    std::string collapsed;
    collapsed.reserve(out.size());
    bool prev_space = false;
    for (char c : out) {
        if (c == ' ') {
            if (!prev_space) collapsed.push_back(c);
            prev_space = true;
        } else {
            collapsed.push_back(c);
            prev_space = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed;
}

// Percent-encode for URL paths. RFC 3986 unreserved chars + '/' pass
// through; everything else gets %HH.
std::string url_path_encode(std::string_view in) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out.push_back((char)c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xf]);
        }
    }
    return out;
}

bool write_png_atomic(const std::string& dest_path, const std::vector<char>& body) {
    const std::string tmp = dest_path + ".part";
    {
        auto* f = std::fopen(tmp.c_str(), "wb");
        if (!f) return false;
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
    if (::rename(tmp.c_str(), dest_path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool try_fetch_one(const std::string& url, const std::string& dest_path) {
    auto resp = foyer::net::get(url);
    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        return false;
    }
    // libcurl populated body — write atomically.
    return write_png_atomic(dest_path, resp.body);
}

}  // namespace

bool bezelproject_has_system(std::string_view system_folder) {
    return find_bp(system_folder) != nullptr;
}

std::vector<std::string> bezel_filename_variants(std::string_view rom_stem) {
    // Order matters — first hit wins. Lead with the exact stem so a
    // user with a No-Intro-named rom doesn't pay extra round-trips.
    std::string exact{rom_stem};
    std::string bare = strip_parens(rom_stem);

    std::vector<std::string> v;
    auto push_unique = [&](std::string s) {
        if (s.empty()) return;
        for (const auto& x : v) if (x == s) return;
        v.emplace_back(std::move(s));
    };
    push_unique(exact);

    static const char* kRegionTags[] = {
        " (USA)",
        " (World)",
        " (Europe)",
        " (Japan)",
        " (USA, Europe)",
        " (Europe) (En,Fr,De,Es,It)",
        " (USA, Australia)",
    };
    for (const char* tag : kRegionTags) {
        push_unique(bare + tag);
    }
    // Last-ditch: try the bare name itself in case the upstream
    // file dropped region tags.
    push_unique(bare);
    return v;
}

bool fetch_bezel_from_bezelproject(std::string_view system_folder,
                                   std::string_view rom_stem,
                                   PerGameBezelProgressCb progress) {
    const auto* m = find_bp(system_folder);
    if (!m) {
        foyer::log::write("[per-game-bezel] no bezelproject mapping for system=%.*s\n",
            (int)system_folder.size(), system_folder.data());
        return false;
    }

    const auto bundle = foyer::scrapers::game_asset_dir(system_folder, rom_stem);
    foyer::scrapers::ensure_parent_dir(bundle + ".placeholder");
    const std::string dest = bundle + "bezel-bezelproject.png";

    const auto variants = bezel_filename_variants(rom_stem);
    int idx = 0;
    for (const auto& name : variants) {
        idx++;
        PerGameBezelProgress p;
        p.step   = idx;
        p.total  = (int)variants.size();
        p.detail = "Trying \"" + name + ".png\"";
        if (progress) progress(p);

        const std::string path = std::string("retroarch/overlay/GameBezels/")
            + m->inner + "/" + name + ".png";
        const std::string url  = "https://raw.githubusercontent.com/thebezelproject/"
            + std::string(m->repo) + "/master/" + url_path_encode(path);
        foyer::log::write("[per-game-bezel] GET %s\n", url.c_str());
        if (try_fetch_one(url, dest)) {
            foyer::log::write("[per-game-bezel] wrote %s\n", dest.c_str());
            return true;
        }
    }
    foyer::log::write(
        "[per-game-bezel] no bezelproject hit for %.*s/%.*s after %d variants\n",
        (int)system_folder.size(), system_folder.data(),
        (int)rom_stem.size(), rom_stem.data(),
        (int)variants.size());
    return false;
}

bool fetch_bezel_from_estefan(std::string_view system_folder,
                              std::string_view rom_stem,
                              PerGameBezelProgressCb progress) {
    // estefan3112 is arcade-only. Limit to foyer's arcade-bearing
    // system slugs — anything else returns false immediately so the
    // UI can hide the action.
    static const char* kArcadeSlugs[] = {
        "arcade", "mame", "fbneo", "fbalpha", "neogeo",
    };
    bool ok = false;
    for (const char* s : kArcadeSlugs) {
        if (system_folder == s) { ok = true; break; }
    }
    if (!ok) {
        foyer::log::write("[per-game-bezel] estefan skip — system=%.*s not arcade\n",
            (int)system_folder.size(), system_folder.data());
        return false;
    }

    const auto bundle = foyer::scrapers::game_asset_dir(system_folder, rom_stem);
    foyer::scrapers::ensure_parent_dir(bundle + ".placeholder");
    const std::string dest = bundle + "bezel-estefan.png";

    // estefan3112 hosts arcade bezels as overlays/<rom>.png in the
    // main repo (no per-system subdir). Try stem variants the same
    // way as bezelproject.
    const auto variants = bezel_filename_variants(rom_stem);
    int idx = 0;
    for (const auto& name : variants) {
        idx++;
        PerGameBezelProgress p;
        p.step   = idx;
        p.total  = (int)variants.size();
        p.detail = "Trying \"" + name + ".png\"";
        if (progress) progress(p);
        const std::string url =
            "https://raw.githubusercontent.com/estefan3112/"
            "Retroarch-Realistic-Bezel-Artwork/master/overlays/"
            + url_path_encode(name) + ".png";
        foyer::log::write("[per-game-bezel] GET %s\n", url.c_str());
        if (try_fetch_one(url, dest)) {
            foyer::log::write("[per-game-bezel] wrote %s\n", dest.c_str());
            return true;
        }
    }
    foyer::log::write(
        "[per-game-bezel] no estefan hit for %.*s/%.*s after %d variants\n",
        (int)system_folder.size(), system_folder.data(),
        (int)rom_stem.size(), rom_stem.data(),
        (int)variants.size());
    return false;
}

namespace {

std::string preview_dir_for(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/data/cache/bezel-preview/"}
         + std::string{sys} + "/" + std::string{stem} + "/";
}

void copy_file_atomic(const std::string& src, const std::string& dst) {
    auto* in  = std::fopen(src.c_str(), "rb");
    if (!in) return;
    const std::string tmp = dst + ".part";
    auto* out = std::fopen(tmp.c_str(), "wb");
    if (!out) { std::fclose(in); return; }
    char buf[64 * 1024];
    while (auto n = std::fread(buf, 1, sizeof(buf), in)) {
        std::fwrite(buf, 1, n, out);
    }
    std::fclose(in);
    std::fclose(out);
    if (::rename(tmp.c_str(), dst.c_str()) != 0) ::unlink(tmp.c_str());
}

// Try the BezelProject URL chain but write to an arbitrary
// destination path instead of the bundle dir. Returns true on
// first hit.
bool fetch_bp_to(std::string_view sys, std::string_view stem,
                 const std::string& dest,
                 PerGameBezelProgressCb progress) {
    const auto* m = find_bp(sys);
    if (!m) return false;
    const auto variants = bezel_filename_variants(stem);
    int idx = 0;
    for (const auto& name : variants) {
        idx++;
        PerGameBezelProgress p;
        p.step = idx; p.total = (int)variants.size();
        p.detail = "Bezel Project — \"" + name + ".png\"";
        if (progress) progress(p);
        const std::string path = std::string("retroarch/overlay/GameBezels/")
            + m->inner + "/" + name + ".png";
        const std::string url = "https://raw.githubusercontent.com/thebezelproject/"
            + std::string(m->repo) + "/master/" + url_path_encode(path);
        if (try_fetch_one(url, dest)) return true;
    }
    return false;
}

bool fetch_estefan_to(std::string_view sys, std::string_view stem,
                       const std::string& dest,
                       PerGameBezelProgressCb progress) {
    static const char* kArcadeSlugs[] = {
        "arcade", "mame", "fbneo", "fbalpha", "neogeo",
    };
    bool ok = false;
    for (const char* s : kArcadeSlugs) if (sys == s) { ok = true; break; }
    if (!ok) return false;
    const auto variants = bezel_filename_variants(stem);
    int idx = 0;
    for (const auto& name : variants) {
        idx++;
        PerGameBezelProgress p;
        p.step = idx; p.total = (int)variants.size();
        p.detail = "Realistic — \"" + name + ".png\"";
        if (progress) progress(p);
        const std::string url =
            "https://raw.githubusercontent.com/estefan3112/"
            "Retroarch-Realistic-Bezel-Artwork/master/overlays/"
            + url_path_encode(name) + ".png";
        if (try_fetch_one(url, dest)) return true;
    }
    return false;
}

}  // namespace

std::vector<PerGameBezelPreview> peek_per_game_bezels(
    std::string_view system_folder,
    std::string_view rom_stem,
    PerGameBezelProgressCb progress) {

    std::vector<PerGameBezelPreview> out;
    const std::string dir = preview_dir_for(system_folder, rom_stem);
    foyer::scrapers::ensure_parent_dir(dir + ".placeholder");

    // Bezel Project.
    if (bezelproject_has_system(system_folder)) {
        const std::string dest = dir + "bezelproject.png";
        ::unlink(dest.c_str());
        if (fetch_bp_to(system_folder, rom_stem, dest, progress)) {
            out.push_back({ "bezelproject", "The Bezel Project", dest });
        }
    }

    // estefan3112 (arcade only).
    {
        const std::string dest = dir + "estefan3112.png";
        ::unlink(dest.c_str());
        if (fetch_estefan_to(system_folder, rom_stem, dest, progress)) {
            out.push_back({ "estefan3112", "Realistic (arcade)", dest });
        }
    }

    foyer::log::write(
        "[per-game-bezel] peek %.*s/%.*s -> %zu hits\n",
        (int)system_folder.size(), system_folder.data(),
        (int)rom_stem.size(), rom_stem.data(),
        out.size());
    return out;
}

bool commit_bezel_to_bundle(std::string_view system_folder,
                            std::string_view rom_stem,
                            std::string_view source,
                            const std::string& temp_path) {
    if (source.empty()) return false;
    struct stat st{};
    if (::stat(temp_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    const auto bundle = foyer::scrapers::game_asset_dir(system_folder, rom_stem);
    foyer::scrapers::ensure_parent_dir(bundle + ".placeholder");
    const std::string dest = bundle + "bezel-" + std::string{source} + ".png";
    copy_file_atomic(temp_path, dest);
    struct stat dst_st{};
    if (::stat(dest.c_str(), &dst_st) != 0) return false;
    foyer::log::write("[per-game-bezel] committed %s -> %s\n",
        temp_path.c_str(), dest.c_str());
    return true;
}

void clear_per_game_bezel_preview_cache(std::string_view system_folder,
                                        std::string_view rom_stem) {
    const std::string dir = preview_dir_for(system_folder, rom_stem);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    while (auto* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string p = dir + e->d_name;
        ::unlink(p.c_str());
    }
    ::closedir(d);
    ::rmdir(dir.c_str());
}

}  // namespace foyer::library
