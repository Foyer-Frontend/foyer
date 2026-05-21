#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

namespace foyer::browser {

// Six tab content classes used by xml/tabs/foyer_settings.xml.
// Each is a self-contained brls::Box subclass that builds its row
// list in the constructor — the TabFrame inflates whichever tab
// the user focuses, so dynamic content (cores manifest, update
// status) stays live.
//
// Each class also exposes a static create() that brls's XML
// inflater calls when it sees a `<FoyerXxxTab/>` element. They get
// registered in main() via Application::registerXMLView before
// the SettingsActivity is pushed.

class FoyerGeneralTab : public brls::Box {
public:
    FoyerGeneralTab();
    static brls::View* create();
};

class FoyerAccountsTab : public brls::Box {
public:
    FoyerAccountsTab();
    static brls::View* create();
};

class FoyerLibraryTab : public brls::Box {
public:
    FoyerLibraryTab();
    static brls::View* create();
};

// Liveness probe for install_queue listener deferred-fire path.
// Defined in settings_tab.cpp alongside the tombstone set.
bool is_tab_alive(const void* p);

// Helper mixed into install-list tabs (cores/bezels/shaders/cheats)
// so a finished install can refresh every "Tap to install" cell's
// label in place. install_queue fires completion listeners on the
// UI thread; each tab subscribes once in its ctor and unsubscribes
// in its dtor.
class InstallRefreshTab : public brls::Box {
public:
    InstallRefreshTab();
    ~InstallRefreshTab() override;

protected:
    // Each cell-builder calls this with a closure that re-evaluates
    // the row's label (Tap to install / re-install / update).
    void add_refresher(std::function<void()> fn);

    // Subscribes to install_queue. Call at the end of the tab's
    // ctor once all add_refresher() calls have landed.
    void start_listening();

    // brls hook — TabFrame calls willAppear when a tab becomes the
    // visible one. We pump every refresher there so a tab switched
    // INTO right after an install completed shows the fresh
    // "Tap to re-install" label without the user having to scroll
    // off-row and back. Cheaper than a global completion listener,
    // and avoids the use-after-free that the earlier subscribe-and-
    // refresh-anywhere path hit when an install lands while a
    // different activity is on top.
    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

    // Walk every refresher closure (cells re-read their installed-
    // version sidecar and update the title). Called from
    // willAppear + the per-tab RepeatingTask.
    void refresh_labels();

    // Manifest-refresh scaffolding (per-tab "Check for updates"). The
    // bug: after a chain-launch back from a core, the boot-time
    // manifest prefetch is skipped (main.cpp fast_returned branch),
    // so manifest_cache::cores() / bezels() / shaders() / cheats()
    // are empty when the user opens Settings → Cores. The tab
    // showed "Manifest unavailable" with no way to refetch other
    // than relaunching the app.
    //
    // Each subclass calls setup_refresh_header() once in its ctor
    // with the visible label ("Cores"/"Bezels"/…) and the prefetch
    // function to call when the user taps Refresh. The helper
    // creates the host Box, the persistent "Refresh manifest" cell,
    // and m_content (where manifest-dependent cells live). Then the
    // subclass overrides populate_content() to build cells into
    // m_content, and the Refresh cell re-runs that override after
    // the prefetch completes.
    void setup_refresh_header(const std::string& label,
                              std::function<void()> prefetch);

    // Subclass override — (re)build the manifest-dependent cells
    // inside m_content. MUST start with reset_content() so a refresh
    // doesn't stack duplicate cells.
    virtual void populate_content() = 0;

    // Clear m_content's children + drop refreshers tied to the old
    // cell instances. Called from each populate_content() override.
    void reset_content();

    // Host Box for the manifest-dependent cells, set up by
    // setup_refresh_header(). Children are cleared + rebuilt on
    // every Refresh tap; the wrapping ScrollView + the persistent
    // Refresh cell live in the parent and are not touched.
    brls::Box* m_content = nullptr;

private:
    int                                m_sub = -1;
    std::vector<std::function<void()>> m_refreshers;
    // 2 s refresh tick — only runs while the tab is the visible
    // one in the TabFrame. Picks up cell-label changes while the
    // user is still looking at the tab (the previous
    // willAppear-only refresh required leaving + re-entering).
    brls::RepeatingTask*               m_poll = nullptr;
    // De-bounce flag for the Refresh-manifest cell — flips true
    // on click, back to false in the brls::sync callback after
    // the prefetch finishes. Touched only from the UI thread
    // (click handler + sync callback), so no atomic needed.
    bool                               m_refreshing = false;
};

class FoyerCoresTab : public InstallRefreshTab {
public:
    FoyerCoresTab();
    static brls::View* create();

protected:
    void populate_content() override;
};

class FoyerEmulatorsTab : public brls::Box {
public:
    FoyerEmulatorsTab();
    static brls::View* create();
};

class FoyerBezelsTab : public InstallRefreshTab {
public:
    FoyerBezelsTab();
    static brls::View* create();

protected:
    void populate_content() override;
};

class FoyerShadersTab : public InstallRefreshTab {
public:
    FoyerShadersTab();
    static brls::View* create();

protected:
    void populate_content() override;
};

class FoyerCheatsTab : public InstallRefreshTab {
public:
    FoyerCheatsTab();
    static brls::View* create();

protected:
    void populate_content() override;
};

// Aggregator tab — Settings sidebar's "Downloads" entry. The tab
// body holds a single click-to-enter cell that pushes
// DownloadsActivity. Earlier iterations auto-pushed on willAppear,
// which surprised users by treating sidebar focus as activation;
// the explicit A-press keeps the two inputs separate.
class FoyerDownloadsTab : public brls::Box {
public:
    FoyerDownloadsTab();
    static brls::View* create();
};

class FoyerUpdatesTab : public brls::Box {
public:
    FoyerUpdatesTab();
    static brls::View* create();
};

class FoyerAboutTab : public brls::Box {
public:
    FoyerAboutTab();
    static brls::View* create();
};

}  // namespace foyer::browser
