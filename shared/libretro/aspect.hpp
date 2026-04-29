#pragma once

namespace foyer::libretro {

enum class AspectMode {
    Display43,        // 4:3 letterboxed
    Display169,       // 16:9 stretched horizontally
    DisplayCore,      // honour core's own aspect ratio
    Stretch,          // fill the screen
    Integer1x,        // pixel-exact 1× (centred)
    Integer2x,        // pixel-exact 2×
    IntegerAuto,      // largest integer scale that fits
};

} // namespace foyer::libretro
