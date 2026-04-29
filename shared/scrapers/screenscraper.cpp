#include "screenscraper.hpp"
#include "accounts.hpp"
#include "cache.hpp"
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
    yyjson_doc_free(doc);

    if (image_url.empty()) {
        foyer::log::write("[ss] no media url in response (crc=%s)\n", crchex);
        return false;
    }

    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(image_url, dest_png)) {
        return false;
    }
    foyer::log::write("[ss] saved %s (crc=%s)\n", dest_png.c_str(), crchex);
    return true;
}

} // namespace foyer::scrapers::screenscraper
