#pragma once

#include <string>

namespace foyer::browser::self_update {

// Pull the running NRO path from libnx's argv (canonicalised: ".new"
// suffix stripped so the path always names the on-disk canonical
// file). Call once at the very start of main(), before brls init.
void detect_paths();

// Rename foyer.nro.new -> foyer.nro after a chain-launched update.
// Must be called AFTER brls::Application::init() — borealis opens
// argv[0] for romfs reads during init, and we can't rename the file
// out from under the not-yet-established fd (0.5.25 fatal regression).
void apply_staged_if_present();

// Idempotent scrub of /foyer/bezels/default.png — the v0.2.x bundled
// CRT-TV bezel that older player NROs still pick up via fallback. No
// effect after first run.
void scrub_legacy_default_bezel();

// LRU sweep of /foyer/data/extract/. Each player launch materialises
// a .zip / .7z's inner rom into that directory and leaves it in
// place so subsequent launches of the same rom skip the re-extract.
// Files untouched for `days_threshold` days are unlinked. Called
// from boot only when the user opts in via Settings → General →
// Scrub extracted games.
void scrub_extract_lru(int days_threshold);

const std::string& nro_path();      // /switch/foyer/foyer.nro
const std::string& nro_new_path();  // /switch/foyer/foyer.nro.new

}  // namespace foyer::browser::self_update
