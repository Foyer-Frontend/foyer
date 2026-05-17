#include "imgui/gl_context.hpp"
#include "platform/log.hpp"

#include <switch.h>

#include <array>
#include <vector>

namespace foyer::player::imgui_shell {

namespace {

const char* egl_err_str(EGLint e) {
    switch (e) {
        case EGL_SUCCESS:           return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:   return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:        return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:         return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:     return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:        return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:       return "EGL_BAD_CONTEXT";
        case EGL_BAD_DISPLAY:       return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:         return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:     return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:       return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:      return "EGL_CONTEXT_LOST";
        default:                    return "EGL_UNKNOWN";
    }
}

}  // namespace

bool gl_context_init(GlContext& out) {
    out = GlContext{};

    out.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (out.display == EGL_NO_DISPLAY) {
        foyer::log::write("[imgui_gl] eglGetDisplay failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    EGLint maj = 0, min = 0;
    if (!eglInitialize(out.display, &maj, &min)) {
        foyer::log::write("[imgui_gl] eglInitialize failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    foyer::log::write("[imgui_gl] EGL %d.%d on Switch\n", maj, min);

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        foyer::log::write("[imgui_gl] eglBindAPI(GLES) failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }

    // Probe ladder. mesa-on-Switch is picky about which configs it
    // advertises; ask for the most-specific first and relax on each
    // miss. Window-bit is required (we need a real swapchain bound to
    // nwindowGetDefault — pbuffer/surfaceless was the libretro shader
    // workaround, but here we own the framebuffer).
    struct Probe { EGLint renderable; EGLint depth; EGLint stencil; };
    const std::array<Probe, 4> probes = {{
        { EGL_OPENGL_ES3_BIT, 0, 0 },
        { EGL_OPENGL_ES3_BIT, 24, 8 },
        { EGL_OPENGL_ES2_BIT, 0, 0 },
        { EGL_OPENGL_ES2_BIT, 24, 8 },
    }};

    EGLConfig cfg{};
    EGLint    n      = 0;
    EGLint    picked = 0;
    bool      ok     = false;
    for (const auto& p : probes) {
        const std::array<EGLint, 17> attrs{{
            EGL_RENDERABLE_TYPE, p.renderable,
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_ALPHA_SIZE,      8,
            EGL_DEPTH_SIZE,      p.depth,
            EGL_STENCIL_SIZE,    p.stencil,
            EGL_NONE,
        }};
        if (eglChooseConfig(out.display, attrs.data(), &cfg, 1, &n)
                && n >= 1) {
            picked = p.renderable;
            ok     = true;
            foyer::log::write(
                "[imgui_gl] config picked: gles=%s depth=%d stencil=%d\n",
                p.renderable == EGL_OPENGL_ES3_BIT ? "3" : "2",
                (int)p.depth, (int)p.stencil);
            break;
        }
    }
    if (!ok) {
        foyer::log::write("[imgui_gl] eglChooseConfig: no match after %zu probes (%s)\n",
            probes.size(), egl_err_str(eglGetError()));
        return false;
    }

    out.surface = eglCreateWindowSurface(out.display, cfg,
        (EGLNativeWindowType)nwindowGetDefault(), nullptr);
    if (out.surface == EGL_NO_SURFACE) {
        foyer::log::write("[imgui_gl] eglCreateWindowSurface failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }

    const EGLint ctx_ver = (picked == EGL_OPENGL_ES3_BIT) ? 3 : 2;
    const std::array<EGLint, 3> ctx_attrs{{
        EGL_CONTEXT_CLIENT_VERSION, ctx_ver,
        EGL_NONE,
    }};
    out.context = eglCreateContext(out.display, cfg, EGL_NO_CONTEXT,
        ctx_attrs.data());
    if (out.context == EGL_NO_CONTEXT) {
        foyer::log::write(
            "[imgui_gl] eglCreateContext(client_ver=%d) failed: %s\n",
            (int)ctx_ver, egl_err_str(eglGetError()));
        eglDestroySurface(out.display, out.surface);
        out.surface = EGL_NO_SURFACE;
        return false;
    }

    if (!eglMakeCurrent(out.display, out.surface, out.surface, out.context)) {
        foyer::log::write("[imgui_gl] eglMakeCurrent failed: %s\n",
            egl_err_str(eglGetError()));
        eglDestroyContext(out.display, out.context);
        eglDestroySurface(out.display, out.surface);
        out.context = EGL_NO_CONTEXT;
        out.surface = EGL_NO_SURFACE;
        return false;
    }

    eglQuerySurface(out.display, out.surface, EGL_WIDTH,  &out.fb_w);
    eglQuerySurface(out.display, out.surface, EGL_HEIGHT, &out.fb_h);
    foyer::log::write("[imgui_gl] surface ready %dx%d\n", out.fb_w, out.fb_h);
    return true;
}

void gl_context_shutdown(GlContext& ctx) {
    if (ctx.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(ctx.display, EGL_NO_SURFACE, EGL_NO_SURFACE,
            EGL_NO_CONTEXT);
        if (ctx.context != EGL_NO_CONTEXT) {
            eglDestroyContext(ctx.display, ctx.context);
        }
        if (ctx.surface != EGL_NO_SURFACE) {
            eglDestroySurface(ctx.display, ctx.surface);
        }
        eglTerminate(ctx.display);
    }
    ctx = GlContext{};
}

void gl_context_swap(GlContext& ctx) {
    if (ctx.display == EGL_NO_DISPLAY) return;
    eglSwapBuffers(ctx.display, ctx.surface);
}

}  // namespace foyer::player::imgui_shell
