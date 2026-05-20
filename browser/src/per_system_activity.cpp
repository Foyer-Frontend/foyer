#include "activity/per_system_activity.hpp"

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

    // Default bezel selector + live preview thumbnail below it.
    // Lists "(none)" + every PNG currently installed at
    // /foyer/content/bezels/ so the user can pick any bezel
    // (regardless of which folder-key install_bezels wrote it
    // under) as this system's default. Resolution at launch is:
    //   per-game custom > per-system config > per-system file fallback
    //
    // The preview Image below the selector refreshes whenever the
    // user makes a new pick — closest brls can get to a "what does
    // this look like" affordance without a fully custom wheel
    // picker. Stretched-fit to a 16:9 box; tiny height (150 px) so
    // it doesn't dominate the per-system page.
    {
        auto names = ::foyer::library::installed_bezel_names();
        std::vector<std::string> labels;
        labels.reserve(names.size() + 1);
        labels.emplace_back("(none)");
        for (const auto& n : names) labels.emplace_back(n);

        const char* current =
            ::foyer::library::config().default_bezel_for(m_folder);
        int initial = 0;
        if (current && *current) {
            for (std::size_t i = 0; i < names.size(); i++) {
                if (names[i] == current) { initial = (int)(i + 1); break; }
            }
        }

        auto* preview = new brls::Image();
        preview->setWidth(360.0f);
        preview->setHeight(150.0f);
        preview->setScalingType(brls::ImageScalingType::FIT);
        preview->setMargins(8.0f, 32.0f, 8.0f, 32.0f);

        auto load_preview = [preview](const std::string& bezel_name) {
            if (bezel_name.empty()) {
                preview->clear();
                return;
            }
            const std::string path =
                "/foyer/content/bezels/" + bezel_name + ".png";
            struct stat st{};
            if (::stat(path.c_str(), &st) == 0) {
                preview->setImageFromFile(path);
            } else {
                preview->clear();
            }
        };
        // Seed with the current selection.
        load_preview(initial > 0 ? names[initial - 1] : std::string{});

        const std::string folder = m_folder;
        auto* cell = new brls::SelectorCell();
        cell->init("Default bezel", labels, initial,
                   [](int) {},
                   [folder, names, load_preview](int selected) {
                       std::string pick;
                       if (selected > 0 && selected <= (int)names.size()) {
                           pick = names[selected - 1];
                       }
                       ::foyer::library::set_default_bezel_for(folder, pick);
                       load_preview(pick);
                   });
        host->addView(cell);
        host->addView(preview);
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
