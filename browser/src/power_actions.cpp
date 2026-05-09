#include "power_actions.hpp"

#include "platform/log.hpp"

#include <switch.h>

#include <cstdio>

namespace foyer::browser::power_actions {

namespace {

// Flush pending SD writes before any disruptive firmware call so
// data on disk matches what the user expected when they hit the
// menu. Both shutdown and reboot tear the FS down hard otherwise.
void flush_sd() {
    if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
        fsFsCommit(fs);
    }
}

}  // namespace

void sleep() {
    foyer::log::write("[power] sleep requested\n");
    appletStartSleepSequence(true);
}

void shutdown() {
    foyer::log::write("[power] shutdown requested\n");
    flush_sd();
    appletRequestToShutdown();
}

void reboot() {
    foyer::log::write("[power] reboot requested\n");
    flush_sd();
    appletRequestToReboot();
}

void reboot_hekate() {
    foyer::log::write("[power] reboot to hekate requested\n");

    // Copy the user's hekate payload to /atmosphere/reboot_payload.bin
    // — atmosphère's reboot_payload.bin convention is what makes a
    // normal reboot land on hekate. If the source is missing we just
    // do a plain reboot and let the user's existing atmosphère
    // config decide what boots next.
    std::FILE* in = std::fopen("/bootloader/update.bin", "rb");
    if (in) {
        std::FILE* out = std::fopen("/atmosphere/reboot_payload.bin", "wb");
        if (out) {
            char buf[64 * 1024];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
                std::fwrite(buf, 1, n, out);
            }
            std::fclose(out);
            foyer::log::write(
                "[power] copied hekate payload to "
                "/atmosphere/reboot_payload.bin\n");
        }
        std::fclose(in);
    } else {
        foyer::log::write(
            "[power] /bootloader/update.bin missing — falling back "
            "to a plain reboot\n");
    }

    flush_sd();
    appletRequestToReboot();
}

}  // namespace foyer::browser::power_actions
