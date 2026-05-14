#include "activity/splash_activity.hpp"

#include "activity/home_activity.hpp"
#include "manifest_cache.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "update_check.hpp"

#include "library/config.hpp"

#include <condition_variable>
#include <mutex>

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

        // Boot-time update check, gated on the user's toggle. Runs
        // INLINE here so the splash stays visible while the user
        // decides whether to update — otherwise the Yes/No dialog
        // would land on top of Home, with boot already complete.
        // The check posts dialogs back to the main thread; we
        // wait on a condvar until update_check signals "user is
        // back in control" (Later picked, or after the download
        // + restart prompt resolved).
        if (::foyer::library::config().update_check_on_boot) {
            w.set_status("Checking for foyer updates…");
            std::mutex             mu;
            std::condition_variable cv;
            bool                    done = false;
            ::foyer::browser::update_check::kick_boot(
                [&mu, &cv, &done]() {
                    {
                        std::scoped_lock lk{mu};
                        done = true;
                    }
                    cv.notify_one();
                });
            {
                std::unique_lock lk{mu};
                cv.wait(lk, [&done] { return done; });
            }
        }

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
        float target = total > 0
            ? std::min(1.0f, static_cast<float>(done) / static_cast<float>(total))
            : 0.0f;

        // If a streaming download is in flight (asset pack on first
        // run is the only one that hits this on the splash), drive
        // the bar from libcurl's byte counter so it moves smoothly
        // through the current step instead of sitting at done/total
        // for the full 30 s pull. The byte fraction is mapped into
        // the slice [done/total .. (done+1)/total] so the bar never
        // jumps backward when the step completes.
        auto& ds = ::foyer::net::current_download();
        if (ds.active.load(std::memory_order_acquire) && total > 0) {
            const auto now_b = ds.now.load(std::memory_order_acquire);
            const auto tot_b = ds.total.load(std::memory_order_acquire);
            if (tot_b > 0) {
                const float frac = std::min(1.0f,
                    static_cast<float>(now_b) / static_cast<float>(tot_b));
                const float step_lo = static_cast<float>(done)     / total;
                const float step_hi = static_cast<float>(done + 1) / total;
                target = std::max(target, step_lo + (step_hi - step_lo) * frac);
            }
        }
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
