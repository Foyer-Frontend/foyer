#include "self_update.hpp"

#include <switch.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
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

    // HOS keeps the running NRO file pinned via its open romfs fd
    // — unlink/rename returns EEXIST (errno=17) or EIO (errno=5)
    // while that fd is held. romfsExit() releases it. The running
    // code stays mapped in memory; only the on-disk directory
    // entry becomes mutable. This is the switchfin pattern (see
    // dragonflylee/switchfin@app/src/utils/version.cpp).
    //
    // After romfsExit any further romfs read returns failure, but
    // we're seconds away from Application::quit so that's fine —
    // the next frame is the last frame, brls draws once with no
    // texture refresh and the process exits.
    const Result rc_rfsexit = romfsExit();
    foyer::log::write("[self_update] romfsExit rc=0x%x\n", (unsigned)rc_rfsexit);

    // Now FAT lets us delete the canonical and drop the .new in
    // its place. The two-step (remove + rename) is the only form
    // libnx newlib's FAT supports — there's no atomic-replace
    // rename on Switch.
    ::unlink(g_path.c_str());
    if (::rename(g_path_new.c_str(), g_path.c_str()) != 0) {
        foyer::log::write(
            "[self_update] rename of staged nro failed errno=%d "
            "(leaving %s in place for the next attempt)\n",
            errno, g_path_new.c_str());
        return;  // do NOT unlink .new on failure
    }
    foyer::log::write("[self_update] applied staged nro -> %s\n",
        g_path.c_str());

    // romfsExit above destroyed THIS process's romfs handle. Any
    // subsequent brls XML inflate / asset load returns garbage,
    // and HomeActivity's first frame crashes on a half-built View
    // chain (foyer + 0xbd574 in v0.6.94: View::getClassString on
    // a null typeId pointer). The running v0.6.94 code is stale
    // anyway — the new bytes are on disk. Exit the process so the
    // user's next launch hits the fresh nro cleanly.
    foyer::log::write(
        "[self_update] exiting so next launch picks up the new nro\n");
    std::exit(0);
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
