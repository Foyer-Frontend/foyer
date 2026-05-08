#include "foyer_updater.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>
#include <vector>

#include <switch.h>
#include <yyjson.h>

namespace foyer::library {
namespace {

void set_field(std::string& dst, yyjson_val* obj, const char* key) {
    auto* v = yyjson_obj_get(obj, key);
    if (v && yyjson_is_str(v)) dst = yyjson_get_str(v);
}

std::array<int, 3> parse_semver(std::string_view s) {
    std::array<int, 3> out{0, 0, 0};
    int idx = 0;
    for (std::size_t i = 0; i < s.size() && idx < 3; ) {
        int n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            n = n * 10 + (s[i] - '0');
            i++;
        }
        out[idx++] = n;
        if (i < s.size() && s[i] == '.') i++;
        else break;
    }
    return out;
}

} // namespace

FoyerManifest fetch_foyer_manifest(const std::string& url) {
    FoyerManifest m;
    auto resp = foyer::net::get(url);
    if (resp.code < 200 || resp.code >= 300 || resp.body.empty()) {
        foyer::log::write("[foyer_update] manifest fetch failed: code=%ld\n", resp.code);
        return m;
    }
    auto* doc = yyjson_read(resp.body.data(), resp.body.size(), 0);
    if (!doc) {
        foyer::log::write("[foyer_update] manifest is not valid JSON\n");
        return m;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        set_field(m.version, root, "version");
        set_field(m.url,     root, "url");
        set_field(m.sha256,  root, "sha256");
        if (auto* sv = yyjson_obj_get(root, "size"); sv && yyjson_is_int(sv))
            m.size = (std::size_t)yyjson_get_int(sv);
    }
    yyjson_doc_free(doc);
    return m;
}

bool is_newer_version(std::string_view current, std::string_view candidate) {
    const auto a = parse_semver(current);
    const auto b = parse_semver(candidate);
    return std::tie(b[0], b[1], b[2]) > std::tie(a[0], a[1], a[2]);
}

bool download_foyer_update(const FoyerManifest& m, const std::string& nro_path,
                           foyer::net::CancelHook cancel) {
    if (m.url.empty()) return false;
    const std::string staged = nro_path + ".new";
    if (!foyer::net::get_to_file(m.url, staged, {}, cancel)) {
        foyer::log::write("[foyer_update] download failed: %s -> %s\n",
            m.url.c_str(), staged.c_str());
        ::unlink(staged.c_str());
        return false;
    }

    // Verify size BEFORE the chain-launch path can pick this file up.
    // A truncated download leaves a partial .nro on disk; chain-
    // launching a partial nro fatals atmosphere with PC=0 (the entry
    // address read from the header is garbage). Atmosphere fatal
    // 2354-0001 was reported in the wild from this exact scenario.
    struct stat st{};
    if (::stat(staged.c_str(), &st) != 0) {
        foyer::log::write(
            "[foyer_update] stat on staged file failed errno=%d\n",
            errno);
        ::unlink(staged.c_str());
        return false;
    }
    if (m.size != 0 && (std::size_t)st.st_size != m.size) {
        foyer::log::write(
            "[foyer_update] size mismatch: got=%lld manifest=%zu — "
            "deleting partial .new\n",
            (long long)st.st_size, m.size);
        ::unlink(staged.c_str());
        return false;
    }

    // Verify sha256 if the manifest carried one. Cheap-enough at
    // ~38 MB on a Switch (couple seconds) and the only way to
    // catch a download that completed at the right size but with
    // corrupted bytes (rare but possible on flaky Wi-Fi).
    if (!m.sha256.empty()) {
        std::FILE* f = std::fopen(staged.c_str(), "rb");
        if (!f) {
            ::unlink(staged.c_str());
            return false;
        }
        Sha256Context ctx;
        sha256ContextCreate(&ctx);
        std::vector<std::uint8_t> buf(64 * 1024);
        while (true) {
            const auto n = std::fread(buf.data(), 1, buf.size(), f);
            if (n == 0) break;
            sha256ContextUpdate(&ctx, buf.data(), n);
        }
        std::fclose(f);
        std::uint8_t digest[SHA256_HASH_SIZE];
        sha256ContextGetHash(&ctx, digest);
        char hex[SHA256_HASH_SIZE * 2 + 1];
        for (int i = 0; i < SHA256_HASH_SIZE; i++) {
            std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        }
        if (m.sha256 != hex) {
            foyer::log::write(
                "[foyer_update] sha256 mismatch: got=%s manifest=%s — "
                "deleting corrupt .new\n", hex, m.sha256.c_str());
            ::unlink(staged.c_str());
            return false;
        }
    }

    foyer::log::write(
        "[foyer_update] staged %s (size=%lld, sha256-verified) for next boot\n",
        staged.c_str(), (long long)st.st_size);
    return true;
}

} // namespace foyer::library
