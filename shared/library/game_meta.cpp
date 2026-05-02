#include "game_meta.hpp"
#include "scrapers/cache.hpp"
#include "platform/log.hpp"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <yyjson.h>

namespace foyer::library {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream in{path};
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void set_field(std::string& dst, yyjson_val* obj, const char* key) {
    auto* v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) dst = yyjson_get_str(v);
}

void emit_field(yyjson_mut_doc* doc, yyjson_mut_val* root,
                const char* key, const std::string& v) {
    if (v.empty()) return;
    yyjson_mut_obj_add_strncpy(doc, root, key, v.data(), v.size());
}

} // namespace

GameMeta load_meta(std::string_view sys, std::string_view stem) {
    GameMeta m;
    const auto path = scrapers::metadata_path(sys, stem);
    const auto body = read_file(path);
    if (body.empty()) return m;

    auto* doc = yyjson_read(body.data(), body.size(), 0);
    if (!doc) {
        foyer::log::write("[meta] parse error in %s\n", path.c_str());
        return m;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        set_field(m.title,       root, "title");
        set_field(m.year,        root, "year");
        set_field(m.publisher,   root, "publisher");
        set_field(m.developer,   root, "developer");
        set_field(m.genre,       root, "genre");
        set_field(m.players,     root, "players");
        set_field(m.rating,      root, "rating");
        set_field(m.description, root, "description");

        if (auto* v = yyjson_obj_get(root, "cheevos_total"); v && yyjson_is_int(v))
            m.cheevos_total = (int)yyjson_get_int(v);
        if (auto* v = yyjson_obj_get(root, "cheevos_unlocked"); v && yyjson_is_int(v))
            m.cheevos_unlocked = (int)yyjson_get_int(v);
    }
    yyjson_doc_free(doc);
    return m;
}

bool save_meta(std::string_view sys, std::string_view stem, const GameMeta& m) {
    const auto path = scrapers::metadata_path(sys, stem);
    scrapers::ensure_parent_dir(path);

    auto* doc  = yyjson_mut_doc_new(nullptr);
    auto* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    emit_field(doc, root, "title",       m.title);
    emit_field(doc, root, "year",        m.year);
    emit_field(doc, root, "publisher",   m.publisher);
    emit_field(doc, root, "developer",   m.developer);
    emit_field(doc, root, "genre",       m.genre);
    emit_field(doc, root, "players",     m.players);
    emit_field(doc, root, "rating",      m.rating);
    emit_field(doc, root, "description", m.description);

    if (m.cheevos_total >= 0)
        yyjson_mut_obj_add_int(doc, root, "cheevos_total", m.cheevos_total);
    if (m.cheevos_unlocked >= 0)
        yyjson_mut_obj_add_int(doc, root, "cheevos_unlocked", m.cheevos_unlocked);

    std::size_t len = 0;
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, &len);
    bool ok = false;
    if (json) {
        std::ofstream out{path, std::ios::trunc};
        if (out) {
            out.write(json, (std::streamsize)len);
            ok = (bool)out;
        }
        std::free(json);
    }
    yyjson_mut_doc_free(doc);
    if (!ok) {
        foyer::log::write("[meta] could not write %s\n", path.c_str());
    }
    return ok;
}

bool meta_exists(std::string_view sys, std::string_view stem) {
    struct stat st{};
    return ::stat(scrapers::metadata_path(sys, stem).c_str(), &st) == 0;
}

} // namespace foyer::library
