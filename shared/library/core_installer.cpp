#include "core_installer.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "scrapers/cache.hpp"

#include <cstdio>
#include <sys/stat.h>

#include <yyjson.h>

namespace foyer::library {
namespace {

constexpr const char* kCoresDir = "/foyer/cores";

void set_field(std::string& dst, yyjson_val* obj, const char* key) {
    auto* v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) dst = yyjson_get_str(v);
}

std::size_t file_size(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return (std::size_t)st.st_size;
}

void write_version_sidecar(const std::string& nro_path,
                           const std::string& version) {
    if (version.empty()) return;
    const std::string side = nro_path + ".version";
    if (auto* f = std::fopen(side.c_str(), "wb")) {
        std::fwrite(version.data(), 1, version.size(), f);
        std::fclose(f);
    }
}

} // namespace

std::string installed_core_version(std::string_view core_nro) {
    const std::string side = std::string{kCoresDir} + "/"
                           + std::string{core_nro} + ".version";
    auto* f = std::fopen(side.c_str(), "rb");
    if (!f) return {};
    char buf[64];
    auto n = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
        n--;
    return std::string(buf, n);
}

CoreManifest fetch_manifest(const std::string& url) {
    CoreManifest m;
    auto resp = foyer::net::get(url);
    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        foyer::log::write("[core_install] manifest fetch failed: code=%ld\n", resp.code);
        return m;
    }

    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) {
        foyer::log::write("[core_install] manifest is not valid JSON\n");
        return m;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return m;
    }

    set_field(m.version, root, "version");

    auto* arr = yyjson_obj_get(root, "cores");
    if (arr && yyjson_is_arr(arr)) {
        std::size_t i, max; yyjson_val* item;
        yyjson_arr_foreach(arr, i, max, item) {
            if (!yyjson_is_obj(item)) continue;
            CoreManifestEntry e;
            set_field(e.name,    item, "name");
            set_field(e.version, item, "version");
            set_field(e.nro,     item, "nro");
            set_field(e.sha256,  item, "sha256");
            set_field(e.url,     item, "url");
            if (auto* sv = yyjson_obj_get(item, "size"); sv && yyjson_is_int(sv))
                e.size = (std::size_t)yyjson_get_int(sv);
            if (e.name.empty() || e.url.empty() || e.nro.empty()) continue;
            m.cores.push_back(std::move(e));
        }
    }
    yyjson_doc_free(doc);
    return m;
}

InstallTotals install_cores(const CoreManifest& manifest,
                            std::function<void(const InstallProgress&)> progress) {
    InstallTotals out;
    foyer::scrapers::ensure_parent_dir(std::string{kCoresDir} + "/.placeholder");

    int idx = 0;
    for (const auto& c : manifest.cores) {
        idx++;
        InstallProgress p;
        p.index = idx;
        p.total = (int)manifest.cores.size();
        p.name  = c.name;

        const std::string dest = std::string{kCoresDir} + "/" + c.nro;
        const auto have = file_size(dest);
        const bool was_present = (have > 0);

        // Skip when the installed version sidecar matches the manifest's
        // version exactly. Sidecar is the source of truth — size compare
        // is a fragile proxy (recompiles with no functional change can
        // shift bytes; genuine fixes can preserve them).
        if (was_present) {
            const auto installed = installed_core_version(c.nro);
            if (!installed.empty() && installed == c.version) {
                p.action = InstallAction::Skipped;
                out.skipped++;
                if (progress) progress(p);
                continue;
            }
        }

        const bool ok = foyer::net::get_to_file(c.url, dest);
        if (!ok) {
            p.action = InstallAction::Failed;
            out.failed++;
            foyer::log::write("[core_install] failed: %s -> %s\n",
                c.url.c_str(), dest.c_str());
        } else {
            write_version_sidecar(dest, c.version);
            p.action = was_present ? InstallAction::Updated : InstallAction::Installed;
            (was_present ? out.updated : out.installed)++;
            foyer::log::write("[core_install] %s %s v%s (%zu bytes)\n",
                was_present ? "updated" : "installed",
                c.nro.c_str(), c.version.c_str(), file_size(dest));
        }

        if (progress) progress(p);
    }
    return out;
}

} // namespace foyer::library
