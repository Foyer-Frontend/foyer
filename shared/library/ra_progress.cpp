#include "ra_progress.hpp"

#include "library/game_meta.hpp"
#include "library/system_db.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "scrapers/accounts.hpp"

#include <rc_consoles.h>
#include <rc_hash.h>
#include <yyjson.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {
namespace {

// Foyer's system_db uses lowercase short folder names ("nes", "snes",
// "gba"…). Map those to rcheevos's RC_CONSOLE_* numeric ids. Folders
// without a known mapping fall through to RC_CONSOLE_UNKNOWN, which
// short-circuits the rest of the pipeline (rc_hash_generate refuses
// UNKNOWN).
uint32_t rc_console_for_folder(std::string_view folder) {
    struct Map { std::string_view k; uint32_t v; };
    static const Map kMap[] = {
        {"nes",              RC_CONSOLE_NINTENDO},
        {"snes",             RC_CONSOLE_SUPER_NINTENDO},
        {"n64",              RC_CONSOLE_NINTENDO_64},
        {"gb",               RC_CONSOLE_GAMEBOY},
        {"gbc",              RC_CONSOLE_GAMEBOY_COLOR},
        {"gba",              RC_CONSOLE_GAMEBOY_ADVANCE},
        {"nds",              RC_CONSOLE_NINTENDO_DS},
        {"genesis",          RC_CONSOLE_MEGA_DRIVE},
        {"megadrive",        RC_CONSOLE_MEGA_DRIVE},
        {"mastersystem",     RC_CONSOLE_MASTER_SYSTEM},
        {"gamegear",         RC_CONSOLE_GAME_GEAR},
        {"sg1000",           RC_CONSOLE_SG1000},
        {"32x",              RC_CONSOLE_SEGA_32X},
        {"segacd",           RC_CONSOLE_SEGA_CD},
        {"saturn",           RC_CONSOLE_SATURN},
        {"dc",               RC_CONSOLE_DREAMCAST},
        {"psx",              RC_CONSOLE_PLAYSTATION},
        {"psp",              RC_CONSOLE_PSP},
        {"atari2600",        RC_CONSOLE_ATARI_2600},
        {"atari7800",        RC_CONSOLE_ATARI_7800},
        {"atarilynx",        RC_CONSOLE_ATARI_LYNX},
        {"atarijaguar",      RC_CONSOLE_ATARI_JAGUAR},
        {"virtualboy",       RC_CONSOLE_VIRTUAL_BOY},
        {"pcengine",         RC_CONSOLE_PC_ENGINE},
        {"pcenginecd",       RC_CONSOLE_PC_ENGINE_CD},
        {"pcfx",             RC_CONSOLE_PCFX},
        {"ngp",              RC_CONSOLE_NEOGEO_POCKET},
        {"ngpc",             RC_CONSOLE_NEOGEO_POCKET},
        {"wonderswan",       RC_CONSOLE_WONDERSWAN},
        {"wonderswancolor",  RC_CONSOLE_WONDERSWAN},
        {"intellivision",    RC_CONSOLE_INTELLIVISION},
        {"colecovision",     RC_CONSOLE_COLECOVISION},
        {"msx",              RC_CONSOLE_MSX},
        {"msx2",             RC_CONSOLE_MSX},
        {"msx2plus",         RC_CONSOLE_MSX},
        {"msxturbor",        RC_CONSOLE_MSX},
        {"amstradcpc",       RC_CONSOLE_AMSTRAD_PC},
        {"c64",              RC_CONSOLE_COMMODORE_64},
        {"amiga",            RC_CONSOLE_AMIGA},
        {"3do",              RC_CONSOLE_3DO},
        {"pokemini",         RC_CONSOLE_POKEMON_MINI},
    };
    for (const auto& m : kMap) {
        if (m.k == folder) return m.v;
    }
    return RC_CONSOLE_UNKNOWN;
}

bool slurp_rom(std::string_view path, std::vector<uint8_t>& out) {
    std::ifstream in{std::string{path}, std::ios::binary};
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto sz = (std::size_t)in.tellg();
    in.seekg(0, std::ios::beg);
    out.resize(sz);
    return (bool)in.read(reinterpret_cast<char*>(out.data()),
                         (std::streamsize)sz);
}

// Resolve a 32-char rcheevos hash to a numeric RA game id. Uses the
// classic dorequest.php endpoint (no auth needed for the gameid
// lookup). Returns 0 on failure (network error, RA unknown hash).
uint32_t resolve_game_id(const char hash[33]) {
    std::string url = "https://retroachievements.org/dorequest.php?r=gameid&m=";
    url += hash;
    const auto resp = ::foyer::net::get(url);
    if (resp.code != 200 || resp.body.empty()) return 0;
    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return 0;
    auto* root = yyjson_doc_get_root(doc);
    uint32_t id = 0;
    if (auto* v = yyjson_obj_get(root, "GameID")) {
        if (yyjson_is_int(v) || yyjson_is_uint(v)) {
            id = (uint32_t)yyjson_get_uint(v);
        }
    }
    yyjson_doc_free(doc);
    return id;
}

// Fetch user-specific progress for a known game id via the modern
// REST endpoint. NumAchievements + NumAwardedToUser are the two
// counts we surface; everything else (badges, individual unlocks)
// stays untouched until the player binary populates it on first
// boot.
struct Progress { int total = -1; int unlocked = -1; };
Progress fetch_progress_for_id(uint32_t id,
                               std::string_view user,
                               std::string_view web_api_key) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "https://retroachievements.org/API/API_GetGameInfoAndUserProgress.php"
        "?u=%.*s&y=%.*s&g=%u",
        (int)user.size(), user.data(),
        (int)web_api_key.size(), web_api_key.data(),
        (unsigned)id);
    const auto resp = ::foyer::net::get(buf);
    Progress p;
    if (resp.code != 200 || resp.body.empty()) return p;
    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return p;
    auto* root = yyjson_doc_get_root(doc);
    auto pull_int = [&](const char* key) -> int {
        auto* v = yyjson_obj_get(root, key);
        if (!v) return -1;
        if (yyjson_is_int(v) || yyjson_is_uint(v))
            return (int)yyjson_get_int(v);
        return -1;
    };
    p.total    = pull_int("NumAchievements");
    p.unlocked = pull_int("NumAwardedToUser");
    yyjson_doc_free(doc);
    return p;
}

}  // namespace

bool fetch_progress(std::string_view system_folder,
                    std::string_view rom_stem,
                    std::string_view rom_path) {
    const auto& acc = ::foyer::scrapers::accounts();
    if (!acc.retroachievements.rest_ready()) {
        foyer::log::write(
            "[ra_progress] skipping %.*s — REST creds missing\n",
            (int)rom_stem.size(), rom_stem.data());
        return false;
    }

    const auto console_id = rc_console_for_folder(system_folder);
    if (console_id == RC_CONSOLE_UNKNOWN) {
        foyer::log::write(
            "[ra_progress] skipping %.*s — no rc_console mapping for %.*s\n",
            (int)rom_stem.size(), rom_stem.data(),
            (int)system_folder.size(), system_folder.data());
        return false;
    }

    std::vector<uint8_t> rom;
    if (!slurp_rom(rom_path, rom) || rom.empty()) {
        foyer::log::write(
            "[ra_progress] could not read rom %.*s\n",
            (int)rom_path.size(), rom_path.data());
        return false;
    }

    char hash[33] = {0};
    if (!rc_hash_generate_from_buffer(hash, console_id, rom.data(), rom.size())) {
        foyer::log::write(
            "[ra_progress] hash failed for %.*s\n",
            (int)rom_stem.size(), rom_stem.data());
        return false;
    }
    foyer::log::write("[ra_progress] %.*s hash=%s\n",
        (int)rom_stem.size(), rom_stem.data(), hash);

    const auto game_id = resolve_game_id(hash);
    if (game_id == 0) {
        foyer::log::write(
            "[ra_progress] %.*s — RA didn't recognize the hash\n",
            (int)rom_stem.size(), rom_stem.data());
        return false;
    }

    const auto p = fetch_progress_for_id(game_id,
        acc.retroachievements.user, acc.retroachievements.web_api_key);
    if (p.total < 0) {
        foyer::log::write(
            "[ra_progress] %.*s — REST progress fetch failed\n",
            (int)rom_stem.size(), rom_stem.data());
        return false;
    }

    auto meta = load_meta(system_folder, rom_stem);
    meta.cheevos_total    = p.total;
    meta.cheevos_unlocked = p.unlocked;
    save_meta(system_folder, rom_stem, meta);
    foyer::log::write("[ra_progress] %.*s id=%u %d/%d cheevos\n",
        (int)rom_stem.size(), rom_stem.data(),
        (unsigned)game_id, p.unlocked, p.total);
    return true;
}

}  // namespace foyer::library
