#pragma once

#include <borealis.hpp>

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

class FoyerCoresTab : public brls::Box {
public:
    FoyerCoresTab();
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
