#pragma once

#include <borealis.hpp>
#include <string>
#include <string_view>

namespace foyer::browser {

// Phase D entry — opened when the user clicks a system tile on Home.
// Stub for alpha.7: just an AppletFrame titled with the system's
// display name. Game list (RecyclerFrame fed by foyer::library::scan)
// lands when the scanner is reintroduced into the brls build.
class SystemActivity : public brls::Activity {
public:
    SystemActivity(std::string_view folder, std::string_view display_name);

    void onContentAvailable() override;

private:
    std::string m_folder;
    std::string m_display_name;
};

}  // namespace foyer::browser
