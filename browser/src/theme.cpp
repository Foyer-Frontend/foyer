#include "theme.hpp"

namespace foyer::browser {

const Theme& theme() {
    static Theme g;
    return g;
}

} // namespace foyer::browser
