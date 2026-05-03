#include "http.hpp"
#include "platform/log.hpp"

#include <atomic>
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

// Curl invokes this every ~200ms (and after each socket write). Returning
// non-zero aborts the transfer with CURLE_ABORTED_BY_CALLBACK.
int xferinfo_cancel(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* hook = static_cast<CancelHook*>(userdata);
    return (hook && *hook && (*hook)()) ? 1 : 0;
}

void apply_common(CURL* curl, const std::string& url, curl_slist* hdrs,
                  CancelHook* cancel_ptr, bool streaming) {
    curl_easy_setopt(curl, CURLOPT_URL,            url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,       1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
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
    if (cancel_ptr && *cancel_ptr) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cancel);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     cancel_ptr);
    }
}

} // namespace

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
    auto* fp = std::fopen(dest_path.c_str(), "wb");
    if (!fp) {
        foyer::log::write("[http] open(%s) failed\n", dest_path.c_str());
        return false;
    }
    auto* curl = curl_easy_init();
    if (!curl) { std::fclose(fp); ::unlink(dest_path.c_str()); return false; }
    auto* hdrs = build_headers(headers);
    apply_common(curl, url, hdrs, &cancel, /*streaming=*/true);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, file_writer);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     fp);

    foyer::log::write("[http] STREAM %s -> %s start\n", url.c_str(), dest_path.c_str());
    const auto rc = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    foyer::log::write("[http] STREAM %s rc=%d code=%ld\n",
        url.c_str(), (int)rc, code);

    std::fclose(fp);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code < 200 || code >= 300) {
        if (rc != CURLE_ABORTED_BY_CALLBACK) {
            foyer::log::write("[http] GET %s -> %ld (curl rc=%d)\n",
                url.c_str(), code, (int)rc);
        }
        ::unlink(dest_path.c_str());
        return false;
    }
    // Reject zero-byte responses (CDN sometimes returns 200 + empty body).
    struct stat st{};
    if (::stat(dest_path.c_str(), &st) != 0 || st.st_size == 0) {
        ::unlink(dest_path.c_str());
        return false;
    }
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
