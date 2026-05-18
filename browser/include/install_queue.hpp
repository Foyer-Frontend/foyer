#pragma once

#include "library/worker.hpp"

#include <functional>
#include <string>
#include <vector>

namespace foyer::browser::install_queue {

// One job in the queue. `tag` is what gets toasted on enqueue /
// start / finish ("fceumm", "all bezels", …). `body` runs on the
// queue's worker thread and may call w.set_status() to publish
// progress that the UI tick relays as brls::Application::notify
// lines.
using JobBody = std::function<void(::foyer::library::Worker&)>;

// Push a job onto the queue. If nothing is currently running, the
// queue immediately starts it. Otherwise the job waits in FIFO
// order. Returns the queue depth after the insert (1 = will run
// next, >1 = N-1 jobs ahead).
std::size_t enqueue(std::string tag, JobBody body);

// brls poll tick — checks the active worker for status updates +
// completion, advances to the next job when done. Driven by a
// RepeatingTimer set up the first time enqueue() is called.

// Drain the queue + abandon the active worker. Used by
// HomeActivity's quit handler so brls doesn't tick into a
// torn-down Application.
void stop();

// Cancel just the currently-running job. The worker's cancel
// flag is set so its body bails at the next poll point (curl
// xferinfo or w.cancelled() check); the next pending job
// starts on the following poll tick. Pending jobs are NOT
// touched.
void cancel_current();

// How many jobs are pending (including the active one). 0 if
// idle.
std::size_t pending();

// Snapshot of the queue for the UI overlay. Active job at the
// top (empty tag means idle), followed by pending tags in FIFO
// order. last_status is the most recent set_status the active
// worker published — typically "[N/M] core - installed" so the
// overlay can parse progress from it.
struct Snapshot {
    std::string              active_tag;
    std::string              last_status;
    std::vector<std::string> pending_tags;
};
Snapshot snapshot();

// Subscribe to job-completion events. The listener fires on the UI
// thread (from poll_tick) right after the install_queue's
// "Installed <tag>" toast, so Settings tabs can refresh "Tap to
// install" → "Tap to re-install" cells without waiting for the
// user to leave and re-enter the tab. Caller MUST unsubscribe in
// its destructor; the registry persists across tab inflates.
using CompletionListener = std::function<void(const std::string& tag)>;
int  subscribe(CompletionListener cb);
void unsubscribe(int id);

// Suppress the queue's progress toasts (and update_check's parallel
// toasts) during the splash worker — we don't want "Update v… ready"
// and "Installed manifest" bubbles drawn on top of the splash bar
// while the user hasn't even reached Home. SplashActivity::onContent
// flips this on; handoff() flips it off.
void set_toasts_muted(bool muted);
bool toasts_muted();

}  // namespace foyer::browser::install_queue
