#include "libretro_index.hpp"
#include "cache.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

namespace foyer::scrapers::libretro_thumb {
namespace {

// v2/ — the v1 cache stored truncated /contents/ responses from
// before we switched to the recursive trees endpoint. Bumping the
// dir invalidates those automatically.
constexpr const char* kCacheDir   = "/foyer/data/cache/libretro_index/v2";
constexpr long        kTtlSeconds = 30LL * 24 * 60 * 60;  // 30 days

// In-process LRU. Avoids re-reading + re-parsing the per-system
// index JSON every time the scraper hits the same system in a
// scrape run. Keyed by "<db>|<category>".
std::mutex                                                   g_cache_mu;
std::unordered_map<std::string, std::vector<std::string>>    g_cache;

// Drop everything in (...) and [...], collapse whitespace, lower-case,
// strip non-alphanumeric. Tight normalization so polluted dump names
// hit canonical entries even when neither side bracket-tags the same
// way.
std::string normalize(std::string_view in) {
    std::string s;
    s.reserve(in.size());
    int p = 0, b = 0;
    for (char c : in) {
        if      (c == '(') ++p;
        else if (c == ')') { if (p > 0) --p; }
        else if (c == '[') ++b;
        else if (c == ']') { if (b > 0) --b; }
        else if (p == 0 && b == 0) {
            const char l = (char)std::tolower((unsigned char)c);
            if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
                s += l;
            }
            // ignore everything else (spaces, punctuation, underscores)
        }
    }
    return s;
}

// Region tier: lower number = preferred. Used when multiple
// canonical entries share the same normalized form (e.g.
// "Game (USA).png" + "Game (Europe).png" both normalize to "game").
int region_score(const std::string& canonical) {
    auto contains = [&](const char* needle) {
        std::string lower = canonical;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return lower.find(needle) != std::string::npos;
    };
    if (contains("(usa")              || contains("(us)"))    return 0;
    if (contains("(world)"))                                  return 1;
    if (contains("(europe")           || contains("(eu)"))    return 2;
    if (contains("(japan")            || contains("(jp)"))    return 3;
    return 4;
}

std::string repo_name(std::string_view db) {
    std::string out{db};
    for (auto& c : out) if (c == ' ') c = '_';
    return out;
}

std::string cache_path(std::string_view db) {
    std::string slug{db};
    for (auto& c : slug) if (c == ' ' || c == '/' || c == '\\') c = '_';
    return std::string(kCacheDir) + "/" + slug + ".json";
}

bool fresh(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    return (std::time(nullptr) - st.st_mtime) < kTtlSeconds;
}

// Fetch the full repo tree from GitHub:
//   https://api.github.com/repos/libretro-thumbnails/<repo>/git/trees/master?recursive=1
// The recursive trees endpoint returns every path in one call (up
// to ~7 MB). The /contents/ endpoint we used earlier caps at 1000
// entries per directory which silently truncated big systems
// (NES > 1000 boxarts) — the recursive-tree shape is the only
// way to get a full index in a single round-trip.
std::vector<char> fetch_remote(const std::string& db) {
    const std::string url =
        "https://api.github.com/repos/libretro-thumbnails/"
      + repo_name(db) + "/git/trees/master?recursive=1";
    auto resp = foyer::net::get(url, {
        "User-Agent: foyer-launcher",
        "Accept: application/vnd.github+json",
    });
    if (resp.code != 200 || resp.body.empty()) {
        foyer::log::write(
            "[lr-index] fetch %s -> code=%ld body=%zu\n",
            url.c_str(), resp.code, resp.body.size());
        return {};
    }
    return std::move(resp.body);
}

// Parse GitHub /git/trees/ JSON.
//   { "tree": [{ "path": "Named_Boxarts/Foo.png", "type": "blob", ... }, ...],
//     "truncated": false }
// Filter to the requested category subtree, strip prefix + ".png".
std::vector<std::string> parse_tree(const std::vector<char>& body,
                                    const char* category) {
    std::vector<std::string> out;
    auto* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) return out;
    auto* root = yyjson_doc_get_root(doc);
    if (root && yyjson_is_obj(root)) {
        if (auto* trunc = yyjson_obj_get(root, "truncated");
            trunc && yyjson_is_true(trunc)) {
            foyer::log::write(
                "[lr-index] WARN: trees response was truncated\n");
        }
        if (auto* tree = yyjson_obj_get(root, "tree");
            tree && yyjson_is_arr(tree)) {
            const std::string prefix = std::string(category) + "/";
            std::size_t i, n; yyjson_val* item;
            yyjson_arr_foreach(tree, i, n, item) {
                auto* path = yyjson_obj_get(item, "path");
                if (!path || !yyjson_is_str(path)) continue;
                std::string p = yyjson_get_str(path);
                if (p.size() <= prefix.size() + 4) continue;
                if (p.compare(0, prefix.size(), prefix) != 0) continue;
                if (p.substr(p.size() - 4) != ".png") continue;
                out.emplace_back(p.substr(prefix.size(),
                                          p.size() - prefix.size() - 4));
            }
        }
    }
    yyjson_doc_free(doc);
    return out;
}

void write_cache(const std::string& path, const std::vector<char>& body) {
    foyer::scrapers::ensure_parent_dir(path);
    if (auto* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
    }
}

std::vector<char> read_cache(const std::string& path) {
    std::vector<char> out;
    if (auto* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        const long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            out.resize((std::size_t)n);
            std::fread(out.data(), 1, (std::size_t)n, f);
        }
        std::fclose(f);
    }
    return out;
}

const std::vector<std::string>& load_index(std::string_view db,
                                           const char* category) {
    const std::string key = std::string{db} + "|" + category;
    {
        std::lock_guard lk{g_cache_mu};
        auto it = g_cache.find(key);
        if (it != g_cache.end()) return it->second;
    }

    // The tree JSON covers every category in the repo — fetch +
    // cache once per db, partition by category in memory.
    const auto path = cache_path(db);
    std::vector<char> body;
    if (fresh(path)) {
        body = read_cache(path);
    }
    if (body.empty()) {
        body = fetch_remote(std::string{db});
        if (!body.empty()) write_cache(path, body);
    }

    auto names = parse_tree(body, category);
    foyer::log::write(
        "[lr-index] %s/%s: %zu canonical entries\n",
        std::string{db}.c_str(), category, names.size());

    std::lock_guard lk{g_cache_mu};
    auto [it, _] = g_cache.emplace(key, std::move(names));
    return it->second;
}

}  // namespace

std::string find_match(std::string_view db,
                       std::string_view stem,
                       const char* category) {
    const auto& index = load_index(db, category);
    if (index.empty()) return {};

    const std::string norm_stem = normalize(stem);
    if (norm_stem.empty()) return {};

    // Pass 1: exact normalized match. Prefer the canonical with
    // the best region score when several entries collapse to the
    // same normalized form.
    std::string best;
    int         best_score = 99;
    for (const auto& canon : index) {
        if (normalize(canon) == norm_stem) {
            const int s = region_score(canon);
            if (s < best_score) {
                best       = canon;
                best_score = s;
            }
        }
    }
    if (!best.empty()) return best;

    // Pass 2: substring. Either side wholly contains the other.
    // Catches "Final Fantasy IV (Easy Type)" → "Final Fantasy IV"
    // and similar.
    for (const auto& canon : index) {
        const auto cn = normalize(canon);
        if (cn.find(norm_stem) != std::string::npos
            || norm_stem.find(cn) != std::string::npos) {
            return canon;
        }
    }
    return {};
}

}  // namespace foyer::scrapers::libretro_thumb
