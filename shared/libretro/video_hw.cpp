#include "video_hw.hpp"
#include "frontend.hpp"
#include "platform/log.hpp"

#include "libretro.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace foyer::libretro {
namespace {

// Static trampolines so the core can call into our HwContext without
// us exposing class-member function pointers.

uintptr_t get_current_framebuffer_cb() {
    return (uintptr_t)HwContext::instance_fbo();
}

retro_proc_address_t get_proc_address_cb(const char* sym) {
    return (retro_proc_address_t)eglGetProcAddress(sym);
}

const char* egl_err_str(EGLint e) {
    switch (e) {
        case EGL_SUCCESS:             return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
        case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
        default:                      return "EGL_UNKNOWN";
    }
}

} // namespace

unsigned HwContext::instance_fbo() {
    return instance().m_fbo;
}

HwContext& HwContext::instance() {
    static HwContext c;
    return c;
}

bool HwContext::request(retro_hw_render_callback* cb) {
    if (!cb) return false;
    // We only support GLES3 via Switch's EGL/Mesa portlibs. Cores that
    // ask for full desktop GL or Vulkan get rejected; they'll fall back
    // to software rendering (or refuse to load — that's the core's
    // call).
    if (cb->context_type != RETRO_HW_CONTEXT_OPENGLES2 &&
        cb->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
        cb->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION) {
        foyer::log::write("[hw_render] rejected context_type=%d (only GLES2/3 supported)\n",
            (int)cb->context_type);
        return false;
    }
    cb->get_current_framebuffer = get_current_framebuffer_cb;
    cb->get_proc_address        = get_proc_address_cb;
    m_callback = cb;
    foyer::log::write("[hw_render] callback registered (type=%d, version=%u.%u, depth=%d, stencil=%d)\n",
        (int)cb->context_type, cb->version_major, cb->version_minor,
        (int)cb->depth, (int)cb->stencil);
    return true;
}

bool HwContext::ensure_context(unsigned width, unsigned height) {
    if (!m_callback) return false;
    if (m_context_alive && m_width == width && m_height == height) return true;

    if (!m_context_alive) {
        m_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (m_egl_display == EGL_NO_DISPLAY) {
            foyer::log::write("[hw_render] eglGetDisplay failed: %s\n",
                egl_err_str(eglGetError()));
            return false;
        }

        EGLint major = 0, minor = 0;
        if (!eglInitialize((EGLDisplay)m_egl_display, &major, &minor)) {
            foyer::log::write("[hw_render] eglInitialize failed: %s\n",
                egl_err_str(eglGetError()));
            return false;
        }
        foyer::log::write("[hw_render] EGL %d.%d on Switch (vendor=%s, "
            "version=%s, client_apis=%s)\n", major, minor,
            eglQueryString((EGLDisplay)m_egl_display, EGL_VENDOR)
                ?: "?",
            eglQueryString((EGLDisplay)m_egl_display, EGL_VERSION)
                ?: "?",
            eglQueryString((EGLDisplay)m_egl_display, EGL_CLIENT_APIS)
                ?: "?");
        // Dump the full config catalogue so we can see on hardware
        // exactly what attrs Mesa-on-Switch exposes — the probe
        // chain below relaxes attrs until something matches, and
        // when ALL probes fail we want to know which configs
        // existed (and why none satisfied our needs).
        EGLint config_count = 0;
        eglGetConfigs((EGLDisplay)m_egl_display, nullptr, 0, &config_count);
        foyer::log::write("[hw_render] available egl configs: %d\n",
            (int)config_count);
        if (config_count > 0) {
            std::vector<EGLConfig> all((std::size_t)config_count);
            eglGetConfigs((EGLDisplay)m_egl_display, all.data(),
                config_count, &config_count);
            // Cap the dump at the first 12 to keep the log readable
            // — more than enough to characterise Mesa's surface
            // catalogue on Switch.
            const int dump_n = std::min(config_count, 12);
            for (int i = 0; i < dump_n; i++) {
                EGLint r = 0, g = 0, b = 0, a = 0;
                EGLint d = 0, s = 0, rt = 0, st = 0;
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_RED_SIZE,        &r);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_GREEN_SIZE,      &g);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_BLUE_SIZE,       &b);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_ALPHA_SIZE,      &a);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_DEPTH_SIZE,      &d);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_STENCIL_SIZE,    &s);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_RENDERABLE_TYPE, &rt);
                eglGetConfigAttrib((EGLDisplay)m_egl_display, all[i], EGL_SURFACE_TYPE,    &st);
                foyer::log::write(
                    "[hw_render]   config #%d: rgba=%d%d%d%d depth=%d stencil=%d "
                    "renderable=0x%x surface_bits=0x%x%s\n",
                    i, r, g, b, a, d, s, (unsigned)rt, (unsigned)st,
                    (rt & EGL_OPENGL_ES3_BIT) ? " (gles3)"
                  : (rt & EGL_OPENGL_ES2_BIT) ? " (gles2)" : "");
            }
            if (config_count > dump_n) {
                foyer::log::write(
                    "[hw_render]   …and %d more configs (truncated)\n",
                    config_count - dump_n);
            }
        }

        if (!eglBindAPI(EGL_OPENGL_ES_API)) {
            foyer::log::write("[hw_render] eglBindAPI(GLES) failed: %s\n",
                egl_err_str(eglGetError()));
            return false;
        }

        // Match the core's declared depth/stencil needs instead of
        // hard-coding DEPTH=24 STENCIL=8 (which Switch's mesa doesn't
        // always have a matching config for — eglChooseConfig
        // returned 0 with EGL_SUCCESS in the v0.2.28 hardware run).
        // Build a list of progressively-relaxed attrib sets and try
        // each until one matches. The first slot mirrors what the
        // core asked for; later slots drop the optional features so
        // we still get *some* GL context even if mesa's config space
        // is sparse.
        const EGLint want_depth   = m_callback->depth   ? 24 : 0;
        const EGLint want_stencil = m_callback->stencil ? 8  : 0;
        const EGLint renderable   =
            (m_callback->context_type == RETRO_HW_CONTEXT_OPENGLES3 ||
             m_callback->version_major >= 3)
                ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;

        // surface_type: -1 = don't filter by surface type at all; 0 =
        // window only; otherwise a specific bit. v0.2.55 always asked
        // for EGL_PBUFFER_BIT, but Switch's mesa portlibs don't ship
        // configs that advertise pbuffer support — every probe came
        // back with 0 matches and EGL_SUCCESS. Walk progressively-
        // looser surface filters too: pbuffer first (some platforms
        // do have it and pbuffer is the cleanest off-screen surface),
        // then window (the swapchain we already own from deko3d),
        // then no filter at all.
        struct AttribProbe {
            EGLint depth;
            EGLint stencil;
            EGLint renderable;
            EGLint surface_type;   // -1 = unfiltered
        };
        const AttribProbe probes[] = {
            { want_depth, want_stencil, renderable,          EGL_PBUFFER_BIT },
            { want_depth, 0,            renderable,          EGL_PBUFFER_BIT },
            { want_depth, want_stencil, renderable,          -1 },              // any surface
            { want_depth, 0,            renderable,          -1 },
            { 16,         0,            renderable,          -1 },
            { 0,          0,            renderable,          -1 },
            { 0,          0,            EGL_OPENGL_ES2_BIT,  -1 },
        };

        EGLConfig config{};
        EGLint    n_configs = 0;
        EGLint    picked_surface = -1;
        bool      picked         = false;
        for (const auto& p : probes) {
            std::vector<EGLint> attribs = {
                EGL_RENDERABLE_TYPE, p.renderable,
                EGL_RED_SIZE,        8,
                EGL_GREEN_SIZE,      8,
                EGL_BLUE_SIZE,       8,
                EGL_ALPHA_SIZE,      8,
                EGL_DEPTH_SIZE,      p.depth,
                EGL_STENCIL_SIZE,    p.stencil,
            };
            if (p.surface_type != -1) {
                attribs.push_back(EGL_SURFACE_TYPE);
                attribs.push_back(p.surface_type);
            }
            attribs.push_back(EGL_NONE);
            if (eglChooseConfig((EGLDisplay)m_egl_display, attribs.data(),
                                &config, 1, &n_configs) && n_configs >= 1) {
                foyer::log::write(
                    "[hw_render] config picked: depth=%d stencil=%d gles=%s surface=%s\n",
                    (int)p.depth, (int)p.stencil,
                    p.renderable == EGL_OPENGL_ES3_BIT ? "3" : "2",
                    p.surface_type == EGL_PBUFFER_BIT ? "pbuffer" :
                    p.surface_type == EGL_WINDOW_BIT  ? "window"  : "any");
                picked         = true;
                picked_surface = p.surface_type;
                break;
            }
        }
        if (!picked) {
            foyer::log::write("[hw_render] eglChooseConfig: no matching config "
                "after %zu probes (last egl err=%s)\n",
                sizeof(probes) / sizeof(probes[0]),
                egl_err_str(eglGetError()));
            return false;
        }

        // 1×1 pbuffer surface — we render to FBOs, never to the
        // window, so any tiny surface that eglMakeCurrent will accept
        // is fine. Try pbuffer first; if the chosen config doesn't
        // support pbuffer fall back to EGL_NO_SURFACE (relies on
        // EGL_KHR_surfaceless_context, which mesa-on-Switch ships).
        m_egl_surface = EGL_NO_SURFACE;
        if (picked_surface != EGL_WINDOW_BIT) {
            const EGLint pb_attribs[] = {
                EGL_WIDTH,  1,
                EGL_HEIGHT, 1,
                EGL_NONE
            };
            m_egl_surface = eglCreatePbufferSurface(
                (EGLDisplay)m_egl_display, config, pb_attribs);
            if (m_egl_surface == EGL_NO_SURFACE) {
                foyer::log::write(
                    "[hw_render] pbuffer surface unavailable (%s); trying "
                    "surfaceless context\n",
                    egl_err_str(eglGetError()));
            }
        }

        // EGL_CONTEXT_CLIENT_VERSION must be 2 or 3 — passing 0
        // (which is what cores like PPSSPP register with when they
        // don't care about a specific minor version) makes Mesa
        // bounce eglCreateContext with EGL_BAD_MATCH and PPSSPP
        // null-derefs deep in its graphics init. Coerce 0 → 2/3
        // based on which renderable type we actually picked above.
        const EGLint ctx_version =
            m_callback->version_major >= 2
                ? (EGLint)m_callback->version_major
                : (renderable == EGL_OPENGL_ES3_BIT ? 3 : 2);
        const EGLint ctx_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, ctx_version,
            EGL_NONE
        };
        m_egl_context = eglCreateContext(
            (EGLDisplay)m_egl_display, config, EGL_NO_CONTEXT, ctx_attribs);
        if (m_egl_context == EGL_NO_CONTEXT) {
            foyer::log::write(
                "[hw_render] eglCreateContext(client_ver=%d) failed: %s\n",
                (int)ctx_version, egl_err_str(eglGetError()));
            return false;
        }
        foyer::log::write(
            "[hw_render] eglCreateContext ok (client_ver=%d)\n",
            (int)ctx_version);

        if (!eglMakeCurrent((EGLDisplay)m_egl_display,
                            (EGLSurface)m_egl_surface,
                            (EGLSurface)m_egl_surface,
                            (EGLContext)m_egl_context)) {
            foyer::log::write("[hw_render] eglMakeCurrent failed: %s\n",
                egl_err_str(eglGetError()));
            return false;
        }

        // Confirm what driver actually answered — "Software Rasterizer"
        // vs "NV12B" (nouveau on Tegra X1) means the difference
        // between "playable HW core" and "blank screen / drop frames".
        // Also dump GL_VERSION + GLSL version so we can spot capability
        // mismatches with what the core requested.
        const char* gl_vendor   = (const char*)glGetString(GL_VENDOR);
        const char* gl_renderer = (const char*)glGetString(GL_RENDERER);
        const char* gl_version  = (const char*)glGetString(GL_VERSION);
        const char* glsl_ver    = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
        foyer::log::write("[hw_render] GL vendor=%s renderer=%s version=%s "
            "glsl=%s\n",
            gl_vendor   ? gl_vendor   : "?",
            gl_renderer ? gl_renderer : "?",
            gl_version  ? gl_version  : "?",
            glsl_ver    ? glsl_ver    : "?");

        glGenFramebuffers (1, &m_fbo);
        glGenTextures     (1, &m_color_tex);
        glGenRenderbuffers(1, &m_depth_stencil);

        m_context_alive = true;
    }

    // (Re)allocate FBO attachments to match requested size.
    glBindTexture(GL_TEXTURE_2D, m_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 (GLsizei)width, (GLsizei)height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depth_stencil);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          (GLsizei)width, (GLsizei)height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, m_color_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_depth_stencil);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        foyer::log::write("[hw_render] FBO incomplete after resize to %ux%u\n",
            width, height);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_width  = width;
    m_height = height;
    m_readback.assign((std::size_t)width * height * 4, 0);

    if (!m_context_reset_done && m_callback->context_reset) {
        foyer::log::write("[hw_render] firing context_reset()\n");
        m_callback->context_reset();
        m_context_reset_done = true;
    }
    return true;
}

void HwContext::begin_frame() {
    if (!m_context_alive) return;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, (GLsizei)m_width, (GLsizei)m_height);
}

void HwContext::end_frame() {
    if (!m_context_alive) return;

    // Read the FBO into the CPU buffer + hand it to the existing
    // software video sink as XRGB8888. GLES gives us RGBA8888;
    // libretro's XRGB8888 is BGRA in memory order on little-endian.
    // We swizzle in-place — fast enough for v1, will revisit if it
    // shows up in profiles.
    glReadPixels(0, 0, (GLsizei)m_width, (GLsizei)m_height,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_readback.data());
    // One-time diagnostic: sample the readback for non-zero content.
    // If glReadPixels keeps returning all-zero buffers despite the
    // core claiming it rendered, the FBO color attachment isn't
    // actually receiving draw calls — that points at a Mesa nouveau
    // FBO bug or a context-binding mismatch, not foyer's pipeline.
    if (!m_readback_diagnosed) {
        bool any_nonzero = false;
        const std::size_t sample_count = std::min<std::size_t>(
            m_readback.size(), 4096);
        for (std::size_t i = 0; i < sample_count; i++) {
            if (m_readback[i] != 0) { any_nonzero = true; break; }
        }
        foyer::log::write(
            "[hw_render] first readback %ux%u: %s (gl_error=0x%04x)\n",
            m_width, m_height,
            any_nonzero ? "non-zero (frame contents OK)"
                        : "ALL-ZERO (FBO not painted, likely Mesa/context bug)",
            (unsigned)glGetError());
        m_readback_diagnosed = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const std::size_t row_bytes = (std::size_t)m_width * 4;

    // GLES origin is bottom-left; libretro expects top-left. Flip rows
    // in-place using a single scanline scratch buffer.
    {
        static thread_local std::vector<std::uint8_t> scratch;
        if (scratch.size() < row_bytes) scratch.resize(row_bytes);
        for (unsigned y = 0; y < m_height / 2; y++) {
            auto* top    = m_readback.data() + (std::size_t)y * row_bytes;
            auto* bottom = m_readback.data() +
                (std::size_t)(m_height - 1 - y) * row_bytes;
            std::memcpy(scratch.data(), top,    row_bytes);
            std::memcpy(top,            bottom, row_bytes);
            std::memcpy(bottom,         scratch.data(), row_bytes);
        }
    }

    // Swizzle RGBA → BGRA (libretro XRGB8888 byte order on LE).
    auto* px = (std::uint32_t*)m_readback.data();
    const std::size_t count = (std::size_t)m_width * m_height;
    for (std::size_t i = 0; i < count; i++) {
        const std::uint32_t v = px[i];
        px[i] = (v & 0xFF00FF00u)
              | ((v & 0x00FF0000u) >> 16)
              | ((v & 0x000000FFu) << 16);
    }

    Frontend::VideoFrame f{
        m_readback.data(),
        m_width, m_height,
        row_bytes,
        RETRO_PIXEL_FORMAT_XRGB8888,
    };
    Frontend::instance().push_video_frame(f);
}

void HwContext::shutdown() {
    if (m_context_alive && m_callback && m_callback->context_destroy) {
        m_callback->context_destroy();
    }
    if (m_egl_context) {
        eglMakeCurrent((EGLDisplay)m_egl_display,
                       EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext((EGLDisplay)m_egl_display, (EGLContext)m_egl_context);
        m_egl_context = nullptr;
    }
    if (m_egl_surface) {
        eglDestroySurface((EGLDisplay)m_egl_display, (EGLSurface)m_egl_surface);
        m_egl_surface = nullptr;
    }
    if (m_egl_display) {
        eglTerminate((EGLDisplay)m_egl_display);
        m_egl_display = nullptr;
    }
    m_fbo = m_color_tex = m_depth_stencil = 0;
    m_context_alive = false;
    m_context_reset_done = false;
    m_width = m_height = 0;
}

} // namespace foyer::libretro
