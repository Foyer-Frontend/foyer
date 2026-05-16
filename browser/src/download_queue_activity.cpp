#include "activity/download_queue_activity.hpp"

#include "install_queue.hpp"
#include "net/http.hpp"

#include <borealis/views/applet_frame.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/header.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <borealis/views/scrolling_frame.hpp>

#include <algorithm>
#include <cstdio>
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

        // Real byte-progress bar — track + fill, same theme keys as
        // the splash bar so light/dark themes both pick up the
        // accent fill. Fed off foyer::net::current_download in
        // refresh(); m_bar_pct tweens the width.
        auto* track = new brls::Box();
        track->setWidth(kBarTrackWidth);
        track->setHeight(10.0f);
        track->setCornerRadius(5.0f);
        track->setBackgroundColor(
            brls::Application::getTheme().getColor("foyer/splash_bar_track"));
        track->setAxis(brls::Axis::ROW);
        track->setMargins(8.0f, 0.0f, 4.0f, 0.0f);

        m_bar_fill = new brls::Box();
        m_bar_fill->setWidth(0.0f);
        m_bar_fill->setHeight(10.0f);
        m_bar_fill->setCornerRadius(5.0f);
        m_bar_fill->setBackgroundColor(
            brls::Application::getTheme().getColor("foyer/splash_bar_fill"));
        track->addView(m_bar_fill);
        col->addView(track);

        m_bar_caption = new brls::Label();
        m_bar_caption->setText("");
        m_bar_caption->setFontSize(16.0f);
        m_bar_caption->setTextColor(
            brls::Application::getTheme().getColor("brls/text_disabled"));
        col->addView(m_bar_caption);

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
        cv->registerAction(
            "Cancel", brls::BUTTON_Y,
            [](brls::View*) {
                ::foyer::browser::install_queue::cancel_current();
                brls::Application::notify("Cancelled active download");
                return true;
            }, false, false, brls::SOUND_CLICK);
        cv->registerAction(
            "Cancel all", brls::BUTTON_X,
            [](brls::View*) {
                ::foyer::browser::install_queue::stop();
                brls::Application::notify("Cancelled all downloads");
                return true;
            }, false, false, brls::SOUND_CLICK);
    }

    refresh();

    if (!m_poll) {
        // Custom RepeatingTask subclass — brls's RepeatingTask
        // hides setCallback (private inheritance from
        // RepeatingTimer); override run() instead.
        class Tick : public brls::RepeatingTask {
        public:
            Tick(DownloadQueueActivity* host)
                : brls::RepeatingTask(500), host(host) {}
            void run() override { host->refresh(); }
        private:
            DownloadQueueActivity* host;
        };
        m_poll = new Tick(this);
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

    // Byte progress bar — read net layer's live counters.
    {
        auto& dl = ::foyer::net::current_download();
        const bool active = dl.active.load(std::memory_order_acquire);
        const std::uint64_t now   = dl.now.load(std::memory_order_acquire);
        const std::uint64_t total = dl.total.load(std::memory_order_acquire);
        const float target = (active && total > 0)
            ? std::min(1.0f, static_cast<float>(double(now) / double(total)))
            : 0.0f;
        if (!active) {
            // Reset the animation snapshot the moment net goes idle
            // so the next download starts the bar back at 0.
            m_bar_target = 0.0f;
            m_bar_pct.stop();
            m_bar_pct.reset(0.0f);
        } else if (target > m_bar_target + 0.005f) {
            m_bar_target = target;
            m_bar_pct.reset();
            m_bar_pct.addStep(target, 200,
                              brls::EasingFunction::quadraticOut);
            m_bar_pct.start();
        }
        if (m_bar_fill) {
            m_bar_fill->setWidth(kBarTrackWidth
                * static_cast<float>(m_bar_pct));
        }
        if (m_bar_caption) {
            if (active && total > 0) {
                char buf[64];
                std::snprintf(buf, sizeof(buf),
                    "%.1f / %.1f MB   (%.0f%%)",
                    double(now) / 1048576.0,
                    double(total) / 1048576.0,
                    double(static_cast<float>(m_bar_pct)) * 100.0);
                m_bar_caption->setText(buf);
            } else if (active) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "%.1f MB downloaded",
                    double(now) / 1048576.0);
                m_bar_caption->setText(buf);
            } else {
                m_bar_caption->setText("");
            }
        }
    }

    if (!m_list) return;

    // Build a signature of the current pending state; skip mutation
    // entirely when nothing changed. Tick fires twice a second, the
    // list shifts only on enqueue/dequeue — most ticks are no-ops.
    std::string sig;
    sig.reserve(s.pending_tags.size() * 16);
    for (const auto& t : s.pending_tags) { sig += t; sig.push_back('\x1f'); }
    if (sig == m_pending_sig) return;
    m_pending_sig = std::move(sig);

    // Previous implementation used brls::DetailCell here — DetailCell
    // is focusable, so brls's focus could land on a pending row, and
    // the next tick's clearViews() would delete the focused view
    // while brls still held it as the current-focus pointer. The
    // next Application::frame() then crashed inside drawHighlight()
    // dereferencing the freed cell's vtable (Atmosphère reported it
    // as an Instruction Abort jumping to garbage). Plain Label
    // widgets are never focusable, so clearing them is safe.
    m_list->clearViews();
    if (s.pending_tags.empty()) {
        auto* none = new brls::Label();
        none->setText("Nothing queued");
        none->setFontSize(20.0f);
        m_list->addView(none);
        return;
    }
    std::size_t idx = 1;
    for (const auto& tag : s.pending_tags) {
        auto* row = new brls::Label();
        row->setText("#" + std::to_string(idx++) + "   " + tag);
        row->setFontSize(20.0f);
        m_list->addView(row);
    }
}

}  // namespace foyer::browser
