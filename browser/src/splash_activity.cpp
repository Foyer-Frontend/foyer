#include "activity/splash_activity.hpp"

#include "activity/home_activity.hpp"
#include "install_queue.hpp"
#include "library_state.hpp"
#include "manifest_cache.hpp"
#include "net/http.hpp"
#include "platform/log.hpp"
#include "update_check.hpp"

#include "library/config.hpp"
#include "library/switch_titles.hpp"

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

    // Pop-restore-focus needs a live target. The boot-update
    // brls::Dialog pushes ON TOP of the splash; when the user picks
    // a button the Dialog dismisses and brls walks focusStack to
    // restore focus on the splash. If the splash has nothing
    // focusable, the focus pointer hangs on the dying Dialog button
    // ("invisible button" highlight) and the next A press deref's
    // a freed view — the same defect 0.6.21 fixed for EmulatorView
    // and 0.6.20 for the pause overlay. Make the content box
    // focusable (no visible highlight) and land focus on it.
    if (auto* cv = this->getContentView()) {
        cv->setFocusable(true);
        cv->setHideHighlight(true);
        cv->setHideHighlightBackground(true);
        brls::Application::giveFocus(cv);
    }

    if (m_worker) return;

    // Mute the install_queue / update_check toast surface for the
    // duration of the splash worker. Without this the boot update
    // check + manifest prefetch fire "Update v… ready" / "Installed
    // manifest" toasts onto the top-right of the splash, on top of
    // the brand. Cleared in handoff() once Home takes over.
    ::foyer::browser::install_queue::set_toasts_muted(true);

    foyer::log::write("[splash] kicking worker\n");
    m_worker = std::make_unique<::foyer::library::Worker>();
    SplashActivity* self = this;
    m_worker->start([self](::foyer::library::Worker& w) {
        w.set_status("Starting…");

        // Fixed slot layout — the byte-progress slicer in tick()
        // assumes the currently-running step occupies [done/total ..
        // (done+1)/total]. Letting manifest_cache::prefetch overwrite
        // top-level done/total used to push done up to total before
        // the update download even started, which made the byte slice
        // begin at 1.0 and the bar overflowed its track. Now every
        // major phase has its own fixed slot, manifest progress is
        // demoted to m_sub_*, and the update download lands in slot 3.
        constexpr int kStepSwitchTitles = 0;
        constexpr int kStepLibrary      = 1;
        constexpr int kStepManifests    = 2;
        constexpr int kStepUpdate       = 3;
        constexpr int kStepSettle       = 4;
        constexpr int kStepTotal        = 5;
        self->m_progress_total.store(kStepTotal, std::memory_order_release);
        self->m_progress_done.store(kStepSwitchTitles, std::memory_order_release);

        // Step 0 — Switch title enumeration.
        //
        // 0.7.18 attempted a cache-first + detached-async-refresh
        // shape via e93288d, but the std::thread that ran the
        // live diff (nsListApplicationRecord +
        // nsGetApplicationControlData per new id) crashed
        // immediately after live_app_ids returned on real
        // hardware — User Break panic from libnx, all three
        // launch attempts hit the same spot. libnx's threading +
        // service-binding doesn't tolerate the IPC chain from a
        // detached pthread in this configuration. Reverted to the
        // original blocking load until a safer async shape lands
        // (brls::async-backed worker or a service-relay through
        // the main thread). The blocking call is fast when the
        // cache is warm — per-id is a cache lookup + stat,
        // microseconds — so the splash bar barely ticks.
        w.set_status("Reading installed Switch titles…");
        foyer::library::load_switch_titles(
            [self, &w](int idx, int total) {
                if (total <= 0) return;
                self->m_sub_total.store(total, std::memory_order_release);
                self->m_sub_done.store(idx, std::memory_order_release);
                char buf[64];
                std::snprintf(buf, sizeof(buf),
                    "Reading Switch titles %d / %d…", idx, total);
                w.set_status(buf);
            });
        self->m_sub_total.store(0, std::memory_order_release);
        self->m_sub_done.store(0, std::memory_order_release);
        self->m_progress_done.store(kStepLibrary, std::memory_order_release);

        // Step 1 — library scan.
        w.set_status("Scanning library…");
        ::foyer::browser::library_state::rescan();
        self->m_progress_done.store(kStepManifests, std::memory_order_release);

        // Step 2 — manifests prefetch. Manifest-cache's per-step
        // callback drives m_sub_* (not top-level done/total) so the
        // outer slot stays at kStepManifests for the entire phase.
        ::foyer::browser::manifest_cache::prefetch(
            [self, &w](int done, int total, const char* label) {
                self->m_sub_done.store(done, std::memory_order_release);
                self->m_sub_total.store(total, std::memory_order_release);
                w.set_status(label);
            });
        self->m_sub_done.store(0, std::memory_order_release);
        self->m_sub_total.store(0, std::memory_order_release);
        self->m_progress_done.store(kStepUpdate, std::memory_order_release);

        // Step 3 — boot update check. Runs inline so the splash
        // stays visible while the user decides whether to update;
        // otherwise the Yes/No dialog would land on top of Home with
        // boot already complete. The actual update DOWNLOAD (if the
        // user accepts) shows up via net::current_download in the
        // byte-progress branch of tick(), slotted into [3/5..4/5]
        // because done is fixed at kStepUpdate for the whole phase.
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
        self->m_progress_done.store(kStepSettle, std::memory_order_release);

        // Step 4 — 400 ms floor so a fully-cached prefetch still
        // gives the brand half a second of screen time.
        svcSleepThread(400'000'000ULL);
        self->m_progress_done.store(kStepTotal, std::memory_order_release);
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

        // Streaming download (asset pack) — drive bar from libcurl's
        // byte counter so 30 s pulls don't sit at done/total for the
        // duration. Mapped into the current step's slice so it never
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

        // Per-item sub-step (Switch title NACP fetch — 200 items × ~50ms
        // each on first boot). Same slice math as the byte-progress
        // branch above; either branch can win but neither can drag the
        // bar backward thanks to the std::max.
        const int sub_total = m_sub_total.load(std::memory_order_acquire);
        const int sub_done  = m_sub_done.load(std::memory_order_acquire);
        if (sub_total > 0 && total > 0) {
            const float frac = std::min(1.0f,
                static_cast<float>(sub_done) / static_cast<float>(sub_total));
            const float step_lo = static_cast<float>(done)     / total;
            const float step_hi = static_cast<float>(done + 1) / total;
            target = std::max(target, step_lo + (step_hi - step_lo) * frac);
        }
        // Kick a new tween step whenever the worker advances past
        // the prior target. Animatable::reset() drops any queued
        // steps but keeps the current value; addStep + start then
        // glide from there. Easing matches brls's own slider feel.
        target = std::min(1.0f, target);
        if (target > m_anim_target + 0.001f) {
            m_anim_target = target;
            m_anim_pct.reset();
            m_anim_pct.addStep(target, kStepDurationMs,
                               brls::EasingFunction::quadraticOut);
            m_anim_pct.start();
        }
        const float pct = std::min(1.0f, static_cast<float>(m_anim_pct));
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
    // Re-enable the toast surface now that Home is taking over — any
    // post-handoff install/update activity should be visible again.
    ::foyer::browser::install_queue::set_toasts_muted(false);

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
