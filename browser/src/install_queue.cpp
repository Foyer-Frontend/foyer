#include "install_queue.hpp"

#include "platform/log.hpp"

#include <borealis.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace foyer::browser::install_queue {

namespace {

struct PendingJob {
    std::string tag;
    JobBody     body;
};

std::mutex                                       g_mutex;
std::deque<PendingJob>                           g_queue;
std::unique_ptr<::foyer::library::Worker>        g_worker;
std::string                                      g_active_tag;
std::string                                      g_last_status;
brls::RepeatingTimer*                            g_timer = nullptr;

void start_next_locked() {
    // Called with g_mutex held. Pop the front job and spin up a
    // Worker that runs its body. The poll timer below picks up
    // its status snapshots + done state.
    if (g_queue.empty()) {
        g_active_tag.clear();
        return;
    }
    auto job = std::move(g_queue.front());
    g_queue.pop_front();
    g_active_tag = job.tag;
    g_last_status.clear();
    g_worker = std::make_unique<::foyer::library::Worker>();
    auto body = std::move(job.body);
    g_worker->start([body = std::move(body)](::foyer::library::Worker& w) {
        body(w);
    });
    brls::Application::notify("Installing " + g_active_tag);
    foyer::log::write("[install_queue] start tag=%s (depth=%zu)\n",
        g_active_tag.c_str(), g_queue.size());
}

void poll_tick() {
    std::unique_lock lk{g_mutex};
    if (!g_worker) return;
    const std::string snap = g_worker->status_snapshot();
    if (!snap.empty() && snap != g_last_status) {
        g_last_status = snap;
        brls::Application::notify(snap);
    }
    if (!g_worker->done()) return;
    g_worker->finish();
    g_worker.reset();
    foyer::log::write("[install_queue] done tag=%s\n", g_active_tag.c_str());
    start_next_locked();
}

void ensure_timer_locked() {
    if (g_timer) return;
    g_timer = new brls::RepeatingTimer();
    g_timer->setPeriod(500);
    g_timer->setCallback(&poll_tick);
    g_timer->start();
}

}  // namespace

std::size_t enqueue(std::string tag, JobBody body) {
    std::unique_lock lk{g_mutex};
    g_queue.push_back({std::move(tag), std::move(body)});
    const std::size_t depth = g_queue.size() + (g_worker ? 1 : 0);
    if (!g_worker) {
        ensure_timer_locked();
        start_next_locked();
    } else {
        brls::Application::notify(
            "Queued — " + std::to_string(depth - 1) + " ahead");
    }
    return depth;
}

void stop() {
    std::unique_lock lk{g_mutex};
    g_queue.clear();
    if (g_timer) {
        auto* dead = g_timer;
        g_timer = nullptr;
        dead->stop();
        // Defer the delete: brls::RepeatingTimer::onUpdate writes
        // after the callback returns, so freeing it inline from
        // the same tick would fault. brls::sync runs it on the
        // next tick when the timer is no longer being driven.
        brls::sync([dead]() { delete dead; });
    }
    // Signal cancel so curl bails out of any in-flight transfer
    // at its next progress callback. The worker is still
    // released (not joined) per SystemActivity's pattern —
    // joining on HOS exit hangs deko3d's watchdog — but
    // cancel cuts curl short so its write callback can't
    // touch process state we're about to tear down.
    if (g_worker) {
        g_worker->cancel();
        (void)g_worker.release();
    }
    g_active_tag.clear();
    g_last_status.clear();
}

std::size_t pending() {
    std::unique_lock lk{g_mutex};
    return g_queue.size() + (g_worker ? 1 : 0);
}

}  // namespace foyer::browser::install_queue
