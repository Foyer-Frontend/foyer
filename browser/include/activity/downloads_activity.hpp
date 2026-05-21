#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Downloads — full-screen content hub for the four manifest
// categories (Cores / Bezels / Shaders / Cheats). Auto-pushed from
// FoyerDownloadsTab's willAppear the moment the user focuses the
// "Downloads" sidebar entry in Settings — no intermediate
// "Open downloads" tap. The downloads_gate just_popped flag
// suppresses re-push on B-back so the user can actually return to
// Settings.
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

// Gate between SettingsActivity's Downloads tab and the
// auto-pushed DownloadsActivity. When DownloadsActivity dtors, it
// flips `just_popped = true`; the next FoyerDownloadsTab::willAppear
// consumes the flag (resets to false) without pushing, so a B-back
// leaves the user safely on Settings instead of bouncing right
// back into Downloads. Subsequent willAppears (after the user
// navigates to another tab and returns) push as expected.
namespace downloads_gate {
    bool consume_just_popped();
    void mark_just_popped();
}

}  // namespace foyer::browser
