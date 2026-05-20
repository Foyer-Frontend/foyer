#include "activity/per_game_activity.hpp"

#include "library/config.hpp"
#include "library/per_game.hpp"
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
        cell->init("Core", labels, initial,
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
        cell->init("Shader", labels, initial,
                   [](int) {},
                   [rom, codes](int selected) {
                       if (selected < 0 || selected >= (int)codes.size()) return;
                       ::foyer::library::set_per_game_shader(rom, codes[selected]);
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
