#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include <switch.h>

namespace foyer::library {

// Lightweight background-task primitive built on libnx Thread. The
// caller supplies a body closure that runs off the UI thread; the body
// is expected to (a) call set_status() periodically with human-readable
// progress and (b) honour cancelled() to bail out promptly when the UI
// asks. All other state (results, downloaded paths, totals) is owned
// by whatever struct embeds the Worker — it's read after finish() joins
// so no extra synchronisation is needed there.
//
// One Worker == one shot. After finish() the instance is reusable for
// another start().
class Worker {
public:
    Worker() = default;
    ~Worker();

    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

    bool active()    const { return m_active.load   (std::memory_order_acquire); }
    bool done()      const { return m_done.load     (std::memory_order_acquire); }
    bool cancelled() const { return m_cancel.load   (std::memory_order_acquire); }

    // Spawn a thread running `body`. stack_size defaults to 256 KB —
    // enough for libcurl/TLS handshakes; bump if the body needs more.
    // Priority 0x30 is below render/audio so a heavy network task
    // doesn't starve the UI.
    bool start(std::function<void(Worker&)> body, std::size_t stack_size = 0x40000);

    // UI-thread cancel signal. The body should poll cancelled() at
    // its loop boundaries. Curl callbacks aborts via the same flag,
    // see net::CancelHook.
    void cancel() { m_cancel.store(true, std::memory_order_release); }

    // Worker-side: publish progress text the UI reads each frame.
    void set_status(std::string s);

    // UI-side: snapshot the latest status string.
    std::string status_snapshot() const;

    // Join the thread, clear the slot, return done() back to false.
    // Caller should only call after done() flips true.
    void finish();

private:
    static void trampoline(void* self);

    Thread             m_thread{};
    std::atomic<bool>  m_active{false};
    std::atomic<bool>  m_done{false};
    std::atomic<bool>  m_cancel{false};

    mutable std::mutex m_status_mu;
    std::string        m_status;

    std::function<void(Worker&)> m_body;
};

} // namespace foyer::library
