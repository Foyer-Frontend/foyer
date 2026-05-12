#pragma once

#include <borealis.hpp>

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
};

}  // namespace foyer::browser
