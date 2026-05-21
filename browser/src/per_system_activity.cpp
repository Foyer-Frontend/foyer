#include "activity/per_system_activity.hpp"

#include "activity/bezel_picker_activity.hpp"
#include "library_state.hpp"
#include "library/bezel_installer.hpp"
#include "library/config.hpp"
#include "library/shader_installer.hpp"
#include "library/system_db.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <sys/stat.h>

#include <string>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

PerSystemActivity::PerSystemActivity(std::string_view folder,
                                     std::string_view display_name)
    : m_folder(folder), m_display_name(display_name) {}

brls::View* PerSystemActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    // Default core selector — same data as Settings →
    // Emulators row for this system.
    if (const auto* def =
            ::foyer::library::find_system_by_folder(m_folder)) {
        std::vector<std::string> labels;
        std::vector<std::string> codes;
        labels.reserve(def->cores.size());
        codes.reserve(def->cores.size());
        for (const auto& c : def->cores) {
            labels.emplace_back(c.display_name);
            codes.emplace_back(c.name);
        }
        const char* current =
            ::foyer::library::config().default_core_for(m_folder);
        int initial = 0;
        if (current && *current) {
            for (std::size_t i = 0; i < codes.size(); i++) {
                if (codes[i] == current) { initial = (int)i; break; }
            }
        }
        const std::string folder = m_folder;
        auto* cell = new brls::SelectorCell();
        cell->init("Default core", labels, initial,
                   [](int) {},
                   [folder, codes](int selected) {
                       if (selected < 0 || selected >= (int)codes.size()) return;
                       ::foyer::library::set_default_core_for(
                           folder, codes[selected]);
                   });
        host->addView(cell);
    }

    // Default bezel — DetailCell that opens BezelPickerActivity.
    // The earlier inline SelectorCell + below-preview shape was
    // flagged as awkward in testing: the wheel-picker dialog has
    // no room for an image, so users couldn't see what they were
    // picking until they closed the dialog. The new picker is a
    // full-screen activity with a large preview front and centre
    // + ←/→ to cycle + A to confirm.
    {
        auto* cell = new brls::DetailCell();
        cell->title->setText("Default bezel");

        auto resolved_label = [this]() -> std::string {
            const char* current =
                ::foyer::library::config().default_bezel_for(m_folder);
            return (current && *current) ? std::string{current} : "(none)";
        };
        cell->detail->setText(resolved_label());

        const std::string folder = m_folder;
        cell->registerClickAction([this, cell, folder, resolved_label](brls::View*) {
            // Build the picker's name list: "(none)" sentinel +
            // every installed bezel basename.
            auto names = ::foyer::library::installed_bezel_names();
            std::vector<std::string> with_none;
            with_none.reserve(names.size() + 1);
            with_none.emplace_back("");  // sentinel
            for (auto& n : names) with_none.emplace_back(std::move(n));

            const char* current =
                ::foyer::library::config().default_bezel_for(folder);
            std::string initial = (current && *current)
                ? std::string{current} : std::string{};

            auto* picker = new BezelPickerActivity(
                std::move(with_none), std::move(initial),
                [folder, cell, resolved_label](const std::string& pick) {
                    ::foyer::library::set_default_bezel_for(folder, pick);
                    // Refresh the cell's detail line so the user
                    // sees the new pick when they land back on
                    // the per-system page.
                    cell->detail->setText(
                        pick.empty() ? "(none)" : pick);
                    (void)resolved_label;
                });
            brls::Application::pushActivity(picker);
            return true;
        });
        host->addView(cell);
    }

    // Default shader selector — same shape, lists "(none)" + every
    // shader preset directory at /foyer/content/shaders/, with
    // pretty labels (CRT Easymode instead of crt-easymode etc.).
    {
        auto names = ::foyer::library::installed_shader_names();
        std::vector<std::string> labels;
        labels.reserve(names.size() + 1);
        labels.emplace_back("(none)");
        for (const auto& n : names) {
            labels.emplace_back(::foyer::library::pretty_shader_name(n));
        }

        const char* current =
            ::foyer::library::config().default_shader_for(m_folder);
        int initial = 0;
        if (current && *current) {
            for (std::size_t i = 0; i < names.size(); i++) {
                if (names[i] == current) { initial = (int)(i + 1); break; }
            }
        }
        const std::string folder = m_folder;
        auto* cell = new brls::SelectorCell();
        cell->init("Default shader", labels, initial,
                   [](int) {},
                   [folder, names](int selected) {
                       if (selected <= 0 || selected > (int)names.size()) {
                           ::foyer::library::set_default_shader_for(folder, {});
                       } else {
                           ::foyer::library::set_default_shader_for(
                               folder, names[selected - 1]);
                       }
                   });
        host->addView(cell);
    }

    // Read-only: game count.
    {
        std::size_t count = 0;
        if (const auto* sys = library_state::find_system(m_folder)) {
            count = sys->games.size();
        }
        auto* cell = new brls::DetailCell();
        cell->title->setText("Games");
        cell->detail->setText(std::to_string(count));
        host->addView(cell);
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle(m_display_name + " — settings");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time", "brls/battery", "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void PerSystemActivity::onContentAvailable() {
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
