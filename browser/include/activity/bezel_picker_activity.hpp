#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

namespace foyer::browser {

// Bezel picker — full-screen activity with a large centered preview
// of the currently-highlighted bezel + L/R to cycle and A to
// confirm. Replaces the brls::SelectorCell wheel picker for
// per-system default bezels, where the wheel format gave no visual
// affordance for what each option actually looked like.
//
// Pushed by PerSystemActivity when the user taps the "Default bezel"
// row. Confirms by calling on_confirm(picked_name) — empty string
// means "(none)" — and popping. B cancels (no write, pops).
class BezelPickerActivity : public brls::Activity {
public:
    using ConfirmFn = std::function<void(const std::string& picked)>;

    BezelPickerActivity(std::vector<std::string> names,
                        std::string initial_pick,
                        ConfirmFn on_confirm);
    ~BezelPickerActivity() override = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void show_index(int idx);

    // names_[0] is the "(none)" sentinel; names_[1..] are the
    // actual bezel basenames (no .png, no folder).
    std::vector<std::string> m_names;
    int                      m_idx = 0;
    ConfirmFn                m_on_confirm;

    brls::Image* m_preview     = nullptr;
    brls::Label* m_name_label  = nullptr;
    brls::Label* m_index_label = nullptr;
};

}  // namespace foyer::browser
