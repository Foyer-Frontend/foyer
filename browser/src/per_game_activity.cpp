#include "activity/per_game_activity.hpp"

#include "activity/bezel_source_browser_activity.hpp"
#include "activity/per_game_bezel_picker_activity.hpp"
#include "library/config.hpp"
#include "library/per_game.hpp"
#include "library/per_game_bezel.hpp"
#include "library/shader_installer.hpp"
#include "library/system_db.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

PerGameActivity::PerGameActivity(std::string_view system_folder,
                                 std::string_view rom_path)
    : m_system_folder(system_folder), m_rom_path(rom_path) {}

brls::View* PerGameActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    const std::string rom = m_rom_path;
    const std::string sys = m_system_folder;

    // Core picker — list every core the SystemDef recognises and tag
    // the row matching the per-system / general default with
    // "(default)". The cell shows the actual resolved core (not
    // "System default") so the user always sees what's in effect.
    // Picking any row writes that core as the per-game override; no
    // separate "follow default" sentinel — picking the (default)
    // row is the explicit way to mirror the default at this moment.
    {
        std::vector<std::string> labels;
        std::vector<std::string> codes;
        if (const auto* def =
                ::foyer::library::find_system_by_folder(m_system_folder)) {
            for (const auto& c : def->cores) {
                labels.emplace_back(c.display_name);
                codes.emplace_back(c.name);
            }
        }
        const auto& cfg = ::foyer::library::config();
        std::string system_default_code;
        if (const char* sd = cfg.default_core_for(m_system_folder);
            sd && *sd) {
            system_default_code = sd;
        } else if (const auto* def =
                ::foyer::library::find_system_by_folder(m_system_folder);
            def && !def->cores.empty()) {
            // SystemDef's first core is the implicit default when no
            // per-system override exists (matches resolve_core).
            system_default_code = def->cores.front().name;
        }
        // Tag the default row.
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == system_default_code) {
                labels[i] += " (default)";
                break;
            }
        }
        // Resolved current = per-game override if any, else the
        // system default. The cell will display whichever row is at
        // `initial` — so finding the index that matches the resolved
        // value is enough to make the cell show the right text.
        const auto per_game = ::foyer::library::per_game_core_for(rom);
        const std::string resolved =
            !per_game.empty() ? per_game : system_default_code;
        int initial = 0;
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == resolved) { initial = (int)i; break; }
        }
        auto* cell = new brls::SelectorCell();
        cell->init("Default core", labels, initial,
                   [](int) {},
                   [rom, codes](int selected) {
                       if (selected < 0 || selected >= (int)codes.size()) return;
                       ::foyer::library::set_per_game_core(rom, codes[selected]);
                   });
        host->addView(cell);
    }

    // Shader picker — same pattern. Lists "None" + every preset
    // directory at /foyer/content/shaders/. The row matching the
    // per-system → general resolution chain gets "(default)" — if
    // general default is "none" the None row gets the tag, matching
    // the user-visible spec ("None (default)").
    {
        std::vector<std::string> labels{"None"};
        std::vector<std::string> codes{"none"};
        const std::filesystem::path dir{"/foyer/content/shaders"};
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            // Match both the legacy file-based presets and the
            // dir-based ones install_shaders lays down. Pretty-label
            // each one via library::pretty_shader_name so the
            // picker reads "CRT Easymode" instead of "crt-easymode".
            std::string stem;
            if (entry.is_regular_file()) {
                const auto& p = entry.path();
                const auto ext = p.extension().string();
                if (ext != ".json" && ext != ".glsl") continue;
                stem = p.stem().string();
            } else if (entry.is_directory()) {
                stem = entry.path().filename().string();
            } else {
                continue;
            }
            labels.emplace_back(::foyer::library::pretty_shader_name(stem));
            codes.emplace_back(std::move(stem));
        }

        const auto& cfg = ::foyer::library::config();
        std::string system_default_code;
        if (const char* sd = cfg.default_shader_for(m_system_folder);
            sd && *sd) {
            system_default_code = sd;
        } else if (!cfg.shader_name.empty()) {
            system_default_code = cfg.shader_name;
        } else {
            system_default_code = "none";
        }
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == system_default_code) {
                labels[i] += " (default)";
                break;
            }
        }

        const auto per_game = ::foyer::library::per_game_shader(rom);
        const std::string resolved =
            !per_game.empty() ? per_game : system_default_code;
        int initial = 0;
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == resolved) { initial = (int)i; break; }
        }
        auto* cell = new brls::SelectorCell();
        cell->init("Default shader", labels, initial,
                   [](int) {},
                   [rom, codes](int selected) {
                       if (selected < 0 || selected >= (int)codes.size()) return;
                       ::foyer::library::set_per_game_shader(rom, codes[selected]);
                   });
        host->addView(cell);
    }

    // Default bezel — DetailCell that opens PerGameBezelPickerActivity.
    // The picker lists every PNG already in the rom's bundle dir
    // (SS scrapes + BezelProject / estefan downloads) plus the
    // installed per-system bezel packs filtered to the rom's
    // hardware family. Picking writes an absolute path into
    // per_game_bezel_choice — bezel_sdl::resolve_path checks that
    // first, so the per-game pick beats the per-rom and per-system
    // resolver fallbacks.
    {
        const std::string stem = std::filesystem::path(rom).stem().string();
        auto resolved_label = [rom]() -> std::string {
            const auto choice = ::foyer::library::per_game_bezel_choice(rom);
            if (choice.empty()) return "(auto)";
            const auto slash = choice.find_last_of('/');
            std::string base = slash == std::string::npos
                ? choice
                : choice.substr(slash + 1);
            if (base.size() > 4
                && base.compare(base.size() - 4, 4, ".png") == 0) {
                base.resize(base.size() - 4);
            }
            return base;
        };
        auto* cell = new brls::DetailCell();
        cell->title->setText("Default bezel");
        cell->detail->setText(resolved_label());

        cell->registerClickAction([rom, sys, stem, cell, resolved_label](brls::View*) {
            const auto current = ::foyer::library::per_game_bezel_choice(rom);
            auto* picker = new ::foyer::browser::PerGameBezelPickerActivity(
                rom, sys, stem, current,
                [rom, cell, resolved_label](const std::string& abs_path) {
                    ::foyer::library::set_per_game_bezel_choice(rom, abs_path);
                    cell->detail->setText(resolved_label());
                });
            brls::Application::pushActivity(picker);
            return true;
        });
        host->addView(cell);
    }

    // Runahead frames — 0..4.
    {
        std::vector<std::string> labels{"Off", "1", "2", "3", "4"};
        const int current = std::max(0,
            std::min(4, ::foyer::library::per_game_runahead(rom)));
        auto* cell = new brls::SelectorCell();
        cell->init("Runahead", labels, current,
                   [](int) {},
                   [rom](int selected) {
                       ::foyer::library::set_per_game_runahead(rom, selected);
                   });
        host->addView(cell);
    }

    // Favourite.
    {
        auto* cell = new brls::BooleanCell();
        cell->init("Favourite",
                   ::foyer::library::per_game_favorite(rom),
                   [rom](bool value) {
                       ::foyer::library::set_per_game_favorite(rom, value);
                   });
        host->addView(cell);
    }

    // Browse online bezels — full-screen preview activity that
    // probes The Bezel Project + estefan3112 (where applicable),
    // writes each hit to /foyer/data/cache/bezel-preview/<sys>/<stem>/
    // and lets the user cycle through with ←/→ before committing
    // one with A. Only surfaced when at least one source might hit:
    // BP requires a mapped per-system repo; estefan is arcade-only.
    {
        const std::string stem = std::filesystem::path(rom).stem().string();
        const bool bp_ok = ::foyer::library::bezelproject_has_system(sys);
        const bool arcade =
            sys == "arcade" || sys == "mame" || sys == "fbneo"
            || sys == "fbalpha" || sys == "neogeo";
        if (bp_ok || arcade) {
            auto* cell = new brls::DetailCell();
            cell->title->setText("Browse online bezels");
            cell->detail->setText(
                bp_ok && arcade ? "The Bezel Project + Realistic" :
                bp_ok           ? "The Bezel Project" :
                                  "Realistic (arcade)");
            cell->registerClickAction([sys, stem](brls::View*) {
                auto* activity =
                    new ::foyer::browser::BezelSourceBrowserActivity(
                        sys, stem, [](const std::string&) {});
                brls::Application::pushActivity(activity);
                return true;
            });
            host->addView(cell);
        }
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Game settings");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void PerGameActivity::onContentAvailable() {
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

}  // namespace foyer::browser
