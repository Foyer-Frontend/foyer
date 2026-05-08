#include "switch_titles.hpp"
#include "platform/log.hpp"

#include <algorithm>
#include <cstring>
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

} // namespace

std::size_t load(NVGcontext* vg) {
    shutdown(vg);

    constexpr s32 kPageSize = 32;
    NsApplicationRecord page[kPageSize]{};
    s32 offset = 0;

    while (true) {
        s32 read = 0;
        Result rc = nsListApplicationRecord(page, kPageSize, offset, &read);
        if (R_FAILED(rc)) {
            foyer::log::write(
                "[switch_titles] nsListApplicationRecord rc=0x%x off=%d\n",
                rc, offset);
            break;
        }
        if (read <= 0) break;

        for (s32 i = 0; i < read; i++) {
            // The libnx version on devkitPro's builder image doesn't
            // expose a `type` field on NsApplicationRecord, so we
            // can't pre-filter for installed-normal-application
            // (type=3) records here. Instead the NACP fetch acts as
            // the natural filter — records that aren't backed by
            // proper control data (gamecard, aoc, phantom, patch)
            // return failure and we skip them.
            if (page[i].application_id == 0) continue;

            ControlBuf buf;
            if (!fetch_control(page[i].application_id, buf)) continue;

            Title t{};
            t.application_id = page[i].application_id;
            const NacpLanguageEntry* lang =
                nacp_pick_language(&buf.data.nacp);
            if (lang) {
                t.name.assign(lang->name);
                t.author.assign(lang->author);
            }

            // Icon JPEG follows the NACP struct in the buffer. Size is
            // (filled - sizeof(NacpStruct)) bytes; nvgCreateImageMem
            // decodes via stb_image internally.
            const std::size_t icon_off = sizeof(NacpStruct);
            if (buf.filled > icon_off) {
                const auto* icon_p = reinterpret_cast<const std::uint8_t*>(
                    &buf.data) + icon_off;
                const int icon_sz = (int)(buf.filled - icon_off);
                t.icon_handle = nvgCreateImageMem(vg, 0,
                    const_cast<unsigned char*>(icon_p), icon_sz);
            }

            g_titles.push_back(std::move(t));
        }
        offset += read;
        if (read < kPageSize) break;
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
