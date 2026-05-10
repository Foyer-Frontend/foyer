#include "steamgriddb.hpp"
#include "accounts.hpp"
#include "cache.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <cstring>
#include <string>
#include <vector>

#include <yyjson.h>

namespace foyer::scrapers::steamgriddb {
namespace {

// SteamGridDB requires Bearer-token auth.
std::vector<std::string> auth_headers(const std::string& key) {
    std::vector<std::string> hdrs;
    hdrs.emplace_back("Authorization: Bearer " + key);
    hdrs.emplace_back("Accept: application/json");
    return hdrs;
}

// Look up the SteamGridDB game id matching `query`. Returns 0 on miss.
// SteamGridDB autocomplete is whole-word matching against clean
// game titles. Rom dump filenames carry region tags ("(USA,
// Europe, Korea)"), translation markers ("[T Eng1.0_*]"),
// dump-quality flags ("[!]") and worse — none of which appear in
// SGDB's index, so the query 404s. Strip parenthetical and bracket
// content + collapse whitespace before sending.
std::string sanitize_query(std::string_view raw) {
    std::string s;
    s.reserve(raw.size());
    int paren_depth = 0;
    int brack_depth = 0;
    for (char c : raw) {
        if      (c == '(') ++paren_depth;
        else if (c == ')') { if (paren_depth > 0) --paren_depth; }
        else if (c == '[') ++brack_depth;
        else if (c == ']') { if (brack_depth > 0) --brack_depth; }
        else if (paren_depth == 0 && brack_depth == 0) {
            // Replace underscores with spaces — common in dump names
            // that swap colons for underscores (FAT32 filename
            // sanitisation).
            s += (c == '_') ? ' ' : c;
        }
    }
    // Collapse runs of whitespace + trim.
    std::string out;
    out.reserve(s.size());
    bool last_space = true;  // suppress leading
    for (char c : s) {
        const bool is_space = (c == ' ' || c == '\t');
        if (is_space) {
            if (!last_space) out += ' ';
            last_space = true;
        } else {
            out += c;
            last_space = false;
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '-')) out.pop_back();
    while (!out.empty() && (out.front() == ' ' || out.front() == '-')) out.erase(out.begin());
    return out;
}

long long search_game_id(const std::string& query, const std::string& key) {
    const auto url = "https://www.steamgriddb.com/api/v2/search/autocomplete/"
                   + foyer::net::url_encode(query);
    const auto resp = foyer::net::get(url, auth_headers(key));
    if (resp.code != 200 || resp.body.empty()) return 0;

    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return 0;
    long long out = 0;
    if (auto* root = yyjson_doc_get_root(doc)) {
        if (auto* data = yyjson_obj_get(root, "data");
            data && yyjson_is_arr(data) && yyjson_arr_size(data) > 0) {
            auto* first = yyjson_arr_get(data, 0);
            if (auto* id = yyjson_obj_get(first, "id"); id && yyjson_is_int(id)) {
                out = yyjson_get_sint(id);
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

std::string first_grid_url(long long game_id, const std::string& key) {
    const auto url = "https://www.steamgriddb.com/api/v2/grids/game/"
                   + std::to_string(game_id);
    const auto resp = foyer::net::get(url, auth_headers(key));
    if (resp.code != 200 || resp.body.empty()) return {};

    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return {};
    std::string out;
    if (auto* root = yyjson_doc_get_root(doc)) {
        if (auto* data = yyjson_obj_get(root, "data");
            data && yyjson_is_arr(data) && yyjson_arr_size(data) > 0) {
            auto* first = yyjson_arr_get(data, 0);
            if (auto* u = yyjson_obj_get(first, "url"); u && yyjson_is_str(u)) {
                out = yyjson_get_str(u);
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

// Fetch up to `limit` grid URLs for the game id. Used by the
// interactive cover-picker flow — grabs N candidates so the user can
// scroll through previews and pick one. SteamGridDB's API returns
// the most-popular grids first.
std::vector<std::string> grid_urls(long long game_id, const std::string& key,
                                   std::size_t limit) {
    std::vector<std::string> out;
    const auto url = "https://www.steamgriddb.com/api/v2/grids/game/"
                   + std::to_string(game_id);
    const auto resp = foyer::net::get(url, auth_headers(key));
    if (resp.code != 200 || resp.body.empty()) return out;

    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return out;
    if (auto* root = yyjson_doc_get_root(doc)) {
        if (auto* data = yyjson_obj_get(root, "data"); data && yyjson_is_arr(data)) {
            std::size_t i, n; yyjson_val* item;
            yyjson_arr_foreach(data, i, n, item) {
                if (out.size() >= limit) break;
                if (auto* u = yyjson_obj_get(item, "url"); u && yyjson_is_str(u)) {
                    out.emplace_back(yyjson_get_str(u));
                }
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

} // namespace

bool fetch_cover(std::string_view system_folder,
                 std::string_view rom_stem,
                 const std::string& dest_png) {
    const auto& a = foyer::scrapers::accounts().steamgriddb;
    if (!a.ready()) {
        foyer::log::write("[sgdb] api_key not set in accounts.jsonc\n");
        return false;
    }

    const std::string query = sanitize_query(rom_stem);
    long long id = search_game_id(query, a.api_key);
    if (id == 0 && query != std::string{rom_stem}) {
        // Sanitised query missed — fall back to the raw stem in
        // case the rom does carry a tag SGDB happens to know about.
        id = search_game_id(std::string{rom_stem}, a.api_key);
    }
    if (id == 0) {
        foyer::log::write("[sgdb] no game match for '%s' (raw '%.*s')\n",
            query.c_str(), (int)rom_stem.size(), rom_stem.data());
        return false;
    }
    const auto img = first_grid_url(id, a.api_key);
    if (img.empty()) {
        foyer::log::write("[sgdb] no grid for id=%lld\n", id);
        return false;
    }

    foyer::scrapers::ensure_parent_dir(dest_png);
    if (!foyer::net::get_to_file(img, dest_png)) return false;
    foyer::log::write("[sgdb] saved %s (id=%lld)\n", dest_png.c_str(), id);
    (void)system_folder;
    return true;
}

std::vector<std::string> fetch_cover_candidates(
    std::string_view rom_stem,
    const std::string& dest_dir,
    std::size_t limit) {
    std::vector<std::string> out;
    const auto& a = foyer::scrapers::accounts().steamgriddb;
    if (!a.ready()) {
        foyer::log::write("[sgdb] api_key not set in accounts.jsonc\n");
        return out;
    }

    const auto query = sanitize_query(rom_stem);
    long long id = search_game_id(query, a.api_key);
    if (id == 0 && query != std::string{rom_stem}) {
        id = search_game_id(std::string{rom_stem}, a.api_key);
    }
    if (id == 0) {
        foyer::log::write("[sgdb] no game match for '%s' (raw '%.*s')\n",
            query.c_str(), (int)rom_stem.size(), rom_stem.data());
        return out;
    }

    const auto urls = grid_urls(id, a.api_key, limit);
    foyer::scrapers::ensure_parent_dir(dest_dir + "/.placeholder");

    out.reserve(urls.size());
    for (std::size_t i = 0; i < urls.size(); i++) {
        // Save each candidate as <dest_dir>/cand_NN.png — caller hands
        // these paths to the option-picker as image_paths so the user
        // sees thumbnails and picks one.
        char fn[64];
        std::snprintf(fn, sizeof(fn), "/cand_%02zu.png", i);
        const std::string dst = dest_dir + fn;
        if (foyer::net::get_to_file(urls[i], dst)) {
            out.push_back(dst);
        }
    }
    foyer::log::write("[sgdb] fetched %zu candidate(s) for '%.*s' (id=%lld)\n",
        out.size(), (int)rom_stem.size(), rom_stem.data(), id);
    return out;
}

} // namespace foyer::scrapers::steamgriddb
