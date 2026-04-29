#include "savestate.hpp"
#include "platform/log.hpp"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <vector>

#include <switch.h>

extern "C" {
    size_t retro_serialize_size(void);
    bool   retro_serialize(void* data, size_t size);
    bool   retro_unserialize(const void* data, size_t size);
}

namespace foyer::libretro {
namespace {

void ensure_dir(const std::string& path) {
    if (auto* fs = fsdevGetDeviceFileSystem("sdmc:")) {
        // Walk path components and create each one.
        for (std::size_t i = 1; i < path.size(); i++) {
            if (path[i] == '/') {
                fsFsCreateDirectory(fs, path.substr(0, i).c_str());
            }
        }
        fsFsCreateDirectory(fs, path.c_str());
    }
}

std::string parent(std::string_view p) {
    const auto pos = p.find_last_of('/');
    return std::string{(pos == std::string_view::npos) ? std::string_view{} : p.substr(0, pos)};
}

std::string basename(std::string_view p) {
    const auto pos = p.find_last_of('/');
    return std::string{(pos == std::string_view::npos) ? p : p.substr(pos + 1)};
}

std::string strip_ext(std::string_view p) {
    const auto pos = p.find_last_of('.');
    return std::string{(pos == std::string_view::npos) ? p : p.substr(0, pos)};
}

} // namespace

std::string state_path_for(std::string_view rom_path,
                           std::string_view system_folder,
                           int               slot) {
    std::string sys{system_folder};
    if (sys.empty()) {
        sys = basename(parent(rom_path));
    }
    std::string dir = "/foyer/states/" + sys;
    ensure_dir(dir);

    char tail[16];
    std::snprintf(tail, sizeof(tail), ".%d.state", slot);
    return dir + "/" + strip_ext(basename(rom_path)) + tail;
}

void inspect_slots(std::string_view rom_path,
                   std::string_view system_folder,
                   StateSlot       out[kStateSlotCount]) {
    for (int i = 0; i < kStateSlotCount; i++) {
        out[i].slot       = i;
        out[i].exists     = false;
        out[i].mtime      = 0;
        out[i].size_bytes = 0;

        const auto path = state_path_for(rom_path, system_folder, i);
        struct stat st{};
        if (::stat(path.c_str(), &st) == 0) {
            out[i].exists     = true;
            out[i].mtime      = st.st_mtime;
            out[i].size_bytes = (std::uint64_t)st.st_size;
        }
    }
}

bool save_state(const std::string& path) {
    const auto sz = retro_serialize_size();
    if (sz == 0) {
        foyer::log::write("[state] core reports 0-size state\n");
        return false;
    }
    std::vector<std::uint8_t> buf(sz);
    if (!retro_serialize(buf.data(), buf.size())) {
        foyer::log::write("[state] retro_serialize failed (sz=%zu)\n", sz);
        return false;
    }

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    if (!out) {
        foyer::log::write("[state] open(%s) for write failed\n", path.c_str());
        return false;
    }
    out.write(reinterpret_cast<const char*>(buf.data()), (std::streamsize)buf.size());
    if (!out) {
        foyer::log::write("[state] write to %s failed\n", path.c_str());
        return false;
    }
    foyer::log::write("[state] saved %zu bytes to %s\n", buf.size(), path.c_str());
    return true;
}

bool load_state(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        foyer::log::write("[state] open(%s) for read failed\n", path.c_str());
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto sz = (std::size_t)in.tellg();
    in.seekg(0, std::ios::beg);

    const auto core_sz = retro_serialize_size();
    if (core_sz != 0 && core_sz != sz) {
        foyer::log::write("[state] size mismatch: file=%zu core=%zu\n", sz, core_sz);
    }

    std::vector<std::uint8_t> buf(sz);
    if (!in.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size())) {
        return false;
    }
    if (!retro_unserialize(buf.data(), buf.size())) {
        foyer::log::write("[state] retro_unserialize refused\n");
        return false;
    }
    foyer::log::write("[state] loaded %zu bytes from %s\n", buf.size(), path.c_str());
    return true;
}

} // namespace foyer::libretro
