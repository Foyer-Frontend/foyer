#include "skipped_versions.hpp"
#include "platform/log.hpp"

#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kPath = "/foyer/data/skipped_versions.json";

const char* kind_str(SkipKind k) {
    switch (k) {
        case SkipKind::Foyer:  return "foyer";
        case SkipKind::Core:   return "core";
        case SkipKind::Bezel:  return "bezel";
        case SkipKind::Cheat:  return "cheat";
        case SkipKind::Shader: return "shader";
    }
    return "?";
}

struct Entry {
    std::string kind;
    std::string id;
    std::string version;
};

std::vector<Entry> read_all() {
    std::vector<Entry> out;
    std::ifstream in{kPath};
    if (!in) return out;
    std::stringstream ss; ss << in.rdbuf();
    const auto txt = ss.str();
    if (txt.empty()) return out;

    auto* doc = yyjson_read(txt.data(), txt.size(), 0);
    if (!doc) return out;
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return out; }

    auto* arr = yyjson_obj_get(root, "skipped");
    if (yyjson_is_arr(arr)) {
        std::size_t i, n;
        yyjson_val* v;
        yyjson_arr_foreach(arr, i, n, v) {
            if (!yyjson_is_obj(v)) continue;
            auto* k = yyjson_obj_get(v, "kind");
            auto* d = yyjson_obj_get(v, "id");
            auto* x = yyjson_obj_get(v, "version");
            if (!yyjson_is_str(k) || !yyjson_is_str(d) || !yyjson_is_str(x))
                continue;
            out.push_back({yyjson_get_str(k),
                           yyjson_get_str(d),
                           yyjson_get_str(x)});
        }
    }
    yyjson_doc_free(doc);
    return out;
}

void write_all(const std::vector<Entry>& entries) {
    // Best-effort directory create — kPath lives at /foyer/data which
    // foyer always creates on first launch.
    std::ofstream out{kPath, std::ios::trunc};
    if (!out) {
        foyer::log::write("[skipped] open(%s) for write failed\n", kPath);
        return;
    }
    out << "{\n  \"skipped\": [";
    for (std::size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        if (i) out << ',';
        out << "\n    {\"kind\":\"" << e.kind
            << "\",\"id\":\""       << e.id
            << "\",\"version\":\""  << e.version << "\"}";
    }
    out << "\n  ]\n}\n";
}

} // namespace

bool is_version_skipped(SkipKind kind,
                        std::string_view id,
                        std::string_view version) {
    const std::string ks = kind_str(kind);
    for (const auto& e : read_all()) {
        if (e.kind == ks && e.id == id && e.version == version) return true;
    }
    return false;
}

void skip_version(SkipKind kind,
                  std::string_view id,
                  std::string_view version) {
    auto all = read_all();
    const std::string ks = kind_str(kind);
    for (const auto& e : all) {
        if (e.kind == ks && e.id == id && e.version == version) return;
    }
    all.push_back({ks, std::string{id}, std::string{version}});
    write_all(all);
}

void clear_skips_for(SkipKind kind, std::string_view id) {
    auto all = read_all();
    const std::string ks = kind_str(kind);
    std::erase_if(all, [&](const Entry& e) {
        return e.kind == ks && e.id == id;
    });
    write_all(all);
}

} // namespace foyer::library
