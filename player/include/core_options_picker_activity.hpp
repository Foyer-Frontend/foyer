#pragma once

#include <borealis.hpp>

namespace foyer::player {

// Lists every libretro core option the running core registered.
// Each row is a SelectorCell bound to that option's current value
// + its declared choice list; picking persists to the per-core
// (and per-game when set_rom_path was called) JSONC stores and
// flips CoreOptions::consume_dirty so the core picks it up on
// the next env_cb poll.
class CoreOptionsPickerActivity : public brls::Activity {
public:
    CoreOptionsPickerActivity() = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;
};

}  // namespace foyer::player
