#pragma once

#include <borealis.hpp>

#include <string>

namespace foyer::browser {

// Two activities split by job:
//   LogListActivity     — lists every .log file under
//                         /foyer/data/logs, newest first. Tapping
//                         a row pushes LogContentsActivity for
//                         that file.
//   LogContentsActivity — shows a single log's contents in a
//                         ScrollingFrame, paging via ~80-line
//                         Label chunks so brls's layout doesn't
//                         stall on a single multi-MB Label. Caps
//                         visible bytes to the last 4 MB; older
//                         entries are dropped with a [truncated]
//                         banner because pre-crash entries are
//                         what matter.
// B pops in both cases. The combined flow is List → Contents → B → List → B → Settings.

class LogListActivity : public brls::Activity {
public:
    LogListActivity() = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;
};

class LogContentsActivity : public brls::Activity {
public:
    explicit LogContentsActivity(std::string log_path);

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    std::string m_log_path;
};

}  // namespace foyer::browser
