#pragma once

#include <borealis.hpp>

namespace foyer::browser {

// HOS power slide-in panel: translucent black scrim across the
// screen plus a right-anchored vertical list of options (Sleep,
// Restart, Power off, Reboot to Hekate). brls::Dialog caps at
// three buttons, so this is a custom Activity rather than a
// Dialog.
class PowerActivity : public brls::Activity {
public:
    brls::View* createContentView() override;

    bool isTranslucent() override { return true; }
};

}  // namespace foyer::browser
