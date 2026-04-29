#pragma once

#include "frontend.hpp"
#include "aspect.hpp"

struct NVGcontext;

namespace foyer::libretro {

// Forwards core video frames into a nanovg image which the player draws
// each frame. Owns the conversion from libretro pixel formats to RGBA8 and
// the lifetime of the underlying texture.
struct VideoSinkImpl {
    static VideoSinkImpl& instance();

    void init(NVGcontext* vg);
    void shutdown();

    // Hand to Frontend::set_video_sink(); the actual function below trampolines
    // to the singleton.
    static void on_frame(const Frontend::VideoFrame& f);

    // Draw the most recent frame. Aspect-fit into the given screen rect; the
    // remaining area stays untouched (caller should clear first).
    void draw(NVGcontext* vg, float screen_w, float screen_h);

    bool has_frame() const { return m_image > 0; }

    void set_aspect(AspectMode m) { m_aspect_mode = m; }
    AspectMode aspect() const { return m_aspect_mode; }

private:
    void  upload(const Frontend::VideoFrame& f);

    NVGcontext* m_vg     = nullptr;
    int         m_image  = 0;
    int         m_w      = 0;
    int         m_h      = 0;
    float       m_par    = 1.0f;     // pixel aspect ratio reported by core
    AspectMode  m_aspect_mode = AspectMode::DisplayCore;
    // Reusable RGBA8 staging buffer; resized when geometry grows.
    void*       m_rgba_buf  = nullptr;
    std::size_t m_rgba_cap  = 0;
};

} // namespace foyer::libretro
