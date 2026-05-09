#include "manifest_cache.hpp"

#include "library/config.hpp"
#include "platform/log.hpp"

namespace foyer::browser::manifest_cache {

namespace {
::foyer::library::CoreManifest g_cores;
}  // namespace

void prefetch() {
    const std::string url = ::foyer::library::config().cores_manifest_url;
    g_cores = ::foyer::library::fetch_manifest(url);
    foyer::log::write(
        "[manifest_cache] cores manifest: %zu entries (version=%s)\n",
        g_cores.cores.size(), g_cores.version.c_str());
}

const ::foyer::library::CoreManifest& cores() {
    return g_cores;
}

}  // namespace foyer::browser::manifest_cache
