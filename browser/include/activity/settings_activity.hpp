#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Settings — opens via the Settings action button on Home. AppletFrame
// gives HOS settings-style chrome (sidebar header + footer hint bar);
// only the Home view drops AppletFrame because the launcher chrome
// differs from settings chrome.
class SettingsActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/foyer_settings.xml");

    void onContentAvailable() override;
};

}  // namespace foyer::browser
