#include "tab/settings_tab.hpp"

#include "activity/wizard_activity.hpp"
#include "library_state.hpp"
#include "i18n/i18n.hpp"
#include "library/config.hpp"
#include "library/bezel_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/core_install_job.hpp"
#include "library/shader_installer.hpp"
#include "library/worker.hpp"
#include "manifest_cache.hpp"
#include "scrapers/accounts.hpp"
#include "update_check.hpp"
#include "widgets/masked_input_cell.hpp"

#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

namespace {

// Mirror brls's bundled settings.xml: outer tab Box keeps its
// default axis/alignment, ScrollingFrame fills the right pane
// with column+stretch+grow, and the inner host uses width=10000
// so cells stretch to the visible width without yoga measuring
// each row at intrinsic size (which is what causes the right
// pane to overflow off-screen).
brls::Box* tab_root_box() {
    auto* root = new brls::Box();
    root->setAxis(brls::Axis::COLUMN);
    root->setAlignItems(brls::AlignItems::STRETCH);
    root->setWidth(10000.0f);
    root->setPadding(20.0f, 32.0f, 32.0f, 32.0f);
    return root;
}

void wrap_with_scroll(brls::Box* host, brls::Box* parent) {
    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);
    parent->addView(scroll);
}

// ---- General tab ---------------------------------------------------------

struct LanguageOption { const char* i18n_key; const char* code; };
constexpr std::array<LanguageOption, 4> kLanguages = {{
    {"foyer/settings/language_follow_system", ""},
    {"English",                               "en"},
    {"Español",                               "es"},
    {"Português",                             "pt-BR"},
}};

int index_for_language(std::string_view current) {
    for (std::size_t i = 0; i < kLanguages.size(); i++) {
        if (kLanguages[i].code == current) return static_cast<int>(i);
    }
    return 0;
}

void apply_language(int idx) {
    if (idx < 0 || idx >= (int)kLanguages.size()) return;
    const std::string code = kLanguages[idx].code;
    foyer::library::set_language(code);
    using L = foyer::i18n::Language;
    if      (code.empty())   foyer::i18n::init();
    else if (code == "en")    foyer::i18n::set_language(L::English);
    else if (code == "es")    foyer::i18n::set_language(L::Spanish);
    else if (code == "pt-BR") foyer::i18n::set_language(L::PortugueseBrazil);
}

}  // namespace

// ============ FoyerGeneralTab ============================================

FoyerGeneralTab::FoyerGeneralTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("foyer/settings/general"_i18n); return h; }());

    const auto& cfg = foyer::library::config();
    std::vector<std::string> lang_labels;
    lang_labels.reserve(kLanguages.size());
    for (const auto& l : kLanguages) {
        const std::string key = l.i18n_key;
        if (key.find('/') != std::string::npos)
            lang_labels.emplace_back(brls::getStr(key));
        else
            lang_labels.emplace_back(key);
    }
    auto* lang = new brls::SelectorCell();
    lang->init("foyer/settings/language"_i18n,
               lang_labels,
               index_for_language(cfg.language),
               [](int) {},
               [](int selected) { apply_language(selected); });
    host->addView(lang);

    static const std::array<std::string_view, 3> kThemeCodes = {
        "", "light", "dark",
    };
    std::vector<std::string> theme_labels = {
        "Auto (follow system)", "Light", "Dark",
    };
    int theme_initial = 0;
    for (std::size_t i = 0; i < kThemeCodes.size(); i++) {
        if (kThemeCodes[i] == cfg.theme_override) {
            theme_initial = static_cast<int>(i);
            break;
        }
    }
    auto* theme = new brls::SelectorCell();
    theme->init("Theme", theme_labels, theme_initial,
                [](int) {},
                [](int selected) {
                    if (selected >= 0 && selected < (int)kThemeCodes.size()) {
                        ::foyer::library::set_theme_override(kThemeCodes[selected]);
                    }
                });
    host->addView(theme);

    auto* rerun_wizard = new brls::DetailCell();
    rerun_wizard->title->setText("Re-run wizard");
    rerun_wizard->detail->setText("Open now");
    rerun_wizard->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new ::foyer::browser::WizardActivity());
        return true;
    });
    host->addView(rerun_wizard);

    wrap_with_scroll(host, this);
}
brls::View* FoyerGeneralTab::create() { return new FoyerGeneralTab(); }

// ============ FoyerAccountsTab ===========================================

FoyerAccountsTab::FoyerAccountsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    const auto& acc = ::foyer::scrapers::accounts();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Scraper source"); return h; }());

    auto* source = new brls::SelectorCell();
    std::vector<std::string> source_labels = {
        "libretro-thumbnails", "ScreenScraper", "SteamGridDB",
    };
    using S = ::foyer::library::Config::Scraper;
    int initial = 0;
    switch (::foyer::library::config().preferred_scraper) {
        case S::Libretro:      initial = 0; break;
        case S::ScreenScraper: initial = 1; break;
        case S::SteamGridDB:   initial = 2; break;
    }
    source->init("Scraper source", source_labels, initial,
                 [](int) {},
                 [](int selected) {
                     using SS = ::foyer::library::Config::Scraper;
                     SS pick = SS::Libretro;
                     if      (selected == 1) pick = SS::ScreenScraper;
                     else if (selected == 2) pick = SS::SteamGridDB;
                     ::foyer::library::set_preferred_scraper(pick);
                 });
    host->addView(source);

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("ScreenScraper"); return h; }());

    auto* ss_user = new brls::InputCell();
    ss_user->init("Username", acc.screenscraper.ssid,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("screenscraper.ssid", v);
        }, "Account username", "", 64);
    host->addView(ss_user);

    auto* ss_pass = new MaskedInputCell();
    ss_pass->init("Password", acc.screenscraper.sspassword,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("screenscraper.sspassword", v);
        }, "Tap to set", "Account password", 64);
    host->addView(ss_pass);

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("SteamGridDB"); return h; }());

    auto* sgdb = new MaskedInputCell();
    sgdb->init("API key", acc.steamgriddb.api_key,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("steamgriddb.api_key", v);
        }, "Tap to set", "steamgriddb.com", 64);
    host->addView(sgdb);

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("RetroAchievements"); return h; }());

    auto* ra_user = new brls::InputCell();
    ra_user->init("Username", acc.retroachievements.user,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("retroachievements.user", v);
        }, "Account username", "", 64);
    host->addView(ra_user);

    auto* ra_token = new MaskedInputCell();
    ra_token->init("Token", acc.retroachievements.token,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("retroachievements.token", v);
        }, "Tap to set", "Web API token", 64);
    host->addView(ra_token);

    wrap_with_scroll(host, this);
}
brls::View* FoyerAccountsTab::create() { return new FoyerAccountsTab(); }

// ============ FoyerLibraryTab ============================================

FoyerLibraryTab::FoyerLibraryTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Library"); return h; }());

    auto* folder = new brls::DetailCell();
    folder->title->setText("Folder");
    folder->detail->setText("/foyer/roms");
    host->addView(folder);

    auto* rescan = new brls::DetailCell();
    rescan->title->setText("Rescan");
    rescan->detail->setText("Scan now");
    rescan->registerClickAction([](brls::View*) {
        library_state::rescan();
        brls::Application::notify("Library rescanned");
        return true;
    });
    host->addView(rescan);

    wrap_with_scroll(host, this);
}
brls::View* FoyerLibraryTab::create() { return new FoyerLibraryTab(); }

// ============ FoyerCoresTab ==============================================

FoyerCoresTab::FoyerCoresTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Cores"); return h; }());

    const auto& mf = manifest_cache::cores();
    if (mf.cores.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("No network / not fetched");
        host->addView(hint);
    } else {
        static std::unique_ptr<::foyer::library::CoreInstallJob> g_core_job;

        auto kick_install = [](::foyer::library::CoreManifest filt,
                               const std::string& tag) {
            if (g_core_job && g_core_job->active()) {
                brls::Application::notify("Install already running");
                return;
            }
            g_core_job = std::make_unique<::foyer::library::CoreInstallJob>();
            if (g_core_job->start(filt, std::string(), false)) {
                brls::Application::notify("Installing " + tag);
            } else {
                brls::Application::notify("Install failed to start");
                g_core_job.reset();
            }
        };

        auto* install_all = new brls::DetailCell();
        install_all->title->setText("Install all");
        install_all->detail->setText(
            std::to_string(mf.cores.size()) + " cores");
        install_all->registerClickAction([&mf, kick_install](brls::View*) {
            kick_install(mf, "all cores");
            return true;
        });
        host->addView(install_all);

        for (std::size_t i = 0; i < mf.cores.size(); i++) {
            const auto& entry = mf.cores[i];
            auto* cell = new brls::DetailCell();
            cell->title->setText(entry.name);
            cell->detail->setText("Tap to install");
            cell->registerClickAction([entry, &mf, kick_install](brls::View*) {
                ::foyer::library::CoreManifest filt{};
                filt.version = mf.version;
                filt.cores.push_back(entry);
                kick_install(filt, entry.name);
                return true;
            });
            host->addView(cell);
        }
    }

    wrap_with_scroll(host, this);
}
brls::View* FoyerCoresTab::create() { return new FoyerCoresTab(); }

// ============ FoyerBezelsTab =============================================

FoyerBezelsTab::FoyerBezelsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Bezels"); return h; }());

    const auto& mf = manifest_cache::bezels();
    if (mf.packs.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("No network / not fetched");
        host->addView(hint);
    } else {
        static std::unique_ptr<::foyer::library::Worker> g_bezel_job;

        auto kick = [](::foyer::library::BezelManifest filt,
                       const std::string& tag) {
            if (g_bezel_job && g_bezel_job->active() && !g_bezel_job->done()) {
                brls::Application::notify("Install already running");
                return;
            }
            g_bezel_job = std::make_unique<::foyer::library::Worker>();
            const auto copy = filt;
            g_bezel_job->start([copy](::foyer::library::Worker& w) {
                w.set_status("Installing bezels…");
                ::foyer::library::install_bezels(copy, {}, {}, false, {});
            });
            brls::Application::notify("Installing " + tag);
        };

        auto* install_all = new brls::DetailCell();
        install_all->title->setText("Install all");
        install_all->detail->setText(
            std::to_string(mf.packs.size()) + " packs");
        install_all->registerClickAction([&mf, kick](brls::View*) {
            kick(mf, "all bezels");
            return true;
        });
        host->addView(install_all);

        for (std::size_t i = 0; i < mf.packs.size(); i++) {
            const auto& entry = mf.packs[i];
            auto* cell = new brls::DetailCell();
            cell->title->setText(entry.name);
            cell->detail->setText("Tap to install");
            cell->registerClickAction([entry, &mf, kick](brls::View*) {
                ::foyer::library::BezelManifest filt{};
                filt.version  = mf.version;
                filt.upstream = mf.upstream;
                filt.packs.push_back(entry);
                kick(filt, entry.name);
                return true;
            });
            host->addView(cell);
        }
    }
    wrap_with_scroll(host, this);
}
brls::View* FoyerBezelsTab::create() { return new FoyerBezelsTab(); }

// ============ FoyerShadersTab ============================================

FoyerShadersTab::FoyerShadersTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Shaders"); return h; }());

    const auto& mf = manifest_cache::shaders();
    if (mf.presets.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("No network / not fetched");
        host->addView(hint);
    } else {
        static std::unique_ptr<::foyer::library::Worker> g_shader_job;

        auto kick = [](::foyer::library::ShaderManifest filt,
                       const std::string& tag) {
            if (g_shader_job && g_shader_job->active() && !g_shader_job->done()) {
                brls::Application::notify("Install already running");
                return;
            }
            g_shader_job = std::make_unique<::foyer::library::Worker>();
            const auto copy = filt;
            g_shader_job->start([copy](::foyer::library::Worker& w) {
                w.set_status("Installing shaders…");
                ::foyer::library::install_shaders(copy, {}, false, {});
            });
            brls::Application::notify("Installing " + tag);
        };

        auto* install_all = new brls::DetailCell();
        install_all->title->setText("Install all");
        install_all->detail->setText(
            std::to_string(mf.presets.size()) + " presets");
        install_all->registerClickAction([&mf, kick](brls::View*) {
            kick(mf, "all shaders");
            return true;
        });
        host->addView(install_all);

        for (std::size_t i = 0; i < mf.presets.size(); i++) {
            const auto& entry = mf.presets[i];
            auto* cell = new brls::DetailCell();
            cell->title->setText(entry.name);
            cell->detail->setText("Tap to install");
            cell->registerClickAction([entry, &mf, kick](brls::View*) {
                ::foyer::library::ShaderManifest filt{};
                filt.version = mf.version;
                filt.presets.push_back(entry);
                kick(filt, entry.name);
                return true;
            });
            host->addView(cell);
        }
    }
    wrap_with_scroll(host, this);
}
brls::View* FoyerShadersTab::create() { return new FoyerShadersTab(); }

// ============ FoyerCheatsTab =============================================

FoyerCheatsTab::FoyerCheatsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Cheats"); return h; }());

    const auto& mf = manifest_cache::cheats();
    if (mf.packs.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("No network / not fetched");
        host->addView(hint);
    } else {
        static std::unique_ptr<::foyer::library::Worker> g_cheat_job;

        auto kick = [](::foyer::library::CheatManifest filt,
                       const std::string& tag) {
            if (g_cheat_job && g_cheat_job->active() && !g_cheat_job->done()) {
                brls::Application::notify("Install already running");
                return;
            }
            g_cheat_job = std::make_unique<::foyer::library::Worker>();
            const auto copy = filt;
            g_cheat_job->start([copy](::foyer::library::Worker& w) {
                w.set_status("Installing cheats…");
                ::foyer::library::install_cheats(copy, {}, {}, false, {});
            });
            brls::Application::notify("Installing " + tag);
        };

        auto* install_all = new brls::DetailCell();
        install_all->title->setText("Install all");
        install_all->detail->setText(
            std::to_string(mf.packs.size()) + " packs");
        install_all->registerClickAction([&mf, kick](brls::View*) {
            kick(mf, "all cheats");
            return true;
        });
        host->addView(install_all);

        for (std::size_t i = 0; i < mf.packs.size(); i++) {
            const auto& entry = mf.packs[i];
            auto* cell = new brls::DetailCell();
            cell->title->setText(entry.name);
            cell->detail->setText("Tap to install");
            cell->registerClickAction([entry, &mf, kick](brls::View*) {
                ::foyer::library::CheatManifest filt{};
                filt.version = mf.version;
                filt.packs.push_back(entry);
                kick(filt, entry.name);
                return true;
            });
            host->addView(cell);
        }
    }
    wrap_with_scroll(host, this);
}
brls::View* FoyerCheatsTab::create() { return new FoyerCheatsTab(); }

// ============ FoyerUpdatesTab ============================================

FoyerUpdatesTab::FoyerUpdatesTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Updates"); return h; }());

    auto* check = new brls::DetailCell();
    check->title->setText("Check for updates");
    check->detail->setText("Prompt if newer");
    check->registerClickAction([](brls::View*) {
        if (!::foyer::browser::update_check::kick(/*verbose=*/true)) {
            brls::Application::notify("Update check already running");
        }
        return true;
    });
    host->addView(check);

    wrap_with_scroll(host, this);
}
brls::View* FoyerUpdatesTab::create() { return new FoyerUpdatesTab(); }

// ============ FoyerAboutTab ==============================================

FoyerAboutTab::FoyerAboutTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("About"); return h; }());

    auto* version = new brls::DetailCell();
    version->title->setText("Version");
    version->detail->setText(FOYER_DISPLAY_VERSION);
    host->addView(version);

    auto* logs = new brls::DetailCell();
    logs->title->setText("Logs");
    logs->detail->setText("/foyer/data/logs/");
    host->addView(logs);

    auto* config_path = new brls::DetailCell();
    config_path->title->setText("Config");
    config_path->detail->setText("/foyer/data/config/");
    host->addView(config_path);

    auto* assets_path = new brls::DetailCell();
    assets_path->title->setText("Assets");
    assets_path->detail->setText("/foyer/assets/");
    host->addView(assets_path);

    wrap_with_scroll(host, this);
}
brls::View* FoyerAboutTab::create() { return new FoyerAboutTab(); }

}  // namespace foyer::browser
