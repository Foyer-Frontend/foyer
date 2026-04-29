#include "mtp.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <switch.h>
#include <haze.h>

namespace foyer::browser {
namespace {

std::atomic<bool> g_running{false};

// Forwards every libhaze filesystem call through the SD card's fsFs* API
// after rewriting the path so the host sees us as if `/foyer/roms` were
// the root of the volume.
struct FsRomRoot final : haze::FileSystemProxyImpl {
    explicit FsRomRoot(std::string root) : m_root(std::move(root)) {
        if (auto* sdmc = fsdevGetDeviceFileSystem("sdmc")) {
            m_fs = *sdmc;
        } else {
            std::memset(&m_fs, 0, sizeof(m_fs));
        }
    }

    const char* GetName() const override        { return ""; }
    const char* GetDisplayName() const override { return "Foyer Roms"; }

    // libhaze hands us paths starting with "/" relative to the volume root.
    // Prepend rom_root so they land under the right SD subtree.
    const char* FixPath(const char* path, char* buf) const {
        const char* tail = (path && path[0]) ? path : "/";
        std::snprintf(buf, FS_MAX_PATH, "%s%s",
            m_root.c_str(),
            tail[0] == '/' ? tail : "/");
        if (tail[0] != '/') {
            std::snprintf(buf, FS_MAX_PATH, "%s/%s", m_root.c_str(), tail);
        }
        return buf;
    }
    const char* FixPath(const char* path) const {
        static thread_local char buf[FS_MAX_PATH];
        return FixPath(path, buf);
    }

    Result GetTotalSpace(const char* p, s64* out) override {
        return fsFsGetTotalSpace(&m_fs, FixPath(p), out);
    }
    Result GetFreeSpace(const char* p, s64* out) override {
        return fsFsGetFreeSpace(&m_fs, FixPath(p), out);
    }
    Result GetEntryType(const char* p, FsDirEntryType* out) override {
        return fsFsGetEntryType(&m_fs, FixPath(p), out);
    }
    Result CreateFile(const char* p, s64 size, u32 opt) override {
        return fsFsCreateFile(&m_fs, FixPath(p), size, opt);
    }
    Result DeleteFile(const char* p) override {
        return fsFsDeleteFile(&m_fs, FixPath(p));
    }
    Result RenameFile(const char* from, const char* to) override {
        char buf[FS_MAX_PATH];
        return fsFsRenameFile(&m_fs, FixPath(from, buf), FixPath(to));
    }
    Result OpenFile(const char* p, u32 mode, FsFile* out) override {
        return fsFsOpenFile(&m_fs, FixPath(p), mode, out);
    }
    Result GetFileSize(FsFile* f, s64* out) override        { return fsFileGetSize(f, out); }
    Result SetFileSize(FsFile* f, s64 size) override        { return fsFileSetSize(f, size); }
    Result ReadFile(FsFile* f, s64 off, void* buf, u64 sz, u32 opt, u64* read) override {
        return fsFileRead(f, off, buf, sz, opt, read);
    }
    Result WriteFile(FsFile* f, s64 off, const void* buf, u64 sz, u32 opt) override {
        return fsFileWrite(f, off, buf, sz, opt);
    }
    void CloseFile(FsFile* f) override { fsFileClose(f); }

    Result CreateDirectory(const char* p) override {
        return fsFsCreateDirectory(&m_fs, FixPath(p));
    }
    Result DeleteDirectoryRecursively(const char* p) override {
        return fsFsDeleteDirectoryRecursively(&m_fs, FixPath(p));
    }
    Result RenameDirectory(const char* from, const char* to) override {
        char buf[FS_MAX_PATH];
        return fsFsRenameDirectory(&m_fs, FixPath(from, buf), FixPath(to));
    }
    Result OpenDirectory(const char* p, u32 mode, FsDir* out) override {
        return fsFsOpenDirectory(&m_fs, FixPath(p), mode, out);
    }
    Result ReadDirectory(FsDir* d, s64* total, size_t max, FsDirectoryEntry* buf) override {
        return fsDirRead(d, total, max, buf);
    }
    Result GetDirectoryEntryCount(FsDir* d, s64* out) override {
        return fsDirGetEntryCount(d, out);
    }
    void CloseDirectory(FsDir* d) override { fsDirClose(d); }

    FsFileSystem m_fs{};
    std::string  m_root;
};

void haze_callback(const haze::CallbackData* /*data*/) {
    // No-op for now; we'll route progress into a banner in a later pass.
}

} // namespace

bool mtp_start() {
    if (g_running.load()) return true;

    auto root = library::config().rom_root;
    if (root.empty()) root = "/foyer/roms";
    // libhaze wants a trailing-slash-free root because we always prepend "/".
    if (root.size() > 1 && root.back() == '/') root.pop_back();

    haze::FsEntries entries;
    entries.emplace_back(std::make_shared<FsRomRoot>(root));

    if (!haze::Initialize(haze_callback, 0x2C, 2, entries)) {
        foyer::log::write("[mtp] haze::Initialize failed\n");
        return false;
    }
    g_running = true;
    foyer::log::write("[mtp] roms over USB started, root=%s\n", root.c_str());
    return true;
}

void mtp_stop() {
    if (!g_running.load()) return;
    haze::Exit();
    g_running = false;
    foyer::log::write("[mtp] stopped\n");
}

bool mtp_running() { return g_running.load(); }

} // namespace foyer::browser
