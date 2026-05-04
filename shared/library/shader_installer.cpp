#include "shader_installer.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "scrapers/cache.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kShadersDir = "/foyer/shaders";

void set_field(std::string& dst, yyjson_val* obj, const char* key) {
    auto* v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) dst = yyjson_get_str(v);
}

void mkdir_p(const std::string& path) {
    std::string cur;
    for (auto c : path) {
        cur.push_back(c);
        if (c == '/' && cur.size() > 1) {
            ::mkdir(cur.c_str(), 0755);
        }
    }
    ::mkdir(path.c_str(), 0755);
}

void rm_rf(const std::string& path) {
    // Cheap recursive delete via libarchive's siblings — but we only
    // need it for our own preset directories. A small custom impl
    // keeps the dependency surface minimal.
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        // Walk the directory.
        char cmd[1024];
        std::snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path.c_str());
        // Devkit's posix has system(), but newlib's may not. Fall back
        // to manual removal: opendir + recurse + unlink.
        // Simpler: just unlink everything we know about. For shader
        // preset dirs we wrote ourselves, the layout is:
        //   /foyer/shaders/<name>/preset.json
        //   /foyer/shaders/<name>/pass-N.glsl
        //   /foyer/shaders/<name>/*.png
        //   /foyer/shaders/<name>/.version
        // Use opendir to enumerate.
        if (auto* d = ::opendir(path.c_str())) {
            struct dirent* e;
            while ((e = ::readdir(d))) {
                if (!std::strcmp(e->d_name, ".")  ||
                    !std::strcmp(e->d_name, "..")) continue;
                const std::string sub = path + "/" + e->d_name;
                struct stat sst{};
                if (::stat(sub.c_str(), &sst) == 0) {
                    if (S_ISDIR(sst.st_mode)) rm_rf(sub);
                    else                      ::unlink(sub.c_str());
                }
            }
            ::closedir(d);
        }
        ::rmdir(path.c_str());
    } else {
        ::unlink(path.c_str());
    }
}

bool extract_zip(const std::string& zip_path, const std::string& out_dir) {
    auto* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, zip_path.c_str(), 64 * 1024) != ARCHIVE_OK) {
        foyer::log::write("[shader_install] open %s failed: %s\n",
            zip_path.c_str(), archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool ok = true;
    archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (!name || !*name) { archive_read_data_skip(a); continue; }
        // Reject "../" traversal — paranoia even though our zips are
        // produced by our own CI.
        if (std::strstr(name, "..")) { archive_read_data_skip(a); continue; }

        std::string out_path = out_dir + "/" + name;
        if (S_ISDIR(archive_entry_mode(entry)) ||
            (name[std::strlen(name) - 1] == '/')) {
            mkdir_p(out_path);
            continue;
        }
        // Make sure parent dir exists.
        const auto last_slash = out_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            mkdir_p(out_path.substr(0, last_slash));
        }

        auto* fp = std::fopen(out_path.c_str(), "wb");
        if (!fp) {
            foyer::log::write("[shader_install] open(%s) for write failed\n",
                out_path.c_str());
            archive_read_data_skip(a);
            ok = false;
            continue;
        }
        char buf[16 * 1024];
        for (;;) {
            la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
            if (n == 0) break;
            if (n < 0)  { ok = false; break; }
            std::fwrite(buf, 1, (std::size_t)n, fp);
        }
        std::fclose(fp);
    }
    archive_read_close(a);
    archive_read_free(a);
    return ok;
}

std::string preset_dir(std::string_view name) {
    return std::string{kShadersDir} + "/" + std::string{name};
}

std::string sidecar_path(std::string_view name) {
    return preset_dir(name) + "/.version";
}

bool dir_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void write_sidecar(std::string_view name, const std::string& version) {
    if (version.empty()) return;
    const auto p = sidecar_path(name);
    if (auto* f = std::fopen(p.c_str(), "wb")) {
        std::fwrite(version.data(), 1, version.size(), f);
        std::fclose(f);
    }
}

} // namespace

ShaderManifest fetch_shader_manifest(const std::string& url) {
    ShaderManifest m;
    auto resp = foyer::net::get(url);
    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        foyer::log::write("[shader_install] manifest fetch failed: code=%ld\n",
            resp.code);
        return m;
    }
    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return m;
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return m; }

    set_field(m.version, root, "version");
    if (auto* arr = yyjson_obj_get(root, "presets");
        arr && yyjson_is_arr(arr)) {
        std::size_t i, max; yyjson_val* item;
        yyjson_arr_foreach(arr, i, max, item) {
            if (!yyjson_is_obj(item)) continue;
            ShaderManifestEntry e;
            set_field(e.name,    item, "name");
            set_field(e.version, item, "version");
            set_field(e.zip,     item, "zip");
            set_field(e.sha256,  item, "sha256");
            set_field(e.url,     item, "url");
            if (auto* sv = yyjson_obj_get(item, "size");
                sv && yyjson_is_int(sv))
                e.size = (std::size_t)yyjson_get_int(sv);
            if (e.name.empty() || e.url.empty() || e.zip.empty()) continue;
            m.presets.push_back(std::move(e));
        }
    }
    yyjson_doc_free(doc);
    return m;
}

std::string installed_shader_version(std::string_view preset_name) {
    const auto path = sidecar_path(preset_name);
    auto* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    char buf[64];
    auto n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        n--;
    return std::string(buf, n);
}

ShaderInstallTotals install_shaders(
    const ShaderManifest& manifest,
    std::function<void(const ShaderInstallProgress&)> progress,
    bool force,
    foyer::net::CancelHook cancel) {
    ShaderInstallTotals out;
    mkdir_p(kShadersDir);

    int idx = 0;
    for (const auto& p : manifest.presets) {
        if (cancel && cancel()) break;
        idx++;
        ShaderInstallProgress prog;
        prog.index = idx;
        prog.total = (int)manifest.presets.size();
        prog.name  = p.name;

        const auto dir = preset_dir(p.name);
        const bool was_present = dir_exists(dir);

        // Skip when already installed at the manifest's version.
        if (was_present && !force) {
            const auto ins = installed_shader_version(p.name);
            if (!ins.empty() && ins == p.version) {
                prog.action = ShaderInstallAction::Skipped;
                out.skipped++;
                if (progress) progress(prog);
                continue;
            }
        }

        // Download zip to a sibling .tmp under the shaders dir.
        const std::string zip_path = dir + ".zip.tmp";
        const bool dl_ok = foyer::net::get_to_file(p.url, zip_path, {}, cancel);
        if (!dl_ok) {
            prog.action = ShaderInstallAction::Failed;
            out.failed++;
            foyer::log::write("[shader_install] download failed: %s\n",
                p.url.c_str());
            if (progress) progress(prog);
            continue;
        }

        // Wipe the old preset dir before extracting (avoids leftover
        // files from a previous version that the new zip doesn't have).
        if (was_present) rm_rf(dir);
        mkdir_p(dir);

        const bool xt_ok = extract_zip(zip_path, dir);
        ::unlink(zip_path.c_str());

        if (!xt_ok) {
            prog.action = ShaderInstallAction::Failed;
            out.failed++;
            foyer::log::write("[shader_install] extract failed: %s\n",
                p.name.c_str());
            if (progress) progress(prog);
            continue;
        }

        write_sidecar(p.name, p.version);
        prog.action = was_present ? ShaderInstallAction::Updated
                                  : ShaderInstallAction::Installed;
        (was_present ? out.updated : out.installed)++;
        foyer::log::write("[shader_install] %s %s v%s\n",
            was_present ? "updated" : "installed",
            p.name.c_str(), p.version.c_str());
        if (progress) progress(prog);
    }
    return out;
}

} // namespace foyer::library
