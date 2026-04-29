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

} // namespace

bool fetch_cover(std::string_view system_folder,
                 std::string_view rom_stem,
                 const std::string& dest_png) {
    const auto& a = foyer::scrapers::accounts().steamgriddb;
    if (!a.ready()) {
        foyer::log::write("[sgdb] api_key not set in accounts.jsonc\n");
        return false;
    }

    const std::string query{rom_stem};
    const auto id = search_game_id(query, a.api_key);
    if (id == 0) {
        foyer::log::write("[sgdb] no game match for '%s'\n", query.c_str());
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

} // namespace foyer::scrapers::steamgriddb
