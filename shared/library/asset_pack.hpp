#pragma once

#include "net/http.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace foyer::library {

// Foyer's bundled splash + theme art was 46 MB and dwarfed every other
// segment in foyer.nro. Past ~50 MB the hbloader chain-launch trips a
// kernel unmap rejection (MAKERESULT(347, 26)) when foyer chains to a
// core's player nro, so all art now lives in a separately-published
// foyer-assets.zip and gets fetched into /foyer/data/assets/ on first
// run.
//
// Layout inside the zip and on disk:
//   systems/<folder>-splash.png                            (flat splashes)
//   themes/foyer/systems/<folder>/{splash,background,logo_*}.{png,jpg}
//   themes/<other-pack>/...
//
// The activities call asset_root() (or the typed path helpers below) to
// resolve a file. Empty string is returned when the pack isn't installed
// yet — the caller falls back to a blank brls::Image, which is fine
// while the first-run download is in flight.
constexpr const char* kAssetRoot = "/foyer/data/assets";

// True when the foyer-assets.zip has been extracted at least once. The
// check is "do the two top-level dirs exist" — cheap and reliable
// enough for the boot-time gate that decides whether to enqueue the
// download.
bool asset_pack_present();

// Absolute path to <pack_dir>/systems/<folder>/<file>. `file` is
// usually "splash.png", "background.jpg", "logo_dark.png", etc.
// Returns "" if the pack isn't installed.
std::string asset_pack_system_file(
    std::string_view pack,         // e.g. "foyer" / "alekfull-nx"
    std::string_view folder,       // SystemDef folder name or "auto-*"
    std::string_view file);

// Convenience wrappers for the three most common lookups in the
// activities. All resolve through `themes/foyer/systems/<folder>/`.
std::string asset_system_splash(std::string_view folder);
std::string asset_system_background(std::string_view folder);
std::string asset_system_logo(std::string_view folder, bool dark);

struct AssetPackProgress {
    std::string phase;              // user-facing status (download / extract)
    std::uint64_t bytes_done = 0;
    std::uint64_t bytes_total = 0;
};

// Fetch + extract the asset zip. Writes to /foyer/data/assets/ atomically
// (download to .tmp, extract on success, remove .tmp). Returns true if
// the on-disk pack is usable after the call (either we installed it or
// it was already there at the right version).
//
// `zip_url` is the direct download URL of the zip (foyer release asset).
bool install_asset_pack(
    const std::string& zip_url,
    std::function<void(const AssetPackProgress&)> progress = {},
    foyer::net::CancelHook cancel = {});

} // namespace foyer::library
