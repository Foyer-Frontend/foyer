#include "mtp.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <sys/stat.h>

#include <switch.h>
#include <haze.h>

namespace foyer::browser {
namespace {

std::atomic<bool> g_running{false};

// haze fires its callback from the IO thread. Cache the most recent
// status under a small lock so the UI can read it on the game thread
// without racing on the std::string buffer. Cleared on session
// teardown so the banner doesn't linger after the cable is unplugged.
std::mutex  g_status_mu;
std::string g_status;

void set_status(std::string s) {
    std::scoped_lock lk{g_status_mu};
    g_status = std::move(s);
}

const char* basename_view(const char* path) {
    if (!path || !*path) return path ? path : "";
    const char* p   = path;
    const char* out = path;
    while (*p) {
        if (*p == '/' || *p == '\\') out = p + 1;
        p++;
    }
    return out;
}

// Forwards every libhaze filesystem call through the SD card's fsFs* API
// after rewriting the path so the host sees us as if the configured
// `m_root` were the root of the volume. Used for both the rom-drop
// mount (/foyer/roms) and the logs mount (/foyer/data/logs) — same
// rewriting logic, only the root + display label differ.
struct FsRomRoot final : haze::FileSystemProxyImpl {
    FsRomRoot(std::string root, std::string display, std::string name)
        : m_root(std::move(root)), m_display(std::move(display)),
          m_name(std::move(name)) {
        // Open a PRIVATE sdmc session for this mount. The previous
        // code copied fsdev's session (fsdevGetDeviceFileSystem) —
        // haze's IO thread then issued fs IPC on the same session
        // the UI thread uses for every stdio call, and concurrent
        // commands on one session garble responses. Host-side
        // symptom: libmtp "could not get object handles" the moment
        // the browser touched the SD while the host enumerated.
        if (R_FAILED(fsOpenSdCardFileSystem(&m_fs))) {
            std::memset(&m_fs, 0, sizeof(m_fs));
            foyer::log::write("[mtp] fsOpenSdCardFileSystem failed for %s\n",
                m_name.c_str());
        }
    }

    ~FsRomRoot() {
        if (m_fs.s.session) fsFsClose(&m_fs);
    }

    // libhaze uses GetName as the internal id per storage. Two
    // mounts with the same empty string here caused MTP to mis-
    // enumerate the second storage (libmtp returned "could not
    // get object handles" on the Logs mount when both Roms and
    // Logs were enabled). Each FsRomRoot now ships a unique id.
    const char* GetName() const override        { return m_name.c_str(); }
    const char* GetDisplayName() const override { return m_display.c_str(); }

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
    std::string  m_display;
    std::string  m_name;
};

void haze_callback(const haze::CallbackData* data) {
    if (!data) return;
    char buf[160];
    switch (data->type) {
        case haze::CallbackType_OpenSession:
            set_status("MTP: connected");
            break;
        case haze::CallbackType_CloseSession:
            set_status({});
            break;
        case haze::CallbackType_CreateFile:
            std::snprintf(buf, sizeof(buf),
                "MTP: creating %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_DeleteFile:
            std::snprintf(buf, sizeof(buf),
                "MTP: deleting %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_RenameFile:
        case haze::CallbackType_RenameFolder:
            std::snprintf(buf, sizeof(buf),
                "MTP: renaming %s", basename_view(data->rename.filename));
            set_status(buf);
            break;
        case haze::CallbackType_CreateFolder:
            std::snprintf(buf, sizeof(buf),
                "MTP: mkdir %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_DeleteFolder:
            std::snprintf(buf, sizeof(buf),
                "MTP: rmdir %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_ReadBegin:
            std::snprintf(buf, sizeof(buf),
                "MTP: ↑ %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_WriteBegin:
            std::snprintf(buf, sizeof(buf),
                "MTP: ↓ %s", basename_view(data->file.filename));
            set_status(buf);
            break;
        case haze::CallbackType_ReadProgress:
        case haze::CallbackType_WriteProgress: {
            // Progress fires often (every chunk); only refresh the
            // banner once per ~64 KiB so we don't churn the mutex.
            const long long off  = data->progress.offset;
            const long long size = data->progress.size;
            if (size <= 0 || (off & 0xFFFF)) break;
            const int pct = (int)((off * 100) / (size ? size : 1));
            std::snprintf(buf, sizeof(buf),
                "MTP: %lld / %lld KiB (%d%%)",
                off / 1024, size / 1024, pct);
            set_status(buf);
            break;
        }
        case haze::CallbackType_ReadEnd:
        case haze::CallbackType_WriteEnd:
            std::snprintf(buf, sizeof(buf),
                "MTP: done %s", basename_view(data->file.filename));
            set_status(buf);
            break;
    }
}

} // namespace

bool mtp_start() {
    if (g_running.load()) return true;

    const auto& cfg = library::config();
    haze::FsEntries entries;

    if (cfg.mtp_expose_roms) {
        auto root = cfg.rom_root;
        if (root.empty()) root = "/foyer/roms";
        if (root.size() > 1 && root.back() == '/') root.pop_back();
        entries.emplace_back(std::make_shared<FsRomRoot>(root, "Foyer Roms", "foyer-roms"));
        foyer::log::write("[mtp] mount: roms -> %s\n", root.c_str());
    }
    // mtp_expose_logs was retired in v0.6.117 — libhaze never
    // surfaced live-written log files reliably (enumerate cache),
    // and the in-app Log viewer (Settings → About → Logs) is the
    // canonical path for grabbing logs off the device. Config
    // field stays in shared/library/config so existing on-disk
    // configs parse cleanly; the value is simply ignored here.

    if (entries.empty()) {
        foyer::log::write("[mtp] no mounts enabled — skipping init\n");
        return false;
    }

    if (!haze::Initialize(haze_callback, 0x2C, 2, entries)) {
        foyer::log::write("[mtp] haze::Initialize failed\n");
        return false;
    }
    g_running = true;
    foyer::log::write("[mtp] started (%zu mount%s)\n",
        entries.size(), entries.size() == 1 ? "" : "s");
    return true;
}

void mtp_stop() {
    if (!g_running.load()) return;
    haze::Exit();
    g_running = false;
    set_status({});
    foyer::log::write("[mtp] stopped\n");
}

bool mtp_running() { return g_running.load(); }

std::string mtp_status() {
    std::scoped_lock lk{g_status_mu};
    return g_status;
}

} // namespace foyer::browser
