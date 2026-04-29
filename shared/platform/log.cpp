#include "log.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#include <switch.h>

namespace foyer::log {
namespace {

constexpr const char* g_log_path = "/foyer/data/log.txt";

std::mutex            g_mutex;
std::atomic<bool>     g_file_open{false};
std::atomic<int>      g_nxlink_sock{0};

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
    if (g_file_open) {
        if (auto* f = std::fopen(g_log_path, "a")) {
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

    // Append mode so the log survives a chain-launch cycle (browser →
    // player → browser). Each session writes a banner so it's easy to
    // tell where one process ends and the next begins.
    if (auto* f = std::fopen(g_log_path, "a")) {
        const auto t = std::time(nullptr);
        const auto* tm = std::localtime(&t);
        std::fprintf(f,
            "\n=== session start %04d-%02d-%02d %02d:%02d:%02d ===\n",
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
