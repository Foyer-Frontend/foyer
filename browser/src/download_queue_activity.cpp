#include "activity/download_queue_activity.hpp"

#include "install_queue.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <string>
#include <vector>

using namespace brls::literals;

namespace foyer::browser {

DownloadQueueActivity::~DownloadQueueActivity() {
    if (m_poll) {
        m_poll->stop();
        delete m_poll;
        m_poll = nullptr;
    }
}

brls::View* DownloadQueueActivity::createContentView() {
    auto* host = new brls::Box();
    host->setAxis(brls::Axis::COLUMN);
    host->setAlignItems(brls::AlignItems::STRETCH);
    host->setWidth(10000.0f);
    host->setPadding(20.0f, 32.0f, 32.0f, 32.0f);

    {
        auto* hdr = new brls::Header();
        hdr->setTitle("Active");
        host->addView(hdr);
    }

    // Active job row — title shows the tag, detail shows the
    // last status snapshot. ProgressSpinner sits beside it as
    // a live indicator (brls's ProgressBar widget needs a
    // [0,1] value; we'd parse [N/M] off status to drive it
    // properly, that's a follow-up).
    {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setMargins(8.0f, 0.0f, 8.0f, 0.0f);

        m_spinner = new brls::ProgressSpinner();
        m_spinner->setWidth(36.0f);
        m_spinner->setHeight(36.0f);
        m_spinner->setMargins(0.0f, 16.0f, 0.0f, 0.0f);
        row->addView(m_spinner);

        auto* col = new brls::Box();
        col->setAxis(brls::Axis::COLUMN);
        col->setAlignItems(brls::AlignItems::STRETCH);
        col->setGrow(1.0f);

        m_active_label = new brls::Label();
        m_active_label->setText("Idle");
        m_active_label->setFontSize(22.0f);
        col->addView(m_active_label);

        m_active_detail = new brls::Label();
        m_active_detail->setText("");
        m_active_detail->setFontSize(18.0f);
        m_active_detail->setMargins(2.0f, 0.0f, 0.0f, 0.0f);
        col->addView(m_active_detail);

        row->addView(col);
        host->addView(row);
    }

    {
        auto* hdr = new brls::Header();
        hdr->setTitle("Pending");
        host->addView(hdr);
    }

    m_list = new brls::Box();
    m_list->setAxis(brls::Axis::COLUMN);
    m_list->setAlignItems(brls::AlignItems::STRETCH);
    host->addView(m_list);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setAxis(brls::Axis::COLUMN);
    scroll->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);
    scroll->setGrow(1.0f);
    scroll->setContentView(host);

    auto* frame = new brls::AppletFrame(scroll);
    frame->setTitle("Downloads");
    if (auto* footer = frame->getFooter()) {
        for (const char* id : {"brls/hints/time",
                               "brls/battery",
                               "brls/wireless"}) {
            if (auto* v = footer->getView(id)) {
                v->setVisibility(brls::Visibility::GONE);
            }
        }
    }
    return frame;
}

void DownloadQueueActivity::onContentAvailable() {
    if (auto* cv = this->getContentView()) {
        cv->registerAction(
            "Back", brls::BUTTON_B,
            [](brls::View*) {
                brls::Application::popActivity(brls::TransitionAnimation::NONE);
                return true;
            }, false, false, brls::SOUND_BACK);
    }

    refresh();

    if (!m_poll) {
        m_poll = new brls::RepeatingTask(500);
        m_poll->setCallback([this]() { this->refresh(); });
        m_poll->start();
    }
}

void DownloadQueueActivity::refresh() {
    const auto s = install_queue::snapshot();

    if (m_active_label)
        m_active_label->setText(s.active_tag.empty() ? "Idle" : s.active_tag);
    if (m_active_detail)
        m_active_detail->setText(s.last_status);
    if (m_spinner) {
        m_spinner->setVisibility(s.active_tag.empty()
            ? brls::Visibility::INVISIBLE
            : brls::Visibility::VISIBLE);
    }

    if (m_list) {
        m_list->clearViews();
        if (s.pending_tags.empty()) {
            auto* none = new brls::DetailCell();
            none->title->setText("Nothing queued");
            none->detail->setText("");
            m_list->addView(none);
        } else {
            std::size_t idx = 1;
            for (const auto& tag : s.pending_tags) {
                auto* cell = new brls::DetailCell();
                cell->title->setText(tag);
                cell->detail->setText("#" + std::to_string(idx++));
                m_list->addView(cell);
            }
        }
    }
}

}  // namespace foyer::browser
