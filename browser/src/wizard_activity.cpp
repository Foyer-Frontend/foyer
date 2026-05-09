#include "activity/wizard_activity.hpp"

#include "first_run.hpp"
#include "manifest_cache.hpp"
#include "platform/log.hpp"
#include "library/core_install_job.hpp"

#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Long-lived install job. Survives the wizard popping so cores
// keep downloading after the user lands on Home. unique_ptr lets
// us replace it from a clean state on subsequent first-run runs
// (config wipe + re-launch). Held by value would crash the
// destructor inside Worker mid-thread on shutdown.
std::unique_ptr<::foyer::library::CoreInstallJob> g_core_install_job;

}  // namespace

WizardActivity::WizardActivity() {
    m_titles = {
        "Welcome",
        "Initial cores",
        "Bezel packs",
        "Shader packs",
        "ScreenScraper",
        "SteamGridDB",
        "Done",
    };

    const auto& mf = manifest_cache::cores();
    m_core_selected.assign(mf.cores.size(), false);
}

brls::View* WizardActivity::createContentView() {
    auto* outer = new brls::Box();
    outer->setAxis(brls::Axis::COLUMN);
    outer->setAlignItems(brls::AlignItems::STRETCH);

    m_step_title = new brls::Label();
    m_step_title->setFontSize(28.0f);
    m_step_title->setMargins(32.0f, 48.0f, 16.0f, 48.0f);
    outer->addView(m_step_title);

    m_step_host = new brls::Box();
    m_step_host->setAxis(brls::Axis::COLUMN);
    m_step_host->setGrow(1.0f);
    outer->addView(m_step_host);

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    row->setMargins(16.0f, 48.0f, 32.0f, 48.0f);

    m_btn_back = new brls::Button();
    m_btn_back->setText("Back");
    m_btn_back->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    m_btn_back->registerClickAction([this](brls::View*) {
        onBack();
        return true;
    });
    row->addView(m_btn_back);

    m_btn_next = new brls::Button();
    m_btn_next->setText("Next");
    m_btn_next->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    m_btn_next->registerClickAction([this](brls::View*) {
        onNext();
        return true;
    });
    row->addView(m_btn_next);

    outer->addView(row);

    auto* frame = new brls::AppletFrame(outer);
    frame->setTitle("First-run setup");

    renderStep();
    return frame;
}

brls::View* WizardActivity::buildWelcomeStep() {
    return buildPlaceholderStep(
        "Welcome to foyer.\n\n"
        "We'll walk through a few setup steps so the launcher is ready "
        "to play. You can skip any step and revisit it from Settings "
        "later.");
}

brls::View* WizardActivity::buildPlaceholderStep(const std::string& body) {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setPadding(48.0f, 48.0f, 48.0f, 48.0f);
    box->setAlignItems(brls::AlignItems::FLEX_START);

    auto* label = new brls::Label();
    label->setText(body);
    label->setFontSize(20.0f);
    box->addView(label);

    return box;
}

brls::View* WizardActivity::buildCoresStep() {
    const auto& mf = manifest_cache::cores();
    if (mf.cores.empty()) {
        return buildPlaceholderStep(
            "Initial cores.\n\n"
            "Couldn't reach the foyer-cores manifest. You can install "
            "cores from Settings later when the network is available.");
    }

    auto* scroll = new brls::ScrollingFrame();
    auto* list   = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setMargins(16.0f, 24.0f, 16.0f, 32.0f);

    auto* hint = new brls::Label();
    hint->setText("Pick which libretro cores to install now. Each is a "
                  "small download (~few MB).");
    hint->setFontSize(18.0f);
    hint->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    list->addView(hint);

    for (std::size_t i = 0; i < mf.cores.size(); i++) {
        const auto& entry = mf.cores[i];
        auto* cell = new brls::BooleanCell();
        cell->title->setText(entry.name);
        cell->init(entry.name, m_core_selected[i],
                   [this, i](bool value) {
                       if (i < m_core_selected.size()) {
                           m_core_selected[i] = value;
                       }
                   });
        list->addView(cell);
    }
    scroll->setContentView(list);
    return scroll;
}

brls::View* WizardActivity::buildDoneStep() {
    int picked = 0;
    for (bool b : m_core_selected) if (b) picked++;

    std::string body = "All set!\n\nPress Finish to save your choices "
                       "and head to the home screen.";
    if (picked > 0) {
        body += "\n\nFoyer will install " + std::to_string(picked)
              + " core" + (picked == 1 ? "" : "s")
              + " in the background.";
    }
    return buildPlaceholderStep(body);
}

void WizardActivity::renderStep() {
    if (!m_step_host || !m_step_title) return;

    m_step_host->clearViews();
    brls::View* view = nullptr;
    switch (m_step) {
        case 0: view = buildWelcomeStep(); break;
        case 1: view = buildCoresStep(); break;
        case 2: view = buildPlaceholderStep(
            "Bezel packs.\n\nOptional system bezels for the libretro "
            "overlay. Selection wiring lands in the next alpha — press "
            "Next for now."); break;
        case 3: view = buildPlaceholderStep(
            "Shader packs.\n\nOptional shader presets for the libretro "
            "player. Selection wiring lands in the next alpha."); break;
        case 4: view = buildPlaceholderStep(
            "ScreenScraper account.\n\nSign in with a ScreenScraper.fr "
            "account for box art + metadata scraping. Username + "
            "password fields land in the next alpha."); break;
        case 5: view = buildPlaceholderStep(
            "SteamGridDB API key.\n\nOptional fallback metadata source. "
            "API key field lands in the next alpha."); break;
        default: view = buildDoneStep(); break;
    }
    m_step_host->addView(view);

    const int last = (int)m_titles.size() - 1;
    if (m_step >= 0 && m_step < (int)m_titles.size()) {
        m_step_title->setText(m_titles[m_step]);
    }
    if (m_btn_back) {
        m_btn_back->setVisibility(m_step > 0
            ? brls::Visibility::VISIBLE
            : brls::Visibility::INVISIBLE);
    }
    if (m_btn_next) {
        m_btn_next->setText(m_step >= last ? "Finish" : "Next");
    }
}

void WizardActivity::onNext() {
    const int last = (int)m_titles.size() - 1;
    if (m_step >= last) {
        finish();
        return;
    }
    m_step++;
    renderStep();
}

void WizardActivity::onBack() {
    if (m_step <= 0) return;
    m_step--;
    renderStep();
}

void WizardActivity::finish() {
    // Build a filtered CoreManifest of the user's picks and kick
    // a background install job. Marker writes regardless of
    // whether the install succeeds — the wizard has done its job
    // (config'd choices); install retries land in Settings.
    const auto& full = manifest_cache::cores();
    ::foyer::library::CoreManifest filtered{};
    filtered.version = full.version;
    for (std::size_t i = 0;
         i < full.cores.size() && i < m_core_selected.size(); i++)
    {
        if (m_core_selected[i]) filtered.cores.push_back(full.cores[i]);
    }

    if (!filtered.cores.empty()) {
        g_core_install_job =
            std::make_unique<::foyer::library::CoreInstallJob>();
        if (g_core_install_job->start(filtered, std::string(), false)) {
            foyer::log::write(
                "[wizard] kicked install job for %zu cores\n",
                filtered.cores.size());
        } else {
            foyer::log::write(
                "[wizard] core install job failed to start\n");
            g_core_install_job.reset();
        }
    }

    first_run::mark_complete();
    brls::Application::popActivity();
}

}  // namespace foyer::browser
