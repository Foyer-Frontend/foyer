#pragma once

#include <borealis.hpp>

#include <string>
#include <string_view>

namespace foyer::browser {

// Per-system settings overlay pushed from HomeActivity's tile +
// button. Right now: pick the system's default core (mirrors
// the Emulators tab row for that system), and show a few
// read-only stats. Shader / runahead defaults will join here
// once config grows per-system override fields.
class PerSystemActivity : public brls::Activity {
public:
    PerSystemActivity(std::string_view folder,
                      std::string_view display_name);
    ~PerSystemActivity() override = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::string m_folder;
    std::string m_display_name;
};

}  // namespace foyer::browser
