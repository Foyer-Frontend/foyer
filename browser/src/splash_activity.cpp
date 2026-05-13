#include "activity/splash_activity.hpp"

#include "activity/home_activity.hpp"
#include "manifest_cache.hpp"
#include "platform/log.hpp"

#include <switch.h>

namespace foyer::browser {

namespace {

class SplashTick : public brls::RepeatingTask {
public:
    SplashTick(SplashActivity* host) : brls::RepeatingTask(100), m_host(host) {}
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
    if (build) build->setText(FOYER_DISPLAY_VERSION);

    if (m_worker) return;

    foyer::log::write("[splash] kicking worker\n");
    m_worker = std::make_unique<::foyer::library::Worker>();
    SplashActivity* self = this;
    m_worker->start([self](::foyer::library::Worker& w) {
        w.set_status("Starting…");
        self->m_progress_done.store(0, std::memory_order_release);
        self->m_progress_total.store(5, std::memory_order_release);

        ::foyer::browser::manifest_cache::prefetch(
            [self, &w](int done, int total, const char* label) {
                self->m_progress_done.store(done, std::memory_order_release);
                self->m_progress_total.store(total, std::memory_order_release);
                w.set_status(label);
            });

        // 400 ms floor so a fully-cached prefetch still gives the
        // brand half a second of screen time.
        svcSleepThread(400'000'000ULL);
        self->m_progress_done.store(
            self->m_progress_total.load(std::memory_order_acquire),
            std::memory_order_release);
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

    if (bar_fill) {
        const int done  = m_progress_done.load(std::memory_order_acquire);
        const int total = m_progress_total.load(std::memory_order_acquire);
        const float target = total > 0
            ? std::min(1.0f, static_cast<float>(done) / static_cast<float>(total))
            : 0.0f;
        // Kick a new tween step whenever the worker advances past
        // the prior target. Animatable::reset() drops any queued
        // steps but keeps the current value; addStep + start then
        // glide from there. Easing matches brls's own slider feel.
        if (target > m_anim_target + 0.001f) {
            m_anim_target = target;
            m_anim_pct.reset();
            m_anim_pct.addStep(target, kStepDurationMs,
                               brls::EasingFunction::quadraticOut);
            m_anim_pct.start();
        }
        const float pct = static_cast<float>(m_anim_pct);
        bar_fill->setWidth(kBarTrackWidth * pct);
    }

    if (m_worker->done() && !m_handed_off) {
        m_handed_off = true;
        SplashActivity* self = this;
        brls::sync([self]() { self->handoff(); });
    }
}

void SplashActivity::handoff() {
    foyer::log::write("[splash] handoff — pushing Home\n");
    m_worker->finish();

    if (m_tick) {
        m_tick->stop();
        delete m_tick;
        m_tick = nullptr;
    }

    brls::Application::pushActivity(
        new ::foyer::browser::HomeActivity(),
        brls::TransitionAnimation::NONE);
}

}  // namespace foyer::browser
