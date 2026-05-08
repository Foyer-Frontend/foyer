#include "activity/search_activity.hpp"

#include "activity/game_activity.hpp"
#include "library_state.hpp"

#include "library/system_db.hpp"

#include <borealis.hpp>
#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

namespace {

bool case_contains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(),   needle.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) ==
                                     std::tolower((unsigned char)b);
                          });
    return it != haystack.end();
}

// Build the result list under the search input. Reads from
// library_state, so Phase D's scan output drives this without any
// additional plumbing.
void populate_results(brls::Box* list, const std::string& query) {
    list->clearViews();

    for (const auto& sys : library_state::systems()) {
        for (const auto& g : sys.games) {
            if (!case_contains(g.display, query)) continue;

            auto* cell = new brls::DetailCell();
            cell->title->setText(g.display);
            // Show the system in the detail line so two ROMs with
            // matching display names are still distinguishable.
            const std::string sysname = sys.def
                ? std::string(sys.def->display_name)
                : std::string("(unknown)");
            cell->detail->setText(sysname + "  ·  " + g.filename);

            const std::string folder = sys.def
                ? std::string(sys.def->folder_name)
                : std::string();
            const std::string path = g.path;
            cell->registerClickAction([folder, path](brls::View*) {
                brls::Application::pushActivity(
                    new GameActivity(folder, path));
                return true;
            });
            list->addView(cell);
        }
    }

    if (list->getChildren().empty() && !query.empty()) {
        auto* empty = new brls::Label();
        empty->setText("No matches.");
        empty->setFontSize(20.0f);
        empty->setMargins(48.0f, 0.0f, 0.0f, 48.0f);
        list->addView(empty);
    }
}

}  // namespace

brls::View* SearchActivity::createContentView() {
    auto* outer = new brls::Box();
    outer->setAxis(brls::Axis::COLUMN);

    auto* search = new brls::InputCell();
    search->init("Search",  // i18n key in alpha.17
                 std::string(),
                 [](std::string) {},
                 "Type to filter…",
                 std::string(),
                 64);

    auto* scroll = new brls::ScrollingFrame();
    auto* list   = new brls::Box();
    list->setAxis(brls::Axis::COLUMN);
    list->setMargins(8.0f, 32.0f, 8.0f, 32.0f);
    scroll->setContentView(list);

    // Initial population — empty query shows everything.
    populate_results(list, std::string());

    // Re-filter on every change to the input value. brls fires the
    // callback after the soft keyboard returns; for live filtering
    // we'd subscribe to text events directly, but keyboard-commit
    // is a simpler first cut.
    search->title->setText("Search");
    // Replace the init's empty change-callback with one that
    // refilters. brls InputCell doesn't expose the underlying
    // event directly, so the cleanest path is to re-init the cell
    // — same pattern Moonlight uses.
    search->init("Search",
                 std::string(),
                 [list](std::string q) { populate_results(list, q); },
                 "Type to filter…",
                 std::string(),
                 64);

    outer->addView(search);
    outer->addView(scroll);

    auto* frame = new brls::AppletFrame(outer);
    frame->setTitle("Search");

    frame->registerAction("hints/back"_i18n, brls::BUTTON_B,
        [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, false, false, brls::SOUND_BACK);

    return frame;
}

}  // namespace foyer::browser
