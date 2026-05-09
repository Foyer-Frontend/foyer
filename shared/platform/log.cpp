#include "log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/stat.h>

#include <switch.h>

namespace foyer::log {
namespace {

// 0.6.0: each run gets its own log at /foyer/data/logs/<datetime>.log.
// The legacy /foyer/data/log.txt was append-mode, so a long-running
// install would accrete sessions and the user had to scroll to find
// the relevant one. Per-run files mean "the latest one" is always the
// most recent boot — easier to share, easier to scrub.
std::mutex            g_mutex;
std::atomic<bool>     g_file_open{false};
std::atomic<int>      g_nxlink_sock{0};
std::string           g_log_path;   // resolved by init_file()

void write_v(const char* fmt, std::va_list ap) {
    if (!g_file_open && !g_nxlink_sock) {
        return;
    }

    char buf[1024];
    const auto t = std::time(nullptr);
    const auto* tm = std::localtime(&t);
    const auto head_n = std::snprintf(buf, sizeof(buf),
        "[%02d:%02d:%02d] ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    std::vsnprintf(buf + head_n, sizeof(buf) - head_n, fmt, ap);

    std::scoped_lock lk{g_mutex};
    if (g_file_open && !g_log_path.empty()) {
        if (auto* f = std::fopen(g_log_path.c_str(), "a")) {
            std::fputs(buf, f);
            std::fclose(f);
        }
    }
    if (g_nxlink_sock) {
        std::fputs(buf, stdout);
    }
}

} // namespace

bool init_file() {
    std::scoped_lock lk{g_mutex};
    if (g_file_open) return false;

    // Make sure /foyer/data/logs exists before fopen. mkdir is a
    // no-op if the directory's already there (we ignore EEXIST).
    ::mkdir("/foyer",            0755);
    ::mkdir("/foyer/data",       0755);
    ::mkdir("/foyer/data/logs",  0755);

    // Build the per-run filename: /foyer/data/logs/YYYY-MM-DD_HH-MM-SS.log.
    const auto t  = std::time(nullptr);
    const auto* tm = std::localtime(&t);
    char path_buf[96];
    std::snprintf(path_buf, sizeof(path_buf),
        "/foyer/data/logs/%04d-%02d-%02d_%02d-%02d-%02d.log",
        tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
        tm->tm_hour, tm->tm_min, tm->tm_sec);
    g_log_path = path_buf;

    if (auto* f = std::fopen(g_log_path.c_str(), "w")) {
        std::fprintf(f,
            "=== session start %04d-%02d-%02d %02d:%02d:%02d ===\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
        std::fclose(f);
        g_file_open = true;
        return true;
    }
    return false;
}

void exit_file() {
    std::scoped_lock lk{g_mutex};
    g_file_open = false;
}

bool init_nxlink() {
    std::scoped_lock lk{g_mutex};
    if (g_nxlink_sock) return false;
    const int s = nxlinkConnectToHost(true, false);
    if (s <= 0) return false;
    g_nxlink_sock = s;
    return true;
}

void exit_nxlink() {
    std::scoped_lock lk{g_mutex};
    if (g_nxlink_sock) {
        ::close(g_nxlink_sock.load());
        g_nxlink_sock = 0;
    }
}

bool is_init() {
    return g_file_open || g_nxlink_sock;
}

void write(const char* fmt, ...) {
    if (!g_file_open && !g_nxlink_sock) return;
    std::va_list ap;
    va_start(ap, fmt);
    write_v(fmt, ap);
    va_end(ap);
}

} // namespace foyer::log
