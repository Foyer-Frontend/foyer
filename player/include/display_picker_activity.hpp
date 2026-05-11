#pragma once

#include <borealis.hpp>

namespace foyer::player {

// Aspect / scale picker pushed from PauseActivity. Mutates the
// running VideoSinkImpl's AspectMode so the change shows up on
// the next frame.
class DisplayPickerActivity : public brls::Activity {
public:
    DisplayPickerActivity() = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;
};

}  // namespace foyer::player
