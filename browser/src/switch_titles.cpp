#include "switch_titles.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include <switch.h>

namespace foyer::browser::switch_titles {
namespace {

std::vector<Title> g_titles;

// NACP control data is large (NsApplicationControlData wraps a 32 KB
// icon buffer). Don't allocate it on the stack — heap-allocate per
// query so the boot scan doesn't blow the libnx user stack.
struct ControlBuf {
    NsApplicationControlData data{};
    std::size_t              filled = 0;
};

// Pull NACP for one title. Returns false if the system has no record
// for it (uncommon but possible if a title got uninstalled mid-scan).
bool fetch_control(std::uint64_t application_id, ControlBuf& out) {
    return R_SUCCEEDED(nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        application_id, &out.data,
        sizeof(out.data),
        &out.filled));
}

// Map the active Switch system language to a NACP language slot
// index. NACP's lang[] array is ordered: AmericanEnglish=0,
// BritishEnglish=1, Japanese=2, French=3, German=4, LatinAmerican-
// Spanish=5, Spanish=6, Italian=7, Dutch=8, CanadianFrench=9,
// Portuguese=10, Russian=11, Korean=12, TraditionalChinese=13,
// SimplifiedChinese=14, BrazilianPortuguese=15. Cached after the
// first call since setGetSystemLanguage doesn't change mid-session.
int preferred_lang_slot() {
    static int cached = -1;
    if (cached >= 0) return cached;
    u64 lang_code = 0;
    if (R_FAILED(setGetSystemLanguage(&lang_code))) {
        cached = 0;  // AmericanEnglish fallback
        return cached;
    }
    SetLanguage lang;
    if (R_FAILED(setMakeLanguage(lang_code, &lang))) {
        cached = 0;
        return cached;
    }
    // Map SetLanguage enum into NACP slot order.
    switch (lang) {
        case SetLanguage_JA:   cached = 2; break;
        case SetLanguage_ENUS: cached = 0; break;
        case SetLanguage_ENGB: cached = 1; break;
        case SetLanguage_FR:   cached = 3; break;
        case SetLanguage_DE:   cached = 4; break;
        case SetLanguage_ES419:cached = 5; break;
        case SetLanguage_ES:   cached = 6; break;
        case SetLanguage_IT:   cached = 7; break;
        case SetLanguage_NL:   cached = 8; break;
        case SetLanguage_FRCA: cached = 9; break;
        case SetLanguage_PT:   cached = 10; break;
        case SetLanguage_RU:   cached = 11; break;
        case SetLanguage_KO:   cached = 12; break;
        case SetLanguage_ZHTW: cached = 13; break;
        case SetLanguage_ZHCN: cached = 14; break;
        default:               cached = 0; break;
    }
    return cached;
}

// Pick the NACP language entry that matches the user's system locale,
// falling back to American English, then any non-empty slot. Avoids
// showing a Japanese title name to a user with a Latin-script locale
// just because it's the first slot the title's NACP populated.
const NacpLanguageEntry* nacp_pick_language(const NacpStruct* nacp) {
    const int preferred = preferred_lang_slot();
    if (preferred >= 0 && preferred < 16
        && nacp->lang[preferred].name[0]) {
        return &nacp->lang[preferred];
    }
    if (nacp->lang[0].name[0]) return &nacp->lang[0];   // AmEn
    if (nacp->lang[1].name[0]) return &nacp->lang[1];   // GbEn
    for (int i = 0; i < 16; i++) {
        if (nacp->lang[i].name[0]) return &nacp->lang[i];
    }
    return nullptr;
}

// On-disk cache for the NACP-fetch results. Reading 197 NACPs through
// nsGetApplicationControlData on every cold boot takes ~30+ seconds on
// real hardware; replaying the same data from a single file read is
// near-instant. The cache is keyed by application_id and invalidated
// per-id when the live nsListApplicationRecord set diverges.
//
// Format (little-endian, native packing):
//   "FSTC" magic(4) + version(u32=1) + entry count(u32)
//   per entry:
//       u64 application_id
//       u32 name_len   + name bytes
//       u32 author_len + author bytes
//       u32 icon_len   + icon JPEG bytes
constexpr const char* kCachePath = "/foyer/data/switch_titles.cache";
constexpr std::uint32_t kCacheVersion = 1;

struct CachedEntry {
    std::uint64_t      application_id = 0;
    std::string        name;
    std::string        author;
    std::vector<u8>    icon_jpeg;
};

bool read_u32(std::FILE* f, std::uint32_t& v) {
    return std::fread(&v, 1, 4, f) == 4;
}
bool read_u64(std::FILE* f, std::uint64_t& v) {
    return std::fread(&v, 1, 8, f) == 8;
}

bool read_cache(std::vector<CachedEntry>& out) {
    auto* f = std::fopen(kCachePath, "rb");
    if (!f) return false;
    char magic[4];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "FSTC", 4) != 0) {
        std::fclose(f); return false;
    }
    std::uint32_t version = 0, count = 0;
    if (!read_u32(f, version) || version != kCacheVersion
        || !read_u32(f, count)) {
        std::fclose(f); return false;
    }
    out.clear();
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; i++) {
        CachedEntry e;
        std::uint32_t name_len = 0, author_len = 0, icon_len = 0;
        if (!read_u64(f, e.application_id) ||
            !read_u32(f, name_len)) { std::fclose(f); return false; }
        e.name.resize(name_len);
        if (name_len && std::fread(e.name.data(), 1, name_len, f) != name_len) {
            std::fclose(f); return false;
        }
        if (!read_u32(f, author_len)) { std::fclose(f); return false; }
        e.author.resize(author_len);
        if (author_len && std::fread(e.author.data(), 1, author_len, f)
                              != author_len) {
            std::fclose(f); return false;
        }
        if (!read_u32(f, icon_len)) { std::fclose(f); return false; }
        e.icon_jpeg.resize(icon_len);
        if (icon_len && std::fread(e.icon_jpeg.data(), 1, icon_len, f)
                              != icon_len) {
            std::fclose(f); return false;
        }
        out.push_back(std::move(e));
    }
    std::fclose(f);
    foyer::log::write(
        "[switch_titles] cache hit: %zu entries\n", out.size());
    return true;
}

void write_cache(const std::vector<CachedEntry>& in) {
    ::mkdir("/foyer/data", 0755);
    auto* f = std::fopen(kCachePath, "wb");
    if (!f) {
        foyer::log::write(
            "[switch_titles] could not write cache (errno=%d)\n", errno);
        return;
    }
    std::fwrite("FSTC", 1, 4, f);
    std::uint32_t v = kCacheVersion;
    std::fwrite(&v, 1, 4, f);
    std::uint32_t count = (std::uint32_t)in.size();
    std::fwrite(&count, 1, 4, f);
    for (const auto& e : in) {
        std::fwrite(&e.application_id, 1, 8, f);
        std::uint32_t n = (std::uint32_t)e.name.size();
        std::fwrite(&n, 1, 4, f);
        std::fwrite(e.name.data(), 1, n, f);
        std::uint32_t a = (std::uint32_t)e.author.size();
        std::fwrite(&a, 1, 4, f);
        std::fwrite(e.author.data(), 1, a, f);
        std::uint32_t ic = (std::uint32_t)e.icon_jpeg.size();
        std::fwrite(&ic, 1, 4, f);
        std::fwrite(e.icon_jpeg.data(), 1, ic, f);
    }
    std::fclose(f);
    foyer::log::write(
        "[switch_titles] wrote cache: %zu entries\n", in.size());
}

// Walk every installed application record and return the set of live
// application_ids. Cheap — no NACP fetch.
std::vector<std::uint64_t> live_app_ids() {
    std::vector<std::uint64_t> ids;
    constexpr s32 kPageSize = 32;
    NsApplicationRecord page[kPageSize]{};
    s32 offset = 0;
    while (true) {
        s32 read = 0;
        if (R_FAILED(nsListApplicationRecord(page, kPageSize, offset, &read)))
            break;
        if (read <= 0) break;
        for (s32 i = 0; i < read; i++) {
            if (page[i].application_id != 0)
                ids.push_back(page[i].application_id);
        }
        offset += read;
        if (read < kPageSize) break;
    }
    return ids;
}

// Pull NACP, peel out the icon JPEG, and append a CachedEntry. Returns
// false if the system has no record for this id.
bool fetch_to_cached(std::uint64_t app_id, CachedEntry& out) {
    ControlBuf buf;
    if (!fetch_control(app_id, buf)) return false;
    out.application_id = app_id;
    if (auto* lang = nacp_pick_language(&buf.data.nacp)) {
        out.name.assign(lang->name);
        out.author.assign(lang->author);
    }
    const std::size_t icon_off = sizeof(NacpStruct);
    if (buf.filled > icon_off) {
        const auto* icon_p = reinterpret_cast<const std::uint8_t*>(
            &buf.data) + icon_off;
        const std::size_t icon_sz = buf.filled - icon_off;
        out.icon_jpeg.assign(icon_p, icon_p + icon_sz);
    }
    return true;
}

} // namespace

std::size_t load(NVGcontext* vg,
                 std::function<void(int idx, int total)> progress) {
    shutdown(vg);

    // Cold-boot fast path: load the cache from disk first. The slow
    // service work then becomes a delta — only fetch NACPs for
    // application_ids the cache doesn't already cover.
    std::vector<CachedEntry> entries;
    read_cache(entries);
    std::unordered_map<std::uint64_t, std::size_t> by_id;
    for (std::size_t i = 0; i < entries.size(); i++) {
        by_id[entries[i].application_id] = i;
    }

    const auto live = live_app_ids();
    if (progress) progress(0, (int)live.size());

    // Build the new entry list in live order, fetching NACP only for
    // ids missing from the cache. Drop cache entries that are no
    // longer installed.
    std::vector<CachedEntry> next;
    next.reserve(live.size());
    int seen = 0;
    bool dirty = false;
    for (std::uint64_t id : live) {
        seen++;
        if (progress) progress(seen, (int)live.size());

        auto it = by_id.find(id);
        if (it != by_id.end()) {
            next.push_back(std::move(entries[it->second]));
            continue;
        }
        // Cache miss — slow NACP fetch.
        CachedEntry e;
        if (!fetch_to_cached(id, e)) continue;
        next.push_back(std::move(e));
        dirty = true;
    }
    // Detect drops too — if cache had more entries than live set,
    // the rewrite will trim them.
    if (next.size() != entries.size()) dirty = true;
    if (dirty) write_cache(next);

    // Materialise into Title objects + decode icon JPEGs into nvg
    // image handles. Decode is fast (~few ms per icon) so we do it
    // here unconditionally even on cache-hit paths.
    g_titles.clear();
    g_titles.reserve(next.size());
    for (auto& e : next) {
        Title t{};
        t.application_id = e.application_id;
        t.name           = std::move(e.name);
        t.author         = std::move(e.author);
        if (!e.icon_jpeg.empty()) {
            t.icon_handle = nvgCreateImageMem(vg, 0,
                e.icon_jpeg.data(), (int)e.icon_jpeg.size());
        }
        g_titles.push_back(std::move(t));
    }

    // Sort alphabetically by display name (case-insensitive). HOS
    // shows the most-recently-played first; we don't track that for
    // installed Switch titles yet, so alphabetical reads cleaner than
    // install-order which is meaningless to the user.
    std::sort(g_titles.begin(), g_titles.end(),
        [](const Title& a, const Title& b) {
            const auto& an = a.name;
            const auto& bn = b.name;
            for (std::size_t i = 0; i < an.size() && i < bn.size(); i++) {
                const char ca = (an[i] >= 'A' && an[i] <= 'Z')
                    ? (char)(an[i] + 32) : an[i];
                const char cb = (bn[i] >= 'A' && bn[i] <= 'Z')
                    ? (char)(bn[i] + 32) : bn[i];
                if (ca != cb) return ca < cb;
            }
            return an.size() < bn.size();
        });

    foyer::log::write("[switch_titles] loaded %zu installed titles\n",
        g_titles.size());
    return g_titles.size();
}

void shutdown(NVGcontext* vg) {
    if (vg) {
        for (auto& t : g_titles) {
            if (t.icon_handle > 0) nvgDeleteImage(vg, t.icon_handle);
        }
    }
    g_titles.clear();
}

const std::vector<Title>& titles() { return g_titles; }

int icon_handle_for(std::uint64_t application_id) {
    for (const auto& t : g_titles) {
        if (t.application_id == application_id) return t.icon_handle;
    }
    return 0;
}

bool launch(std::uint64_t application_id) {
    if (application_id == 0) return false;
    Result rc = appletRequestLaunchApplication(application_id, nullptr);
    if (R_FAILED(rc)) {
        foyer::log::write(
            "[switch_titles] appletRequestLaunchApplication rc=0x%x id=%016lx\n",
            rc, application_id);
        return false;
    }
    foyer::log::write(
        "[switch_titles] launching %016lx\n", application_id);
    return true;
}

} // namespace foyer::browser::switch_titles
