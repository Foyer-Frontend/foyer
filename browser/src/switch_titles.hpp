#pragma once

// Switch-title launcher (0.5.5).
//
// Walks every installed application via nsListApplicationRecord, pulls
// the NACP control data (title name + author + 256x256 icon JPEG), and
// caches the icon as an nvg image handle so the Switch carousel tile
// can render the titles like a system. launch() hands the
// application_id off to appletRequestLaunchApplication, which
// terminates foyer and boots the title.

#include <cstdint>
#include <string>
#include <vector>

#include <nanovg.h>

namespace foyer::browser::switch_titles {

struct Title {
    std::uint64_t application_id = 0;
    std::string   name;
    std::string   author;
    int           icon_handle = 0;   // nvg handle (>0) or 0 if no icon
};

// Populate the title list at boot. Returns the number of titles
// successfully loaded. Cheap to call again after a remount or major
// state change — tears down the previous nvg handles before reloading.
std::size_t load(NVGcontext* vg);

// Drop nvg handles. Called on shutdown.
void shutdown(NVGcontext* vg);

// View into the cached titles. Stable for the lifetime of the
// process unless load() is called again.
const std::vector<Title>& titles();

// Hand the chosen title off to qlaunch for boot. Returns true on
// success — caller should app.quit() so the firmware actually
// transitions. False if the call failed or the id is invalid.
bool launch(std::uint64_t application_id);

} // namespace foyer::browser::switch_titles
