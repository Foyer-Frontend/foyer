#pragma once

#include "library/bezel_installer.hpp"
#include "library/core_installer.hpp"
#include "library/shader_installer.hpp"

namespace foyer::browser::manifest_cache {

// One-shot synchronous fetch of all three manifests the wizard
// needs (cores / bezels / shaders). Each is small JSON so the
// total cost is a few seconds on a healthy connection. Called
// from main() right before pushing the wizard.
void prefetch();

const ::foyer::library::CoreManifest&   cores();
const ::foyer::library::BezelManifest&  bezels();
const ::foyer::library::ShaderManifest& shaders();

}  // namespace foyer::browser::manifest_cache
