#include "widgets/cover_flow.hpp"

#include "library/per_game.hpp"
#include "platform/log.hpp"
#include "scrapers/cache.hpp"

#include <nanovg.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace foyer::browser {

namespace {

// Tiles to keep textured either side of the focus. ~25 covers at
// portrait size ≈ 33 MB of GPU memory — bounded no matter how big
// the library is.
constexpr int kKeep = 12;

// Texture uploads per frame. A decode+upload is ~25-40 ms for a big
// cover; capping at one per frame keeps the scroll animation fluid
// while a fast flick streams covers in progressively.
constexpr int kMaxDecodesPerFrame = 1;

// Scroll animation: exponential approach. ~120 ms settle at 60 fps.
constexpr float kScrollLerp = 0.28f;

}  // namespace

CoverFlowView::CoverFlowView(float max_tile_h)
    : m_max_tile_h(max_tile_h) {
    this->setFocusable(true);
    // The strip draws its own focused-tile ring — brls's whole-view
    // highlight would wrap the entire carousel.
    this->setHideHighlightBackground(true);
    this->setHideHighlightBorder(true);

    this->registerAction(
        "Prev", brls::BUTTON_LEFT,
        [this](brls::View*) { move(-1); return true; },
        /*hidden=*/true, /*allowRepeating=*/true, brls::SOUND_FOCUS_CHANGE);
    this->registerAction(
        "Next", brls::BUTTON_RIGHT,
        [this](brls::View*) { move(+1); return true; },
        /*hidden=*/true, /*allowRepeating=*/true, brls::SOUND_FOCUS_CHANGE);
    this->registerAction(
        "Open details", brls::BUTTON_A,
        [this](brls::View*) {
            if (onOpenCb && !m_entries.empty()) onOpenCb(m_focus);
            return true;
        }, false, false, brls::SOUND_CLICK);
    this->registerAction(
        "Quick launch", brls::BUTTON_Y,
        [this](brls::View*) {
            if (onLaunchCb && !m_entries.empty()) onLaunchCb(m_focus);
            return true;
        }, false, false, brls::SOUND_CLICK);
    this->registerAction(
        "Favourite", brls::BUTTON_X,
        [this](brls::View*) {
            if (m_entries.empty()) return false;
            const auto& e = m_entries[(std::size_t)m_focus];
            const bool was = ::foyer::library::per_game_favorite(e.path);
            ::foyer::library::set_per_game_favorite(e.path, !was);
            brls::Application::notify(was
                ? "Removed from favourites"
                : "Added to favourites");
            return true;
        }, false, false, brls::SOUND_CLICK);

    this->addGestureRecognizer(new brls::TapGestureRecognizer(
        [this](brls::TapGestureStatus status, brls::Sound* sound) {
            (void)sound;
            if (status.state != brls::GestureState::END) return;
            if (m_entries.empty()) return;
            // Tap left / right third = nav; centre tap = open.
            const float local = status.position.x - this->getX();
            const float w     = this->getWidth();
            if (local < w / 3.0f)            move(-1);
            else if (local > w * 2.0f / 3.0f) move(+1);
            else if (onOpenCb)               onOpenCb(m_focus);
        }));

    this->setHeight(m_max_tile_h + 18.0f);
}

CoverFlowView::~CoverFlowView() {
    auto* vg = brls::Application::getNVGContext();
    if (!vg) return;
    for (auto& e : m_entries) {
        if (e.tex) nvgDeleteImage(vg, e.tex);
    }
    for (auto& [_, s] : m_splashes) {
        if (s.tex) nvgDeleteImage(vg, s.tex);
    }
}

void CoverFlowView::setEntries(std::vector<Entry> entries, int initial) {
    auto* vg = brls::Application::getNVGContext();
    for (auto& e : m_entries) {
        if (e.tex && vg) nvgDeleteImage(vg, e.tex);
    }
    (*m_generation)++;
    m_entries = std::move(entries);

    // Prefix offsets — x position of tile i relative to strip start.
    m_offsets.resize(m_entries.size());
    float acc = 0.0f;
    for (std::size_t i = 0; i < m_entries.size(); i++) {
        m_offsets[i] = acc;
        acc += m_entries[i].tile_w + m_gap;
    }
    m_total_w = acc;

    if (m_entries.empty()) {
        m_focus  = 0;
        m_scroll = 0.0f;
        return;
    }
    m_focus = std::clamp(initial, 0, (int)m_entries.size() - 1);
    m_scroll = m_offsets[(std::size_t)m_focus];
    notify_focus();
}

const CoverFlowView::Entry* CoverFlowView::entryAt(int idx) const {
    if (idx < 0 || idx >= (int)m_entries.size()) return nullptr;
    return &m_entries[(std::size_t)idx];
}

void CoverFlowView::setIndex(int idx, bool animate) {
    if (m_entries.empty()) return;
    m_focus = std::clamp(idx, 0, (int)m_entries.size() - 1);
    if (!animate) m_scroll = m_offsets[(std::size_t)m_focus];
    notify_focus();
}

void CoverFlowView::move(int delta) {
    if (m_entries.empty()) return;
    const int n = (int)m_entries.size();
    const int next = ((m_focus + delta) % n + n) % n;
    // Wraparound: snap the scroll so last -> first doesn't animate
    // across the whole strip.
    if ((delta > 0 && next < m_focus) || (delta < 0 && next > m_focus)) {
        m_scroll = m_offsets[(std::size_t)next];
    }
    m_focus = next;
    notify_focus();
}

void CoverFlowView::notify_focus() {
    if (onFocusChangedCb) onFocusChangedCb(m_focus);
}

void CoverFlowView::onFocusGained() {
    brls::Box::onFocusGained();
    notify_focus();
}

void CoverFlowView::resolve_cover(Entry& e) {
    if (e.cover_resolved) return;
    e.cover_resolved = true;
    namespace fs = std::filesystem;
    // Probe order mirrors the old GameTile: pre-resolved box_art →
    // per-game SS bundle box-2D* → legacy covers path. Runs only for
    // tiles entering the visible window, so a 5000-game library pays
    // for ~25 of these per session, not 5000 at activity push.
    if (!e.cover.empty() && fs::exists(fs::path(e.cover))) return;
    const auto bundle_dir = scrapers::game_asset_dir(e.system, e.stem);
    if (auto p = scrapers::find_in_dir(bundle_dir, "box-2D"); !p.empty()) {
        e.cover = std::move(p);
        return;
    }
    const auto canon = scrapers::cover_path(e.system, e.stem);
    if (fs::exists(fs::path(canon))) {
        e.cover = canon;
        return;
    }
    e.cover.clear();
}

void CoverFlowView::queue_decode(int idx) {
    auto& e = m_entries[(std::size_t)idx];
    if (e.tex || e.decode_queued || e.cover.empty()) return;
    if (m_decodes_this_frame >= kMaxDecodesPerFrame) return;
    m_decodes_this_frame++;
    e.decode_queued = true;

    const std::string path = e.cover;
    auto gen = m_generation;
    const int my_gen = *gen;
    auto* self = this;
    brls::async([self, gen, my_gen, idx, path]() {
        // Worker thread: file read only. Decode + GPU upload happen
        // on the UI thread (nvg context affinity).
        auto buf = std::make_shared<std::string>();
        {
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f) {
                const auto size = (std::size_t)f.tellg();
                f.seekg(0);
                buf->resize(size);
                f.read(buf->data(), (std::streamsize)size);
            }
        }
        brls::sync([self, gen, my_gen, idx, buf]() {
            if (*gen != my_gen) return;  // data set swapped — drop
            if (idx >= (int)self->m_entries.size()) return;
            auto& e = self->m_entries[(std::size_t)idx];
            e.decode_queued = false;
            if (buf->empty()) { e.cover.clear(); return; }
            auto* vg = brls::Application::getNVGContext();
            if (!vg) return;
            const int tex = nvgCreateImageMem(vg, 0,
                (unsigned char*)buf->data(), (int)buf->size());
            if (tex == 0) { e.cover.clear(); return; }
            int w = 0, h = 0;
            nvgImageSize(vg, tex, &w, &h);
            e.tex   = tex;
            e.nat_w = (float)w;
            e.nat_h = (float)h;
        });
    });
}

void CoverFlowView::evict_outside(int lo, int hi) {
    auto* vg = brls::Application::getNVGContext();
    if (!vg) return;
    for (int i = 0; i < (int)m_entries.size(); i++) {
        if (i >= lo && i <= hi) continue;
        auto& e = m_entries[(std::size_t)i];
        if (e.tex) {
            nvgDeleteImage(vg, e.tex);
            e.tex   = 0;
            e.nat_w = 0.0f;
            e.nat_h = 0.0f;
        }
    }
}

int CoverFlowView::splash_tex_for(NVGcontext* vg, const std::string& path,
                                  float* out_w, float* out_h) {
    if (path.empty()) return 0;
    auto it = m_splashes.find(path);
    if (it == m_splashes.end()) {
        SplashTex s;
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (f) {
            const auto size = (std::size_t)f.tellg();
            f.seekg(0);
            std::string buf(size, '\0');
            f.read(buf.data(), (std::streamsize)size);
            s.tex = nvgCreateImageMem(vg, 0,
                (unsigned char*)buf.data(), (int)buf.size());
            if (s.tex) {
                int w = 0, h = 0;
                nvgImageSize(vg, s.tex, &w, &h);
                s.w = (float)w;
                s.h = (float)h;
            }
        }
        it = m_splashes.emplace(path, s).first;
    }
    *out_w = it->second.w;
    *out_h = it->second.h;
    return it->second.tex;
}

void CoverFlowView::draw(NVGcontext* vg, float x, float y, float width,
                         float height, brls::Style style,
                         brls::FrameContext* ctx) {
    (void)style; (void)ctx;
    if (m_entries.empty()) return;
    m_decodes_this_frame = 0;

    // Animate scroll toward the focused tile's offset.
    const float target = m_offsets[(std::size_t)m_focus];
    m_scroll += (target - m_scroll) * kScrollLerp;
    if (std::fabs(target - m_scroll) < 0.5f) m_scroll = target;

    const int   n        = (int)m_entries.size();
    const float center_x = x + width * 0.5f;
    const auto& fe       = m_entries[(std::size_t)m_focus];
    // Strip-space x of the screen-left edge: the focused tile's
    // centre sits at the view centre.
    const float strip_left =
        m_scroll + fe.tile_w * 0.5f - width * 0.5f;

    // Visible range — binary-search the prefix offsets.
    const auto lo_it = std::upper_bound(
        m_offsets.begin(), m_offsets.end(), strip_left);
    int lo = (int)std::distance(m_offsets.begin(), lo_it) - 1;
    lo = std::max(0, lo - 1);
    int hi = lo;
    while (hi + 1 < n && m_offsets[(std::size_t)hi] < strip_left + width)
        hi++;

    evict_outside(std::max(0, m_focus - kKeep),
                  std::min(n - 1, m_focus + kKeep));

    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);

    for (int i = lo; i <= hi; i++) {
        auto& e = m_entries[(std::size_t)i];
        resolve_cover(e);
        queue_decode(i);

        const float tile_x = center_x
            + (m_offsets[(std::size_t)i] - m_scroll)
            - fe.tile_w * 0.5f;
        // Bottom-align tiles of differing heights (virtual rows).
        const float tile_y = y + 9.0f + (m_max_tile_h - e.tile_h);

        // Tile background — same slate as the old GameTile.
        nvgBeginPath(vg);
        nvgRoundedRect(vg, tile_x, tile_y, e.tile_w, e.tile_h, 6.0f);
        nvgFillColor(vg, nvgRGB(40, 50, 70));
        nvgFill(vg);

        // Cover (or per-system splash) — bottom-anchored fit.
        int   tex = e.tex;
        float nw  = e.nat_w;
        float nh  = e.nat_h;
        if (!tex) tex = splash_tex_for(vg, e.splash, &nw, &nh);
        if (tex && nw > 0.0f && nh > 0.0f) {
            float w = e.tile_w;
            float h = w * (nh / nw);
            if (h > e.tile_h) {
                h = e.tile_h;
                w = h * (nw / nh);
            }
            const float ix = tile_x + (e.tile_w - w) * 0.5f;
            const float iy = tile_y + (e.tile_h - h);  // bottom anchor
            const NVGpaint paint =
                nvgImagePattern(vg, ix, iy, w, h, 0.0f, tex, 1.0f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, ix, iy, w, h, 4.0f);
            nvgFillPaint(vg, paint);
            nvgFill(vg);
        }

        // Focus ring — only while the strip itself holds brls focus
        // so it doesn't linger while the user is up in the action
        // row.
        if (i == m_focus && this->isFocused()) {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, tile_x - 3.0f, tile_y - 3.0f,
                           e.tile_w + 6.0f, e.tile_h + 6.0f, 8.0f);
            nvgStrokeColor(vg, nvgRGB(0x3B, 0x82, 0xF6));
            nvgStrokeWidth(vg, 3.0f);
            nvgStroke(vg);
        }
    }

    nvgRestore(vg);
}

}  // namespace foyer::browser
