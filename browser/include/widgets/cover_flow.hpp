#pragma once

#include <borealis.hpp>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace foyer::browser {

// Horizontal cover-flow that scales to multi-thousand-game
// libraries. ONE brls view for the whole strip — yoga sees a single
// node, draw() walks only the on-screen window, and covers decode
// lazily into a small LRU of nvg textures. Replaces the old
// one-GameTile-per-game Box, whose per-game view cost (layout O(N)
// every frame, 3-5 stat() probes + a splash decode per tile at
// construction) made big libraries crawl.
//
// Focus model: the view itself is the single focusable; LEFT/RIGHT
// move an internal index (with wraparound) and a smooth scroll
// animation follows. That also retires the per-tile giveFocus
// choreography that caused the 0.7.12-0.7.14 focus-vtable crashes.
class CoverFlowView : public brls::Box {
public:
    struct Entry {
        std::string system;   // origin system folder (virtual lists differ per row)
        std::string path;     // absolute rom path
        std::string stem;     // rom filename stem
        std::string cover;    // resolved cover path; may start empty
        std::string splash;   // per-system splash path (placeholder art)
        float tile_w = 0.0f;  // per-entry tile size — virtual systems
        float tile_h = 0.0f;  // mix portrait/landscape box dims

        // Internal state — managed by the view.
        bool  cover_resolved = false;  // probes done?
        int   tex            = 0;      // nvg image handle (0 = none yet)
        bool  decode_queued  = false;  // async read in flight
        float nat_w          = 0.0f;   // texture natural size
        float nat_h          = 0.0f;
    };

    CoverFlowView(float max_tile_h);
    ~CoverFlowView() override;

    // Replace the data set. Keeps the view alive (no teardown /
    // re-add), so resume paths just call this again. `initial`
    // clamps into range.
    void setEntries(std::vector<Entry> entries, int initial = 0);

    int  index() const { return m_focus; }
    void setIndex(int idx, bool animate = false);
    int  count() const { return (int)m_entries.size(); }
    const Entry* entryAt(int idx) const;

    // Callbacks into the host activity.
    std::function<void(int)> onFocusChangedCb;  // index follows L/R nav
    std::function<void(int)> onOpenCb;          // A — open details
    std::function<void(int)> onLaunchCb;        // Y — quick launch

    // brls::View
    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;

private:
    void move(int delta);
    void resolve_cover(Entry& e);
    void queue_decode(int idx);
    void evict_outside(int lo, int hi);
    void notify_focus();
    int  splash_tex_for(NVGcontext* vg, const std::string& path,
                        float* out_w, float* out_h);

    std::vector<Entry> m_entries;
    // Prefix sums of (tile_w + gap) so variable-width strips (the
    // virtual Recent / Favorites rows mix systems with different
    // box dims) get O(1) x-position lookup.
    std::vector<float> m_offsets;
    float m_total_w  = 0.0f;
    float m_max_tile_h;
    float m_gap = 14.0f;

    int   m_focus  = 0;
    float m_scroll = 0.0f;   // animated, in px of strip offset

    // Per-system splash textures, decoded once + shared by every
    // unloaded tile of that system. Key = splash path.
    struct SplashTex { int tex = 0; float w = 0.0f, h = 0.0f; };
    std::map<std::string, SplashTex> m_splashes;

    // Decode throttle — at most this many texture uploads per frame
    // so a fast scroll doesn't stack decode hitches.
    int m_decodes_this_frame = 0;

    // Generation counter — bumped by setEntries so async reads that
    // land after a data swap are dropped instead of writing into a
    // recycled slot.
    std::shared_ptr<int> m_generation = std::make_shared<int>(0);
};

}  // namespace foyer::browser
