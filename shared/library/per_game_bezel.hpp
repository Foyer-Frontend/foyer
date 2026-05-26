#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::library {

// Per-game bezel fetchers. Land their PNG in
// /foyer/assets/system/<system_folder>/<rom_stem>/bezel.png — the
// same dir bezel_sdl::resolve_path() searches first (path #1). Any
// existing region-tagged SS scrape (bezel-16-9(us).png, ...) keeps
// priority over the bare bezel.png written here, so a user who
// already scraped via ScreenScraper isn't surprised when they grab
// a Bezel-Project alternate.

struct PerGameBezelProgress {
    int   step  = 0;
    int   total = 0;
    std::string detail;   // e.g. "Trying <candidate>.png"
};

using PerGameBezelProgressCb = std::function<void(const PerGameBezelProgress&)>;

// True if the named foyer system is mapped to a TheBezelProject
// per-system repo. Browser UI uses this to hide the action on
// systems without coverage.
bool bezelproject_has_system(std::string_view system_folder);

// Fetch a per-game bezel from TheBezelProject for the given
// system + rom stem. Tries a small set of stem variants
// (strip / add region tags) to bridge No-Intro naming
// differences. Returns true if a candidate was found and
// written to disk.
bool fetch_bezel_from_bezelproject(std::string_view system_folder,
                                   std::string_view rom_stem,
                                   PerGameBezelProgressCb progress = {});

// Same shape for the estefan3112 arcade collection. Coverage is
// arcade-only (foyer system = "arcade"/"mame"/"fbneo"/etc.).
bool fetch_bezel_from_estefan(std::string_view system_folder,
                              std::string_view rom_stem,
                              PerGameBezelProgressCb progress = {});

// Generate filename variants used by the BezelProject + estefan
// fetchers. Exposed for tests / debug.
std::vector<std::string> bezel_filename_variants(std::string_view rom_stem);

// Preview-before-commit flow. peek_per_game_bezels writes each
// matching candidate to a temp dir below /foyer/data/cache/
// bezel-preview/<sys>/<stem>/<source>.png and returns the list of
// sources that hit. The browser BezelSourceBrowserActivity walks the
// list, shows each PNG full-screen, and asks the user to commit one
// via commit_bezel_to_bundle().
struct PerGameBezelPreview {
    std::string source;     // "bezelproject" / "estefan3112"
    std::string label;      // user-facing label
    std::string temp_path;  // PNG on disk, ready for brls::Image
};

std::vector<PerGameBezelPreview> peek_per_game_bezels(
    std::string_view system_folder,
    std::string_view rom_stem,
    PerGameBezelProgressCb progress = {});

// Commit a previewed temp PNG into the bundle dir under a
// source-tagged filename (bezel-<source>.png) so the resolver
// picks it up. Removes the temp file on success.
bool commit_bezel_to_bundle(std::string_view system_folder,
                            std::string_view rom_stem,
                            std::string_view source,
                            const std::string& temp_path);

// Drop everything under /foyer/data/cache/bezel-preview/<sys>/<stem>/.
// Called when the source-browser activity pops so the cache doesn't
// linger on disk.
void clear_per_game_bezel_preview_cache(std::string_view system_folder,
                                        std::string_view rom_stem);

} // namespace foyer::library
