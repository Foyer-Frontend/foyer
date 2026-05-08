#include "switch_titles.hpp"
#include "platform/log.hpp"

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

// Read the user's preferred language slot out of the NACP. The 16
// language slots are ordered (American English first); we just take
// the first non-empty one so any title with a localized name shows up
// without us linking against setLanguage.
const NacpLanguageEntry* nacp_pick_language(const NacpStruct* nacp) {
    for (int i = 0; i < 16; i++) {
        const auto& e = nacp->lang[i];
        if (e.name[0]) return &e;
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
            // Filter the records HOS itself hides on the home menu —
            // type=3 is "installed normal application", everything else
            // is gamecard / aoc / patch / phantom and not directly
            // launchable from the launcher.
            if (page[i].type != 0x3) continue;

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
