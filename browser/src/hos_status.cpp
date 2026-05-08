#include "hos_status.hpp"
#include "platform/log.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#include <switch.h>

namespace foyer::browser::hos_status {
namespace {

int                            g_avatar_handle = 0;
std::string                    g_nickname;
AccountUid                     g_active_uid{};
int                            g_battery_pct   = -1;
bool                           g_charging      = false;
int                            g_wifi_strength = 0;
bool                           g_wifi_connected = false;

// Secondary user roster — every profile registered on the console
// minus the currently-active one. Loaded once at boot; refreshed by
// switch_active() so the cluster reflects the new active user
// immediately after a switch.
struct SecondaryUser {
    AccountUid  uid;
    int         avatar  = 0;   // nvg handle (>0) or 0 if no image
    std::string nickname;
};
std::vector<SecondaryUser>     g_secondaries;

std::chrono::steady_clock::time_point g_last_poll{};

// Active-user avatar JPEG is up to 256×256, comfortably under 64 KB
// in practice but the buffer's sized 128 KB to leave headroom for
// future firmwares that might bump the texture resolution.
constexpr std::size_t kAvatarBufBytes = 128 * 1024;

// Load the user identified by `uid` into the supplied output slots.
// Returns true on success. Logs and returns false on any libnx error.
bool load_user_into(NVGcontext* vg, AccountUid uid,
                    int& out_handle, std::string& out_nick) {
    AccountProfile profile{};
    Result rc = accountGetProfile(&profile, uid);
    if (R_FAILED(rc)) {
        foyer::log::write(
            "[hos_status] accountGetProfile rc=0x%x\n", rc);
        return false;
    }
    AccountProfileBase base{};
    AccountUserData    udata{};
    if (R_SUCCEEDED(accountProfileGet(&profile, &udata, &base))) {
        out_nick.assign(base.nickname);
    }
    out_handle = 0;
    std::vector<std::uint8_t> buf(kAvatarBufBytes);
    u32 actual = 0;
    if (R_SUCCEEDED(accountProfileLoadImage(
            &profile, buf.data(), buf.size(), &actual))
        && actual > 0) {
        out_handle = nvgCreateImageMem(vg, 0, buf.data(), (int)actual);
    }
    accountProfileClose(&profile);
    return true;
}

// Roster sync: walk every account, set the active user, and rebuild
// the secondary list with avatars decoded into nvg handles. Drops
// any prior nvg handles before allocating fresh ones.
void sync_users(NVGcontext* vg, AccountUid active) {
    if (g_avatar_handle > 0 && vg) nvgDeleteImage(vg, g_avatar_handle);
    g_avatar_handle = 0;
    g_nickname.clear();
    for (auto& u : g_secondaries) {
        if (u.avatar > 0 && vg) nvgDeleteImage(vg, u.avatar);
    }
    g_secondaries.clear();

    s32 count = 0;
    AccountUid uids[ACC_USER_LIST_SIZE]{};
    if (R_FAILED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count))) {
        return;
    }
    g_active_uid = active;
    for (int i = 0; i < count; i++) {
        const auto& u = uids[i];
        const bool is_active = (u.uid[0] == active.uid[0]
                              && u.uid[1] == active.uid[1]);
        if (is_active) {
            load_user_into(vg, u, g_avatar_handle, g_nickname);
        } else {
            SecondaryUser su{};
            su.uid = u;
            load_user_into(vg, u, su.avatar, su.nickname);
            g_secondaries.push_back(std::move(su));
        }
    }
    foyer::log::write("[hos_status] roster: 1 active + %zu secondary\n",
        g_secondaries.size());
}

void load_active_user(NVGcontext* vg) {
    AccountUid uid{};
    Result rc = accountGetPreselectedUser(&uid);
    if (R_FAILED(rc) || (!uid.uid[0] && !uid.uid[1])) {
        // No preselected user (foyer was launched without an account
        // pre-pick — common under hbloader). Fall back to listing all
        // and picking the first.
        s32 count = 0;
        AccountUid uids[ACC_USER_LIST_SIZE]{};
        if (R_SUCCEEDED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &count))
            && count > 0) {
            uid = uids[0];
        } else {
            foyer::log::write(
                "[hos_status] no users available rc=0x%x\n", rc);
            return;
        }
    }
    sync_users(vg, uid);
}

void refresh_battery() {
    u32 pct = 0;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&pct))) {
        g_battery_pct = (int)pct;
    }
    PsmChargerType type = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetChargerType(&type))) {
        g_charging = (type != PsmChargerType_Unconnected);
    }
}

void refresh_wifi() {
    NifmInternetConnectionType type = (NifmInternetConnectionType)0;
    u32 strength = 0;
    NifmInternetConnectionStatus status =
        (NifmInternetConnectionStatus)0;
    if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
        g_wifi_connected = (status == NifmInternetConnectionStatus_Connected);
        // libnx reports strength 0..3; clamp defensively.
        g_wifi_strength  = (int)strength;
        if (g_wifi_strength < 0) g_wifi_strength = 0;
        if (g_wifi_strength > 3) g_wifi_strength = 3;
    } else {
        g_wifi_connected = false;
        g_wifi_strength  = 0;
    }
}

} // namespace

void init(NVGcontext* vg) {
    load_active_user(vg);
    refresh_battery();
    refresh_wifi();
    g_last_poll = std::chrono::steady_clock::now();
}

void poll() {
    // Debounce. The values driving this don't change fast — battery
    // resolution is whole percent, wifi strength is a 0-3 bucket — so
    // re-querying every frame burns service calls for nothing.
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_poll)
            .count() < 1000) {
        return;
    }
    g_last_poll = now;
    refresh_battery();
    refresh_wifi();
}

void shutdown(NVGcontext* vg) {
    if (g_avatar_handle > 0 && vg) {
        nvgDeleteImage(vg, g_avatar_handle);
    }
    g_avatar_handle = 0;
    g_nickname.clear();
}

int                avatar_handle()    { return g_avatar_handle; }
const std::string& nickname()         { return g_nickname; }
int                battery_pct()      { return g_battery_pct; }
bool               charging()         { return g_charging; }
int                wifi_strength()    { return g_wifi_strength; }
bool               wifi_connected()   { return g_wifi_connected; }

int  other_avatar_count() { return (int)g_secondaries.size(); }
int  other_avatar_handle(int i) {
    if (i < 0 || i >= (int)g_secondaries.size()) return 0;
    return g_secondaries[i].avatar;
}
const std::string& other_nickname(int i) {
    static const std::string empty;
    if (i < 0 || i >= (int)g_secondaries.size()) return empty;
    return g_secondaries[i].nickname;
}

void switch_active(int secondary_idx, NVGcontext* vg) {
    if (secondary_idx < 0
        || secondary_idx >= (int)g_secondaries.size()) return;
    const AccountUid new_active = g_secondaries[secondary_idx].uid;
    sync_users(vg, new_active);
}

} // namespace foyer::browser::hos_status
