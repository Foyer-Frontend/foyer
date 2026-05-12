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

private:
    int                                m_sub = -1;
    std::vector<std::function<void()>> m_refreshers;
    // 2 s refresh tick — only runs while the tab is the visible
    // one in the TabFrame. Picks up cell-label changes while the
    // user is still looking at the tab (the previous
    // willAppear-only refresh required leaving + re-entering).
    brls::RepeatingTask*               m_poll = nullptr;
};

class FoyerCoresTab : public InstallRefreshTab {
public:
    FoyerCoresTab();
    static brls::View* create();
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
};

class FoyerShadersTab : public InstallRefreshTab {
public:
    FoyerShadersTab();
    static brls::View* create();
};

class FoyerCheatsTab : public InstallRefreshTab {
public:
    FoyerCheatsTab();
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
