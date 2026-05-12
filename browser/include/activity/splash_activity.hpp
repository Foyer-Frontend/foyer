#pragma once

#include "library/worker.hpp"

#include <atomic>
#include <borealis.hpp>
#include <borealis/core/animation.hpp>
#include <memory>

namespace foyer::browser {

// Boot splash. Shown immediately after createWindow so the user
// sees foyer-branded chrome instead of a blank window while the
// expensive boot work (library scan, optional manifest prefetch
// for first-run) runs on a background Worker.
//
// A short RepeatingTask polls the Worker every 100 ms and copies
// its status string into the on-screen label + advances the
// progress bar fill width based on m_progress_done / m_progress_total.
class SplashActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/splash.xml");

    SplashActivity();
    ~SplashActivity() override;

    void onContentAvailable() override;

    BRLS_BIND(brls::Label, status,   "foyer/splash_status");
    BRLS_BIND(brls::Box,   bar_fill, "foyer/splash_bar_fill");

    void tick();

private:
    std::unique_ptr<::foyer::library::Worker> m_worker;
    brls::RepeatingTask*                      m_tick = nullptr;
    bool                                      m_handed_off = false;

    // Track width is hardcoded in splash.xml (520 px). The worker
    // thread writes m_progress_done / m_progress_total lock-free;
    // each UI tick converts that to a target percent and, when the
    // target moves forward, kicks brls's tween animation so the bar
    // glides between manifest steps instead of snapping.
    std::atomic<int> m_progress_done{0};
    std::atomic<int> m_progress_total{4};
    brls::Animatable m_anim_pct{0.0f};
    float            m_anim_target = 0.0f;
    static constexpr float kBarTrackWidth = 520.0f;
    static constexpr int   kStepDurationMs = 350;

    void handoff();
};

} // namespace foyer::browser
