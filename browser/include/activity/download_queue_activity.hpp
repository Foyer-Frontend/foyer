#pragma once

#include <borealis.hpp>
#include <borealis/core/animation.hpp>

#include <string>

namespace foyer::browser {

// Overlay listing the install_queue state. Active job at the top
// with a progress bar fed off the worker's "[N/M] foo" status
// snapshot; pending tags listed below in FIFO order. Pushed
// from Settings (X / Y on any tab) so the user can see what's
// downloading without leaving Settings.
class DownloadQueueActivity : public brls::Activity {
public:
    DownloadQueueActivity() = default;
    ~DownloadQueueActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void refresh();

    brls::RepeatingTask* m_poll = nullptr;
    brls::Box*           m_list = nullptr;
    brls::Label*         m_active_label = nullptr;
    brls::Label*         m_active_detail = nullptr;
    brls::ProgressSpinner* m_spinner = nullptr;

    // Real byte-progress bar — fed off foyer::net::current_download
    // (the streaming get_to_file path publishes now/total atomics
    // every libcurl xferinfo tick). The track Box is laid out in
    // createContentView(); the fill width is tweened from refresh().
    brls::Box*        m_bar_fill = nullptr;
    brls::Label*      m_bar_caption = nullptr;
    brls::Animatable  m_bar_pct{0.0f};
    float             m_bar_target = 0.0f;
    static constexpr float kBarTrackWidth = 480.0f;

    // Hash of the last rendered pending list. Tick fires twice a
    // second; rebuilding the views every tick previously deleted
    // the focused DetailCell and crashed brls's drawHighlight on
    // the next frame. With Label rows + sig dedup, the list only
    // mutates when the actual queue changes.
    std::string m_pending_sig;
};

}  // namespace foyer::browser
