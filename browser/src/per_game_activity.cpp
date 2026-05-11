#include "activity/per_game_activity.hpp"

#include "library/per_game.hpp"
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

    // Core override — None + every core the SystemDef recognises.
    {
        std::vector<std::string> labels{"System default"};
        std::vector<std::string> codes{""};
        if (const auto* def =
                ::foyer::library::find_system_by_folder(m_system_folder)) {
            for (const auto& c : def->cores) {
                labels.emplace_back(c.display_name);
                codes.emplace_back(c.name);
            }
        }
        const auto current = ::foyer::library::per_game_core_for(rom);
        int initial = 0;
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == current) { initial = (int)i; break; }
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

    // Shader override.
    {
        std::vector<std::string> labels{"System default", "None"};
        std::vector<std::string> codes{"", "none"};
        // Scan /foyer/content/shaders/ for *.json + *.glsl presets.
        const std::filesystem::path dir{"/foyer/content/shaders"};
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            const auto& p = entry.path();
            const auto ext = p.extension().string();
            if (ext != ".json" && ext != ".glsl") continue;
            const auto stem = p.stem().string();
            labels.emplace_back(stem);
            codes.emplace_back(stem);
        }
        const auto current = ::foyer::library::per_game_shader(rom);
        int initial = 0;
        for (std::size_t i = 0; i < codes.size(); i++) {
            if (codes[i] == current) { initial = (int)i; break; }
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
    frame->setTitle("Per-game settings");
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
