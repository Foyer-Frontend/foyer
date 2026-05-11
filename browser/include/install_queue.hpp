#pragma once

#include "library/worker.hpp"

#include <functional>
#include <string>

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

// How many jobs are pending (including the active one). 0 if
// idle.
std::size_t pending();

}  // namespace foyer::browser::install_queue
