#pragma once

#include <borealis.hpp>
#include <string>
#include <vector>

namespace foyer::browser {

// First-run setup wizard. Pushed on top of HomeActivity at boot
// when /foyer/data/first_run_complete is missing.
//
// Steps:
//   0. Welcome
//   1. Initial cores — picks from the foyer-cores release manifest;
//      Finish queues a CoreInstallJob for the chosen subset.
//   2. Bezel packs        (UI in alpha.21)
//   3. Shader packs       (UI in alpha.21)
//   4. ScreenScraper      (UI in alpha.21)
//   5. SteamGridDB        (UI in alpha.21)
//   6. Done — writes the marker, kicks queued jobs, pops to Home.
class WizardActivity : public brls::Activity {
public:
    WizardActivity();

    brls::View* createContentView() override;

private:
    int                         m_step = 0;
    brls::Box*                  m_step_host  = nullptr;
    brls::Label*                m_step_title = nullptr;
    brls::Button*               m_btn_back   = nullptr;
    brls::Button*               m_btn_next   = nullptr;
    std::vector<std::string>    m_titles;

    // Per-core selection mask, indexed alongside the cores
    // manifest the wizard prefetched. Size matches the manifest;
    // true = install on Finish.
    std::vector<bool>           m_core_selected;

    void renderStep();
    void onNext();
    void onBack();
    void finish();

    brls::View* buildWelcomeStep();
    brls::View* buildCoresStep();
    brls::View* buildPlaceholderStep(const std::string& body);
    brls::View* buildDoneStep();
};

}  // namespace foyer::browser
