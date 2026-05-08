#pragma once

#include <borealis.hpp>
#include <string>
#include <string_view>

namespace foyer::browser {

// Phase D entry — opened when the user clicks a system tile on Home.
// Stub for alpha.7: an AppletFrame titled with the system's display
// name. Game list (RecyclerFrame fed by foyer::library::scan) lands
// when the scanner is reintroduced into the brls build.
class SystemActivity : public brls::Activity {
public:
    SystemActivity(std::string_view folder, std::string_view display_name);

    // Following the brls pattern: override createContentView() so the
    // Application lifecycle installs our content. Setting it in the
    // constructor caused a crash — Application::pushActivity calls
    // setContentView(createContentView()) AFTER constructor runs,
    // which deleted the constructor-set view and replaced it with
    // null, then dereferenced the null content.
    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    std::string m_folder;
    std::string m_display_name;
};

}  // namespace foyer::browser
