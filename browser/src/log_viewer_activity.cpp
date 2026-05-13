#include "activity/log_viewer_activity.hpp"

#include "platform/log.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/label.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace foyer::browser {

namespace {

constexpr const char* kLogDir    = "/foyer/data/logs";
constexpr const char* kCrashDir  = "/atmosphere/crash_reports";
constexpr std::size_t kChunkLines = 80;
constexpr std::size_t kMaxBytes   = 4 * 1024 * 1024;

std::vector<std::string> list_dir_logs(const char* dir) {
    std::vector<std::string> names;
    DIR* d = ::opendir(dir);
    if (!d) return names;
    while (auto* ent = ::readdir(d)) {
        if (ent->d_name[0] == '.') continue;
        const std::string nm = ent->d_name;
        if (nm.size() < 4) continue;
        if (nm.substr(nm.size() - 4) != ".log") continue;
        names.push_back(nm);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end(), std::greater<>{});
    return names;
}

std::vector<std::string> list_logs()    { return list_dir_logs(kLogDir);   }
std::vector<std::string> list_crashes() { return list_dir_logs(kCrashDir); }

std::string format_size(std::size_t bytes) {
    char buf[32];
    if (bytes < 1024) {
        std::snprintf(buf, sizeof(buf), "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f MB",
            bytes / (1024.0 * 1024.0));
    }
    return buf;
}

void hide_footer_extras(brls::AppletFrame* frame) {
    if (!frame) return;
    auto* footer = frame->getFooter();
    if (!footer) return;
    for (const char* id : {"brls/hints/time",
                           "brls/battery",
                           "brls/wireless"}) {
        if (auto* v = footer->getView(id)) {
            v->setVisibility(brls::Visibility::GONE);
        }
    }
}

}  // namespace

brls::View* LogListActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    {
        auto* h = new brls::Header();
        h->setTitle("Recent log files");
        host->addView(h);
    }

    auto add_log_section = [&](const char* dir,
                                const std::vector<std::string>& names,
                                const char* empty_msg) {
        if (names.empty()) {
            auto* none = new brls::Label();
            none->setText(empty_msg);
            none->setFontSize(20.0f);
            host->addView(none);
            return;
        }
        for (const auto& nm : names) {
            const std::string full = std::string{dir} + "/" + nm;
            struct stat st{};
            std::string detail;
            if (::stat(full.c_str(), &st) == 0) {
                detail = format_size(static_cast<std::size_t>(st.st_size));
            }
            auto* cell = new brls::DetailCell();
            cell->title->setText(nm);
            cell->detail->setText(detail);
            cell->registerClickAction([full](brls::View*) {
                brls::Application::pushActivity(
                    new ::foyer::browser::LogContentsActivity(full));
                return true;
            });
            host->addView(cell);
        }
    };

    add_log_section(kLogDir, list_logs(), "No log files yet.");

    {
        auto* spacer = new brls::Box();
        spacer->setHeight(12.0f);
        host->addView(spacer);
        auto* h = new brls::Header();
        h->setTitle("Atmosphère crash reports");
        host->addView(h);
    }
    add_log_section(kCrashDir, list_crashes(),
        "No crash reports under /atmosphere/crash_reports.");

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Logs");
    hide_footer_extras(frame);
    return frame;
}

void LogListActivity::onContentAvailable() {
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

LogContentsActivity::LogContentsActivity(std::string log_path)
    : m_log_path(std::move(log_path)) {}

brls::View* LogContentsActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(16.0f, 24.0f, 24.0f, 24.0f);

    std::ifstream in{m_log_path, std::ios::binary};
    if (!in) {
        auto* err = new brls::Label();
        err->setText("Cannot open " + m_log_path);
        err->setFontSize(20.0f);
        host->addView(err);
    } else {
        in.seekg(0, std::ios::end);
        const std::size_t size = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        if (size > kMaxBytes) {
            const std::size_t skip = size - kMaxBytes;
            in.seekg(static_cast<std::streamoff>(skip), std::ios::beg);
            // Discard the first partial line after the seek so the
            // first visible label doesn't start mid-message.
            std::string discard;
            std::getline(in, discard);

            auto* hint = new brls::Label();
            hint->setText("[truncated: showing last "
                + format_size(kMaxBytes)
                + " of " + format_size(size) + "]");
            hint->setFontSize(16.0f);
            host->addView(hint);
        }

        std::vector<std::string> chunk;
        chunk.reserve(kChunkLines);
        std::string line;
        auto flush = [&]() {
            if (chunk.empty()) return;
            std::ostringstream ss;
            for (const auto& l : chunk) ss << l << '\n';
            chunk.clear();
            // Each chunk is wrapped in a focusable Box with the
            // highlight + outline suppressed. brls's ScrollingFrame
            // moves contentOffsetY whenever focus jumps to a child
            // that's outside the visible viewport, so DPAD up/down
            // walks chunk-by-chunk and the scrollbar follows. Plain
            // Labels are non-focusable, which is why the previous
            // log-viewer build only scrolled with touch — there was
            // nothing for the focus walk to hop between.
            auto* wrap = new brls::Box();
            wrap->setAxis(brls::Axis::COLUMN);
            wrap->setAlignItems(brls::AlignItems::STRETCH);
            wrap->setFocusable(true);
            wrap->setHideHighlight(true);
            wrap->setHideHighlightBackground(true);
            wrap->setHideHighlightBorder(true);
            auto* lbl = new brls::Label();
            lbl->setText(ss.str());
            lbl->setFontSize(18.0f);
            wrap->addView(lbl);
            host->addView(wrap);
        };

        while (std::getline(in, line)) {
            chunk.push_back(std::move(line));
            if (chunk.size() >= kChunkLines) flush();
        }
        flush();
    }

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::NATURAL);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    const auto slash = m_log_path.find_last_of('/');
    frame->setTitle(slash == std::string::npos
        ? m_log_path : m_log_path.substr(slash + 1));
    hide_footer_extras(frame);
    return frame;
}

void LogContentsActivity::onContentAvailable() {
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity();
                return true;
            }, false, false, brls::SOUND_BACK);
    }
}

}  // namespace foyer::browser
