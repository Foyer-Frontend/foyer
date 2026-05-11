#include "activity/splash_activity.hpp"

#include "manifest_cache.hpp"
#include "platform/log.hpp"

#include <switch.h>

namespace foyer::browser {

namespace {

// Polls the Worker once per tick and copies its status snapshot
// into the splash label. Owned + cancelled by SplashActivity's
// destructor.
class SplashTick : public brls::RepeatingTask {
public:
    SplashTick(SplashActivity* host) : brls::RepeatingTask(200), m_host(host) {}
    void run() override {
        if (m_host) m_host->tick();
    }
private:
    SplashActivity* m_host;
};

}  // namespace

SplashActivity::SplashActivity() {
    foyer::log::write("[splash] ctor\n");
}

SplashActivity::~SplashActivity() {
    if (m_tick) {
        m_tick->stop();
        delete m_tick;
        m_tick = nullptr;
    }
}

void SplashActivity::onContentAvailable() {
    if (m_worker) return;

    foyer::log::write("[splash] kicking worker\n");
    m_worker = std::make_unique<::foyer::library::Worker>();
    // Library scan + manifest prefetch already happened in main()
    // before this activity was pushed. The splash now exists purely
    // to cover the brief boot-state hitch — the worker just sleeps
    // a moment so the user actually sees the foyer brand.
    m_worker->start([](::foyer::library::Worker& w) {
        w.set_status("Loading…");
        // Prefetch the cores/bezels/shaders manifests so the
        // Settings → Cores tab has data on first open. First-run
        // wizard path runs its own prefetch in main(); this covers
        // every other boot.
        ::foyer::browser::manifest_cache::prefetch();
        // 600 ms floor so the brand registers even when the
        // prefetch returns from cache instantly.
        svcSleepThread(600'000'000ULL);
        w.set_status("Ready");
    });

    m_tick = new SplashTick(this);
    m_tick->start();
}

void SplashActivity::tick() {
    if (!m_worker) return;

    if (status) {
        const auto snap = m_worker->status_snapshot();
        if (!snap.empty()) status->setText(snap);
    }

    if (m_worker->done() && !m_handed_off) {
        m_handed_off = true;
        // Defer the handoff to the next main-loop iteration via
        // brls::sync. Calling handoff() inline would delete this
        // RepeatingTask + the SplashActivity while we're still
        // inside SplashTick::run() — classic use-after-free.
        SplashActivity* self = this;
        brls::sync([self]() { self->handoff(); });
    }
}

void SplashActivity::handoff() {
    foyer::log::write("[splash] handoff — popping self, revealing Home\n");
    m_worker->finish();

    if (m_tick) {
        m_tick->stop();
        delete m_tick;
        m_tick = nullptr;
    }

    // Home is already underneath us — main pushed Home first, then
    // Splash on top. A single popActivity reveals it. No push race.
    // No fade — the fade phase marks the popped splash as
    // translucent and shows HomeActivity bleeding through
    // around its edges for the duration.
    brls::Application::popActivity(brls::TransitionAnimation::NONE);
}

}  // namespace foyer::browser
