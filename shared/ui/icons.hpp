#pragma once

// UTF-8 encodings for Nintendo Switch shared-font button glyphs. Foyer's App
// adds the NintendoExt shared font as a fallback so any nanovg text containing
// these codepoints renders with the proper console-style icon.
//
// Codepoints reflect the layout used by hb-menu / sphaira / RetroArch on
// Switch — they're stable across firmware versions for the buttons we need.

namespace foyer::ui::icons {

constexpr const char* A         = "\xEE\x82\xA0"; // U+E0A0
constexpr const char* B         = "\xEE\x82\xA1"; // U+E0A1
constexpr const char* X         = "\xEE\x82\xA2"; // U+E0A2
constexpr const char* Y         = "\xEE\x82\xA3"; // U+E0A3
constexpr const char* L         = "\xEE\x82\xA4"; // U+E0A4
constexpr const char* R         = "\xEE\x82\xA5"; // U+E0A5
constexpr const char* ZL        = "\xEE\x82\xA6"; // U+E0A6
constexpr const char* ZR        = "\xEE\x82\xA7"; // U+E0A7
constexpr const char* SL        = "\xEE\x82\xA8";
constexpr const char* SR        = "\xEE\x82\xA9";
constexpr const char* StickL    = "\xEE\x82\xB1"; // U+E0B1
constexpr const char* StickR    = "\xEE\x82\xB2"; // U+E0B2
constexpr const char* DPad      = "\xEE\x82\xB0"; // U+E0B0
constexpr const char* Plus      = "\xEE\x82\xB5"; // U+E0B5
constexpr const char* Minus     = "\xEE\x82\xB6"; // U+E0B6
constexpr const char* Home      = "\xEE\x82\xB1";
constexpr const char* Up        = "\xEE\x82\xAA"; // U+E0AA
constexpr const char* Down      = "\xEE\x82\xAB"; // U+E0AB
constexpr const char* Left      = "\xEE\x82\xAC"; // U+E0AC
constexpr const char* Right     = "\xEE\x82\xAD"; // U+E0AD

} // namespace foyer::ui::icons
