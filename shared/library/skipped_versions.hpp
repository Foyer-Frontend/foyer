#pragma once

#include <string>
#include <string_view>

namespace foyer::library {

// Per-item "skip this version" mute. When the user picks "Skip this
// version" in the Updates page picker, we record (kind, id, version)
// in /foyer/data/skipped_versions.json. The Updates aggregator then
// hides any row whose available version matches a skipped entry, so
// the user isn't nagged about the same release every boot.
//
// A different (newer) version of the same item shows up again — skip
// is per-version, not per-item.
//
// Storage format (one object per skip):
//   {
//     "skipped": [
//       {"kind": "core",  "id": "snes9x",   "version": "1.62.3"},
//       {"kind": "foyer", "id": "foyer",    "version": "0.2.46"}
//     ]
//   }

enum class SkipKind { Foyer, Core, Bezel, Cheat, Shader };

// Returns true if (kind, id, version) is currently in the mute list.
bool is_version_skipped(SkipKind kind,
                        std::string_view id,
                        std::string_view version);

// Add (kind, id, version) to the mute list. Idempotent — adding the
// same triple twice writes once. Persists to disk immediately.
void skip_version(SkipKind kind,
                  std::string_view id,
                  std::string_view version);

// Drop every skip for `id` regardless of version. Used by the
// "un-skip" path if we ever expose one in the UI.
void clear_skips_for(SkipKind kind, std::string_view id);

} // namespace foyer::library
