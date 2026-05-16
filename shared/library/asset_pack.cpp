#include "asset_pack.hpp"

#include "net/http.hpp"
#include "platform/log.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace foyer::library {
namespace {

void mkdir_p(const std::string& path) {
    std::string cur;
    for (char c : path) {
        cur.push_back(c);
        if (c == '/' && cur.size() > 1) ::mkdir(cur.c_str(), 0755);
    }
    ::mkdir(path.c_str(), 0755);
}

bool dir_exists(const std::string& p) {
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// libarchive zip → out_dir. Mirrors shader_installer.cpp::extract_zip
// but kept local so asset_pack is self-contained; the two helpers will
// converge into shared/util/ if a third caller needs them.
bool extract_zip(const std::string& zip_path, const std::string& out_dir,
                 std::function<void(const AssetPackProgress&)> progress,
                 foyer::net::CancelHook cancel) {
    auto* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_filter_all(a);
    if (archive_read_open_filename(a, zip_path.c_str(), 64 * 1024) != ARCHIVE_OK) {
        foyer::log::write("[asset_pack] open %s failed: %s\n",
            zip_path.c_str(), archive_error_string(a));
        archive_read_free(a);
        return false;
    }

    bool ok = true;
    std::uint64_t files_seen = 0;
    archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (cancel && cancel()) { ok = false; break; }
        const char* name = archive_entry_pathname(entry);
        if (!name || !*name) { archive_read_data_skip(a); continue; }
        if (std::strstr(name, "..")) { archive_read_data_skip(a); continue; }

        std::string out_path = out_dir + "/" + name;
        if (S_ISDIR(archive_entry_mode(entry)) ||
            name[std::strlen(name) - 1] == '/') {
            mkdir_p(out_path);
            continue;
        }
        const auto last_slash = out_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            mkdir_p(out_path.substr(0, last_slash));
        }

        auto* fp = std::fopen(out_path.c_str(), "wb");
        if (!fp) {
            foyer::log::write("[asset_pack] open(%s) wb failed\n", out_path.c_str());
            archive_read_data_skip(a);
            ok = false;
            continue;
        }
        char buf[32 * 1024];
        for (;;) {
            la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
            if (n == 0) break;
            if (n < 0)  { ok = false; break; }
            std::fwrite(buf, 1, (std::size_t)n, fp);
        }
        std::fclose(fp);
        files_seen++;
        if (progress && (files_seen % 16) == 0) {
            AssetPackProgress p{};
            p.phase = "Extracting artwork…";
            p.bytes_done = files_seen;
            progress(p);
        }
    }
    archive_read_close(a);
    archive_read_free(a);
    return ok;
}

std::string pack_system_dir(std::string_view pack, std::string_view folder) {
    std::string out{kAssetRoot};
    out += "/themes/";
    out += pack;
    out += "/systems/";
    out += folder;
    return out;
}

} // namespace

// Bump this every time the foyer-assets.zip content changes in a
// way that needs forcing existing users to re-download. The
// sidecar at /foyer/data/assets/.version stores the version the
// LOCAL extract was installed with; asset_pack_present compares
// to the running binary's expectation and forces a redownload on
// mismatch.
constexpr const char* kAssetPackVersion = "4";

bool asset_pack_present() {
    const std::string root{kAssetRoot};
    if (!dir_exists(root + "/systems") || !dir_exists(root + "/themes"))
        return false;
    auto* f = std::fopen((root + "/.version").c_str(), "rb");
    if (!f) return false;
    char buf[16] = {0};
    const auto n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::string ver{buf, n};
    while (!ver.empty()
        && (ver.back() == '\n' || ver.back() == '\r' || ver.back() == ' '))
        ver.pop_back();
    return ver == kAssetPackVersion;
}

std::string asset_pack_system_file(std::string_view pack,
                                   std::string_view folder,
                                   std::string_view file) {
    if (!asset_pack_present()) return {};
    std::string path = pack_system_dir(pack, folder);
    path.push_back('/');
    path.append(file);
    return path;
}

std::string asset_system_splash(std::string_view folder) {
    return asset_pack_system_file("foyer", folder, "splash.png");
}

std::string asset_system_background(std::string_view folder) {
    return asset_pack_system_file("foyer", folder, "background.jpg");
}

std::string asset_system_logo(std::string_view folder, bool dark) {
    return asset_pack_system_file(
        "foyer", folder, dark ? "logo_dark.png" : "logo_light.png");
}

bool install_asset_pack(
    const std::string& zip_url,
    std::function<void(const AssetPackProgress&)> progress,
    foyer::net::CancelHook cancel) {

    mkdir_p(kAssetRoot);

    if (progress) {
        AssetPackProgress p{};
        p.phase = "Downloading artwork pack…";
        progress(p);
    }

    const std::string zip_path =
        std::string{kAssetRoot} + "/foyer-assets.zip.tmp";

    if (!foyer::net::get_to_file(zip_url, zip_path, {}, cancel)) {
        foyer::log::write("[asset_pack] download failed: %s\n", zip_url.c_str());
        ::unlink(zip_path.c_str());
        return false;
    }

    if (progress) {
        AssetPackProgress p{};
        p.phase = "Extracting artwork…";
        progress(p);
    }

    const bool xt = extract_zip(zip_path, kAssetRoot, progress, cancel);
    ::unlink(zip_path.c_str());

    if (!xt) {
        foyer::log::write("[asset_pack] extract failed\n");
        return false;
    }

    // Stamp the version sidecar so asset_pack_present() can detect
    // future content bumps without an http HEAD check.
    if (auto* vf = std::fopen(
            (std::string{kAssetRoot} + "/.version").c_str(), "wb")) {
        std::fwrite(kAssetPackVersion, 1,
                    std::strlen(kAssetPackVersion), vf);
        std::fclose(vf);
    }

    foyer::log::write("[asset_pack] installed to %s (v=%s)\n",
        kAssetRoot, kAssetPackVersion);
    return true;
}

} // namespace foyer::library
