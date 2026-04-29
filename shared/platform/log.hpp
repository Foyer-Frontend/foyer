#pragma once

namespace foyer::log {

// Open the file logger at /config/foyer/log.txt (truncating prior contents).
// Returns true if the file is now writable.
bool init_file();

// Close the file logger.
void exit_file();

// Whether any sink (file or nxlink) is active. Used by `write` to skip work.
bool is_init();

// Initialise the nxlink sink so log lines tee to a connected dev host.
bool init_nxlink();

void exit_nxlink();

// printf-style log. No-op when no sink is active. Lines are timestamped.
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace foyer::log
