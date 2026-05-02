#include "cache.hpp"

#include <string>
#include <switch.h>

namespace foyer::scrapers {

std::string cover_path(std::string_view sys, std::string_view stem) {
    return std::string{"/foyer/assets/covers/"} + std::string{sys} + "/" + std::string{stem} + ".png";
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
