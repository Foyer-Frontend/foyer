#pragma once
//
// player/imgui/gl_context — libnx EGL window surface + GLES3 context
// for the ImGui render shell. One context per process, bound to
// nwindowGetDefault().
//
// The probe ladder mirrors shared/libretro/video_hw.cpp: ask for an
// EGL_WINDOW_BIT first, then fall back to permissive configs if mesa
// doesn't advertise a window-capable one with the exact channel sizes
// we wanted. RGBA8 + no depth/stencil is enough for ImGui + the
// fullscreen game quad.
//
// Phase ImGui-1 boot only stands up the surface + a black-screen
// clear. Phase ImGui-2 hands the same display/context to
// HwContext::attach_external so HW-render cores share it.

#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace foyer::player::imgui_shell {

struct GlContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int        fb_w    = 0;
    int        fb_h    = 0;
    bool valid() const { return context != EGL_NO_CONTEXT; }
};

// Bring up EGL + GLES3 against nwindowGetDefault(). Returns a valid
// context on success, leaves the struct unchanged on failure.
bool gl_context_init(GlContext& out);

// Tear down in the inverse order: release current, destroy context,
// destroy surface, terminate display. Safe to call on a partially-
// initialised struct.
void gl_context_shutdown(GlContext& ctx);

// Present the back buffer. Cheap wrapper so callers don't have to
// pull <EGL/egl.h>.
void gl_context_swap(GlContext& ctx);

// Per-frame poll. Detects HOS dock/undock transitions and resizes
// the NWindow + fb_w/fb_h accordingly so the swapchain follows the
// live operation mode without a player restart. Returns true if the
// dimensions changed (caller may want to glViewport again).
bool gl_context_tick(GlContext& ctx);

}  // namespace foyer::player::imgui_shell
