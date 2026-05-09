#pragma once

// HOS-style top-bar status: active user (avatar + nickname), battery,
// network, charging glyph. Polled once per second from the browser
// main loop so the chrome stays current without burning a service
// call every frame.
//
// Avatar lives as an nvg image handle managed inside this module.
// poll() needs the live NVGcontext to decode the JPEG once at first
// run; after that it just refreshes the scalar fields.

#include <nanovg.h>
#include <cstdint>
#include <string>
#include <vector>

namespace foyer::browser::hos_status {

// One-shot init at startup. Loads the active user's profile + avatar
// JPEG, decodes it via nvg, caches the handle. Falls back to a blank
// placeholder when libnx returns no preselected user (rare under
// hbloader-applet but possible on a fresh console).
void init(NVGcontext* vg);

// Refresh the scalar fields (battery, charging, wifi). Cheap — debounced
// internally to ~once per second; safe to call every frame.
void poll();

// Drop the cached nvg image. Called on shutdown.
void shutdown(NVGcontext* vg);

// --- read-only accessors used by views.cpp draw_hos_top_bar ---------

// nvg image handle for the user's 256x256 avatar JPEG. <=0 when no
// profile picture loaded — drawer falls back to a flat circle.
int                avatar_handle();

// Raw JPEG bytes for the active user's avatar. Empty when no
// profile picture loaded. Used by the brls path to feed
// Image::setImageFromMem (brls's Image view can't wrap an
// existing nvg handle, so we hand it the bytes and let it
// decode once into brls's texture cache).
const std::vector<std::uint8_t>& avatar_jpeg();

// Active user's display nickname. Empty when no profile available.
const std::string& nickname();

// Number of secondary user avatars known to the system (excluding the
// currently-active one). 0..3 typical; HOS supports up to 8 profiles.
int                other_avatar_count();
int                other_avatar_handle(int i);
const std::string& other_nickname(int i);

// Switch the active user to the given index (0..other_avatar_count-1
// -> a secondary avatar; -1 keeps the currently-active one). Reloads
// the avatar/nickname so the next frame paints the new active user.
void               switch_active(int secondary_idx, NVGcontext* vg);

// Battery 0..100. -1 when the read failed (rare).
int                battery_pct();

// True while a charger is connected (any of USB-PD / Type-C / dock).
bool               charging();

// Connection strength bucket 0..3 (matches HOS's three-arc icon).
// 0 means not connected.
int                wifi_strength();

bool               wifi_connected();

} // namespace foyer::browser::hos_status
