#include "worker.hpp"

#include "platform/log.hpp"

#include <utility>

namespace foyer::library {

Worker::~Worker() {
    if (m_active.load(std::memory_order_acquire)) {
        // Drain — the UI never explicitly finished us. Don't leak the
        // thread handle on shutdown.
        m_cancel.store(true, std::memory_order_release);
        threadWaitForExit(&m_thread);
        threadClose(&m_thread);
    }
}

bool Worker::start(std::function<void(Worker&)> body, std::size_t stack_size) {
    if (m_active.exchange(true, std::memory_order_acq_rel)) return false;

    m_done.store  (false, std::memory_order_release);
    m_cancel.store(false, std::memory_order_release);
    {
        std::lock_guard lk{m_status_mu};
        m_status.clear();
    }
    m_body = std::move(body);

    const Result rc = threadCreate(&m_thread, &Worker::trampoline,
                                   this, nullptr, stack_size, 0x30, -2);
    if (R_FAILED(rc)) {
        foyer::log::write("[worker] threadCreate rc=0x%X\n", rc);
        m_active.store(false, std::memory_order_release);
        return false;
    }
    threadStart(&m_thread);
    return true;
}

void Worker::set_status(std::string s) {
    std::lock_guard lk{m_status_mu};
    m_status = std::move(s);
}

std::string Worker::status_snapshot() const {
    std::lock_guard lk{m_status_mu};
    return m_status;
}

void Worker::finish() {
    if (!m_active.load(std::memory_order_acquire)) return;
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);
    m_active.store(false, std::memory_order_release);
    m_done.store  (false, std::memory_order_release);
    m_cancel.store(false, std::memory_order_release);
    m_body = nullptr;
}

void Worker::trampoline(void* self) {
    auto* w = static_cast<Worker*>(self);
    if (w->m_body) w->m_body(*w);
    w->m_done.store(true, std::memory_order_release);
}

} // namespace foyer::library
