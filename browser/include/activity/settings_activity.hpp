#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Settings — opens via the Settings action button on Home. Custom
// status row above a TabFrame so the chrome (clock · wifi ·
// battery) matches Home / System / Game; AppletFrame's built-in
// header is hidden via headerHidden=true. Footer (hint bar)
// stays visible from AppletFrame.
class SettingsActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/foyer_settings.xml");

    SettingsActivity() = default;
    ~SettingsActivity() override;

    void onContentAvailable() override;

    // Called by FoyerDownloadsTab::willAppear when the user focuses
    // the Downloads sidebar entry in the main frame. Hides the
    // main TabFrame, shows the downloads TabFrame, gives focus to
    // its first tab. Idempotent — re-entry while already in
    // downloads mode (or in the just-exited cooldown) is a no-op.
    static void enter_downloads_mode();

private:
    BRLS_BIND(brls::Label,    clock,           "foyer/clock");
    BRLS_BIND(brls::TabFrame, mainFrame,       "foyer/settings_main");
    BRLS_BIND(brls::TabFrame, downloadsFrame,  "foyer/settings_downloads");
    brls::RepeatingTask* m_clock_task = nullptr;

    // Sidebar-swap state machine.
    //   0 = main mode (default; main TabFrame visible)
    //   1 = downloads mode (downloads TabFrame visible)
    //   2 = just-exited-downloads: suppresses the next
    //       FoyerDownloadsTab willAppear that fires after we re-
    //       focus the Downloads sidebar entry on B-back. Consumed
    //       on first hit; reverts to 0.
    int m_downloads_mode = 0;

    void exit_downloads_mode();

    // Static accessor so FoyerDownloadsTab (which doesn't otherwise
    // know its containing SettingsActivity) can call enter_*.
    static SettingsActivity* g_live;
};

}  // namespace foyer::browser
