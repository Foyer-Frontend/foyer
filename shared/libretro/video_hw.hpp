#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct retro_hw_render_callback;

namespace foyer::libretro {

// EGL/GLES3 context + framebuffer object that satisfies a libretro
// core's `retro_hw_render_callback`. v1 strategy: render into an
// offscreen FBO, glReadPixels back into a CPU buffer each frame, and
// hand that to the existing software video path so deko3d can blit it
// without needing GL ⟷ deko3d texture interop.
//
// Lifecycle:
//   1. Core calls retro_set_environment(...) → env_cb sees
//      SET_HW_RENDER → HwContext::request(callback).
//   2. Frontend calls retro_load_game(...).
//   3. Frontend calls HwContext::ensure_context(width, height) once
//      av_info is known. EGL display + context + FBO get created;
//      callback->context_reset() fires.
//   4. Each frame: HwContext::begin_frame() / end_frame() wrap the
//      core's retro_run, binding the FBO and reading pixels back.
//   5. On unload: HwContext::shutdown() fires context_destroy() and
//      tears everything down.
struct HwContext {
    static HwContext& instance();

    // Static accessor used by the C trampoline that the core calls
    // for `get_current_framebuffer`. Returns the FBO id.
    static unsigned instance_fbo();

    // Stash the callback the core handed us. Called from env_cb on
    // RETRO_ENVIRONMENT_SET_HW_RENDER. Frontend takes ownership of the
    // get_current_framebuffer + get_proc_address fields.
    bool request(retro_hw_render_callback* cb);

    // Create the EGL context + FBO if not already done, sized to the
    // core's max framebuffer. Idempotent — safe to call from
    // begin_frame() to lazily resize.
    bool ensure_context(unsigned width, unsigned height);

    // Bind the FBO before retro_run() so the core renders into it.
    void begin_frame();

    // After retro_run(): glReadPixels into m_readback and tell the
    // existing video sink to consume it as XRGB8888.
    void end_frame();

    // Tear down the context, fire context_destroy().
    void shutdown();

    // Whether a HW callback was registered. False = software-only core.
    bool active() const { return m_callback != nullptr; }

    // Last framebuffer dimensions reported by the core via get_av_info.
    unsigned width()  const { return m_width;  }
    unsigned height() const { return m_height; }

private:
    HwContext() = default;

    // Opaque to outside callers — keeps the EGL/GL types out of the
    // header so consumers don't have to drag in the EGL/GLES headers.
    void* m_egl_display = nullptr;   // EGLDisplay
    void* m_egl_context = nullptr;   // EGLContext
    void* m_egl_surface = nullptr;   // EGLSurface (pbuffer)
    unsigned m_fbo            = 0;
    unsigned m_color_tex      = 0;
    unsigned m_depth_stencil  = 0;
    unsigned m_width          = 0;
    unsigned m_height         = 0;
    bool     m_context_alive  = false;
    bool     m_context_reset_done = false;

    retro_hw_render_callback* m_callback = nullptr;

    // Pre-allocated CPU readback buffer (RGBA8888 / 4 bytes per pixel).
    std::vector<std::uint8_t> m_readback;
};

} // namespace foyer::libretro
