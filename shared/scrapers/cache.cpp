#include "cache.hpp"

#include <dirent.h>
#include <string>
#include <switch.h>

namespace foyer::scrapers {

std::string cover_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/covers/"} + std::string{sys} + "/" + std::string{stem} + ".png";
}

std::string snap_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/snaps/"} + std::string{sys} + "/" + std::string{stem} + ".png";
}

std::string title_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/titles/"} + std::string{sys} + "/" + std::string{stem} + ".png";
}

std::string background_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/backgrounds/"} + std::string{sys} + "/" + std::string{stem} + ".jpg";
}

std::string system_logo_path(std::string_view sys) {
    return std::string{"/foyer/assets/systems/"} + std::string{sys} + ".png";
}

std::string metadata_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/metadata/"} + std::string{sys} + "/" + std::string{stem} + ".json";
}

std::string game_asset_dir(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/system/"}
         + std::string{sys} + "/" + std::string{stem} + "/";
}

std::string find_in_dir(std::string_view dir, std::string_view prefix) {
    std::string path{dir};
    DIR* d = ::opendir(path.c_str());
    if (!d) return {};
    std::string out;
    while (auto* ent = ::readdir(d)) {
        const std::string nm = ent->d_name;
        if (nm.size() < prefix.size()) continue;
        if (nm.compare(0, prefix.size(), prefix) == 0) {
            out = path + nm;
            break;
        }
    }
    ::closedir(d);
    return out;
}

void ensure_parent_dir(std::string_view path) {
    auto* fs = fsdevGetDeviceFileSystem("sdmc:");
    if (!fs) return;
    // Walk path and create each dir component (idempotent).
    std::string p{path};
    for (std::size_t i = 1; i < p.size(); i++) {
        if (p[i] == '/') {
            const std::string sub = p.substr(0, i);
            fsFsCreateDirectory(fs, sub.c_str());
        }
    }
}

} // namespace foyer::scrapers
