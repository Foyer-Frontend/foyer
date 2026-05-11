#include "self_update.hpp"

#include <switch.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "platform/log.hpp"

namespace foyer::browser::self_update {
namespace {

std::string g_path;
std::string g_path_new;

}  // namespace

void detect_paths() {
    std::string p;
    if (envHasArgv()) {
        const char* a = static_cast<const char*>(envGetArgv());
        if (a) {
            while (*a == ' ') a++;
            const char* end = nullptr;
            if (*a == '"') { a++; end = std::strchr(a, '"'); }
            else           { end = std::strchr(a, ' '); }
            p.assign(a, end ? static_cast<std::size_t>(end - a) : std::strlen(a));
        }
    }
    if (p.compare(0, 5, "sdmc:") == 0) p.erase(0, 5);
    if (p.empty()) p = "/switch/foyer/foyer.nro";
    // Canonicalise: strip ".new" so g_path always names the on-disk
    // canonical file, not the staged-update sibling we just chain-
    // launched from. Otherwise nro_new_path() ends up ".../foyer.nro.new.new"
    // and we'd never find the staged file to apply.
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".new") == 0) {
        p.erase(p.size() - 4);
    }
    g_path     = p;
    g_path_new = p + ".new";
    foyer::log::write("[self_update] running nro path = %s\n",
        g_path.c_str());
}

void apply_staged_if_present() {
    struct stat st{};
    if (::stat(g_path_new.c_str(), &st) != 0) return;

    // 0.5.21 floor: drop suspiciously-small staged files. A truncated
    // download leaves a partial foyer.nro.new; renaming it to foyer.nro
    // makes the next boot fatal (PC=0, 2354-0001). 1 MB is below any
    // plausible foyer build (~38 MB current).
    if (st.st_size < 1024 * 1024) {
        foyer::log::write(
            "[self_update] staged file is %lld bytes — too small, "
            "deleting (probably a partial download)\n",
            (long long)st.st_size);
        ::unlink(g_path_new.c_str());
        return;
    }

    // FAT/exFAT rename of the staged file onto the canonical name
    // bites two ways:
    //   1) the destination exists → EEXIST, even with hbloader's
    //      libnx newlib (no POSIX overwrite-rename here).
    //   2) the unlink-then-rename fallback the prior implementation
    //      used was destructive: unlink succeeded, second rename
    //      then failed because the .new file was still mapped by
    //      the running process (we chain-launch .new directly),
    //      leaving the user with NO foyer.nro and a stranded .new.
    //
    // Promote via byte-copy instead. fopen("wb") on the canonical
    // path doesn't require the underlying mapping to release —
    // it truncates the file in place. After the bytes are copied
    // we drop the .new sentinel; if the unlink fails (because the
    // running process is mapped from .new), the canonical file is
    // already correct and the next boot will run from canonical
    // and re-clean.
    auto copy_file = [](const char* from, const char* to) -> bool {
        FILE* src = ::fopen(from, "rb");
        if (!src) {
            foyer::log::write(
                "[self_update] fopen(%s, rb) failed errno=%d\n",
                from, errno);
            return false;
        }
        FILE* dst = ::fopen(to, "wb");
        if (!dst) {
            foyer::log::write(
                "[self_update] fopen(%s, wb) failed errno=%d\n",
                to, errno);
            ::fclose(src);
            return false;
        }
        std::vector<unsigned char> buf(64 * 1024);
        bool ok = true;
        for (;;) {
            const std::size_t n = ::fread(buf.data(), 1, buf.size(), src);
            if (n == 0) break;
            if (::fwrite(buf.data(), 1, n, dst) != n) {
                foyer::log::write("[self_update] fwrite short on %s\n", to);
                ok = false;
                break;
            }
        }
        ::fclose(dst);
        ::fclose(src);
        return ok;
    };

    if (!copy_file(g_path_new.c_str(), g_path.c_str())) {
        foyer::log::write(
            "[self_update] copy of staged nro failed; leaving %s "
            "in place for the next attempt\n", g_path_new.c_str());
        return;
    }
    // Best-effort cleanup of the sentinel. If we're running from
    // the .new file, the unlink may fail on FAT because the file
    // is still mapped — the canonical copy is already correct,
    // so just log and move on.
    if (::unlink(g_path_new.c_str()) != 0) {
        foyer::log::write(
            "[self_update] unlink(%s) failed errno=%d "
            "(will retry on next boot)\n",
            g_path_new.c_str(), errno);
    }
    foyer::log::write("[self_update] applied staged nro -> %s\n",
        g_path.c_str());
}

void scrub_legacy_default_bezel() {
    if (::unlink("/foyer/content/bezels/default.png") == 0) {
        foyer::log::write(
            "[bezel] scrubbed legacy /foyer/content/bezels/default.png\n");
    }
}

void scrub_extract_lru(int days_threshold) {
    if (days_threshold < 1) days_threshold = 1;
    const time_t kMaxAgeSeconds =
        (time_t)days_threshold * 24LL * 60 * 60;
    const time_t now = ::time(nullptr);

    const char* kDir = "/foyer/data/extract";
    DIR* d = ::opendir(kDir);
    if (!d) return;
    int dropped = 0;
    while (auto* ent = ::readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        const std::string path = std::string(kDir) + "/" + ent->d_name;
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        if (now - st.st_mtime < kMaxAgeSeconds) continue;
        if (::unlink(path.c_str()) == 0) dropped++;
    }
    ::closedir(d);
    if (dropped > 0) {
        foyer::log::write(
            "[extract] scrubbed %d stale rom(s) from /foyer/data/extract/\n",
            dropped);
    }
}

const std::string& nro_path()     { return g_path; }
const std::string& nro_new_path() { return g_path_new; }

}  // namespace foyer::browser::self_update
