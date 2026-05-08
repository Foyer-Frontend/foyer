#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Phase A stub. Loads romfs:/xml/activity/home.xml which currently shows
// a centred "foyer 0.6.0" label so we can confirm brls boots end-to-end
// on Switch. Phases B+ replace the XML with real Home / Settings / Game
// views.
class HomeActivity : public brls::Activity
{
public:
    CONTENT_FROM_XML_RES("activity/home.xml");
};

}  // namespace foyer::browser
