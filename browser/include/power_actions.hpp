#pragma once

namespace foyer::browser::power_actions {

// libnx-driven HOS power actions. Each function commits SD writes
// before calling the firmware so pending file IO doesn't get
// truncated on shutdown.

// `appletStartSleepSequence(true)` — drops the console into sleep.
void sleep();

// `appletRequestToShutdown()` — full power off.
void shutdown();

// `appletRequestToReboot()` — soft reboot back into HOS.
void reboot();

// Atmosphère's reboot-to-payload mechanism: copy the user's hekate
// at /bootloader/update.bin to /atmosphere/reboot_payload.bin and
// reboot. If /bootloader/update.bin is missing, falls back to a
// plain reboot (atmosphère's existing config decides what boots).
void reboot_hekate();

}  // namespace foyer::browser::power_actions
