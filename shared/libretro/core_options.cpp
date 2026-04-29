#include "core_options.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include <yyjson.h>

namespace foyer::libretro {
namespace {

std::string strip_comments(const std::string& in) {
    std::string out; out.reserve(in.size());
    bool in_str = false, escape = false;
    for (std::size_t i = 0; i < in.size(); i++) {
        const char c = in[i];
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

// Parse a legacy retro_variable->value string of the form
// "Description; first|second|third" — first choice is also the default.
void parse_legacy_value(const char* v, CoreOption& out) {
    if (!v) return;
    const char* sep = std::strstr(v, "; ");
    std::string desc;
    std::string choices;
    if (sep) {
        desc.assign(v, sep - v);
        choices = sep + 2;
    } else {
        choices = v;
    }
    out.desc = desc;
    std::size_t cursor = 0;
    while (cursor < choices.size()) {
        const auto bar = choices.find('|', cursor);
        const auto end = (bar == std::string::npos) ? choices.size() : bar;
        out.choices.emplace_back(choices.substr(cursor, end - cursor));
        if (bar == std::string::npos) break;
        cursor = bar + 1;
    }
    if (!out.choices.empty()) {
        out.default_value = out.choices.front();
        out.value         = out.default_value;
    }
}

std::string config_path_for(const std::string& core_name) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "/foyer/config/cores/%s.jsonc",
        core_name.empty() ? "unknown" : core_name.c_str());
    return std::string{buf};
}

} // namespace

CoreOptions& CoreOptions::instance() {
    static CoreOptions s;
    return s;
}

void CoreOptions::set_core_name(std::string_view name) {
    m_core_name.assign(name);
}

void CoreOptions::ingest_legacy(const retro_variable* vars) {
    if (!vars) return;
    for (auto* v = vars; v->key; v++) {
        CoreOption opt;
        opt.key = v->key;
        parse_legacy_value(v->value, opt);
        m_opts.push_back(std::move(opt));
    }
    load_overrides_from_disk();
    m_dirty = true;
}

void CoreOptions::ingest_v1(const retro_core_option_definition* defs) {
    if (!defs) return;
    for (auto* d = defs; d->key; d++) {
        CoreOption opt;
        opt.key  = d->key;
        opt.desc = d->desc ? d->desc : "";
        for (const auto& v : d->values) {
            if (!v.value) break;
            opt.choices.emplace_back(v.value);
        }
        opt.default_value = d->default_value
            ? d->default_value
            : (opt.choices.empty() ? "" : opt.choices.front());
        opt.value = opt.default_value;
        m_opts.push_back(std::move(opt));
    }
    load_overrides_from_disk();
    m_dirty = true;
}

void CoreOptions::ingest_v2(const retro_core_options_v2* opts) {
    if (!opts || !opts->definitions) return;
    for (auto* d = opts->definitions; d->key; d++) {
        CoreOption opt;
        opt.key  = d->key;
        opt.desc = d->desc ? d->desc : "";
        for (const auto& v : d->values) {
            if (!v.value) break;
            opt.choices.emplace_back(v.value);
        }
        opt.default_value = d->default_value
            ? d->default_value
            : (opt.choices.empty() ? "" : opt.choices.front());
        opt.value = opt.default_value;
        m_opts.push_back(std::move(opt));
    }
    load_overrides_from_disk();
    m_dirty = true;
}

const char* CoreOptions::get(std::string_view key) const {
    for (const auto& o : m_opts) {
        if (o.key == key) return o.value.c_str();
    }
    return nullptr;
}

bool CoreOptions::consume_dirty() {
    const bool d = m_dirty;
    m_dirty = false;
    return d;
}

void CoreOptions::set(std::string_view key, std::string_view value) {
    for (auto& o : m_opts) {
        if (o.key == key) {
            o.value = std::string{value};
            m_dirty = true;
            save_to_disk();
            return;
        }
    }
}

void CoreOptions::load_overrides_from_disk() {
    const auto path = config_path_for(m_core_name);
    std::ifstream in{path};
    if (!in) return;

    std::stringstream ss;
    ss << in.rdbuf();
    const auto stripped = strip_comments(ss.str());

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[core_opts] parse failed: %s\n", path.c_str());
        return;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (root && yyjson_is_obj(root)) {
        std::size_t i, max; yyjson_val *k, *v;
        yyjson_obj_foreach(root, i, max, k, v) {
            if (!yyjson_is_str(k) || !yyjson_is_str(v)) continue;
            const char* kk = yyjson_get_str(k);
            const char* vv = yyjson_get_str(v);
            for (auto& o : m_opts) {
                if (o.key == kk) {
                    // Only honor values that are still in the choice list —
                    // skip stale entries that the core has since dropped.
                    bool found = false;
                    for (const auto& c : o.choices) if (c == vv) { found = true; break; }
                    if (found) o.value = vv;
                    break;
                }
            }
        }
    }
    yyjson_doc_free(doc);
    foyer::log::write("[core_opts] loaded overrides from %s (%zu opts)\n",
        path.c_str(), m_opts.size());
}

void CoreOptions::save_to_disk() const {
    const auto path = config_path_for(m_core_name);
    std::ofstream out{path, std::ios::trunc};
    if (!out) {
        foyer::log::write("[core_opts] could not write %s\n", path.c_str());
        return;
    }
    out << "// foyer per-core libretro variables. Edited by the pause overlay.\n";
    out << "{\n";
    bool first = true;
    for (const auto& o : m_opts) {
        if (o.value == o.default_value) continue;  // omit defaults to keep file tidy
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << o.key << "\": \"" << o.value << "\"";
    }
    out << (first ? "" : "\n") << "}\n";
}

} // namespace foyer::libretro
