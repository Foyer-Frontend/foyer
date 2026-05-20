#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Downloads — full-screen content hub for the four manifest
// categories (Cores / Bezels / Shaders / Cheats). Pushed by the
// "Downloads" entry in FoyerDownloadsTab in Settings, so the user
// gets a single-sidebar layout instead of the original double-
// sidebar nested-TabFrame.
//
// B button pops back to Settings. Y button routes to the global
// DownloadQueueActivity so the user can review the queue without
// leaving the Downloads scope.
class DownloadsActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/foyer_downloads.xml");

    DownloadsActivity() = default;
    ~DownloadsActivity() override;

    void onContentAvailable() override;

private:
    BRLS_BIND(brls::Label, clock, "foyer/clock");
    brls::RepeatingTask* m_clock_task = nullptr;
};

}  // namespace foyer::browser
