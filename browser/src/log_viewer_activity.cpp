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

constexpr const char* kLogDir = "/foyer/data/logs";
constexpr std::size_t kChunkLines = 80;
constexpr std::size_t kMaxBytes   = 4 * 1024 * 1024;

std::vector<std::string> list_logs() {
    std::vector<std::string> names;
    DIR* d = ::opendir(kLogDir);
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

    const auto names = list_logs();
    if (names.empty()) {
        auto* none = new brls::Label();
        none->setText("No log files yet.");
        none->setFontSize(20.0f);
        host->addView(none);
    } else {
        for (const auto& nm : names) {
            const std::string full = std::string{kLogDir} + "/" + nm;
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
    }

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
            auto* lbl = new brls::Label();
            lbl->setText(ss.str());
            lbl->setFontSize(18.0f);
            host->addView(lbl);
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
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
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
