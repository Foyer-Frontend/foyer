#include "activity/wizard_activity.hpp"

#include "first_run.hpp"
#include "manifest_cache.hpp"
#include "platform/log.hpp"
#include "library/bezel_installer.hpp"
#include "library/core_install_job.hpp"
#include "library/shader_installer.hpp"
#include "library/worker.hpp"
#include "scrapers/accounts.hpp"

#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/button.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include "widgets/masked_input_cell.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Long-lived install job + bezel/shader workers. They survive the
// wizard popping so the downloads keep running while the user lands
// on Home. unique_ptr lets us replace each one from a clean state
// on subsequent first-run runs (config wipe + re-launch). Holding
// by value would crash the destructor inside Worker mid-thread on
// shutdown.
std::unique_ptr<::foyer::library::CoreInstallJob> g_core_install_job;
std::unique_ptr<::foyer::library::Worker>         g_bezel_worker;
std::unique_ptr<::foyer::library::Worker>         g_shader_worker;

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

brls::View* WizardActivity::buildBezelsStep() {
    const auto& mf = manifest_cache::bezels();
    if (mf.packs.empty()) {
        return buildPlaceholderStep(
            "Bezel packs.\n\n"
            "Couldn't reach the foyer-bezels manifest. You can install "
            "bezels from Settings later when the network is available.");
    }

    if (m_bezel_selected.size() != mf.packs.size()) {
        m_bezel_selected.assign(mf.packs.size(), false);
    }

    auto* scroll = new brls::ScrollingFrame();
    auto* list   = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setMargins(16.0f, 24.0f, 16.0f, 32.0f);

    auto* hint = new brls::Label();
    hint->setText("Pick which bezel packs to install now. One small zip "
                  "per system.");
    hint->setFontSize(18.0f);
    hint->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    list->addView(hint);

    for (std::size_t i = 0; i < mf.packs.size(); i++) {
        const auto& entry = mf.packs[i];
        auto* cell = new brls::BooleanCell();
        cell->title->setText(entry.name);
        cell->init(entry.name, m_bezel_selected[i],
                   [this, i](bool value) {
                       if (i < m_bezel_selected.size()) {
                           m_bezel_selected[i] = value;
                       }
                   });
        list->addView(cell);
    }
    scroll->setContentView(list);
    return scroll;
}

brls::View* WizardActivity::buildShadersStep() {
    const auto& mf = manifest_cache::shaders();
    if (mf.presets.empty()) {
        return buildPlaceholderStep(
            "Shader packs.\n\n"
            "Couldn't reach the foyer-shaders manifest. You can install "
            "shaders from Settings later when the network is available.");
    }

    if (m_shader_selected.size() != mf.presets.size()) {
        m_shader_selected.assign(mf.presets.size(), false);
    }

    auto* scroll = new brls::ScrollingFrame();
    auto* list   = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setMargins(16.0f, 24.0f, 16.0f, 32.0f);

    auto* hint = new brls::Label();
    hint->setText("Pick which shader presets to install now. Each is a "
                  "small download.");
    hint->setFontSize(18.0f);
    hint->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    list->addView(hint);

    for (std::size_t i = 0; i < mf.presets.size(); i++) {
        const auto& entry = mf.presets[i];
        auto* cell = new brls::BooleanCell();
        cell->title->setText(entry.name);
        cell->init(entry.name, m_shader_selected[i],
                   [this, i](bool value) {
                       if (i < m_shader_selected.size()) {
                           m_shader_selected[i] = value;
                       }
                   });
        list->addView(cell);
    }
    scroll->setContentView(list);
    return scroll;
}

brls::View* WizardActivity::buildScreenScraperStep() {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setPadding(48.0f, 48.0f, 48.0f, 48.0f);

    auto* hint = new brls::Label();
    hint->setText("Optional ScreenScraper.fr account for box art + "
                  "metadata scraping. Leave blank to use the anonymous "
                  "tier (slower, lower daily quota).");
    hint->setFontSize(18.0f);
    hint->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    box->addView(hint);

    auto* user_cell = new brls::InputCell();
    user_cell->init("Username", m_ss_user,
        [this](std::string v) { m_ss_user = std::move(v); },
        "ScreenScraper username", "", 64);
    box->addView(user_cell);

    auto* pass_cell = new MaskedInputCell();
    pass_cell->init("Password", m_ss_pass,
        [this](std::string v) { m_ss_pass = std::move(v); },
        "Tap to set", "ScreenScraper password", 64);
    box->addView(pass_cell);

    return box;
}

brls::View* WizardActivity::buildSteamGridDBStep() {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setPadding(48.0f, 48.0f, 48.0f, 48.0f);

    auto* hint = new brls::Label();
    hint->setText("Optional SteamGridDB API key. Used as a fallback "
                  "metadata source when ScreenScraper has no match. "
                  "Generate a key at steamgriddb.com/profile/preferences/api.");
    hint->setFontSize(18.0f);
    hint->setMargins(0.0f, 0.0f, 16.0f, 0.0f);
    box->addView(hint);

    auto* key_cell = new MaskedInputCell();
    key_cell->init("API key", m_sgdb_key,
        [this](std::string v) { m_sgdb_key = std::move(v); },
        "Tap to set", "SteamGridDB API key", 64);
    box->addView(key_cell);

    return box;
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

    // Lazy-load credential drafts the first time the user reaches
    // the relevant step. Pulling from the on-disk accounts.jsonc
    // means re-running the wizard preserves what the user already
    // typed.
    if (m_step == 4 && m_ss_user.empty() && m_ss_pass.empty()) {
        const auto& acc = ::foyer::scrapers::accounts().screenscraper;
        m_ss_user = acc.ssid;
        m_ss_pass = acc.sspassword;
    }
    if (m_step == 5 && m_sgdb_key.empty()) {
        m_sgdb_key = ::foyer::scrapers::accounts().steamgriddb.api_key;
    }

    brls::View* view = nullptr;
    switch (m_step) {
        case 0: view = buildWelcomeStep();        break;
        case 1: view = buildCoresStep();          break;
        case 2: view = buildBezelsStep();         break;
        case 3: view = buildShadersStep();        break;
        case 4: view = buildScreenScraperStep();  break;
        case 5: view = buildSteamGridDBStep();    break;
        default: view = buildDoneStep();          break;
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

    // Bezel install — same pattern, filtered manifest fed to a
    // detached Worker so the user lands on Home immediately while
    // the zips download.
    {
        const auto& bm = manifest_cache::bezels();
        ::foyer::library::BezelManifest bezel_filt{};
        bezel_filt.version  = bm.version;
        bezel_filt.upstream = bm.upstream;
        for (std::size_t i = 0;
             i < bm.packs.size() && i < m_bezel_selected.size(); i++)
        {
            if (m_bezel_selected[i]) {
                bezel_filt.packs.push_back(bm.packs[i]);
            }
        }
        if (!bezel_filt.packs.empty()) {
            g_bezel_worker = std::make_unique<::foyer::library::Worker>();
            const auto manifest_copy = bezel_filt;
            if (g_bezel_worker->start([manifest_copy](::foyer::library::Worker& w) {
                    w.set_status("Installing bezels…");
                    ::foyer::library::install_bezels(
                        manifest_copy, {}, {}, false, {});
                })) {
                foyer::log::write(
                    "[wizard] kicked bezel install for %zu packs\n",
                    bezel_filt.packs.size());
            } else {
                foyer::log::write(
                    "[wizard] bezel worker failed to start\n");
                g_bezel_worker.reset();
            }
        }
    }

    // Shader install — same again. install_shaders signature drops
    // bezels' only_pack arg (presets are always installed in bulk).
    {
        const auto& sm = manifest_cache::shaders();
        ::foyer::library::ShaderManifest shader_filt{};
        shader_filt.version = sm.version;
        for (std::size_t i = 0;
             i < sm.presets.size() && i < m_shader_selected.size(); i++)
        {
            if (m_shader_selected[i]) {
                shader_filt.presets.push_back(sm.presets[i]);
            }
        }
        if (!shader_filt.presets.empty()) {
            g_shader_worker = std::make_unique<::foyer::library::Worker>();
            const auto manifest_copy = shader_filt;
            if (g_shader_worker->start([manifest_copy](::foyer::library::Worker& w) {
                    w.set_status("Installing shaders…");
                    ::foyer::library::install_shaders(
                        manifest_copy, {}, false, {});
                })) {
                foyer::log::write(
                    "[wizard] kicked shader install for %zu presets\n",
                    shader_filt.presets.size());
            } else {
                foyer::log::write(
                    "[wizard] shader worker failed to start\n");
                g_shader_worker.reset();
            }
        }
    }

    // Persist scraper credentials. Empty values overwrite any prior
    // ones — by design, so the wizard's "leave blank" path actually
    // clears the field instead of silently retaining a stale entry.
    ::foyer::scrapers::set_account_field("screenscraper.ssid",       m_ss_user);
    ::foyer::scrapers::set_account_field("screenscraper.sspassword", m_ss_pass);
    ::foyer::scrapers::set_account_field("steamgriddb.api_key",      m_sgdb_key);

    // Toast summary so the user sees something happened the
    // moment they hit Finish — actual install progress lives
    // in the per-run log file at /foyer/data/logs/.
    int kicked_cores   = 0;
    int kicked_bezels  = 0;
    int kicked_shaders = 0;
    if (g_core_install_job) {
        kicked_cores = (int)filtered.cores.size();
    }
    if (g_bezel_worker)  kicked_bezels  = 1;
    if (g_shader_worker) kicked_shaders = 1;
    if (kicked_cores || kicked_bezels || kicked_shaders) {
        std::string summary = "Installing in background:";
        if (kicked_cores)   summary += " " + std::to_string(kicked_cores) + " cores";
        if (kicked_bezels)  summary += " · bezels";
        if (kicked_shaders) summary += " · shaders";
        brls::Application::notify(summary);
    } else {
        brls::Application::notify("Setup complete");
    }

    first_run::mark_complete();
    brls::Application::popActivity();
}

}  // namespace foyer::browser
