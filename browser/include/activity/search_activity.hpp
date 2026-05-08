#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// Cross-system search. Top of the AppletFrame holds an InputCell
// for the search query; below it sits a ScrollingFrame whose
// children are DetailCells for each match. Updates live as the
// user types — filter is a case-insensitive substring match
// across game.display.
class SearchActivity : public brls::Activity {
public:
    brls::View* createContentView() override;
};

}  // namespace foyer::browser
