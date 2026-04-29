#include "libretro_thumbnails.hpp"
#include "cache.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <cstring>
#include <string>

namespace foyer::scrapers::libretro_thumb {
namespace {

// The repo URL-encodes path components, so spaces become %20 etc. Names also
// drop characters illegal on Windows filesystems — libretro applies this:
//
//   & * / : ` < > ? \ | "  →  _
//
// (libretro's exact mapping). We mirror it before percent-encoding.
std::string sanitize(std::string_view in) {
    static const char bad[] = "&*/:`<>?\\|\"";
    std::string out{in};
    for (auto& c : out) {
        if (std::strchr(bad, c)) c = '_';
    }
    return out;
}

bool fetch(std::string_view db, std::string_view stem,
           const char* category, const std::string& dest_png) {
    if (db.empty() || stem.empty()) return false;

    const auto db_enc   = foyer::net::url_encode(std::string{db});
    const auto stem_enc = foyer::net::url_encode(sanitize(stem));

    const std::string url =
        "https://thumbnails.libretro.com/" + db_enc + "/" + category + "/" +
        stem_enc + ".png";

    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(url, dest_png)) {
        return false;
    }
    foyer::log::write("[lr-thumb] saved %s\n", dest_png.c_str());
    return true;
}

} // namespace

bool fetch_cover(std::string_view db, std::string_view stem, const std::string& dest_png) {
    return fetch(db, stem, "Named_Boxarts", dest_png);
}

bool fetch_screenshot(std::string_view db, std::string_view stem, const std::string& dest_png) {
    return fetch(db, stem, "Named_Snaps", dest_png);
}

} // namespace foyer::scrapers::libretro_thumb
