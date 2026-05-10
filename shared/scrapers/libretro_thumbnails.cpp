#include "libretro_thumbnails.hpp"
#include "cache.hpp"
#include "libretro_index.hpp"
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

// Drop everything in parens and brackets, collapse whitespace, trim.
// Used as a fallback when the exact-stem URL 404s — most rom dumps
// carry tags ("(USA, Europe, Korea)", "[T Eng1.0_*]") that don't
// appear in libretro-thumbnails' canonical filenames, but the
// pre-tag base usually does.
std::string strip_tags(std::string_view in) {
    std::string s;
    s.reserve(in.size());
    int p = 0, b = 0;
    for (char c : in) {
        if      (c == '(') ++p;
        else if (c == ')') { if (p > 0) --p; }
        else if (c == '[') ++b;
        else if (c == ']') { if (b > 0) --b; }
        else if (p == 0 && b == 0) s += c;
    }
    std::string out; out.reserve(s.size());
    bool last_space = true;
    for (char c : s) {
        const bool is_space = (c == ' ' || c == '\t');
        if (is_space) { if (!last_space) out += ' '; last_space = true; }
        else          { out += c; last_space = false; }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '-')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '-')) out.erase(out.begin());
    return out;
}

bool try_one(const std::string& db_enc, const std::string& stem,
             const char* category, const std::string& dest_png) {
    const auto stem_enc = foyer::net::url_encode(sanitize(stem));
    const std::string url =
        "https://thumbnails.libretro.com/" + db_enc + "/" + category + "/" +
        stem_enc + ".png";
    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(url, dest_png)) return false;
    foyer::log::write("[lr-thumb] saved %s (stem='%s')\n",
        dest_png.c_str(), stem.c_str());
    return true;
}

bool fetch(std::string_view db, std::string_view stem,
           const char* category, const std::string& dest_png) {
    if (db.empty() || stem.empty()) return false;

    const auto db_enc = foyer::net::url_encode(std::string{db});

    // 1) exact stem (matches dumps named to canonical No-Intro).
    const std::string raw{stem};
    if (try_one(db_enc, raw, category, dest_png)) return true;

    // 2) cleaned stem — drop region/translation tags. Cheap and
    //    covers the "Ice Climber (USA, Europe, Korea)" superset
    //    case where libretro happens to also know it without the
    //    tag.
    const auto clean = strip_tags(stem);
    if (!clean.empty() && clean != raw) {
        if (try_one(db_enc, clean, category, dest_png)) return true;
    }

    // 3) full canonical match via the cached per-system index.
    //    Normalizes both sides, picks USA/Europe/Japan-preferred
    //    canonical, then downloads. Hits all the awkward
    //    bracket-tag and translation-suffix cases the simple
    //    string strips miss.
    const auto canon = ::foyer::scrapers::libretro_thumb::find_match(
        db, stem, category);
    if (!canon.empty() && canon != raw && canon != clean) {
        if (try_one(db_enc, canon, category, dest_png)) return true;
    }

    return false;
}

} // namespace

bool fetch_cover(std::string_view db, std::string_view stem, const std::string& dest_png) {
    return fetch(db, stem, "Named_Boxarts", dest_png);
}

bool fetch_screenshot(std::string_view db, std::string_view stem, const std::string& dest_png) {
    return fetch(db, stem, "Named_Snaps", dest_png);
}

bool fetch_title(std::string_view db, std::string_view stem, const std::string& dest_png) {
    return fetch(db, stem, "Named_Titles", dest_png);
}

} // namespace foyer::scrapers::libretro_thumb
