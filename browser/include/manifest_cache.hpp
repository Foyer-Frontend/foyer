#pragma once

#include "library/bezel_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/core_installer.hpp"
#include "library/shader_installer.hpp"

#include <functional>

namespace foyer::browser::manifest_cache {

// Synchronous fetch of every manifest the browser cares about
// (cores / bezels / shaders / cheats). Called from main() before
// the wizard and from the splash worker so Settings tabs and the
// Updates check have data to compare against without re-fetching
// each tab open.
//
// `on_step` is invoked at each manifest boundary with (index, total,
// label) so the splash can paint granular progress.
void prefetch(std::function<void(int, int, const char*)> on_step = {});

void prefetch_cores();
void prefetch_bezels();
void prefetch_shaders();
void prefetch_cheats();

const ::foyer::library::CoreManifest&   cores();
const ::foyer::library::BezelManifest&  bezels();
const ::foyer::library::ShaderManifest& shaders();
const ::foyer::library::CheatManifest&  cheats();

}  // namespace foyer::browser::manifest_cache
