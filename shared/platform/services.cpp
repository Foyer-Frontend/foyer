// libnx service init/teardown shared by browser and player nros.
//
// Lives in foyer_shared (not foyer_render) because the brls-based browser
// also depends on accountsService / nifm / psm / socket but does not link
// the nanovg-deko3d render shell. Keeping a single definition here avoids
// the prior bug where the browser had no userAppInit and accountsService
// faulted on the first call from hos_status::init.

#include "log.hpp"

#include <switch.h>

extern "C" {

void userAppInit(void) {
    Result rc;
    if (R_FAILED(rc = appletLockExit()))                 diagAbortWithResult(rc);
    if (R_FAILED(rc = plInitialize(PlServiceType_User))) diagAbortWithResult(rc);
    if (R_FAILED(rc = nifmInitialize(NifmServiceType_User))) diagAbortWithResult(rc);
    if (R_FAILED(rc = setInitialize()))                  diagAbortWithResult(rc);
    if (R_FAILED(rc = psmInitialize()))                  diagAbortWithResult(rc);
    if (R_FAILED(rc = accountInitialize(AccountServiceType_Application)))
        diagAbortWithResult(rc);
    if (R_FAILED(rc = nsInitialize())) {
        foyer::log::write(
            "[platform] nsInitialize rc=0x%x — Switch tile disabled\n", rc);
    }

    static const SocketInitConfig kSockets = {
        .tcp_tx_buf_size      = 1024 * 32,
        .tcp_rx_buf_size      = 1024 * 64,
        .tcp_tx_buf_max_size  = 1024 * 256,
        .tcp_rx_buf_max_size  = 1024 * 256,
        .udp_tx_buf_size      = 0x2400,
        .udp_rx_buf_size      = 0xA500,
        .sb_efficiency        = 4,
        .num_bsd_sessions     = 8,
        .bsd_service_type     = BsdServiceType_Auto,
    };
    if (R_FAILED(rc = socketInitialize(&kSockets)))      diagAbortWithResult(rc);

    appletSetScreenShotPermission(AppletScreenShotPermission_Enable);
}

void userAppExit(void) {
    socketExit();
    nsExit();
    accountExit();
    psmExit();
    setExit();
    nifmExit();
    plExit();
    if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }
    appletUnlockExit();
}

} // extern "C"
