#include "activity/wizard_activity.hpp"

#include "first_run.hpp"

#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/label.hpp>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// One of the wizard's content panes. Each step renders into a
// separate Box so swapping between steps is a clearChildren +
// addView sequence on the host slot.
brls::View* make_placeholder(const std::string& body) {
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

brls::View* step_view(int step) {
    switch (step) {
        case 0:
            return make_placeholder(
                "Welcome to foyer.\n\n"
                "We'll walk through a few setup steps so the launcher is "
                "ready to play. You can skip any step and revisit it from "
                "Settings later.");
        case 1:
            return make_placeholder(
                "Initial cores.\n\n"
                "Pick which libretro cores to install now. List + download "
                "wiring lands in the next alpha; press Next to continue.");
        case 2:
            return make_placeholder(
                "Bezel packs.\n\n"
                "Optional system bezels for the libretro overlay. Wiring "
                "lands in the next alpha.");
        case 3:
            return make_placeholder(
                "Shader packs.\n\n"
                "Optional shader presets for the libretro player. Wiring "
                "lands in the next alpha.");
        case 4:
            return make_placeholder(
                "ScreenScraper account.\n\n"
                "Sign in with a ScreenScraper.fr account for box art + "
                "metadata scraping. Username + password fields land in "
                "the next alpha.");
        case 5:
            return make_placeholder(
                "SteamGridDB API key.\n\n"
                "Optional fallback metadata source. API key field lands "
                "in the next alpha.");
        default:
            return make_placeholder(
                "All set!\n\n"
                "Press Finish to save your choices and head to the home "
                "screen. Cores and packs you selected will download in "
                "the background.");
    }
}

}  // namespace

brls::View* WizardActivity::createContentView() {
    m_titles = {
        "Welcome",
        "Initial cores",
        "Bezel packs",
        "Shader packs",
        "ScreenScraper",
        "SteamGridDB",
        "Done",
    };

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

    // Bottom button row — Back on the left, Next/Finish on the right.
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

    // No B-back from the wizard — the user must complete or
    // explicitly choose a step. This avoids accidentally bouncing
    // back to a half-mounted Home before the marker file is
    // written.

    renderStep();
    return frame;
}

void WizardActivity::renderStep() {
    if (!m_step_host || !m_step_title) return;

    m_step_host->clearViews();
    m_step_host->addView(step_view(m_step));

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
    first_run::mark_complete();
    brls::Application::popActivity();
}

}  // namespace foyer::browser
