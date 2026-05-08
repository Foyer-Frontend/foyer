#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Settings — opens via a button on the Home top bar. Uses brls's
// AppletFrame inside this activity so we keep the HOS settings-style
// header + footer hint bar; only the home view dropped AppletFrame
// (because the launcher chrome differs from settings chrome).
class SettingsActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/foyer_settings.xml");
};

}  // namespace foyer::browser
