#include "http.hpp"
#include "platform/log.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

namespace foyer::net {
namespace {

std::atomic<bool> g_inited{false};
std::mutex        g_init_mu;

// Singleton download counters — see DownloadStatus in http.hpp.
DownloadStatus    g_download;

std::size_t mem_writer(void* ptr, std::size_t sz, std::size_t n, void* userdata) {
    auto* out = static_cast<std::vector<char>*>(userdata);
    const auto bytes = sz * n;
    out->insert(out->end(), (char*)ptr, (char*)ptr + bytes);
    return bytes;
}

std::size_t file_writer(void* ptr, std::size_t sz, std::size_t n, void* userdata) {
    auto* fp = static_cast<std::FILE*>(userdata);
    return std::fwrite(ptr, sz, n, fp) * sz / sz;
}

curl_slist* build_headers(const std::vector<std::string>& hdrs) {
    curl_slist* sl = nullptr;
    for (const auto& h : hdrs) sl = curl_slist_append(sl, h.c_str());
    return sl;
}

// Curl invokes this every ~200ms (and after each socket write).
// Publishes live progress to g_download for the UI to read, and
// returns non-zero to abort the transfer with CURLE_ABORTED_BY_CALLBACK
// when the cancel hook says so. The progress publish runs even when
// the cancel hook is empty — `active` is only flipped on/off by the
// streaming get_to_file path, so the small one-shot `get` doesn't
// disturb it (its bytes go to nobody who cares).
int xferinfo_cancel(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t, curl_off_t) {
    g_download.now.store  (static_cast<std::uint64_t>(dlnow),   std::memory_order_relaxed);
    g_download.total.store(static_cast<std::uint64_t>(dltotal), std::memory_order_relaxed);
    auto* hook = static_cast<CancelHook*>(userdata);
    return (hook && *hook && (*hook)()) ? 1 : 0;
}

// Always wire xferinfo_cancel when streaming so progress publishes,
// even if the caller didn't supply a cancel hook. For non-streaming
// (one-shot) GETs we still wire it when there's a cancel hook so the
// UI can abort, but the progress publish there is harmless overhead
// (active stays false, so no UI reads it).
void apply_common(CURL* curl, const std::string& url, curl_slist* hdrs,
                  CancelHook* cancel_ptr, bool streaming) {
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // History: v0.2.17 added CURLOPT_HTTP_VERSION = 1.1 +
    // CURLOPT_FRESH_CONNECT + CURLOPT_FORBID_REUSE to dodge a
    // suspected stale-pooled-connection hang. All three turned out
    // to break github's three-hop redirect chain (latest -> versioned
    // -> CDN) and made manifest fetches return HTML instead of JSON.
    // None of those options are set here. The hang is back to being
    // intermittent rather than universal — but a fetch returning the
    // wrong body silently is worse than one that times out.
    if (streaming) {
        // Multi-MB downloads: no overall wall-clock timeout
        // (a 30 MB nro on a 2 Mbps connection takes ~120s and is
        // still working). Instead require a minimum throughput so a
        // genuinely stuck transfer fails promptly: < 4 KB/s for 30s
        // aborts. The cancel hook (set below) gives the UI an extra
        // override.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT,         0L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 4L * 1024L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);
    } else {
        // Small one-shot GET (JSON manifest, scraper API): 45s is
        // plenty and we want quick failure on a hung server.
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
    }
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "foyer/0.1");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Switch CA bundle drama
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    // For streaming downloads we always wire the xferinfo callback so
    // the UI gets live byte progress; the cancel hook is optional.
    if (streaming || (cancel_ptr && *cancel_ptr)) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cancel);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     cancel_ptr);
    }
}

} // namespace

DownloadStatus& current_download() { return g_download; }

void init() {
    // Double-checked under a mutex so a second thread can't race past
    // an in-flight curl_global_init. The atomic alone (exchange) was
    // wrong: it returns true the moment thread A flips the flag,
    // letting thread B fall through and start using curl while A
    // hasn't finished initialising it.
    if (g_inited.load(std::memory_order_acquire)) return;
    std::lock_guard lk{g_init_mu};
    if (g_inited.load(std::memory_order_relaxed)) return;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    g_inited.store(true, std::memory_order_release);
}

void exit() {
    std::lock_guard lk{g_init_mu};
    if (!g_inited.load(std::memory_order_relaxed)) return;
    curl_global_cleanup();
    g_inited.store(false, std::memory_order_release);
}

Response get(const std::string& url, const std::vector<std::string>& headers,
             CancelHook cancel) {
    init();
    Response r;
    auto* curl = curl_easy_init();
    if (!curl) {
        foyer::log::write("[http] curl_easy_init failed for GET %s\n", url.c_str());
        return r;
    }
    auto* hdrs = build_headers(headers);
    apply_common(curl, url, hdrs, &cancel, /*streaming=*/false);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &r.body);

    foyer::log::write("[http] GET %s start\n", url.c_str());
    const auto rc = curl_easy_perform(curl);
    foyer::log::write("[http] GET %s rc=%d body=%zu\n",
        url.c_str(), (int)rc, r.body.size());
    if (rc != CURLE_OK && rc != CURLE_ABORTED_BY_CALLBACK) {
        foyer::log::write("[http] GET %s failed: %s\n", url.c_str(), curl_easy_strerror(rc));
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.code);
    curl_easy_cleanup(curl);
    if (hdrs) curl_slist_free_all(hdrs);
    if (rc == CURLE_ABORTED_BY_CALLBACK) { r.body.clear(); r.code = 0; }
    return r;
}

bool get_to_file(const std::string& url,
                 const std::string& dest_path,
                 const std::vector<std::string>& headers,
                 CancelHook cancel) {
    init();
    // Stage to a sibling .tmp file. Only rename onto dest_path on a
    // fully successful transfer — this protects an existing copy of
    // the core (or other downloaded asset) from being clobbered if
    // the download fails partway through. Previous behaviour opened
    // dest_path with "wb" which truncates the existing file at open
    // time, so any failure left the user with no core at all.
    const std::string tmp_path = dest_path + ".tmp";
    auto* fp = std::fopen(tmp_path.c_str(), "wb");
    if (!fp) {
        foyer::log::write("[http] open(%s) failed\n", tmp_path.c_str());
        return false;
    }
    auto* curl = curl_easy_init();
    if (!curl) { std::fclose(fp); ::unlink(tmp_path.c_str()); return false; }
    auto* hdrs = build_headers(headers);
    apply_common(curl, url, hdrs, &cancel, /*streaming=*/true);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     fp);

    // Mark the global download counters live for the duration of the
    // transfer. UI code reads `active` to decide whether to render the
    // progress bar. Reset on entry so a previous transfer's residual
    // values don't flash up between consecutive downloads.
    g_download.now.store  (0, std::memory_order_relaxed);
    g_download.total.store(0, std::memory_order_relaxed);
    g_download.active.store(true, std::memory_order_release);

    foyer::log::write("[http] STREAM %s -> %s start\n", url.c_str(), tmp_path.c_str());
    const auto rc = curl_easy_perform(curl);

    g_download.active.store(false, std::memory_order_release);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    foyer::log::write("[http] STREAM %s rc=%d code=%ld\n",
        url.c_str(), (int)rc, code);

    std::fclose(fp);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    // Failure: drop the partial .tmp, leave the existing dest_path
    // (if any) alone. The previous install of this asset survives.
    if (rc != CURLE_OK || code < 200 || code >= 300) {
        if (rc != CURLE_ABORTED_BY_CALLBACK) {
            foyer::log::write("[http] GET %s -> %ld (curl rc=%d)\n",
                url.c_str(), code, (int)rc);
        }
        ::unlink(tmp_path.c_str());
        return false;
    }
    // Reject zero-byte responses (CDN sometimes returns 200 + empty body).
    struct stat st{};
    if (::stat(tmp_path.c_str(), &st) != 0 || st.st_size == 0) {
        foyer::log::write("[http] STREAM %s -> empty body, discarded\n", url.c_str());
        ::unlink(tmp_path.c_str());
        return false;
    }
    // Atomic-ish swap. POSIX rename(2) replaces an existing dest in
    // one step on the same filesystem, but devkitA64's FAT/exFAT
    // backing of the SD doesn't always honour that — some libc
    // builds return EEXIST when the destination exists. Try direct
    // rename first; on failure, fall back to unlink-then-rename
    // (brief window where dest doesn't exist, but the .tmp is
    // already fully written so we won't lose data on a power-cut).
    if (::rename(tmp_path.c_str(), dest_path.c_str()) != 0) {
        ::unlink(dest_path.c_str());
        if (::rename(tmp_path.c_str(), dest_path.c_str()) != 0) {
            foyer::log::write("[http] STREAM rename(%s -> %s) failed errno=%d\n",
                tmp_path.c_str(), dest_path.c_str(), errno);
            ::unlink(tmp_path.c_str());
            return false;
        }
    }
    foyer::log::write("[http] STREAM %s -> %s ok (%lld bytes)\n",
        url.c_str(), dest_path.c_str(), (long long)st.st_size);
    return true;
}

Response post_form(const std::string& url,
                   const std::string& body,
                   const std::vector<std::string>& headers) {
    init();
    Response r;
    auto* curl = curl_easy_init();
    if (!curl) return r;
    std::vector<std::string> all = headers;
    all.emplace_back("Content-Type: application/x-www-form-urlencoded");
    auto* hdrs = build_headers(all);
    apply_common(curl, url, hdrs, nullptr, /*streaming=*/false);
    curl_easy_setopt(curl, CURLOPT_POST,         1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,   body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,(long)body.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mem_writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &r.body);
    const auto rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        foyer::log::write("[http] POST %s failed: %s\n", url.c_str(), curl_easy_strerror(rc));
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.code);
    curl_easy_cleanup(curl);
    if (hdrs) curl_slist_free_all(hdrs);
    return r;
}

std::string url_encode(const std::string& s) {
    init();
    auto* curl = curl_easy_init();
    if (!curl) return s;
    auto* enc = curl_easy_escape(curl, s.c_str(), (int)s.size());
    std::string out = enc ? std::string{enc} : s;
    if (enc) curl_free(enc);
    curl_easy_cleanup(curl);
    return out;
}

} // namespace foyer::net
