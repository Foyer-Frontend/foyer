#include "screenscraper.hpp"
#include "accounts.hpp"
#include "cache.hpp"
#include "library/config.hpp"
#include "library/game_meta.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include <yyjson.h>
#include <zlib.h>

namespace foyer::scrapers::screenscraper {
namespace {

struct SystemMap {
    std::string_view folder;
    int              id;
};

// Mapping from foyer's folder names to ScreenScraper system IDs. List is
// expanded as new cores land. IDs cross-checked against api.screenscraper.fr's
// systemesListe.php (use the "name_eu" / "name_us" field for confirmation).
constexpr SystemMap kMap[] = {
    { "nes",             3   },
    { "snes",            4   },
    { "gb",              9   },
    { "gbc",             10  },
    { "gba",             12  },
    { "n64",             14  },
    { "nds",             15  },
    { "gc",              13  },
    { "genesis",         1   },
    { "megadrive",       1   },
    { "mastersystem",    2   },
    { "gamegear",        21  },
    { "saturn",          22  },
    { "dc",              23  },
    { "psx",             57  },
    { "psp",             61  },
    { "ngp",             25  },
    { "ngpc",            82  },
    { "__switch",        225 },  // ScreenScraper "Nintendo Switch"
    { "atari2600",       26  },
    { "atari5200",       40  },
    { "atari7800",       41  },
    { "lynx",            28  },
    { "jaguar",          27  },
    { "virtualboy",      11  },
    { "wonderswan",      45  },
    { "wonderswancolor", 46  },
    { "supergrafx",      105 },
    { "pcengine",        31  },
    { "tg16",            31  },
    { "pcenginecd",      114 },
    { "32x",             19  },
    { "segacd",          20  },
    { "3do",             29  },
    { "neogeo",          142 },
    { "colecovision",    48  },
    { "amiga",           64  },
    { "amiga600",        64  },
    { "amiga1200",       64  },
    { "amigacd32",       130 },
    { "c64",             66  },
    { "msx",             113 },
    { "zxspectrum",      76  },
    { "gw",              52  },
    { "pokemini",        211 },
    { "intellivision",   115 },
    { "vectrex",         102 },
    { "scummvm",         123 },
    { "dos",             135 },
    { "atarist",         42  },
    { "channelf",        80  },
};

constexpr const char* kSoftname = "foyer";

} // namespace

int system_id_for_folder(std::string_view folder) {
    for (const auto& m : kMap) {
        if (m.folder == folder) return m.id;
    }
    return 0;
}

std::uint32_t crc32_file(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) return 0;
    std::uint32_t crc = ::crc32(0, nullptr, 0);
    char buf[64 * 1024];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        const auto n = (uInt)in.gcount();
        crc = ::crc32(crc, reinterpret_cast<const Bytef*>(buf), n);
    }
    return crc;
}

// Hardcoded fallback dev credentials. ScreenScraper requires a
// devid/devpassword on every API call; until foyer's own pair is
// issued by Bigon (request thread on forum.screenscraper.fr) we
// fall back to EmuDeck's plaintext-published pair so users can
// scrape immediately. When foyer gets its own credentials, swap
// the constants below — kFoyerDevid / kFoyerDevpassword — and
// the fallback comment can be deleted.
namespace {
constexpr const char* kFallbackDevid       = "djrodtc";
constexpr const char* kFallbackDevpassword = "diFay35WElL";
}

bool fetch_cover(std::string_view system_folder,
                 const std::string& rom_path,
                 std::string_view rom_stem,
                 const std::string& dest_png) {
    const auto& a = accounts().screenscraper;
    std::string devid       = a.devid.empty()       ? kFallbackDevid       : a.devid;
    std::string devpassword = a.devpassword.empty() ? kFallbackDevpassword : a.devpassword;
    if (devid.empty() || devpassword.empty()) {
        foyer::log::write("[ss] devid/devpassword unavailable\n");
        return false;
    }

    const int system_id = system_id_for_folder(system_folder);
    if (system_id == 0) {
        foyer::log::write("[ss] unknown system: %.*s\n",
            (int)system_folder.size(), system_folder.data());
        return false;
    }

    // Installed Switch titles have a "switch://<hex>" pseudo-path
    // and no rom file on disk — nothing to CRC. SS coverage of
    // Switch indie / obscure titles is sparse: jeuInfos with just
    // romnom returns 404 for any name that doesn't exact-match an
    // indexed rom, and jeuRecherche returns shallow {} objects for
    // titles SS has heard of but never scraped. The fallback chain
    // below tries jeuInfos exact match first; on 404 it tries
    // jeuRecherche to get a jeuid; if that yields one, it re-runs
    // jeuInfos by jeuid for the full media payload.
    const bool is_switch_virtual = rom_path.rfind("switch://", 0) == 0;

    auto creds_suffix = [&]() {
        std::string s;
        if (a.user_ready()) {
            s += "&ssid="       + foyer::net::url_encode(a.ssid);
            s += "&sspassword=" + foyer::net::url_encode(a.sspassword);
        }
        return s;
    };

    auto build_jeuinfos_url = [&](const std::string& name,
                                  const std::string& crc_hex) {
        std::string u =
            std::string{"https://api.screenscraper.fr/api2/jeuInfos.php?"}
            + "devid="       + foyer::net::url_encode(devid)
            + "&devpassword="+ foyer::net::url_encode(devpassword)
            + "&softname="   + foyer::net::url_encode(kSoftname)
            + "&output=json"
            + "&systemeid="  + std::to_string(system_id)
            + "&romnom="     + foyer::net::url_encode(name);
        if (!crc_hex.empty()) u += "&crc=" + crc_hex;
        u += creds_suffix();
        return u;
    };

    std::string crc_hex;
    if (!is_switch_virtual) {
        char crchex[16];
        const auto crc = crc32_file(rom_path);
        std::snprintf(crchex, sizeof(crchex), "%08x", crc);
        crc_hex = crchex;
    }
    std::string url = build_jeuinfos_url(std::string{rom_stem}, crc_hex);

    auto resp = foyer::net::get(url);

    // Switch fallback chain — only when the initial jeuInfos didn't
    // find anything AND we're on the Switch virtual path (real roms
    // get the canonical CRC path and don't need fuzzy fallback).
    if (is_switch_virtual
        && (resp.code == 404 || resp.body.empty()
            || resp.body.find("\"jeu\"") == std::string::npos)) {
        const std::string rurl =
            std::string{"https://api.screenscraper.fr/api2/jeuRecherche.php?"}
            + "devid="       + foyer::net::url_encode(devid)
            + "&devpassword="+ foyer::net::url_encode(devpassword)
            + "&softname="   + foyer::net::url_encode(kSoftname)
            + "&output=json"
            + "&systemeid="  + std::to_string(system_id)
            + "&recherche="  + foyer::net::url_encode(std::string{rom_stem})
            + creds_suffix();
        auto rresp = foyer::net::get(rurl);
        if (rresp.code >= 200 && rresp.code < 300 && !rresp.body.empty()) {
            // Grab the first jeuid out of jeux[]. The light response
            // has just "id": "<n>" in each entry — strstr is enough
            // and avoids re-parsing the whole doc.
            const auto id_pos = rresp.body.find("\"id\":");
            if (id_pos != std::string::npos) {
                auto q1 = rresp.body.find('"', id_pos + 5);
                auto q2 = rresp.body.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos
                    && q2 > q1 + 1) {
                    const std::string jeuid =
                        rresp.body.substr(q1 + 1, q2 - q1 - 1);
                    if (!jeuid.empty() && jeuid.front() != '{') {
                        // Re-fetch with jeuid for the full media set.
                        const std::string jurl =
                            std::string{"https://api.screenscraper.fr/api2/jeuInfos.php?"}
                            + "devid="       + foyer::net::url_encode(devid)
                            + "&devpassword="+ foyer::net::url_encode(devpassword)
                            + "&softname="   + foyer::net::url_encode(kSoftname)
                            + "&output=json"
                            + "&systemeid="  + std::to_string(system_id)
                            + "&gameid="     + jeuid
                            + creds_suffix();
                        foyer::log::write(
                            "[ss] switch fallback via jeuid=%s\n",
                            jeuid.c_str());
                        resp = foyer::net::get(jurl);
                    }
                }
            }
        }
    }

    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        foyer::log::write("[ss] lookup failed: code=%ld bytes=%zu\n",
            resp.code, resp.body.size());
        return false;
    }

    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) {
        foyer::log::write("[ss] response not JSON\n");
        return false;
    }

    auto* root = yyjson_doc_get_root(doc);
    auto* response = yyjson_obj_get(root, "response");
    // jeuInfos returns response.jeu (single); jeuRecherche returns
    // response.jeux (array). For the Switch virtual path we take
    // the first jeux entry — SS ranks by relevance so [0] is the
    // top match for the title name we searched on.
    auto* jeu = response ? yyjson_obj_get(response, "jeu") : nullptr;
    if (!jeu && response) {
        auto* jeux = yyjson_obj_get(response, "jeux");
        if (jeux && yyjson_is_arr(jeux) && yyjson_arr_size(jeux) > 0) {
            jeu = yyjson_arr_get(jeux, 0);
        }
    }
    auto* medias   = jeu ? yyjson_obj_get(jeu, "medias") : nullptr;

    // When jeu/medias come back missing, SS hands us either an
    // empty response (no rom match) or an error response (rate
    // limit, bad creds). Surface the response body's first
    // 160 chars so log readers can tell the two cases apart —
    // "Erreur :" prefix from SS means throttled / auth, while a
    // valid 200 with no jeu means "we have no entry for this
    // crc+romnom". Without this we previously logged nothing
    // and the user saw a generic "no hits".
    if (!jeu) {
        std::string snippet{resp.body.data(),
            std::min<std::size_t>(resp.body.size(), 160)};
        foyer::log::write("[ss] no jeu for stem=%.*s body=%s\n",
            (int)rom_stem.size(), rom_stem.data(), snippet.c_str());
    }

    // For each media kind we want, walk the medias array and pick
    // the best-region entry. Returns {url, region} or empty url.
    struct MediaPick { std::string url; std::string region; };
    auto pick_media = [&](const char* kind) -> MediaPick {
        MediaPick out;
        if (!medias || !yyjson_is_arr(medias)) return out;
        // Region preference. SS tags media with "us", "eu", "wor",
        // "jp", region-specific BR/FR, etc. Prefer worldwide → US
        // → Europe → Japan → first-available, unless the user
        // pinned a region in Settings → General, in which case
        // that one gets first dibs.
        std::vector<const char*> prefs = { "wor", "us", "eu", "jp" };
        const auto& user_region = ::foyer::library::config().region;
        if (!user_region.empty()) {
            prefs.insert(prefs.begin(), user_region.c_str());
        }
        std::size_t i, max; yyjson_val* item;
        for (auto* want_region : prefs) {
            yyjson_arr_foreach(medias, i, max, item) {
                auto* type_val = yyjson_obj_get(item, "type");
                auto* url_val  = yyjson_obj_get(item, "url");
                auto* reg_val  = yyjson_obj_get(item, "region");
                if (!type_val || !url_val) continue;
                if (!yyjson_is_str(type_val) || !yyjson_is_str(url_val)) continue;
                if (std::strcmp(yyjson_get_str(type_val), kind) != 0) continue;
                const char* reg = (reg_val && yyjson_is_str(reg_val))
                    ? yyjson_get_str(reg_val) : "";
                if (std::strcmp(reg, want_region) == 0) {
                    out.url    = yyjson_get_str(url_val);
                    out.region = reg;
                    return out;
                }
            }
        }
        // Fallback: first item of this kind.
        yyjson_arr_foreach(medias, i, max, item) {
            auto* type_val = yyjson_obj_get(item, "type");
            auto* url_val  = yyjson_obj_get(item, "url");
            auto* reg_val  = yyjson_obj_get(item, "region");
            if (type_val && url_val
                && yyjson_is_str(type_val) && yyjson_is_str(url_val)
                && !std::strcmp(yyjson_get_str(type_val), kind)) {
                out.url    = yyjson_get_str(url_val);
                out.region = (reg_val && yyjson_is_str(reg_val))
                    ? yyjson_get_str(reg_val) : "";
                return out;
            }
        }
        return out;
    };

    // Box-2D goes to dest_png so the existing scrape_job hit
    // counter still works. Other media are downloaded into the
    // per-game asset dir alongside box-2D for the GameActivity
    // detail panel + system tile to find.
    const auto box     = pick_media("box-2D");
    std::string image_url = box.url;

    // Extract sidecar metadata while we still hold the parsed doc. Each
    // localized field uses a region/language preference list — wor (world)
    // → us → eu → jp for regions, en → fr for languages — to match what an
    // English-speaking user would expect to see.
    foyer::library::GameMeta meta;

    auto pick_localized = [&](yyjson_val* arr, const char* key, const char* val_key,
                              std::initializer_list<const char*> prefs) -> std::string {
        if (!arr || !yyjson_is_arr(arr)) return {};
        for (auto* want : prefs) {
            std::size_t i, max; yyjson_val* item;
            yyjson_arr_foreach(arr, i, max, item) {
                auto* k = yyjson_obj_get(item, key);
                auto* v = yyjson_obj_get(item, val_key);
                if (k && v && yyjson_is_str(k) && yyjson_is_str(v)
                    && !std::strcmp(yyjson_get_str(k), want)) {
                    return yyjson_get_str(v);
                }
            }
        }
        // Fall back to the first entry if none of the preferred regions matched.
        if (yyjson_arr_size(arr) > 0) {
            auto* item = yyjson_arr_get_first(arr);
            auto* v = yyjson_obj_get(item, val_key);
            if (v && yyjson_is_str(v)) return yyjson_get_str(v);
        }
        return {};
    };

    if (jeu) {
        meta.title = pick_localized(yyjson_obj_get(jeu, "noms"),
                                    "region", "text", {"wor","us","eu","jp"});

        auto date = pick_localized(yyjson_obj_get(jeu, "dates"),
                                   "region", "text", {"wor","us","eu","jp"});
        // ScreenScraper returns "YYYY-MM-DD" or "YYYY". Take the year.
        if (date.size() >= 4) meta.year = date.substr(0, 4);

        if (auto* e = yyjson_obj_get(jeu, "editeur")) {
            if (auto* t = yyjson_obj_get(e, "text"); t && yyjson_is_str(t))
                meta.publisher = yyjson_get_str(t);
        }
        if (auto* d = yyjson_obj_get(jeu, "developpeur")) {
            if (auto* t = yyjson_obj_get(d, "text"); t && yyjson_is_str(t))
                meta.developer = yyjson_get_str(t);
        }
        if (auto* j = yyjson_obj_get(jeu, "joueurs")) {
            if (auto* t = yyjson_obj_get(j, "text"); t && yyjson_is_str(t))
                meta.players = yyjson_get_str(t);
        }
        // Genre: each entry has a `noms` array of {langue, text}.
        if (auto* genres = yyjson_obj_get(jeu, "genres");
            genres && yyjson_is_arr(genres) && yyjson_arr_size(genres) > 0) {
            auto* g0 = yyjson_arr_get_first(genres);
            meta.genre = pick_localized(yyjson_obj_get(g0, "noms"),
                                        "langue", "text", {"en","fr"});
        }
        // Classification: prefer PEGI then ESRB.
        if (auto* clas = yyjson_obj_get(jeu, "classifications");
            clas && yyjson_is_arr(clas)) {
            for (const char* want : {"PEGI", "ESRB"}) {
                std::size_t i, max; yyjson_val* item;
                yyjson_arr_foreach(clas, i, max, item) {
                    auto* type = yyjson_obj_get(item, "type");
                    auto* text = yyjson_obj_get(item, "text");
                    if (type && text && yyjson_is_str(type) && yyjson_is_str(text)
                        && !std::strcmp(yyjson_get_str(type), want)) {
                        meta.rating = std::string{want} + " " + yyjson_get_str(text);
                        break;
                    }
                }
                if (!meta.rating.empty()) break;
            }
        }
        meta.description = pick_localized(yyjson_obj_get(jeu, "synopsis"),
                                          "langue", "text", {"en","fr"});
    }

    yyjson_doc_free(doc);

    // Persist metadata even when no image is present — the sidebar still
    // has something useful to render in that case.
    const bool any_meta =
        !meta.title.empty()      || !meta.year.empty()    ||
        !meta.publisher.empty()  || !meta.developer.empty() ||
        !meta.genre.empty()      || !meta.players.empty() ||
        !meta.rating.empty()     || !meta.description.empty();
    if (any_meta) {
        foyer::library::save_meta(system_folder, rom_stem, meta);
    }

    if (image_url.empty()) {
        foyer::log::write("[ss] no media url in response (stem=%.*s)\n",
            (int)rom_stem.size(), rom_stem.data());
        return any_meta; // metadata-only success still counts as a hit
    }

    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(image_url, dest_png)) {
        return false;
    }
    foyer::log::write("[ss] saved %s (stem=%.*s)\n",
        dest_png.c_str(), (int)rom_stem.size(), rom_stem.data());

    // Save the same box-2D into the per-game asset bundle dir
    // alongside the other media we're about to fetch. The bundle
    // is the source-of-truth GameActivity reads from for the
    // detail panel; the dest_png copy stays for legacy callers.
    const auto bundle_dir = foyer::scrapers::game_asset_dir(system_folder, rom_stem);
    auto bundle_path = [&](const std::string& kind, const std::string& region,
                           const char* ext) {
        std::string nm = bundle_dir + kind;
        if (!region.empty()) nm += "(" + region + ")";
        nm += ext;
        return nm;
    };
    foyer::scrapers::ensure_parent_dir(bundle_dir + ".placeholder");
    if (!box.url.empty()) {
        foyer::net::get_to_file(box.url,
            bundle_path("box-2D", box.region, ".png"));
    }

    // Companion media — best-effort, individual failures don't
    // count against the scrape hit total. Each lands at the
    // canonical filename the user expects.
    const auto title_pick = pick_media("sstitle");
    if (!title_pick.url.empty()) {
        foyer::net::get_to_file(title_pick.url,
            bundle_path("sstitle", title_pick.region, ".png"));
    }
    const auto ss_pick = pick_media("ss");
    if (!ss_pick.url.empty()) {
        foyer::net::get_to_file(ss_pick.url,
            bundle_path("ss", ss_pick.region, ".png"));
    }
    const auto fanart_pick = pick_media("fanart");
    if (!fanart_pick.url.empty()) {
        // fanart filename has no region tag in the user's reference
        // layout — single fanart per game.
        foyer::net::get_to_file(fanart_pick.url,
            bundle_dir + "fanart.jpg");
    }
    const auto bezel_pick = pick_media("bezel-16-9");
    if (!bezel_pick.url.empty()) {
        foyer::net::get_to_file(bezel_pick.url,
            bundle_path("bezel-16-9", bezel_pick.region, ".png"));
    }
    const auto video_pick = pick_media("video-normalized");
    if (!video_pick.url.empty()) {
        foyer::net::get_to_file(video_pick.url,
            bundle_dir + "video-normalized.mp4");
    }

    // Drop a metadata.json next to the media so the detail panel
    // can render name + publisher + developer + players + rating
    // + genre + release_date + synopsis without re-parsing the
    // legacy /foyer/assets/metadata sidecar. Hand-rolled writer
    // to escape the synopsis correctly without pulling in yyjson
    // builder boilerplate for a small fixed-shape doc.
    auto json_escape = [](const std::string& s) {
        std::string out; out.reserve(s.size() + 8);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((unsigned char)c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    };
    if (auto* f = std::fopen((bundle_dir + "metadata.json").c_str(), "wb")) {
        std::fprintf(f, "{\n");
        std::fprintf(f, "  \"name\":         \"%s\",\n", json_escape(meta.title).c_str());
        std::fprintf(f, "  \"publisher\":    \"%s\",\n", json_escape(meta.publisher).c_str());
        std::fprintf(f, "  \"developer\":    \"%s\",\n", json_escape(meta.developer).c_str());
        std::fprintf(f, "  \"players\":      \"%s\",\n", json_escape(meta.players).c_str());
        std::fprintf(f, "  \"rating\":       \"%s\",\n", json_escape(meta.rating).c_str());
        std::fprintf(f, "  \"genre\":        \"%s\",\n", json_escape(meta.genre).c_str());
        std::fprintf(f, "  \"release_date\": \"%s\",\n", json_escape(meta.year).c_str());
        std::fprintf(f, "  \"synopsis\":     \"%s\"\n",  json_escape(meta.description).c_str());
        std::fprintf(f, "}\n");
        std::fclose(f);
    }

    return true;
}

} // namespace foyer::scrapers::screenscraper
