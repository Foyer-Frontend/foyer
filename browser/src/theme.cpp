#include "theme.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <yyjson.h>

#include "platform/log.hpp"

namespace foyer::browser {
namespace {

constexpr const char* kThemesDir = "/foyer/config/themes";

Theme& mutable_theme() {
    static Theme g;
    return g;
}

// Mirrors the JSONC strip used by config.cpp / accounts.cpp. Kept local so
// the theme module stays self-contained.
std::string strip_comments(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool in_str = false, escape = false;
    for (std::size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (in_str) {
            out.push_back(c);
            if (escape)         escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); continue; }
        if (c == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            while (i < in.size() && in[i] != '\n') i++;
            if (i < in.size()) out.push_back(in[i]);
            continue;
        }
        out.push_back(c);
    }
    return out;
}

// Parses "#RRGGBB" or "#RRGGBBAA" (case insensitive) into NVGcolor. Returns
// fallback if the string doesn't parse so a typo doesn't blank the screen.
NVGcolor parse_color(const char* s, NVGcolor fallback) {
    if (!s || s[0] != '#') return fallback;
    const auto len = std::strlen(s);
    if (len != 7 && len != 9) return fallback;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[8];
    for (std::size_t i = 1; i < len; i++) {
        const int h = hex(s[i]);
        if (h < 0) return fallback;
        v[i - 1] = h;
    }
    const auto r = (unsigned char)((v[0] << 4) | v[1]);
    const auto g = (unsigned char)((v[2] << 4) | v[3]);
    const auto b = (unsigned char)((v[4] << 4) | v[5]);
    const auto a = (len == 9) ? (unsigned char)((v[6] << 4) | v[7]) : (unsigned char)0xFF;
    return nvgRGBA(r, g, b, a);
}

void apply_color(yyjson_val* obj, const char* key, NVGcolor& out) {
    auto* v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) out = parse_color(yyjson_get_str(v), out);
}

void apply_float(yyjson_val* obj, const char* key, float& out) {
    auto* v = yyjson_obj_get(obj, key);
    if (!v) return;
    if (yyjson_is_real(v)) out = (float)yyjson_get_real(v);
    else if (yyjson_is_int(v)) out = (float)yyjson_get_int(v);
}

} // namespace

const Theme& theme() { return mutable_theme(); }

void load_theme(std::string_view name) {
    auto& th = mutable_theme();
    th = Theme{}; // reset to defaults

    if (name.empty() || name == "default") {
        foyer::log::write("[theme] using default palette\n");
        return;
    }

    char path[256];
    std::snprintf(path, sizeof(path), "%s/%.*s.jsonc",
        kThemesDir, (int)name.size(), name.data());

    std::ifstream in{path};
    if (!in) {
        foyer::log::write("[theme] missing %s, falling back to default\n", path);
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const auto stripped = strip_comments(ss.str());

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[theme] parse failed: %s\n", path);
        return;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        foyer::log::write("[theme] root not an object: %s\n", path);
        yyjson_doc_free(doc);
        return;
    }

    apply_color(root, "bg",          th.bg);
    apply_color(root, "bg_panel",    th.bg_panel);
    apply_color(root, "bg_panel_hi", th.bg_panel_hi);
    apply_color(root, "accent",      th.accent);
    apply_color(root, "accent_dim",  th.accent_dim);
    apply_color(root, "text_strong", th.text_strong);
    apply_color(root, "text",        th.text);
    apply_color(root, "text_dim",    th.text_dim);
    apply_color(root, "border",      th.border);

    apply_float(root, "pad",        th.pad);
    apply_float(root, "radius",     th.radius);
    apply_float(root, "title_size", th.title_size);
    apply_float(root, "head_size",  th.head_size);
    apply_float(root, "body_size",  th.body_size);
    apply_float(root, "label_size", th.label_size);

    yyjson_doc_free(doc);
    foyer::log::write("[theme] loaded %s\n", path);
}

std::vector<std::string> list_themes() {
    std::vector<std::string> out;
    out.emplace_back("default");

    auto* dir = ::opendir(kThemesDir);
    if (!dir) return out;
    while (auto* e = ::readdir(dir)) {
        if (!e->d_name[0] || e->d_name[0] == '.') continue;
        std::string n{e->d_name};
        const auto dot = n.rfind(".jsonc");
        if (dot == std::string::npos || dot + 6 != n.size()) continue;
        n.erase(dot);
        if (n != "default") out.push_back(std::move(n));
    }
    ::closedir(dir);
    std::sort(out.begin() + 1, out.end());
    return out;
}

} // namespace foyer::browser
