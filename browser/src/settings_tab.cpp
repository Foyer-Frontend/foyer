#include "tab/settings_tab.hpp"

#include "activity/settings_activity.hpp"
#include "activity/wizard_activity.hpp"
#include "library_state.hpp"
#include "i18n/i18n.hpp"
#include "library/config.hpp"
#include "library/bezel_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/core_installer.hpp"
#include "library/shader_installer.hpp"
#include "library/system_db.hpp"
#include "library/worker.hpp"
#include "install_queue.hpp"
#include "manifest_cache.hpp"
#include "scrapers/accounts.hpp"
#include "update_check.hpp"
#include "widgets/masked_input_cell.hpp"

#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <switch.h>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <array>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
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

// Poll a job (anything with done(), finish(), status_snapshot())
// at 500 ms cadence. Notifies a fresh status line each time the
// worker publishes one ("[1/55] fceumm - installed", …). When
// done: invoke on_done(), then defer the timer's delete to next
// tick — brls's RepeatingTimer::onUpdate writes after the
// callback returns, so freeing the timer inline would fault.
template <typename Job>
void watch_job(Job* job, std::function<void()> on_done) {
    auto* timer = new brls::RepeatingTimer();
    timer->setPeriod(500);
    auto last = std::make_shared<std::string>();
    timer->setCallback([job, on_done, timer, last]() {
        if (!job) return;
        const std::string snap = job->status_snapshot();
        if (!snap.empty() && snap != *last) {
            *last = snap;
            brls::Application::notify(snap);
        }
        if (!job->done()) return;
        job->finish();
        on_done();
        brls::sync([timer]() { timer->stop(); delete timer; });
    });
    timer->start();
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
                    if (selected < 0 || selected >= (int)kThemeCodes.size())
                        return;
                    const auto code = kThemeCodes[selected];
                    ::foyer::library::set_theme_override(code);

                    // Apply immediately so the new variant is live
                    // before we re-push: theme_watcher would catch
                    // it on the next tick, but waiting leaves the
                    // re-pushed Settings inflated with the old
                    // theme on its very first frame.
                    brls::ThemeVariant want;
                    if (code == "light") {
                        want = brls::ThemeVariant::LIGHT;
                    } else if (code == "dark") {
                        want = brls::ThemeVariant::DARK;
                    } else {
                        ColorSetId id = ColorSetId_Light;
                        setsysGetColorSetId(&id);
                        want = (id == ColorSetId_Dark)
                            ? brls::ThemeVariant::DARK
                            : brls::ThemeVariant::LIGHT;
                    }
                    brls::Application::getPlatform()->setThemeVariant(want);

                    // brls caches some theme-derived colors at
                    // view-attach time, so swapping the variant
                    // mid-Settings only repaints half the chrome.
                    // Pop + re-push SettingsActivity on the next
                    // tick to land in a freshly-inflated view that
                    // reads colors fresh.
                    brls::sync([]() {
                        brls::Application::popActivity();
                        brls::Application::pushActivity(
                            new ::foyer::browser::SettingsActivity());
                    });
                });
    host->addView(theme);

    static const std::array<std::string_view, 6> kRegionCodes = {
        "", "us", "eu", "jp", "br", "wor",
    };
    std::vector<std::string> region_labels = {
        "Auto", "United States", "Europe", "Japan", "Brazil", "World",
    };
    int region_initial = 0;
    for (std::size_t i = 0; i < kRegionCodes.size(); i++) {
        if (kRegionCodes[i] == cfg.region) {
            region_initial = static_cast<int>(i);
            break;
        }
    }
    auto* region = new brls::SelectorCell();
    region->init("Region", region_labels, region_initial,
                 [](int) {},
                 [](int selected) {
                     if (selected >= 0 && selected < (int)kRegionCodes.size()) {
                         ::foyer::library::set_region(kRegionCodes[selected]);
                     }
                 });
    host->addView(region);

    auto* boot_check = new brls::BooleanCell();
    boot_check->init("Check for updates on boot",
                     cfg.update_check_on_boot,
                     [](bool v) {
                         ::foyer::library::set_update_check_on_boot(v);
                     });
    host->addView(boot_check);

    auto* scrub_toggle = new brls::BooleanCell();
    scrub_toggle->init("Scrub extracted games",
                       cfg.scrub_extracted_enabled,
                       [](bool v) {
                           ::foyer::library::set_scrub_extracted_enabled(v);
                       });
    host->addView(scrub_toggle);

    static const std::array<int, 6> kScrubDays = { 3, 7, 10, 14, 30, 60 };
    std::vector<std::string> scrub_labels;
    scrub_labels.reserve(kScrubDays.size());
    for (int d : kScrubDays) scrub_labels.emplace_back(std::to_string(d) + " days");
    int scrub_initial = 2;  // 10 days default
    for (std::size_t i = 0; i < kScrubDays.size(); i++) {
        if (kScrubDays[i] == cfg.scrub_extracted_days) {
            scrub_initial = static_cast<int>(i);
            break;
        }
    }
    auto* scrub_days = new brls::SelectorCell();
    scrub_days->init("Scrub after", scrub_labels, scrub_initial,
                     [](int) {},
                     [](int selected) {
                         if (selected >= 0 && selected < (int)kScrubDays.size()) {
                             ::foyer::library::set_scrub_extracted_days(
                                 kScrubDays[selected]);
                         }
                     });
    host->addView(scrub_days);

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
        // All core / bezel / shader / cheat installs share one
        // global FIFO queue. enqueue() drops the job at the back
        // and starts it immediately if nothing is currently
        // running; otherwise the user gets a "Queued — N ahead"
        // toast and the queue runner picks it up after the
        // current job finishes.
        auto kick_install = [](::foyer::library::CoreManifest filt,
                               const std::string& tag, bool force) {
            ::foyer::browser::install_queue::enqueue(
                tag,
                [filt = std::move(filt), force]
                (::foyer::library::Worker& w) {
                    w.set_status("Starting core install…");
                    ::foyer::library::install_cores(filt,
                        [&w](const ::foyer::library::InstallProgress& p) {
                            const char* verb =
                                p.action == ::foyer::library::InstallAction::Skipped   ? "skipped" :
                                p.action == ::foyer::library::InstallAction::Updated   ? "updated" :
                                p.action == ::foyer::library::InstallAction::Installed ? "installed"
                                                                                       : "FAILED";
                            char buf[160];
                            std::snprintf(buf, sizeof(buf), "[%d/%d] %s - %s",
                                p.index, p.total, p.name.c_str(), verb);
                            w.set_status(buf);
                        },
                        force,
                        [&w]{ return w.cancelled(); });
                });
        };

        auto* install_all = new brls::DetailCell();
        install_all->title->setText("Install all");
        install_all->detail->setText(
            std::to_string(mf.cores.size()) + " cores");
        install_all->registerClickAction([&mf, kick_install](brls::View*) {
            // force=false → cores already at the manifest version
            // skip the download. Re-install per-row when needed.
            kick_install(mf, "all cores", /*force=*/false);
            return true;
        });
        host->addView(install_all);

        // Per-cell label + force flag depend on what's already on
        // disk for this core's nro. Reads the version sidecar
        // installed_core_version() writes after each successful
        // download. "Tap to install" only fires for cores the user
        // hasn't pulled yet; the rest get "Tap to re-install" or
        // "Tap to update".
        auto label_for = [](const ::foyer::library::CoreManifestEntry& e)
            -> std::pair<std::string, bool> {
            const std::string have =
                ::foyer::library::installed_core_version(e.nro);
            if (have.empty()) return {"Tap to install", false};
            if (have == e.version) return {"Tap to re-install", true};
            return {"Tap to update (v" + e.version + ")", false};
        };

        // Walk every SystemDef and emit a Header + a cell for each
        // of its cores that the manifest knows about. We iterate
        // systems (not manifest entries) so consoles that share a
        // core list — Genesis / Mega Drive / Master System / Game
        // Gear all wire genesisplusgx etc — each get their own
        // section. The same core appears under multiple headers
        // intentionally; install resolves the same nro regardless
        // of which section it was tapped from.
        auto find_manifest = [&mf](std::string_view name)
            -> const ::foyer::library::CoreManifestEntry* {
            for (const auto& e : mf.cores) if (e.name == name) return &e;
            return nullptr;
        };
        std::set<std::string> shown;
        for (const auto& sys : ::foyer::library::all_systems()) {
            if (::foyer::library::is_virtual_system(sys)) continue;

            std::vector<const ::foyer::library::CoreDef*> defs;
            for (const auto& cd : sys.cores) {
                if (find_manifest(cd.name)) defs.push_back(&cd);
            }
            if (defs.empty()) continue;
            host->addView([&sys]() {
                auto* h = new brls::Header();
                h->setTitle(std::string(sys.display_name));
                return h;
            }());
            for (const auto* cd : defs) {
                shown.insert(std::string(cd->name));
                const auto* entry = find_manifest(cd->name);
                ::foyer::library::CoreManifestEntry copy = *entry;
                const std::string label{cd->display_name};
                const auto [detail, force] = label_for(copy);
                auto* cell = new brls::DetailCell();
                cell->title->setText(label);
                cell->detail->setText(detail);
                cell->registerClickAction(
                    [copy, mf_version = mf.version, kick_install, force]
                    (brls::View*) {
                        ::foyer::library::CoreManifest filt{};
                        filt.version = mf_version;
                        filt.cores.push_back(copy);
                        kick_install(filt, copy.name, force);
                        return true;
                    });
                host->addView(cell);
            }
        }
        // Manifest entries that aren't referenced by any SystemDef
        // (e.g. an experimental core we haven't wired into
        // system_db yet) — keep them reachable under "Other".
        std::vector<const ::foyer::library::CoreManifestEntry*> orphans;
        for (const auto& e : mf.cores)
            if (!shown.count(e.name)) orphans.push_back(&e);
        if (!orphans.empty()) {
            host->addView([]() {
                auto* h = new brls::Header();
                h->setTitle("Other");
                return h;
            }());
            for (const auto* e : orphans) {
                ::foyer::library::CoreManifestEntry copy = *e;
                const auto [detail, force] = label_for(copy);
                auto* cell = new brls::DetailCell();
                cell->title->setText(copy.name);
                cell->detail->setText(detail);
                cell->registerClickAction(
                    [copy, mf_version = mf.version, kick_install, force]
                    (brls::View*) {
                        ::foyer::library::CoreManifest filt{};
                        filt.version = mf_version;
                        filt.cores.push_back(copy);
                        kick_install(filt, copy.name, force);
                        return true;
                    });
                host->addView(cell);
            }
        }
    }

    wrap_with_scroll(host, this);
}
brls::View* FoyerCoresTab::create() { return new FoyerCoresTab(); }

// ============ FoyerEmulatorsTab ==========================================

FoyerEmulatorsTab::FoyerEmulatorsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();
    host->addView([]() {
        auto* h = new brls::Header();
        h->setTitle("Default emulator per system");
        return h;
    }());

    const auto& cfg = ::foyer::library::config();
    for (const auto& sys : ::foyer::library::all_systems()) {
        if (::foyer::library::is_virtual_system(sys)) continue;
        if (sys.cores.empty()) continue;

        // Selector options = the system's core span. Order matters:
        // cores[0] is the system's bundled default, so picking index
        // 0 maps back to "" in config (i.e. use the default rather
        // than pin a name).
        std::vector<std::string> labels;
        std::vector<std::string> codes;
        labels.reserve(sys.cores.size());
        codes.reserve(sys.cores.size());
        for (const auto& c : sys.cores) {
            labels.emplace_back(c.display_name);
            codes.emplace_back(c.name);
        }
        const char* current = cfg.default_core_for(sys.folder_name);
        int initial = 0;
        if (current && *current) {
            for (std::size_t i = 0; i < codes.size(); i++) {
                if (codes[i] == current) { initial = static_cast<int>(i); break; }
            }
        }
        const std::string folder{sys.folder_name};
        auto* cell = new brls::SelectorCell();
        cell->init(std::string(sys.display_name), labels, initial,
                   [](int) {},
                   [folder, codes](int selected) {
                       if (selected < 0 || selected >= (int)codes.size()) return;
                       ::foyer::library::set_default_core_for(
                           folder, codes[selected]);
                   });
        host->addView(cell);
    }

    wrap_with_scroll(host, this);
}
brls::View* FoyerEmulatorsTab::create() { return new FoyerEmulatorsTab(); }

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
        auto kick = [](::foyer::library::BezelManifest filt,
                       const std::string& tag) {
            ::foyer::browser::install_queue::enqueue(
                tag,
                [filt = std::move(filt)]
                (::foyer::library::Worker& w) {
                    w.set_status("Starting bezel install…");
                    ::foyer::library::install_bezels(filt,
                        [&w](const ::foyer::library::BezelInstallProgress& p) {
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "[%d/%d] %s",
                                p.index, p.total, p.name.c_str());
                            w.set_status(buf);
                        },
                        {}, false, [&w]{ return w.cancelled(); });
                });
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

        // Group manifest entries by SystemDef. Bezel packs are
        // keyed by foyer folder name (e.g. "nes", "snes"), so we
        // can look up the matching SystemDef directly.
        std::vector<bool> placed(mf.packs.size(), false);
        for (const auto& sys : ::foyer::library::all_systems()) {
            if (::foyer::library::is_virtual_system(sys)) continue;
            std::vector<std::size_t> picks;
            for (std::size_t i = 0; i < mf.packs.size(); i++) {
                if (placed[i]) continue;
                if (mf.packs[i].name == sys.folder_name) picks.push_back(i);
            }
            if (picks.empty()) continue;
            host->addView([&sys]() {
                auto* h = new brls::Header();
                h->setTitle(std::string(sys.display_name));
                return h;
            }());
            for (std::size_t i : picks) {
                placed[i] = true;
                const auto& entry = mf.packs[i];
                auto* cell = new brls::DetailCell();
                cell->title->setText(entry.name);
                const auto have =
                    ::foyer::library::installed_bezel_version(entry.name);
                cell->detail->setText(
                    have.empty()           ? "Tap to install" :
                    have == entry.version  ? "Tap to re-install"
                                           : "Tap to update");
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
        std::vector<std::size_t> rest;
        for (std::size_t i = 0; i < mf.packs.size(); i++)
            if (!placed[i]) rest.push_back(i);
        if (!rest.empty()) {
            host->addView([]() {
                auto* h = new brls::Header();
                h->setTitle("Other");
                return h;
            }());
            for (std::size_t i : rest) {
                const auto& entry = mf.packs[i];
                auto* cell = new brls::DetailCell();
                cell->title->setText(entry.name);
                const auto have =
                    ::foyer::library::installed_bezel_version(entry.name);
                cell->detail->setText(
                    have.empty()           ? "Tap to install" :
                    have == entry.version  ? "Tap to re-install"
                                           : "Tap to update");
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
        auto kick = [](::foyer::library::ShaderManifest filt,
                       const std::string& tag) {
            ::foyer::browser::install_queue::enqueue(
                tag,
                [filt = std::move(filt)]
                (::foyer::library::Worker& w) {
                    w.set_status("Starting shader install…");
                    ::foyer::library::install_shaders(filt,
                        [&w](const ::foyer::library::ShaderInstallProgress& p) {
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "[%d/%d] %s",
                                p.index, p.total, p.name.c_str());
                            w.set_status(buf);
                        },
                        false, [&w]{ return w.cancelled(); });
                });
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
            const auto have =
                ::foyer::library::installed_shader_version(entry.name);
            cell->detail->setText(
                have.empty()           ? "Tap to install" :
                have == entry.version  ? "Tap to re-install"
                                       : "Tap to update");
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
        auto kick = [](::foyer::library::CheatManifest filt,
                       const std::string& tag) {
            ::foyer::browser::install_queue::enqueue(
                tag,
                [filt = std::move(filt)]
                (::foyer::library::Worker& w) {
                    w.set_status("Starting cheat install…");
                    ::foyer::library::install_cheats(filt,
                        [&w](const ::foyer::library::CheatInstallProgress& p) {
                            char buf[160];
                            std::snprintf(buf, sizeof(buf),
                                "[%d/%d] %s",
                                p.index, p.total, p.name.c_str());
                            w.set_status(buf);
                        },
                        {}, false, [&w]{ return w.cancelled(); });
                });
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

        std::vector<bool> placed(mf.packs.size(), false);
        for (const auto& sys : ::foyer::library::all_systems()) {
            if (::foyer::library::is_virtual_system(sys)) continue;
            std::vector<std::size_t> picks;
            for (std::size_t i = 0; i < mf.packs.size(); i++) {
                if (placed[i]) continue;
                if (mf.packs[i].name == sys.folder_name) picks.push_back(i);
            }
            if (picks.empty()) continue;
            host->addView([&sys]() {
                auto* h = new brls::Header();
                h->setTitle(std::string(sys.display_name));
                return h;
            }());
            for (std::size_t i : picks) {
                placed[i] = true;
                const auto& entry = mf.packs[i];
                auto* cell = new brls::DetailCell();
                cell->title->setText(entry.name);
                const auto have =
                    ::foyer::library::installed_cheat_version(entry.name);
                cell->detail->setText(
                    have.empty()           ? "Tap to install" :
                    have == entry.version  ? "Tap to re-install"
                                           : "Tap to update");
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
        std::vector<std::size_t> rest;
        for (std::size_t i = 0; i < mf.packs.size(); i++)
            if (!placed[i]) rest.push_back(i);
        if (!rest.empty()) {
            host->addView([]() {
                auto* h = new brls::Header();
                h->setTitle("Other");
                return h;
            }());
            for (std::size_t i : rest) {
                const auto& entry = mf.packs[i];
                auto* cell = new brls::DetailCell();
                cell->title->setText(entry.name);
                const auto have =
                    ::foyer::library::installed_cheat_version(entry.name);
                cell->detail->setText(
                    have.empty()           ? "Tap to install" :
                    have == entry.version  ? "Tap to re-install"
                                           : "Tap to update");
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
