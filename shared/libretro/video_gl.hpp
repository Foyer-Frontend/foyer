#pragma once
//
// shared/libretro/video_gl — GLES3 sibling of video.hpp's VideoSinkImpl.
// Same job (receive libretro frames + drive aspect-fit blit) but with
// the texture + draw call living on the player's own GL context, not
// on a nanovg image. Used by the ImGui player shell; brls path keeps
// video.cpp.
//
// Phase 3 will plumb ShaderPipeline::process_texture between upload
// and draw — for Phase 2 the active shader (if any) is ignored on the
// GL path.

#include "frontend.hpp"
#include "aspect.hpp"

#include <cstdint>

namespace foyer::libretro {

struct VideoSinkGl {
    static VideoSinkGl& instance();

    // Compile shaders + allocate VAO. Idempotent. Must be called once
    // an EGL context is current.
    bool init();

    // Release textures + shader program. Safe on uninitialised state.
    void shutdown();

    // Hand to Frontend::set_video_sink(); trampolines to instance().
    static void on_frame(const Frontend::VideoFrame& f);

    // Draw the most recent frame aspect-fit into [0, screen_w] ×
    // [0, screen_h] (top-left origin, GL convention). Caller is
    // expected to glClear before this if a black background is
    // wanted.
    void draw(float screen_w, float screen_h);

    bool has_frame() const { return m_tex != 0; }
    unsigned texture()       const { return m_tex; }
    unsigned width()         const { return m_w;   }
    unsigned height()        const { return m_h;   }

    void set_aspect(AspectMode m) { m_aspect_mode = m; }
    AspectMode aspect() const { return m_aspect_mode; }

private:
    void upload(const Frontend::VideoFrame& f);

    unsigned    m_program = 0;
    int         m_loc_pos   = -1;     // u_pos    — NDC top-left
    int         m_loc_scale = -1;     // u_scale  — NDC size
    int         m_loc_tex   = -1;     // u_tex    — sampler

    unsigned    m_vao = 0;
    unsigned    m_tex = 0;
    unsigned    m_w   = 0;
    unsigned    m_h   = 0;
    float       m_par = 1.0f;
    AspectMode  m_aspect_mode = AspectMode::DisplayCore;

    // RGBA8 staging buffer; reused across frames, resized as
    // geometry grows.
    void*       m_rgba_buf = nullptr;
    std::size_t m_rgba_cap = 0;
};

}  // namespace foyer::libretro
