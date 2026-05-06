#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace foyer::net {

// Cancel hook: callable that returns true to abort an in-flight
// download. Wired through libcurl's XFERINFOFUNCTION so curl breaks
// out of recv() at the next callback boundary instead of hanging on
// a blocking socket read. Used by background workers (CoreInstallJob,
// FoyerUpdateJob, ScrapeJob) to honour a UI cancel button promptly.
using CancelHook = std::function<bool()>;

// Live byte counters for the in-flight streaming download. The xferinfo
// callback in http.cpp publishes here on every libcurl progress tick;
// the UI reads it (lock-free) to render a progress bar in the banner.
//
// Only `get_to_file` flips `active` on/off — the small one-shot `get`
// path used by manifest pulls leaves it untouched, since those are
// already small enough to not need a progress bar.
//
// Single global because we only run one streaming download at a time
// (install_cores / install_pack / foyer self-update are all sequential
// on the main thread).
struct DownloadStatus {
    std::atomic<std::uint64_t> now{0};      // bytes received so far
    std::atomic<std::uint64_t> total{0};    // bytes expected (0 if unknown)
    std::atomic<bool>          active{false};
};
DownloadStatus& current_download();

// Optional pump callback fired from inside libcurl's xferinfo
// callback during streaming downloads. The xferinfo callback runs on
// the same thread that called get_to_file (in foyer's case, the main
// thread), so the pump can safely call into the platform layer to
// render a fresh frame — that's the only way the byte progress bar
// animates while we're blocked in curl_easy_perform on a single
// multi-megabyte download. Set to nullptr (default) to disable.
using PumpCallback = std::function<void()>;
void set_pump_callback(PumpCallback cb);

struct Response {
    long              code = 0;     // HTTP status (0 if no response)
    std::vector<char> body;          // empty when streaming to a file
};

// One-shot synchronous GET. Honours `Accept: */*`. Caller may inject extra
// headers via `headers` (each "Key: Value"). Empty `body` on non-2xx.
// `cancel`, if set, is polled periodically — return true to abort the
// transfer (the call returns an empty body + code=0).
Response get(const std::string& url,
             const std::vector<std::string>& headers = {},
             CancelHook cancel = {});

// One-shot synchronous GET that streams to a file path. Returns true on a
// 2xx response with the file written (truncated atomically). Removes the
// destination on any failure so partial files don't litter the SD.
// `cancel` semantics match get(): aborts the curl transfer cleanly and
// removes any partial file.
bool get_to_file(const std::string& url,
                 const std::string& dest_path,
                 const std::vector<std::string>& headers = {},
                 CancelHook cancel = {});

// Simple POST application/x-www-form-urlencoded for OAuth-style endpoints.
Response post_form(const std::string& url,
                   const std::string& body,
                   const std::vector<std::string>& headers = {});

// Percent-encode the value of a query-string parameter.
std::string url_encode(const std::string& s);

// One-time global init/teardown. Safe to call multiple times.
void init();
void exit();

} // namespace foyer::net
