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

    // Per-pack selection masks, one slot per manifest entry. Sized
    // lazily on first render so the manifest_cache prefetches don't
    // need to be done before the constructor runs.
    std::vector<bool>           m_core_selected;
    std::vector<bool>           m_bezel_selected;
    std::vector<bool>           m_shader_selected;

    // Scraper credential drafts. Pre-loaded from the on-disk
    // accounts.jsonc so re-running the wizard is non-destructive.
    std::string                 m_ss_user;
    std::string                 m_ss_pass;
    std::string                 m_sgdb_key;

    void renderStep();
    void onNext();
    void onBack();
    void finish();

    brls::View* buildWelcomeStep();
    brls::View* buildCoresStep();
    brls::View* buildBezelsStep();
    brls::View* buildShadersStep();
    brls::View* buildScreenScraperStep();
    brls::View* buildSteamGridDBStep();
    brls::View* buildPlaceholderStep(const std::string& body);
    brls::View* buildDoneStep();
};

}  // namespace foyer::browser
