#pragma once

#include "library/per_game_bezel.hpp"

#include <borealis.hpp>

#include <functional>
#include <string>
#include <vector>

namespace foyer::browser {

// Per-game online-bezel browser. Probes The Bezel Project + estefan3112
// for the rom on construction (writes any hits to
// /foyer/data/cache/bezel-preview/<sys>/<stem>/<source>.png) and shows
// a full-screen preview of each hit. ←/→ cycles between sources,
// A copies the highlighted preview into the bundle dir under a
// source-tagged name (bezel-<source>.png), B cancels.
//
// Pushed by PerGameActivity when the user taps the "Browse online
// bezels" cell. Calls `on_commit(source_slug)` after a successful
// commit so the caller can refresh any related UI.
class BezelSourceBrowserActivity : public brls::Activity {
public:
    using CommitFn = std::function<void(const std::string& source_slug)>;

    BezelSourceBrowserActivity(std::string system_folder,
                               std::string rom_stem,
                               CommitFn on_commit);
    ~BezelSourceBrowserActivity() override;

    brls::View* createContentView() override;
    void onContentAvailable() override;

private:
    void kick_search();
    void on_search_done(std::vector<::foyer::library::PerGameBezelPreview> hits);
    void show_index(int idx);

    std::string m_system_folder;
    std::string m_rom_stem;
    CommitFn    m_on_commit;

    std::vector<::foyer::library::PerGameBezelPreview> m_hits;
    int                                                m_idx = 0;
    bool                                               m_searching = true;

    brls::Image* m_preview     = nullptr;
    brls::Label* m_status      = nullptr;
    brls::Label* m_source_lbl  = nullptr;
    brls::Label* m_index_lbl   = nullptr;
    brls::Label* m_hint_lbl    = nullptr;
};

}  // namespace foyer::browser
