#include "cheat_installer.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

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

constexpr const char* kCheatsDir = "/foyer/cheats";

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
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
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

bool extract_zip(const std::string& zip_path, const std::string& out_root) {
    auto* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, zip_path.c_str(), 64 * 1024) != ARCHIVE_OK) {
        foyer::log::write("[cheat_install] open %s failed: %s\n",
            zip_path.c_str(), archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool ok = true;
    archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (!name || !*name) { archive_read_data_skip(a); continue; }
        if (std::strstr(name, "..")) { archive_read_data_skip(a); continue; }

        std::string out_path = out_root + "/" + name;
        if (S_ISDIR(archive_entry_mode(entry)) ||
            (name[std::strlen(name) - 1] == '/')) {
            mkdir_p(out_path);
            continue;
        }
        const auto last_slash = out_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            mkdir_p(out_path.substr(0, last_slash));
        }

        auto* fp = std::fopen(out_path.c_str(), "wb");
        if (!fp) {
            foyer::log::write("[cheat_install] open(%s) for write failed\n",
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

std::string pack_dir(std::string_view name) {
    return std::string{kCheatsDir} + "/" + std::string{name};
}

std::string sidecar_path(std::string_view name) {
    return pack_dir(name) + "/.version";
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

CheatManifest fetch_cheat_manifest(const std::string& url) {
    CheatManifest m;
    auto resp = foyer::net::get(url);
    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        foyer::log::write("[cheat_install] manifest fetch failed: code=%ld\n",
            resp.code);
        return m;
    }
    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) return m;
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return m; }

    set_field(m.version,  root, "version");
    set_field(m.upstream, root, "upstream");
    if (auto* arr = yyjson_obj_get(root, "packs");
        arr && yyjson_is_arr(arr)) {
        std::size_t i, max; yyjson_val* item;
        yyjson_arr_foreach(arr, i, max, item) {
            if (!yyjson_is_obj(item)) continue;
            CheatManifestEntry e;
            set_field(e.name,    item, "name");
            set_field(e.version, item, "version");
            set_field(e.zip,     item, "zip");
            set_field(e.sha256,  item, "sha256");
            set_field(e.url,     item, "url");
            if (auto* sv = yyjson_obj_get(item, "size");
                sv && yyjson_is_int(sv))
                e.size = (std::size_t)yyjson_get_int(sv);
            if (e.name.empty() || e.url.empty() || e.zip.empty()) continue;
            m.packs.push_back(std::move(e));
        }
    }
    yyjson_doc_free(doc);
    return m;
}

std::string installed_cheat_version(std::string_view pack_name) {
    const auto path = sidecar_path(pack_name);
    auto* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    char buf[64];
    auto n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        n--;
    return std::string(buf, n);
}

CheatInstallTotals install_cheats(
    const CheatManifest& manifest,
    std::function<void(const CheatInstallProgress&)> progress,
    std::string_view only_pack,
    bool force,
    foyer::net::CancelHook cancel) {
    CheatInstallTotals out;
    mkdir_p(kCheatsDir);

    int idx = 0;
    for (const auto& p : manifest.packs) {
        if (cancel && cancel()) break;
        if (!only_pack.empty() && only_pack != p.name) continue;
        idx++;
        CheatInstallProgress prog;
        prog.index = idx;
        prog.total = only_pack.empty() ? (int)manifest.packs.size() : 1;
        prog.name  = p.name;

        const auto dir = pack_dir(p.name);
        const bool was_present = dir_exists(dir);

        if (was_present && !force) {
            const auto ins = installed_cheat_version(p.name);
            if (!ins.empty() && ins == p.version) {
                prog.action = CheatInstallAction::Skipped;
                out.skipped++;
                if (progress) progress(prog);
                continue;
            }
        }

        // Atomic-ish download to a sibling .tmp. The zip ships with a
        // top-level <system>/ directory, so we extract into kCheatsDir
        // and let the archive's own layout drop files into the right
        // place.
        const std::string zip_path = dir + ".zip.tmp";
        foyer::log::write("[cheat_install] %s: GET %s -> %s\n",
            p.name.c_str(), p.url.c_str(), zip_path.c_str());
        const bool dl_ok = foyer::net::get_to_file(p.url, zip_path, {}, cancel);
        if (!dl_ok) {
            prog.action = CheatInstallAction::Failed;
            out.failed++;
            foyer::log::write("[cheat_install] %s: download FAILED (url=%s)\n",
                p.name.c_str(), p.url.c_str());
            if (progress) progress(prog);
            continue;
        }
        struct stat dl_st{};
        if (::stat(zip_path.c_str(), &dl_st) == 0) {
            foyer::log::write("[cheat_install] %s: downloaded %lld bytes\n",
                p.name.c_str(), (long long)dl_st.st_size);
        }

        if (was_present) rm_rf(dir);
        mkdir_p(dir);

        const bool xt_ok = extract_zip(zip_path, kCheatsDir);
        ::unlink(zip_path.c_str());

        if (!xt_ok) {
            prog.action = CheatInstallAction::Failed;
            out.failed++;
            foyer::log::write("[cheat_install] extract failed: %s\n",
                p.name.c_str());
            if (progress) progress(prog);
            continue;
        }

        write_sidecar(p.name, p.version);
        prog.action = was_present ? CheatInstallAction::Updated
                                  : CheatInstallAction::Installed;
        (was_present ? out.updated : out.installed)++;
        foyer::log::write("[cheat_install] %s %s v%s\n",
            was_present ? "updated" : "installed",
            p.name.c_str(), p.version.c_str());
        if (progress) progress(prog);
    }
    return out;
}

} // namespace foyer::library
