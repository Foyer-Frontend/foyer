#include "session.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

#include <yyjson.h>

namespace foyer::browser {
namespace {

constexpr const char* kSessionPath = "/foyer/data/session.json";

const char* view_to_str(View v) {
    switch (v) {
        case View::Home:       return "home";
        case View::System:     return "system";
        case View::GameDetail: return "detail";
        case View::Settings:   return "settings";
        case View::Search:     return "search";
    }
    return "home";
}

View view_from_str(const char* s) {
    if (!s) return View::Home;
    if (!std::strcmp(s, "system"))   return View::System;
    if (!std::strcmp(s, "detail"))   return View::GameDetail;
    if (!std::strcmp(s, "settings")) return View::Settings;
    if (!std::strcmp(s, "search"))   return View::Search;
    return View::Home;
}

} // namespace

void save_session(const State& s) {
    std::ofstream out{kSessionPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[session] open(%s) for write failed\n", kSessionPath);
        return;
    }
    out << "{";
    out << "\"v\":1,";
    out << "\"saved_at\":" << (long)std::time(nullptr) << ",";
    out << "\"view\":\"" << view_to_str(s.view) << "\",";
    out << "\"system_index\":" << s.system_index << ",";
    out << "\"game_index\":"   << s.game_index   << ",";
    out << "\"detail_core_index\":" << s.detail_core_index;
    out << "}\n";
}

void load_and_consume_session(State& s, bool restore) {
    std::ifstream in{kSessionPath};
    if (!in) return; // no prior session

    if (!restore) {
        // Cold launch via hbmenu — the marker token isn't present,
        // so we discard whatever's on disk without restoring.
        in.close();
        ::unlink(kSessionPath);
        return;
    }

    std::stringstream ss; ss << in.rdbuf();
    in.close();
    ::unlink(kSessionPath);  // one-shot consume

    const auto txt = ss.str();
    if (txt.empty()) return;

    auto* doc = yyjson_read(txt.data(), txt.size(), 0);
    if (!doc) return;
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return; }

    auto pull_int = [&](const char* k) -> std::int64_t {
        auto* v = yyjson_obj_get(root, k);
        return (v && (yyjson_is_int(v) || yyjson_is_uint(v)))
            ? yyjson_get_int(v) : 0;
    };
    auto pull_str = [&](const char* k) -> const char* {
        auto* v = yyjson_obj_get(root, k);
        return (v && yyjson_is_str(v)) ? yyjson_get_str(v) : nullptr;
    };

    s.view              = view_from_str(pull_str("view"));
    s.system_index      = (std::size_t)pull_int("system_index");
    s.game_index        = (std::size_t)pull_int("game_index");
    s.detail_core_index = (std::size_t)pull_int("detail_core_index");
    yyjson_doc_free(doc);

    foyer::log::write(
        "[session] restored view=%s sys=%zu game=%zu\n",
        view_to_str(s.view), s.system_index, s.game_index);
}

bool argv_has_resume_marker(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;
        const char* a = argv[i];
        // Accept the token both with and without surrounding quotes,
        // since hbloader sometimes preserves them.
        if (!std::strcmp(a, "foyer-resume"))     return true;
        if (!std::strcmp(a, "\"foyer-resume\"")) return true;
    }
    return false;
}

} // namespace foyer::browser
