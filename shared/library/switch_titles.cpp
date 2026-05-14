#include "switch_titles.hpp"

#include "platform/log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#include <switch.h>

namespace foyer::library {
namespace {

std::vector<SwitchTitle> g_titles;

constexpr const char* kIconDir   = "/foyer/data/cache/switch_icons";
constexpr const char* kCachePath = "/foyer/data/cache/switch_titles.cache";
constexpr std::uint32_t kCacheVersion = 2;

struct ControlBuf {
    NsApplicationControlData data{};
    std::size_t              filled = 0;
};

bool fetch_control(std::uint64_t application_id, ControlBuf& out) {
    return R_SUCCEEDED(nsGetApplicationControlData(
        NsApplicationControlSource_Storage,
        application_id, &out.data,
        sizeof(out.data),
        &out.filled));
}

int preferred_lang_slot() {
    static int cached = -1;
    if (cached >= 0) return cached;
    u64 lang_code = 0;
    if (R_FAILED(setGetSystemLanguage(&lang_code))) { cached = 0; return cached; }
    SetLanguage lang;
    if (R_FAILED(setMakeLanguage(lang_code, &lang))) { cached = 0; return cached; }
    switch (lang) {
        case SetLanguage_JA:    cached = 2;  break;
        case SetLanguage_ENUS:  cached = 0;  break;
        case SetLanguage_ENGB:  cached = 1;  break;
        case SetLanguage_FR:    cached = 3;  break;
        case SetLanguage_DE:    cached = 4;  break;
        case SetLanguage_ES419: cached = 5;  break;
        case SetLanguage_ES:    cached = 6;  break;
        case SetLanguage_IT:    cached = 7;  break;
        case SetLanguage_NL:    cached = 8;  break;
        case SetLanguage_FRCA:  cached = 9;  break;
        case SetLanguage_PT:    cached = 10; break;
        case SetLanguage_RU:    cached = 11; break;
        case SetLanguage_KO:    cached = 12; break;
        case SetLanguage_ZHTW:  cached = 13; break;
        case SetLanguage_ZHCN:  cached = 14; break;
        default:                cached = 0;  break;
    }
    return cached;
}

const NacpLanguageEntry* nacp_pick_language(const NacpStruct* nacp) {
    const int p = preferred_lang_slot();
    if (p >= 0 && p < 16 && nacp->lang[p].name[0]) return &nacp->lang[p];
    if (nacp->lang[0].name[0]) return &nacp->lang[0];
    if (nacp->lang[1].name[0]) return &nacp->lang[1];
    for (int i = 0; i < 16; i++)
        if (nacp->lang[i].name[0]) return &nacp->lang[i];
    return nullptr;
}

void ensure_dirs() {
    ::mkdir("/foyer",                  0755);
    ::mkdir("/foyer/data",             0755);
    ::mkdir("/foyer/data/cache",       0755);
    ::mkdir(kIconDir,                  0755);
}

std::string icon_path_for(std::uint64_t app_id) {
    char hex[24];
    std::snprintf(hex, sizeof(hex), "%016lx.jpg",
        static_cast<unsigned long>(app_id));
    return std::string{kIconDir} + "/" + hex;
}

bool write_icon_jpeg(std::uint64_t app_id, const std::uint8_t* p, std::size_t n) {
    if (n == 0) return false;
    const auto path = icon_path_for(app_id);
    auto* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const auto wrote = std::fwrite(p, 1, n, f);
    std::fclose(f);
    return wrote == n;
}

// Cache: kCacheVersion=2 stores app_id + name + author only. Icons
// live as separate files so existing icon bytes are reused on
// cache-hit without re-walking NACPs.
struct CachedEntry {
    std::uint64_t application_id = 0;
    std::string   name;
    std::string   author;
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
        std::uint32_t nlen = 0, alen = 0;
        if (!read_u64(f, e.application_id) || !read_u32(f, nlen)) {
            std::fclose(f); return false;
        }
        e.name.resize(nlen);
        if (nlen && std::fread(e.name.data(), 1, nlen, f) != nlen) {
            std::fclose(f); return false;
        }
        if (!read_u32(f, alen)) { std::fclose(f); return false; }
        e.author.resize(alen);
        if (alen && std::fread(e.author.data(), 1, alen, f) != alen) {
            std::fclose(f); return false;
        }
        out.push_back(std::move(e));
    }
    std::fclose(f);
    foyer::log::write("[switch_titles] cache hit: %zu entries\n", out.size());
    return true;
}

void write_cache(const std::vector<CachedEntry>& in) {
    ensure_dirs();
    auto* f = std::fopen(kCachePath, "wb");
    if (!f) {
        foyer::log::write(
            "[switch_titles] could not write cache (errno=%d)\n", errno);
        return;
    }
    std::fwrite("FSTC", 1, 4, f);
    std::uint32_t v = kCacheVersion;
    std::fwrite(&v, 1, 4, f);
    std::uint32_t count = static_cast<std::uint32_t>(in.size());
    std::fwrite(&count, 1, 4, f);
    for (const auto& e : in) {
        std::fwrite(&e.application_id, 1, 8, f);
        std::uint32_t n = static_cast<std::uint32_t>(e.name.size());
        std::fwrite(&n, 1, 4, f);
        std::fwrite(e.name.data(), 1, n, f);
        std::uint32_t a = static_cast<std::uint32_t>(e.author.size());
        std::fwrite(&a, 1, 4, f);
        std::fwrite(e.author.data(), 1, a, f);
    }
    std::fclose(f);
    foyer::log::write("[switch_titles] wrote cache: %zu entries\n", in.size());
}

std::vector<std::uint64_t> live_app_ids() {
    std::vector<std::uint64_t> ids;

    // libnx doesn't auto-init the ns:am2 service for AppletType_Application
    // — nsListApplicationRecord lives behind it, so without this call we
    // get rc=0xe401 (session closed) and zero records. nsInitialize is
    // idempotent + cheap; the matching nsExit on quit is handled by
    // libnx's __libnx_exit hook so we don't need to track it here.
    const Result rc_ns = nsInitialize();
    if (R_FAILED(rc_ns)) {
        foyer::log::write("[switch_titles] nsInitialize rc=0x%x — "
            "Switch title list will be empty\n", (unsigned)rc_ns);
        return ids;
    }

    constexpr s32 kPageSize = 32;
    NsApplicationRecord page[kPageSize]{};
    s32 offset = 0;
    int rc_first = 0;
    s32 total_read = 0;
    while (true) {
        s32 read = 0;
        const Result rc = nsListApplicationRecord(page, kPageSize, offset, &read);
        if (offset == 0) rc_first = (int)rc;
        if (R_FAILED(rc)) {
            foyer::log::write(
                "[switch_titles] nsListApplicationRecord offset=%d rc=0x%x\n",
                (int)offset, (unsigned)rc);
            break;
        }
        if (read <= 0) break;
        total_read += read;
        for (s32 i = 0; i < read; i++) {
            if (page[i].application_id != 0)
                ids.push_back(page[i].application_id);
        }
        offset += read;
        if (read < kPageSize) break;
    }
    // Probe AppletType so the log explains a 0-record result.
    // hbloader's default AppletType_LibraryApplet has nsListApplicationRecord
    // service-gated to zero records on most firmwares — user has to
    // launch foyer in "application" mode (forwarder / title-takeover)
    // for the Switch tile to fill in.
    const AppletType at = appletGetAppletType();
    const char* at_name =
        at == AppletType_Application       ? "Application"        :
        at == AppletType_SystemApplication ? "SystemApplication"  :
        at == AppletType_LibraryApplet     ? "LibraryApplet"      :
        at == AppletType_OverlayApplet     ? "OverlayApplet"      :
        at == AppletType_SystemApplet      ? "SystemApplet"       :
                                             "Other";
    foyer::log::write(
        "[switch_titles] applet=%s rc_first=0x%x records=%d ids=%zu\n",
        at_name, (unsigned)rc_first, (int)total_read, ids.size());
    return ids;
}

bool fetch_nacp(std::uint64_t app_id, CachedEntry& out) {
    ControlBuf buf;
    if (!fetch_control(app_id, buf)) return false;
    out.application_id = app_id;
    if (auto* lang = nacp_pick_language(&buf.data.nacp)) {
        out.name.assign(lang->name);
        out.author.assign(lang->author);
    }
    const std::size_t icon_off = sizeof(NacpStruct);
    if (buf.filled > icon_off) {
        const auto* icon_p = reinterpret_cast<const std::uint8_t*>(&buf.data) + icon_off;
        const std::size_t icon_sz = buf.filled - icon_off;
        write_icon_jpeg(app_id, icon_p, icon_sz);
    }
    return true;
}

bool icon_exists(std::uint64_t app_id) {
    struct stat st{};
    return ::stat(icon_path_for(app_id).c_str(), &st) == 0 && st.st_size > 0;
}

}  // namespace

std::size_t load_switch_titles(
    std::function<void(int idx, int total)> progress) {
    ensure_dirs();

    std::vector<CachedEntry> cached;
    read_cache(cached);
    std::unordered_map<std::uint64_t, std::size_t> by_id;
    for (std::size_t i = 0; i < cached.size(); i++)
        by_id[cached[i].application_id] = i;

    const auto live = live_app_ids();
    if (progress) progress(0, static_cast<int>(live.size()));

    std::vector<CachedEntry> next;
    next.reserve(live.size());
    bool dirty = false;
    int seen = 0;
    for (std::uint64_t id : live) {
        seen++;
        if (progress) progress(seen, static_cast<int>(live.size()));
        auto it = by_id.find(id);
        if (it != by_id.end() && icon_exists(id)) {
            next.push_back(std::move(cached[it->second]));
            continue;
        }
        CachedEntry e;
        if (!fetch_nacp(id, e)) continue;
        next.push_back(std::move(e));
        dirty = true;
    }
    if (next.size() != cached.size()) dirty = true;
    if (dirty) write_cache(next);

    g_titles.clear();
    g_titles.reserve(next.size());
    for (auto& e : next) {
        SwitchTitle t{};
        t.application_id = e.application_id;
        t.name           = std::move(e.name);
        t.author         = std::move(e.author);
        t.icon_path      = icon_exists(e.application_id)
            ? icon_path_for(e.application_id) : std::string{};
        g_titles.push_back(std::move(t));
    }

    std::sort(g_titles.begin(), g_titles.end(),
        [](const SwitchTitle& a, const SwitchTitle& b) {
            const auto& an = a.name;
            const auto& bn = b.name;
            for (std::size_t i = 0; i < an.size() && i < bn.size(); i++) {
                const char ca = (an[i] >= 'A' && an[i] <= 'Z')
                    ? char(an[i] + 32) : an[i];
                const char cb = (bn[i] >= 'A' && bn[i] <= 'Z')
                    ? char(bn[i] + 32) : bn[i];
                if (ca != cb) return ca < cb;
            }
            return an.size() < bn.size();
        });

    foyer::log::write("[switch_titles] loaded %zu installed titles\n",
        g_titles.size());
    return g_titles.size();
}

const std::vector<SwitchTitle>& switch_titles() { return g_titles; }

const SwitchTitle* find_switch_title(std::uint64_t application_id) {
    for (const auto& t : g_titles) {
        if (t.application_id == application_id) return &t;
    }
    return nullptr;
}

std::string switch_path_for(std::uint64_t application_id) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "switch://%016lx",
        static_cast<unsigned long>(application_id));
    return std::string{buf};
}

std::uint64_t switch_id_from_path(std::string_view path) {
    constexpr std::string_view kPrefix = "switch://";
    if (path.size() < kPrefix.size() + 16) return 0;
    if (path.substr(0, kPrefix.size()) != kPrefix) return 0;
    const auto hex = path.substr(kPrefix.size());
    std::uint64_t v = 0;
    for (char c : hex) {
        std::uint64_t d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return 0;
        v = (v << 4) | d;
    }
    return v;
}

bool launch_switch_title(std::uint64_t application_id) {
    if (application_id == 0) return false;
    const Result rc = appletRequestLaunchApplication(application_id, nullptr);
    if (R_FAILED(rc)) {
        foyer::log::write(
            "[switch_titles] appletRequestLaunchApplication rc=0x%x id=%016lx\n",
            rc, application_id);
        return false;
    }
    foyer::log::write("[switch_titles] launching %016lx\n", application_id);
    return true;
}

}  // namespace foyer::library
