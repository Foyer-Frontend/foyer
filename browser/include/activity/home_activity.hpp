#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Home view. Phase A shipped this as a centred-label stub. Phase B
// adds a status cluster (clock · wifi · battery) at the top-right of
// the content area, mirroring HOS's launcher chrome instead of brls's
// stock bottom-bar placement. Phase C will replace the central area
// with the system carousel.
class HomeActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/home.xml");

    void onContentAvailable() override;

    BRLS_BIND(brls::Label, clock, "foyer/clock");

private:
    brls::RepeatingTask* clockTask = nullptr;
};

}  // namespace foyer::browser
