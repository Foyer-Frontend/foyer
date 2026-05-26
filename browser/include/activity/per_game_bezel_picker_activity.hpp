#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

namespace foyer::browser {

// Per-game bezel picker — full-screen activity that lists every
// bezel the rom could render: an "(auto)" sentinel (fall through
// the normal resolver chain), every PNG already in the rom's
// /foyer/assets/system/<sys>/<stem>/ bundle dir (SS scrapes +
// downloaded BezelProject / estefan packs), plus the per-system
// installed packs at /foyer/content/bezels/ filtered to the
// rom's hardware family.
//
// Confirming via A writes the picked absolute path into
// per_game_bezel_choice — bezel_sdl::resolve_path checks that
// first, so the pick beats every other fallback path.
class PerGameBezelPickerActivity : public brls::Activity {
public:
    using CommitFn = std::function<void(const std::string& abs_path)>;

    PerGameBezelPickerActivity(std::string rom_path,
                               std::string system_folder,
                               std::string rom_stem,
                               std::string current_choice,
                               CommitFn on_commit);
    ~PerGameBezelPickerActivity() override = default;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    struct Entry {
        std::string label;   // shown under preview
        std::string path;    // absolute file path; empty = "(auto)"
    };

    void build_entries();
    void show_index(int idx);

    std::string m_rom_path;
    std::string m_system_folder;
    std::string m_rom_stem;
    std::string m_current_choice;
    CommitFn    m_on_commit;

    std::vector<Entry> m_entries;
    int                m_idx = 0;

    brls::Image* m_preview    = nullptr;
    brls::Label* m_label      = nullptr;
    brls::Label* m_index_lbl  = nullptr;
};

}  // namespace foyer::browser
