#include "tab/settings_tab.hpp"

#include "activity/downloads_activity.hpp"
#include "activity/log_viewer_activity.hpp"
#include "activity/settings_activity.hpp"
#include "activity/wizard_activity.hpp"
#include "library_state.hpp"
#include "i18n/i18n.hpp"
#include "mtp.hpp"
#include "library/config.hpp"
#include "library/bezel_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/core_installer.hpp"
#include "library/shader_installer.hpp"
#include "library/system_db.hpp"
#include "library/worker.hpp"
#include "install_queue.hpp"
#include "manifest_cache.hpp"
#include "platform/log.hpp"
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
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

namespace {
// Tombstone set: install_queue listeners run through brls::sync,
// which defers them to the next UI tick. If the tab is destroyed
// before that tick the captured `this` would be dangling.
// Subscribing tab adds itself here on ctor and removes on dtor,
// both on the UI thread — the deferred lambda gates its refresh
// walk on this membership check.
std::mutex                       g_alive_mu;
std::unordered_set<const void*>  g_alive_tabs;

bool tab_is_alive(const void* p) {
    std::unique_lock lk{g_alive_mu};
    return g_alive_tabs.count(p) > 0;
}
}  // namespace

bool is_tab_alive(const void* p) { return tab_is_alive(p); }

InstallRefreshTab::InstallRefreshTab() {
    std::unique_lock lk{g_alive_mu};
    g_alive_tabs.insert(this);
}

InstallRefreshTab::~InstallRefreshTab() {
    if (m_sub >= 0) {
        ::foyer::browser::install_queue::unsubscribe(m_sub);
        m_sub = -1;
    }
    if (m_poll) {
        m_poll->stop();
        delete m_poll;
        m_poll = nullptr;
    }
    std::unique_lock lk{g_alive_mu};
    g_alive_tabs.erase(this);
}

void InstallRefreshTab::add_refresher(std::function<void()> fn) {
    m_refreshers.push_back(std::move(fn));
}

void InstallRefreshTab::refresh_labels() {
    for (auto& r : m_refreshers) if (r) r();
}

void InstallRefreshTab::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    refresh_labels();

    // Keep refreshing while the user is still on the tab so a
    // download that finishes mid-view flips its row label without
    // requiring a tab switch. brls::RepeatingTask runs on the UI
    // thread, so the cell mutations are safe.
    if (!m_poll) {
        class TabPoll : public brls::RepeatingTask {
        public:
            explicit TabPoll(InstallRefreshTab* h)
                : brls::RepeatingTask(2000), host(h) {}
            void run() override { host->refresh_labels(); }
        private:
            InstallRefreshTab* host;
        };
        m_poll = new TabPoll(this);
    }
    m_poll->start();
}

void InstallRefreshTab::willDisappear(bool resetState) {
    if (m_poll) m_poll->stop();
    brls::Box::willDisappear(resetState);
}

void InstallRefreshTab::start_listening() {
    if (m_sub >= 0) return;
    m_sub = ::foyer::browser::install_queue::subscribe(
        [this](const std::string& /*tag*/) {
            // Defer the refresh through brls::sync so it lands on
            // the NEXT UI frame instead of running inside
            // install_queue::poll_tick's stack frame. Two reasons:
            //  - The poll_tick path may have already spawned the
            //    next queued worker + called brls::Application::
            //    notify under the install_queue mutex; chaining a
            //    view mutation onto that re-entry is fragile.
            //  - Gives the dtor a chance to fire first if the tab
            //    is being torn down by an activity pop happening
            //    on the same tick. Once we unsubscribe in the
            //    dtor, install_queue can't dispatch new listener
            //    invocations — but a brls::sync lambda queued
            //    BEFORE the unsubscribe can still fire later, so
            //    we capture by raw `this` + check liveness via a
            //    member-owned guard.
            auto* self = this;
            brls::sync([self]() {
                if (!::foyer::browser::is_tab_alive(self)) return;
                for (auto& r : self->m_refreshers) if (r) r();
            });
        });
}

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

void InstallRefreshTab::reset_content() {
    if (!m_content) return;
    // Drop refresher closures that point at the cells we're about to
    // free, then free the cells themselves. The poll loop reads
    // m_refreshers under the UI thread; we're on the UI thread here,
    // so the clear is race-free.
    m_refreshers.clear();
    m_content->clearViews();
}

void InstallRefreshTab::setup_refresh_header(const std::string& label,
                                             std::function<void()> prefetch) {
    auto* host = tab_root_box();

    // Persistent "Check for updates" cell. Lives outside m_content so
    // the cell instance (and its captured this/refresh pointer) stays
    // stable across populate_content() rebuilds. Also doubles as the
    // manifest entry point on chain-back-from-core paths: main.cpp's
    // fast_returned branch skips the boot prefetch, so this cell is
    // the user's only way to populate the tab without restarting
    // foyer through the splash.
    auto* refresh = new brls::DetailCell();
    refresh->title->setText(std::string("Check for updates · ") + label);
    refresh->detail->setText("Tap to fetch the latest manifest");

    refresh->registerClickAction(
        [this, prefetch = std::move(prefetch), refresh]
        (brls::View*) {
            if (m_refreshing) return true;
            m_refreshing = true;
            refresh->detail->setText("Fetching manifest…");
            brls::async([this, prefetch, refresh]() {
                try {
                    prefetch();
                } catch (...) {
                    foyer::log::write(
                        "[settings] manifest prefetch threw\n");
                }
                brls::sync([this, refresh]() {
                    auto* self = this;
                    if (!::foyer::browser::is_tab_alive(self)) return;
                    self->m_refreshing = false;
                    refresh->detail->setText(
                        "Tap to fetch the latest manifest");
                    self->populate_content();
                });
            });
            return true;
        });
    host->addView(refresh);

    m_content = new brls::Box();
    m_content->setAxis(brls::Axis::COLUMN);
    m_content->setAlignItems(brls::AlignItems::STRETCH);
    host->addView(m_content);

    wrap_with_scroll(host, this);
}

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
    // SteamGridDB hidden from the picker until its scraper path is
    // polished (auth flow, asset-kind filtering, fanart fallback —
    // separate change). The stored Config::SteamGridDB value still
    // works for users who picked it on a previous build; we just
    // don't surface it as a new option. Libretro + ScreenScraper
    // are the supported choices for now.
    std::vector<std::string> source_labels = {
        "libretro-thumbnails", "ScreenScraper",
    };
    using S = ::foyer::library::Config::Scraper;
    int initial = 0;
    switch (::foyer::library::config().preferred_scraper) {
        case S::Libretro:      initial = 0; break;
        case S::ScreenScraper: initial = 1; break;
        case S::SteamGridDB:   initial = 1; break;  // collapse onto SS for the picker view
    }
    source->init("Scraper source", source_labels, initial,
                 [](int) {},
                 [](int selected) {
                     using SS = ::foyer::library::Config::Scraper;
                     const SS pick = (selected == 1)
                         ? SS::ScreenScraper : SS::Libretro;
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

    // SteamGridDB section hidden alongside the scraper-picker entry
    // (above) until the scraper integration is polished. The
    // existing stored key (if any) is preserved in accounts.jsonc.

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("RetroAchievements"); return h; }());

    auto* ra_user = new brls::InputCell();
    ra_user->init("Username", acc.retroachievements.user,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("retroachievements.user", v);
        }, "Account username", "", 64);
    host->addView(ra_user);

    // Web password for the user's RA account. rcheevos exchanges it
    // for a session token on first login.
    auto* ra_pass = new MaskedInputCell();
    ra_pass->init("Password", acc.retroachievements.password,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("retroachievements.password", v);
        }, "Tap to set", "RA web password", 64);
    host->addView(ra_pass);

    // Web API Key — optional, REST stats only. Used by the browser
    // to pre-fill achievement progress on game detail views before
    // the user has booted the rom. The user's Connect API Token is
    // no longer surfaced as a separate field — rcheevos derives it
    // from username + password on first login. RA settings page →
    // Keys section → "Web API Key".
    auto* ra_webapi = new MaskedInputCell();
    ra_webapi->init("Web API Key (optional)", acc.retroachievements.web_api_key,
        [](std::string v) {
            ::foyer::scrapers::set_account_field("retroachievements.web_api_key", v);
        }, "Tap to set", "REST stats only", 64);
    host->addView(ra_webapi);

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
        library_state::rescan_forced();
        brls::Application::notify("Library rescanned");
        return true;
    });
    host->addView(rescan);

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("USB (MTP)"); return h; }());

    // Restart helper — libhaze::Initialize is rejected if it's
    // already running, so flipping a mount on requires stopping
    // first. mtp_stop is a no-op when nothing is running.
    auto restart_mtp = []() {
        ::foyer::browser::mtp_stop();
        const auto& cfg = ::foyer::library::config();
        if (cfg.mtp_expose_roms || cfg.mtp_expose_logs) {
            ::foyer::browser::mtp_start();
            brls::Application::notify("MTP server restarted");
        } else {
            brls::Application::notify("MTP server stopped");
        }
    };

    {
        auto* cell = new brls::BooleanCell();
        cell->init("Expose roms (/foyer/roms)",
            ::foyer::library::config().mtp_expose_roms,
            [restart_mtp](bool v) {
                ::foyer::library::set_bool("mtp_expose_roms", v);
                restart_mtp();
            });
        host->addView(cell);
    }
    // "Expose logs" cell removed in v0.6.117 — see mtp.cpp.

    wrap_with_scroll(host, this);
}
brls::View* FoyerLibraryTab::create() { return new FoyerLibraryTab(); }

// ============ FoyerCoresTab ==============================================

FoyerCoresTab::FoyerCoresTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);
    setup_refresh_header("Cores",
        []() { ::foyer::browser::manifest_cache::prefetch_cores(); });
    populate_content();
    start_listening();
}

void FoyerCoresTab::populate_content() {
    reset_content();
    auto* host = m_content;

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Cores"); return h; }());

    const auto& mf = manifest_cache::cores();
    if (mf.cores.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("Tap \"Check for updates\" above to fetch");
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
                            // Started fires BEFORE the download, so report
                            // "downloading" rather than the catch-all "FAILED"
                            // the prior switch used — Started was unmatched
                            // and fell through to the failure label, which
                            // made the first toast for every core read
                            // "[1/1] fceumm - FAILED" right as the download
                            // kicked off.
                            const char* verb =
                                p.action == ::foyer::library::InstallAction::Started   ? "downloading" :
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
            if (have.empty())
                return {"Not installed · Tap to install v" + e.version, false};
            if (have == e.version)
                return {"Installed v" + have
                        + " (latest) · Tap to re-install", true};
            return {"Installed v" + have
                    + " · Tap to update to v" + e.version, false};
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
                auto* cell = new brls::DetailCell();
                cell->title->setText(label);
                {
                    const auto [detail, force] = label_for(copy);
                    cell->detail->setText(detail);
                }
                add_refresher([cell, copy, label_for]() {
                    const auto [detail, force] = label_for(copy);
                    cell->detail->setText(detail);
                });
                cell->registerClickAction(
                    [copy, mf_version = mf.version, kick_install, label_for]
                    (brls::View*) {
                        const auto [detail, force] = label_for(copy);
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
                auto* cell = new brls::DetailCell();
                cell->title->setText(copy.name);
                {
                    const auto [detail, force] = label_for(copy);
                    cell->detail->setText(detail);
                }
                add_refresher([cell, copy, label_for]() {
                    const auto [detail, force] = label_for(copy);
                    cell->detail->setText(detail);
                });
                cell->registerClickAction(
                    [copy, mf_version = mf.version, kick_install, label_for]
                    (brls::View*) {
                        const auto [detail, force] = label_for(copy);
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
    setup_refresh_header("Bezels",
        []() { ::foyer::browser::manifest_cache::prefetch_bezels(); });
    populate_content();
    start_listening();
}

void FoyerBezelsTab::populate_content() {
    reset_content();
    auto* host = m_content;
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Bezels"); return h; }());

    const auto& mf = manifest_cache::bezels();
    if (mf.packs.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("Tap \"Check for updates\" above to fetch");
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
                            char buf[200];
                            if (!p.phase.empty()) {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s — %s %d/%d",
                                    p.index, p.total, p.name.c_str(),
                                    p.phase.c_str(),
                                    p.phase_index, p.phase_total);
                            } else {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s",
                                    p.index, p.total, p.name.c_str());
                            }
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
                auto bezel_label = [](const ::foyer::library::BezelManifestEntry& e) {
                    const auto have =
                        ::foyer::library::installed_bezel_version(e.name);
                    return std::string(
                        have.empty()       ? "Tap to install" :
                        have == e.version  ? "Tap to re-install"
                                           : "Tap to update");
                };
                cell->detail->setText(bezel_label(entry));
                add_refresher([cell, entry, bezel_label]() {
                    cell->detail->setText(bezel_label(entry));
                });
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
                auto bezel_label = [](const ::foyer::library::BezelManifestEntry& e) {
                    const auto have =
                        ::foyer::library::installed_bezel_version(e.name);
                    return std::string(
                        have.empty()       ? "Tap to install" :
                        have == e.version  ? "Tap to re-install"
                                           : "Tap to update");
                };
                cell->detail->setText(bezel_label(entry));
                add_refresher([cell, entry, bezel_label]() {
                    cell->detail->setText(bezel_label(entry));
                });
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
}
brls::View* FoyerBezelsTab::create() { return new FoyerBezelsTab(); }

// ============ FoyerShadersTab ============================================

FoyerShadersTab::FoyerShadersTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);
    setup_refresh_header("Shaders",
        []() { ::foyer::browser::manifest_cache::prefetch_shaders(); });
    populate_content();
    start_listening();
}

void FoyerShadersTab::populate_content() {
    reset_content();
    auto* host = m_content;
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Shaders"); return h; }());

    const auto& mf = manifest_cache::shaders();
    if (mf.presets.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("Tap \"Check for updates\" above to fetch");
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
                            char buf[200];
                            if (!p.phase.empty()) {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s — %s %d/%d",
                                    p.index, p.total, p.name.c_str(),
                                    p.phase.c_str(),
                                    p.phase_index, p.phase_total);
                            } else {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s",
                                    p.index, p.total, p.name.c_str());
                            }
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
            auto shader_label = [](const ::foyer::library::ShaderManifestEntry& e) {
                const auto have =
                    ::foyer::library::installed_shader_version(e.name);
                return std::string(
                    have.empty()       ? "Tap to install" :
                    have == e.version  ? "Tap to re-install"
                                       : "Tap to update");
            };
            cell->detail->setText(shader_label(entry));
            add_refresher([cell, entry, shader_label]() {
                cell->detail->setText(shader_label(entry));
            });
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
}
brls::View* FoyerShadersTab::create() { return new FoyerShadersTab(); }

// ============ FoyerCheatsTab =============================================

FoyerCheatsTab::FoyerCheatsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);
    setup_refresh_header("Cheats",
        []() { ::foyer::browser::manifest_cache::prefetch_cheats(); });
    populate_content();
    start_listening();
}

void FoyerCheatsTab::populate_content() {
    reset_content();
    auto* host = m_content;
    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Cheats"); return h; }());

    const auto& mf = manifest_cache::cheats();
    if (mf.packs.empty()) {
        auto* hint = new brls::DetailCell();
        hint->title->setText("Manifest unavailable");
        hint->detail->setText("Tap \"Check for updates\" above to fetch");
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
                            char buf[200];
                            if (!p.phase.empty()) {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s — %s %d/%d",
                                    p.index, p.total, p.name.c_str(),
                                    p.phase.c_str(),
                                    p.phase_index, p.phase_total);
                            } else {
                                std::snprintf(buf, sizeof(buf),
                                    "[%d/%d] %s",
                                    p.index, p.total, p.name.c_str());
                            }
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
                auto cheat_label = [](const ::foyer::library::CheatManifestEntry& e) {
                    const auto have =
                        ::foyer::library::installed_cheat_version(e.name);
                    return std::string(
                        have.empty()       ? "Tap to install" :
                        have == e.version  ? "Tap to re-install"
                                           : "Tap to update");
                };
                cell->detail->setText(cheat_label(entry));
                add_refresher([cell, entry, cheat_label]() {
                    cell->detail->setText(cheat_label(entry));
                });
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
                auto cheat_label = [](const ::foyer::library::CheatManifestEntry& e) {
                    const auto have =
                        ::foyer::library::installed_cheat_version(e.name);
                    return std::string(
                        have.empty()       ? "Tap to install" :
                        have == e.version  ? "Tap to re-install"
                                           : "Tap to update");
                };
                cell->detail->setText(cheat_label(entry));
                add_refresher([cell, entry, cheat_label]() {
                    cell->detail->setText(cheat_label(entry));
                });
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
}
brls::View* FoyerCheatsTab::create() { return new FoyerCheatsTab(); }

// ============ FoyerDownloadsTab ==========================================

FoyerDownloadsTab::FoyerDownloadsTab() {
    // Single click-to-enter cell. The earlier willAppear auto-push
    // surprised users (focusing the sidebar entry instantly threw
    // them into a different activity); switching back to an explicit
    // A-press keeps tab focus and activity entry as separate inputs.
    // No header, no paragraph — just the affordance.
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    auto* open_cell = new brls::DetailCell();
    open_cell->title->setText("Open downloads");
    open_cell->detail->setText("Cores · Bezels · Shaders · Cheats");
    open_cell->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(new DownloadsActivity());
        return true;
    });
    host->addView(open_cell);

    wrap_with_scroll(host, this);
}
brls::View* FoyerDownloadsTab::create() { return new FoyerDownloadsTab(); }

// ============ FoyerUpdatesTab ============================================

FoyerUpdatesTab::FoyerUpdatesTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setAlignItems(brls::AlignItems::STRETCH);

    auto* host = tab_root_box();

    host->addView([]() { auto* h = new brls::Header(); h->setTitle("Updates"); return h; }());

    // 4 separate buttons so users can poll just the bucket they care
    // about. Foyer check pops the download prompt; content checks just
    // toast a count summary.
    {
        auto* c = new brls::DetailCell();
        c->title->setText("Check for foyer updates");
        c->detail->setText("App self-update");
        c->registerClickAction([](brls::View*) {
            if (!::foyer::browser::update_check::kick(/*verbose=*/true)) {
                brls::Application::notify("Update check already running");
            }
            return true;
        });
        host->addView(c);
    }
    {
        auto* c = new brls::DetailCell();
        c->title->setText("Check for cores updates");
        c->detail->setText("Compare installed cores vs manifest");
        c->registerClickAction([](brls::View*) {
            if (!::foyer::browser::update_check::kick_content(
                    ::foyer::browser::update_check::Section::Cores)) {
                brls::Application::notify("Content check already running");
            }
            return true;
        });
        host->addView(c);
    }
    {
        auto* c = new brls::DetailCell();
        c->title->setText("Check for bezels updates");
        c->detail->setText("Compare installed bezel packs vs manifest");
        c->registerClickAction([](brls::View*) {
            if (!::foyer::browser::update_check::kick_content(
                    ::foyer::browser::update_check::Section::Bezels)) {
                brls::Application::notify("Content check already running");
            }
            return true;
        });
        host->addView(c);
    }
    {
        auto* c = new brls::DetailCell();
        c->title->setText("Check for cheats updates");
        c->detail->setText("Compare installed cheat packs vs manifest");
        c->registerClickAction([](brls::View*) {
            if (!::foyer::browser::update_check::kick_content(
                    ::foyer::browser::update_check::Section::Cheats)) {
                brls::Application::notify("Content check already running");
            }
            return true;
        });
        host->addView(c);
    }

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
    logs->detail->setText("Tap to view");
    logs->registerClickAction([](brls::View*) {
        brls::Application::pushActivity(
            new ::foyer::browser::LogListActivity());
        return true;
    });
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
