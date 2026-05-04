#include "seed_assets.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace foyer::browser {
namespace {

bool path_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// Stream-copy from src to dst — no in-memory buffer of the whole file
// since some bezel PNGs are >500 KiB and we'd rather pace through SD.
// Returns false if src can't be opened or dst can't be written.
bool copy_file(const std::string& src, const std::string& dst) {
    std::ifstream in{src, std::ios::binary};
    if (!in) return false;
    std::ofstream out{dst, std::ios::binary | std::ios::trunc};
    if (!out) return false;
    constexpr std::size_t kChunk = 64 * 1024;
    char buf[kChunk];
    while (in.read(buf, kChunk) || in.gcount() > 0) {
        out.write(buf, in.gcount());
        if (!out) return false;
    }
    return true;
}

void ensure_dir(const std::string& path) {
    ::mkdir(path.c_str(), 0777);
}

// Walk one romfs subtree and copy every regular file across to the
// matching SD destination. Subdirectories under the source are
// recreated under the destination. Files that already exist on SD are
// skipped untouched.
//
// `src_root` is a romfs path like "romfs:/bezels"; `dst_root` is an
// SD path like "/foyer/bezels". The recursion preserves whatever
// nesting the romfs subtree carries.
int copy_tree(const std::string& src_root, const std::string& dst_root) {
    auto* dir = ::opendir(src_root.c_str());
    if (!dir) return 0;

    int copied = 0;
    while (auto* e = ::readdir(dir)) {
        if (!e->d_name[0] || e->d_name[0] == '.') continue;
        const std::string src = src_root + "/" + e->d_name;
        const std::string dst = dst_root + "/" + e->d_name;
        if (e->d_type == DT_DIR) {
            ensure_dir(dst);
            copied += copy_tree(src, dst);
            continue;
        }
        if (e->d_type != DT_REG) continue;
        if (path_exists(dst)) continue;
        if (copy_file(src, dst)) copied++;
    }
    ::closedir(dir);
    return copied;
}

} // namespace

void seed_assets_if_missing() {
    ensure_dir("/foyer/bezels");
    ensure_dir("/foyer/cheats");

    int b = copy_tree("romfs:/bezels", "/foyer/bezels");
    int c = copy_tree("romfs:/cheats", "/foyer/cheats");
    if (b > 0 || c > 0) {
        foyer::log::write(
            "[seed] copied %d bezel(s) and %d cheat file(s) to SD\n",
            b, c);
    }
}

} // namespace foyer::browser
