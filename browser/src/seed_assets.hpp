#pragma once

namespace foyer::browser {

// Copies bundled assets from the browser nro's romfs onto the SD card
// on first boot, so users get useful defaults without any manual
// setup.
//
// What gets seeded today:
//   romfs:/bezels/<system>.png  ->  /foyer/bezels/<system>.png
//   romfs:/cheats/<system>/...  ->  /foyer/cheats/<system>/...
//
// Conservative copy semantics: a destination file that already exists
// is never overwritten. User edits, third-party packs, and prior
// versions of the same asset all win over the bundled copy. New foyer
// versions only fill in files that are missing.
//
// Idempotent and cheap when nothing is missing — costs one stat() per
// bundled file. Safe to call every boot.
void seed_assets_if_missing();

} // namespace foyer::browser
