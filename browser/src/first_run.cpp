#include "first_run.hpp"

#include "platform/log.hpp"

#include <cstdio>
#include <sys/stat.h>

namespace foyer::browser::first_run {

namespace {
constexpr const char* kMarkerPath = "/foyer/data/first_run_complete";
}  // namespace

bool is_complete() {
    struct stat st{};
    return ::stat(kMarkerPath, &st) == 0;
}

void mark_complete() {
    if (auto* f = std::fopen(kMarkerPath, "wb")) {
        // Write a tiny token so the file isn't zero-bytes — avoids
        // confusion with the partial-download sentinel checks
        // elsewhere.
        std::fputs("first_run_complete\n", f);
        std::fclose(f);
        foyer::log::write("[first_run] marker written: %s\n", kMarkerPath);
    } else {
        foyer::log::write(
            "[first_run] failed to write marker %s\n", kMarkerPath);
    }
}

}  // namespace foyer::browser::first_run
