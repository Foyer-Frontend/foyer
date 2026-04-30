#include "accounts.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

#include <yyjson.h>

namespace foyer::scrapers {
namespace {

constexpr const char* kPath = "/foyer/config/accounts.jsonc";

constexpr const char* kStub =
    "// foyer scraper credentials.\n"
    "// Fill in the keys you want to use; leave others blank.\n"
    "{\n"
    "    \"screenscraper\": {\n"
    "        \"devid\":       \"\",\n"
    "        \"devpassword\": \"\",\n"
    "        \"ssid\":        \"\",\n"
    "        \"sspassword\":  \"\"\n"
    "    },\n"
    "    \"steamgriddb\": {\n"
    "        \"api_key\": \"\"\n"
    "    },\n"
    "    \"retroachievements\": {\n"
    "        \"user\":  \"\",\n"
    "        \"token\": \"\"\n"
    "    }\n"
    "}\n";

std::mutex          g_mutex;
Accounts            g_accounts{};
std::atomic<bool>   g_loaded{false};

// Strip C-style // line comments. Keeps strings intact (won't strip "//"
// inside quotes). yyjson otherwise rejects JSONC.
std::string strip_jsonc_comments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_str = false;
    bool escape = false;
    for (std::size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (in_str) {
            out.push_back(c);
            if (escape)            escape = false;
            else if (c == '\\')    escape = true;
            else if (c == '"')     in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') i++;
            if (i < in.size()) out.push_back(in[i]); // keep '\n'
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string get_str(yyjson_val* obj, const char* key) {
    if (!obj) return {};
    auto* v = yyjson_obj_get(obj, key);
    if (!v || !yyjson_is_str(v)) return {};
    return std::string{yyjson_get_str(v)};
}

void write_stub_if_missing() {
    std::ifstream in{kPath};
    if (in) return;
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[accounts] could not create %s\n", kPath);
        return;
    }
    out.write(kStub, (std::streamsize)std::strlen(kStub));
    foyer::log::write("[accounts] wrote stub to %s\n", kPath);
}

void load_locked() {
    g_accounts = Accounts{};

    std::ifstream in{kPath};
    if (!in) {
        write_stub_if_missing();
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto stripped = strip_jsonc_comments(ss.str());

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[accounts] failed to parse %s as JSON\n", kPath);
        return;
    }
    auto* root = yyjson_doc_get_root(doc);

    if (auto* ss_obj = yyjson_obj_get(root, "screenscraper")) {
        g_accounts.screenscraper.devid       = get_str(ss_obj, "devid");
        g_accounts.screenscraper.devpassword = get_str(ss_obj, "devpassword");
        g_accounts.screenscraper.ssid        = get_str(ss_obj, "ssid");
        g_accounts.screenscraper.sspassword  = get_str(ss_obj, "sspassword");
    }
    if (auto* sg_obj = yyjson_obj_get(root, "steamgriddb")) {
        g_accounts.steamgriddb.api_key = get_str(sg_obj, "api_key");
    }
    if (auto* ra_obj = yyjson_obj_get(root, "retroachievements")) {
        g_accounts.retroachievements.user  = get_str(ra_obj, "user");
        g_accounts.retroachievements.token = get_str(ra_obj, "token");
    }
    yyjson_doc_free(doc);

    foyer::log::write("[accounts] loaded — ss=%d sg=%d ra=%d\n",
        (int)g_accounts.screenscraper.ready(),
        (int)g_accounts.steamgriddb.ready(),
        (int)g_accounts.retroachievements.ready());
}

} // namespace

const Accounts& accounts() {
    if (!g_loaded.load()) {
        std::scoped_lock lk{g_mutex};
        if (!g_loaded.load()) {
            load_locked();
            g_loaded = true;
        }
    }
    return g_accounts;
}

void reload_accounts() {
    std::scoped_lock lk{g_mutex};
    load_locked();
    g_loaded = true;
}

namespace {

// Re-emit the JSONC file from g_accounts. Keeps section ordering stable.
void write_locked() {
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[accounts] could not write %s\n", kPath);
        return;
    }
    out << "// foyer scraper credentials.\n";
    out << "{\n";
    out << "    \"screenscraper\": {\n";
    out << "        \"devid\":       \"" << g_accounts.screenscraper.devid << "\",\n";
    out << "        \"devpassword\": \"" << g_accounts.screenscraper.devpassword << "\",\n";
    out << "        \"ssid\":        \"" << g_accounts.screenscraper.ssid << "\",\n";
    out << "        \"sspassword\":  \"" << g_accounts.screenscraper.sspassword << "\"\n";
    out << "    },\n";
    out << "    \"steamgriddb\": {\n";
    out << "        \"api_key\": \"" << g_accounts.steamgriddb.api_key << "\"\n";
    out << "    },\n";
    out << "    \"retroachievements\": {\n";
    out << "        \"user\":  \"" << g_accounts.retroachievements.user << "\",\n";
    out << "        \"token\": \"" << g_accounts.retroachievements.token << "\"\n";
    out << "    }\n";
    out << "}\n";
}

} // namespace

void set_account_field(std::string_view path, std::string_view value) {
    std::scoped_lock lk{g_mutex};
    if (!g_loaded.load()) { load_locked(); g_loaded = true; }
    const std::string v{value};
    if      (path == "screenscraper.devid")       g_accounts.screenscraper.devid       = v;
    else if (path == "screenscraper.devpassword") g_accounts.screenscraper.devpassword = v;
    else if (path == "screenscraper.ssid")        g_accounts.screenscraper.ssid        = v;
    else if (path == "screenscraper.sspassword")  g_accounts.screenscraper.sspassword  = v;
    else if (path == "steamgriddb.api_key")       g_accounts.steamgriddb.api_key       = v;
    else if (path == "retroachievements.user")    g_accounts.retroachievements.user    = v;
    else if (path == "retroachievements.token")   g_accounts.retroachievements.token   = v;
    else return;
    write_locked();
}

} // namespace foyer::scrapers
