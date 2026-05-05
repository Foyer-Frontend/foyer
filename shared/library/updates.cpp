#include "updates.hpp"
#include "skipped_versions.hpp"

namespace foyer::library {
namespace {

bool not_skipped(SkipKind k, const std::string& id, const std::string& ver) {
    return !is_version_skipped(k, id, ver);
}

} // namespace

UpdateBuckets compute_pending_updates(
    const FoyerManifest&  foyer_m,
    const std::string&    foyer_current,
    const CoreManifest&   cores_m,
    const BezelManifest&  bezels_m,
    const CheatManifest&  cheats_m) {
    UpdateBuckets out;
    out.scraped_at = std::time(nullptr);

    // Foyer self — only rows when the manifest is strictly newer than
    // the running binary. We don't have a "currently installed"
    // sidecar for foyer.nro itself; the running version is authoritative.
    if (!foyer_m.version.empty() &&
        is_newer_version(foyer_current, foyer_m.version) &&
        not_skipped(SkipKind::Foyer, "foyer", foyer_m.version)) {
        UpdateItem it;
        it.kind          = UpdateKind::Foyer;
        it.id            = "foyer";
        it.display       = "Foyer";
        it.installed_ver = foyer_current;
        it.available_ver = foyer_m.version;
        it.download_size = foyer_m.size;
        out.foyer.push_back(std::move(it));
    }

    // Cores — iterate every manifest entry and compare against the
    // <nro>.version sidecar. Items not yet installed (empty sidecar)
    // belong in the Catalog view, not here.
    for (const auto& c : cores_m.cores) {
        const auto installed = installed_core_version(c.nro);
        if (installed.empty() || installed == c.version) continue;
        if (!not_skipped(SkipKind::Core, c.name, c.version)) continue;
        UpdateItem it;
        it.kind          = UpdateKind::Core;
        it.id            = c.name;
        it.display       = c.name;
        it.installed_ver = installed;
        it.available_ver = c.version;
        it.download_size = c.size;
        out.cores.push_back(std::move(it));
    }

    // Bezel packs — same shape, sidecar lives at /foyer/bezels/.<pack>.version.
    for (const auto& b : bezels_m.packs) {
        const auto installed = installed_bezel_version(b.name);
        if (installed.empty() || installed == b.version) continue;
        if (!not_skipped(SkipKind::Bezel, b.name, b.version)) continue;
        UpdateItem it;
        it.kind          = UpdateKind::Bezel;
        it.id            = b.name;
        it.display       = b.name;
        it.installed_ver = installed;
        it.available_ver = b.version;
        it.download_size = b.size;
        out.bezels.push_back(std::move(it));
    }

    // Cheat packs.
    for (const auto& c : cheats_m.packs) {
        const auto installed = installed_cheat_version(c.name);
        if (installed.empty() || installed == c.version) continue;
        if (!not_skipped(SkipKind::Cheat, c.name, c.version)) continue;
        UpdateItem it;
        it.kind          = UpdateKind::Cheat;
        it.id            = c.name;
        it.display       = c.name;
        it.installed_ver = installed;
        it.available_ver = c.version;
        it.download_size = c.size;
        out.cheats.push_back(std::move(it));
    }

    return out;
}

} // namespace foyer::library
