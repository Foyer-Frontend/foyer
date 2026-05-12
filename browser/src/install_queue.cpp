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

std::mutex                                       g_listeners_mu;
std::vector<std::pair<int, CompletionListener>>  g_listeners;
int                                              g_next_listener_id = 1;

[[maybe_unused]] void fire_completion(const std::string& tag) {
    // Snapshot listeners under lock so a listener that calls
    // unsubscribe() doesn't invalidate the iterator we're walking.
    std::vector<std::pair<int, CompletionListener>> copy;
    {
        std::unique_lock lk{g_listeners_mu};
        copy = g_listeners;
    }
    for (auto& [_, cb] : copy) {
        if (cb) cb(tag);
    }
}

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
    // Explicit completion banner — the per-tick status notify
    // only fires when set_status changes, so a fast install
    // (cache hit, skip-by-version) easily slips past without
    // visible feedback. Always toast on done.
    brls::Application::notify("Installed " + g_active_tag);
    start_next_locked();
    // NOTE: completion listeners were temporarily disabled — when
    // the deferred brls::sync path was active the user hit a
    // brls::fatal during install downloads (vtable corruption in
    // some brls XML attribute setter, root not yet pinned).
    // Until that's understood we keep the queue's own toasts
    // ("Installed <tag>") and skip the per-cell live refresh.
    // Cells refresh on tab switch as before.
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

    // Deduplicate by tag — if the same install is already running
    // or queued, don't add a second copy. Reasons:
    //   - User mashing "Tap to install" before the row label
    //     refreshes shouldn't queue the same core twice.
    //   - "Install all" + a single-row tap for one of the cores
    //     within that batch should not run the same core twice.
    // Active job tag lives in g_active_tag; pending tags walk
    // g_queue. depth is reported as the existing position so the
    // user sees the same "Queued — N ahead" they would have seen
    // had they been the first to enqueue.
    if (g_worker && g_active_tag == tag) {
        brls::Application::notify(tag + " is already installing");
        return g_queue.size() + 1;
    }
    for (std::size_t i = 0; i < g_queue.size(); ++i) {
        if (g_queue[i].tag == tag) {
            brls::Application::notify(
                tag + " is already queued (#" + std::to_string(i + 1) + ")");
            return g_queue.size() + (g_worker ? 1 : 0);
        }
    }

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

int subscribe(CompletionListener cb) {
    std::unique_lock lk{g_listeners_mu};
    int id = g_next_listener_id++;
    g_listeners.emplace_back(id, std::move(cb));
    return id;
}

void unsubscribe(int id) {
    std::unique_lock lk{g_listeners_mu};
    for (auto it = g_listeners.begin(); it != g_listeners.end(); ++it) {
        if (it->first == id) { g_listeners.erase(it); return; }
    }
}

Snapshot snapshot() {
    std::unique_lock lk{g_mutex};
    Snapshot s;
    s.active_tag  = g_worker ? g_active_tag : std::string{};
    s.last_status = g_worker ? g_last_status : std::string{};
    s.pending_tags.reserve(g_queue.size());
    for (const auto& j : g_queue) s.pending_tags.push_back(j.tag);
    return s;
}

}  // namespace foyer::browser::install_queue
