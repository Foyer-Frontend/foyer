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

private:
    BRLS_BIND(brls::Label, clock, "foyer/clock");
    brls::RepeatingTask* m_clock_task = nullptr;
};

}  // namespace foyer::browser
