#pragma once

#include "library/worker.hpp"

#include <borealis.hpp>
#include <memory>

namespace foyer::browser {

// Boot splash. Shown immediately after createWindow so the user
// sees foyer-branded chrome instead of a blank window while the
// expensive boot work (library scan, optional manifest prefetch
// for first-run) runs on a background Worker.
//
// A short RepeatingTask polls the Worker every 200 ms and copies
// its status string into the on-screen label. When the Worker
// finishes the splash pops itself off the stack and pushes the
// real Home (and the first-run wizard if applicable).
class SplashActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/splash.xml");

    SplashActivity();
    ~SplashActivity() override;

    void onContentAvailable() override;

    BRLS_BIND(brls::Label, status, "foyer/splash_status");

    // Called from the SplashTick repeating task to refresh the
    // status label and check the worker for completion.
    void tick();

private:
    std::unique_ptr<::foyer::library::Worker> m_worker;
    brls::RepeatingTask*                      m_tick = nullptr;
    bool                                      m_handed_off = false;

    void handoff();
};

} // namespace foyer::browser
