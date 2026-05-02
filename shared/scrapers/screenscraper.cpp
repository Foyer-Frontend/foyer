#include "screenscraper.hpp"
#include "accounts.hpp"
#include "cache.hpp"
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
// expanded as new cores land.
constexpr SystemMap kMap[] = {
    { "nes",          3   },
    { "snes",         4   },
    { "gb",           9   },
    { "gbc",          10  },
    { "gba",          12  },
    { "n64",          14  },
    { "nds",          15  },
    { "gc",           13  },
    { "genesis",      1   },
    { "megadrive",    1   },
    { "mastersystem", 2   },
    { "gamegear",     21  },
    { "saturn",       22  },
    { "dc",           23  },
    { "psx",          57  },
    { "psp",          61  },
    { "ngp",          25  },
    { "ngpc",         82  },
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

bool fetch_cover(std::string_view system_folder,
                 const std::string& rom_path,
                 std::string_view rom_stem,
                 const std::string& dest_png) {
    const auto& a = accounts().screenscraper;
    if (!a.ready()) {
        foyer::log::write("[ss] devid/devpassword not set in accounts.jsonc\n");
        return false;
    }

    const int system_id = system_id_for_folder(system_folder);
    if (system_id == 0) {
        foyer::log::write("[ss] unknown system: %.*s\n",
            (int)system_folder.size(), system_folder.data());
        return false;
    }

    char crchex[16];
    const auto crc = crc32_file(rom_path);
    std::snprintf(crchex, sizeof(crchex), "%08x", crc);

    // Build the jeuInfos URL. We pass user creds when available — they
    // give a higher API quota.
    std::string url =
        std::string{"https://api.screenscraper.fr/api2/jeuInfos.php?"}
        + "devid="       + foyer::net::url_encode(a.devid)
        + "&devpassword="+ foyer::net::url_encode(a.devpassword)
        + "&softname="   + foyer::net::url_encode(kSoftname)
        + "&output=json"
        + "&systemeid="  + std::to_string(system_id)
        + "&crc="        + crchex
        + "&romnom="     + foyer::net::url_encode(std::string{rom_stem});
    if (a.user_ready()) {
        url += "&ssid="       + foyer::net::url_encode(a.ssid);
        url += "&sspassword=" + foyer::net::url_encode(a.sspassword);
    }

    auto resp = foyer::net::get(url);
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
    auto* jeu      = response ? yyjson_obj_get(response, "jeu") : nullptr;
    auto* medias   = jeu ? yyjson_obj_get(jeu, "medias") : nullptr;

    std::string image_url;
    if (medias && yyjson_is_arr(medias)) {
        std::size_t i, max;
        yyjson_val* item;
        // Prefer "box-2D" → "box-3D" → "screenshot" for the cover slot.
        const char* preferred[] = { "box-2D", "box-3D", "ss", "screenshot" };
        for (auto* want : preferred) {
            if (!image_url.empty()) break;
            yyjson_arr_foreach(medias, i, max, item) {
                auto* type_val = yyjson_obj_get(item, "type");
                auto* url_val  = yyjson_obj_get(item, "url");
                if (!type_val || !url_val) continue;
                if (yyjson_is_str(type_val) && yyjson_is_str(url_val)
                    && !std::strcmp(yyjson_get_str(type_val), want)) {
                    image_url = yyjson_get_str(url_val);
                    break;
                }
            }
        }
    }

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
        foyer::log::write("[ss] no media url in response (crc=%s)\n", crchex);
        return any_meta; // metadata-only success still counts as a hit
    }

    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(image_url, dest_png)) {
        return false;
    }
    foyer::log::write("[ss] saved %s (crc=%s)\n", dest_png.c_str(), crchex);
    return true;
}

} // namespace foyer::scrapers::screenscraper
