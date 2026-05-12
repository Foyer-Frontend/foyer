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

void prefetch_cores() {
    const auto& cfg = ::foyer::library::config();
    g_cores = ::foyer::library::fetch_manifest(cfg.cores_manifest_url);
    foyer::log::write(
        "[manifest_cache] cores: %zu entries (version=%s)\n",
        g_cores.cores.size(), g_cores.version.c_str());
}

void prefetch_bezels() {
    const auto& cfg = ::foyer::library::config();
    g_bezels = ::foyer::library::fetch_bezel_manifest(cfg.bezels_manifest_url);
    foyer::log::write(
        "[manifest_cache] bezels: %zu packs (version=%s)\n",
        g_bezels.packs.size(), g_bezels.version.c_str());
}

void prefetch_shaders() {
    const auto& cfg = ::foyer::library::config();
    g_shaders = ::foyer::library::fetch_shader_manifest(cfg.shaders_manifest_url);
    foyer::log::write(
        "[manifest_cache] shaders: %zu presets (version=%s)\n",
        g_shaders.presets.size(), g_shaders.version.c_str());
}

void prefetch_cheats() {
    const auto& cfg = ::foyer::library::config();
    g_cheats = ::foyer::library::fetch_cheat_manifest(cfg.cheats_manifest_url);
    foyer::log::write(
        "[manifest_cache] cheats: %zu packs (version=%s)\n",
        g_cheats.packs.size(), g_cheats.version.c_str());
}

void prefetch(std::function<void(int, int, const char*)> on_step) {
    constexpr int kTotal = 4;
    if (on_step) on_step(0, kTotal, "Fetching cores manifest…");
    prefetch_cores();
    if (on_step) on_step(1, kTotal, "Fetching bezels manifest…");
    prefetch_bezels();
    if (on_step) on_step(2, kTotal, "Fetching shaders manifest…");
    prefetch_shaders();
    if (on_step) on_step(3, kTotal, "Fetching cheats manifest…");
    prefetch_cheats();
    if (on_step) on_step(4, kTotal, "Ready");
}

const ::foyer::library::CoreManifest&   cores()   { return g_cores; }
const ::foyer::library::BezelManifest&  bezels()  { return g_bezels; }
const ::foyer::library::ShaderManifest& shaders() { return g_shaders; }
const ::foyer::library::CheatManifest&  cheats()  { return g_cheats; }

}  // namespace foyer::browser::manifest_cache
