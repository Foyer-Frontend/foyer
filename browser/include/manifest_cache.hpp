#pragma once

#include "library/bezel_installer.hpp"
#include "library/cheat_installer.hpp"
#include "library/core_installer.hpp"
#include "library/shader_installer.hpp"

namespace foyer::browser::manifest_cache {

// Synchronous fetch of every manifest the browser cares about
// (cores / bezels / shaders / cheats). Called from main() before
// the wizard and from the splash worker so Settings tabs and the
// Updates check have data to compare against without re-fetching
// each tab open.
void prefetch();

const ::foyer::library::CoreManifest&   cores();
const ::foyer::library::BezelManifest&  bezels();
const ::foyer::library::ShaderManifest& shaders();
const ::foyer::library::CheatManifest&  cheats();

}  // namespace foyer::browser::manifest_cache
