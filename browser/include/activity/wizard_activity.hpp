#pragma once

#include <borealis.hpp>
#include <vector>

namespace foyer::browser {

// First-run setup wizard. Pushed on top of HomeActivity at boot
// when /foyer/data/first_run_complete is missing. Walks the user
// through:
//   0. Welcome
//   1. Initial cores selection
//   2. Bezel packs selection
//   3. Shader packs selection
//   4. ScreenScraper account
//   5. SteamGridDB API key
//   6. Done — writes marker, kicks any queued downloads, pops back
//      to HomeActivity.
//
// Skeleton ships first (navigation + step swapping + marker write).
// Real download wiring + scraper credential persistence land in
// follow-up alphas.
class WizardActivity : public brls::Activity {
public:
    brls::View* createContentView() override;

private:
    int                       m_step = 0;
    brls::Box*                m_step_host = nullptr;
    brls::Label*              m_step_title = nullptr;
    brls::Button*             m_btn_back   = nullptr;
    brls::Button*             m_btn_next   = nullptr;
    std::vector<std::string>  m_titles;

    void renderStep();
    void onNext();
    void onBack();
    void finish();
};

}  // namespace foyer::browser
