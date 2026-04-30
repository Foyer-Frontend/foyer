#pragma once

#include <string>
#include <string_view>

namespace foyer::util {

// Peek the archive at `archive_path`. Walks the entries and returns the
// FIRST entry whose extension matches one of the |-separated extensions in
// `valid_extensions` (e.g. "nes|fds|unif"). Returns an empty string if no
// match is found or the archive can't be opened.
std::string archive_peek_inner_rom(std::string_view archive_path,
                                   std::string_view valid_extensions);

// Extracts the FIRST matching inner rom from `archive_path` into
// `out_path`, creating parent directories as needed. `valid_extensions` is
// the same |-separated list used by archive_peek_inner_rom. Returns true
// on success.
bool archive_extract_inner_rom(std::string_view archive_path,
                               std::string_view valid_extensions,
                               std::string_view out_path);

} // namespace foyer::util
