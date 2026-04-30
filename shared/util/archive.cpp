#include "archive.hpp"
#include "platform/log.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace foyer::util {
namespace {

bool ext_matches(std::string_view name, std::string_view list) {
    // Find file extension after last '.'.
    const auto dot = name.rfind('.');
    if (dot == std::string_view::npos) return false;
    auto raw = name.substr(dot + 1);
    // Lowercase compare.
    auto lower = [](char c) {
        return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    };

    std::size_t cursor = 0;
    while (cursor < list.size()) {
        const auto bar = list.find('|', cursor);
        const auto seg_end = (bar == std::string_view::npos) ? list.size() : bar;
        const auto seg = list.substr(cursor, seg_end - cursor);
        if (raw.size() == seg.size()) {
            bool eq = true;
            for (std::size_t i = 0; i < seg.size(); i++) {
                if (lower(raw[i]) != lower(seg[i])) { eq = false; break; }
            }
            if (eq) return true;
        }
        if (bar == std::string_view::npos) break;
        cursor = bar + 1;
    }
    return false;
}

archive* open_for_read(std::string_view path) {
    auto* a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_filter_all(a);
    const std::string p{path};
    if (archive_read_open_filename(a, p.c_str(), 64 * 1024) != ARCHIVE_OK) {
        foyer::log::write("[archive] open %s failed: %s\n",
            p.c_str(), archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

void mkdir_p(const std::string& path) {
    // Walk components, creating each directory.
    std::string cur;
    for (std::size_t i = 0; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && cur != "/") {
                ::mkdir(cur.c_str(), 0777);
            }
        }
        if (i < path.size()) cur.push_back(path[i]);
    }
}

} // namespace

std::string archive_peek_inner_rom(std::string_view archive_path,
                                   std::string_view valid_extensions) {
    auto* a = open_for_read(archive_path);
    if (!a) return {};

    std::string found;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (!name) continue;
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        if (!ext_matches(name, valid_extensions)) continue;
        found = name;
        break;
    }
    archive_read_free(a);
    return found;
}

bool archive_extract_inner_rom(std::string_view archive_path,
                               std::string_view valid_extensions,
                               std::string_view out_path) {
    auto* a = open_for_read(archive_path);
    if (!a) return false;

    bool ok = false;
    archive_entry* entry = nullptr;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (!name) continue;
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        if (!ext_matches(name, valid_extensions)) continue;

        const std::string dst{out_path};
        const auto slash = dst.rfind('/');
        if (slash != std::string::npos) mkdir_p(dst.substr(0, slash));

        FILE* fp = std::fopen(dst.c_str(), "wb");
        if (!fp) {
            foyer::log::write("[archive] open out %s failed\n", dst.c_str());
            break;
        }
        const void* buf;
        std::size_t buf_sz;
        la_int64_t off;
        bool write_ok = true;
        while (archive_read_data_block(a, &buf, &buf_sz, &off) == ARCHIVE_OK) {
            if (std::fwrite(buf, 1, buf_sz, fp) != buf_sz) {
                write_ok = false;
                break;
            }
        }
        std::fclose(fp);
        ok = write_ok;
        break;
    }
    archive_read_free(a);
    return ok;
}

} // namespace foyer::util
