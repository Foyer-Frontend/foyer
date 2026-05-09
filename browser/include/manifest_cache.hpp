#pragma once

#include "library/core_installer.hpp"

namespace foyer::browser::manifest_cache {

// One-shot synchronous fetch of the foyer-cores release manifest.
// Called at boot from main(), before pushing the wizard so the
// Cores step can render the list immediately. ~7 KB JSON, finishes
// in roughly a second on a healthy network; the wizard's UI just
// blocks until it's done (we don't paint a progress splash for it
// at this scale).
void prefetch();

// Cached result. Empty .cores when the prefetch failed (network
// down, parse error, etc.) — the wizard's Cores step then renders
// a "couldn't reach the manifest, install cores from Settings
// later" message instead of the list.
const ::foyer::library::CoreManifest& cores();

}  // namespace foyer::browser::manifest_cache
