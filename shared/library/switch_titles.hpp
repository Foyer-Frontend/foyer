#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace foyer::library {

// Enumerates installed Switch applications via libnx's
// nsListApplicationRecord + nsGetApplicationControlData. Returns
// each title's NACP metadata (name / author) plus the on-disk path
// of its 256×256 JPEG icon (cached under /foyer/data/cache/
// switch_icons/<appid>.jpg). The browser's scanner injects these as
// Game entries under the __switch virtual system; launch.cpp peels
// the application_id back out of the "switch://<hex>" path and
// hands it to appletRequestLaunchApplication.
//
// Free of any UI / rendering deps so the call site stays in shared/.

struct SwitchTitle {
    std::uint64_t application_id = 0;
    std::string   name;
    std::string   author;
    std::string   icon_path;  // /foyer/data/cache/switch_icons/<hex>.jpg
};

// Populate the title list. Returns the count of titles successfully
// enumerated. Re-running is cheap: the cache covers everything that
// hasn't changed since the prior boot; only NEW application_ids hit
// the slow nsGetApplicationControlData path. `progress` fires
// (idx,total) after each record so the boot splash can report
// "Reading Switch titles N/M" without freezing.
std::size_t load_switch_titles(
    std::function<void(int idx, int total)> progress = {});

// Cache-first variant — reads the on-disk cache only, no live-diff,
// no nsGetApplicationControlData IPC. Populates the title list
// instantly so the splash + Home carousel render with whatever the
// previous boot saw. Pair with refresh_switch_titles_async() to
// pick up newly-installed titles in the background.
std::size_t load_switch_titles_cached();

// Run the live-diff (nsListApplicationRecord + per-new-id
// nsGetApplicationControlData) on a detached worker thread. Calls
// `on_changed` on the brls UI thread iff the title list materially
// changed (new IDs added, stale ones dropped) so the carousel can
// rescan + repaint. Skips the callback when nothing changed —
// callers shouldn't trigger an unnecessary rescan.
void refresh_switch_titles_async(std::function<void()> on_changed = {});

const std::vector<SwitchTitle>& switch_titles();

// Look up a title by application_id. Returns nullptr when not
// installed. Used by launch.cpp.
const SwitchTitle* find_switch_title(std::uint64_t application_id);

// "switch://<hex>" path encoder/decoder used by the scanner +
// launch.cpp.
std::string         switch_path_for(std::uint64_t application_id);
std::uint64_t       switch_id_from_path(std::string_view path);

// appletRequestLaunchApplication wrapper. Caller is responsible
// for calling Application::quit() afterwards so HOS actually does
// the title hand-off.
bool launch_switch_title(std::uint64_t application_id);

}  // namespace foyer::library
