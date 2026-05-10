#include "manifest_cache.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"

namespace foyer::browser::manifest_cache {

namespace {
::foyer::library::CoreManifest   g_cores;
::foyer::library::BezelManifest  g_bezels;
::foyer::library::ShaderManifest g_shaders;
::foyer::library::CheatManifest  g_cheats;
}  // namespace

void prefetch() {
    const auto& cfg = ::foyer::library::config();

    g_cores = ::foyer::library::fetch_manifest(cfg.cores_manifest_url);
    foyer::log::write(
        "[manifest_cache] cores: %zu entries (version=%s)\n",
        g_cores.cores.size(), g_cores.version.c_str());

    g_bezels = ::foyer::library::fetch_bezel_manifest(cfg.bezels_manifest_url);
    foyer::log::write(
        "[manifest_cache] bezels: %zu packs (version=%s)\n",
        g_bezels.packs.size(), g_bezels.version.c_str());

    g_shaders = ::foyer::library::fetch_shader_manifest(cfg.shaders_manifest_url);
    foyer::log::write(
        "[manifest_cache] shaders: %zu presets (version=%s)\n",
        g_shaders.presets.size(), g_shaders.version.c_str());

    g_cheats = ::foyer::library::fetch_cheat_manifest(cfg.cheats_manifest_url);
    foyer::log::write(
        "[manifest_cache] cheats: %zu packs (version=%s)\n",
        g_cheats.packs.size(), g_cheats.version.c_str());
}

const ::foyer::library::CoreManifest&   cores()   { return g_cores; }
const ::foyer::library::BezelManifest&  bezels()  { return g_bezels; }
const ::foyer::library::ShaderManifest& shaders() { return g_shaders; }
const ::foyer::library::CheatManifest&  cheats()  { return g_cheats; }

}  // namespace foyer::browser::manifest_cache
