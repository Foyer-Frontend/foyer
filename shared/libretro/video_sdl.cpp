#include "video_sdl.hpp"
#include "platform/log.hpp"
#include "shader.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace foyer::libretro {
namespace {

inline std::uint8_t expand5(std::uint32_t v) {
    return (std::uint8_t)((v << 3) | (v >> 2));
}
inline std::uint8_t expand6(std::uint32_t v) {
    return (std::uint8_t)((v << 2) | (v >> 4));
}

void convert_0rgb1555_to_rgba8(const void* src_v, std::size_t pitch,
                               unsigned w, unsigned h,
                               std::uint8_t* dst, std::size_t dst_pitch) {
    const auto* src = static_cast<const std::uint8_t*>(src_v);
    for (unsigned y = 0; y < h; y++) {
        const auto* sp = reinterpret_cast<const std::uint16_t*>(src + y * pitch);
        auto*       dp = dst + y * dst_pitch;
        for (unsigned x = 0; x < w; x++) {
            const std::uint16_t px = sp[x];
            dp[0] = expand5((px >> 10) & 0x1F);
            dp[1] = expand5((px >>  5) & 0x1F);
            dp[2] = expand5((px >>  0) & 0x1F);
            dp[3] = 0xFF;
            dp += 4;
        }
    }
}

void convert_rgb565_to_rgba8(const void* src_v, std::size_t pitch,
                             unsigned w, unsigned h,
                             std::uint8_t* dst, std::size_t dst_pitch) {
    const auto* src = static_cast<const std::uint8_t*>(src_v);
    for (unsigned y = 0; y < h; y++) {
        const auto* sp = reinterpret_cast<const std::uint16_t*>(src + y * pitch);
        auto*       dp = dst + y * dst_pitch;
        for (unsigned x = 0; x < w; x++) {
            const std::uint16_t px = sp[x];
            dp[0] = expand5((px >> 11) & 0x1F);
            dp[1] = expand6((px >>  5) & 0x3F);
            dp[2] = expand5((px >>  0) & 0x1F);
            dp[3] = 0xFF;
            dp += 4;
        }
    }
}

void convert_xrgb8888_to_rgba8(const void* src_v, std::size_t pitch,
                               unsigned w, unsigned h,
                               std::uint8_t* dst, std::size_t dst_pitch) {
    const auto* src = static_cast<const std::uint8_t*>(src_v);
    for (unsigned y = 0; y < h; y++) {
        const auto* sp = reinterpret_cast<const std::uint32_t*>(src + y * pitch);
        auto*       dp = dst + y * dst_pitch;
        for (unsigned x = 0; x < w; x++) {
            const std::uint32_t px = sp[x];
            dp[0] = (std::uint8_t)((px >> 16) & 0xFF);
            dp[1] = (std::uint8_t)((px >>  8) & 0xFF);
            dp[2] = (std::uint8_t)((px >>  0) & 0xFF);
            dp[3] = 0xFF;
            dp += 4;
        }
    }
}

}  // namespace

VideoSinkSdl& VideoSinkSdl::instance() {
    static VideoSinkSdl s;
    return s;
}

bool VideoSinkSdl::init(SDL_Renderer* renderer) {
    if (m_renderer && m_renderer == renderer) return true;
    m_renderer = renderer;
    Frontend::instance().set_video_sink(&VideoSinkSdl::on_frame);
    foyer::log::write("[video_sdl] init ok renderer=%p\n", (void*)renderer);
    return true;
}

void VideoSinkSdl::shutdown() {
    if (m_tex) { SDL_DestroyTexture(m_tex); m_tex = nullptr; }
    if (m_gl_src_tex) {
        glDeleteTextures(1, (const GLuint*)&m_gl_src_tex);
        m_gl_src_tex = 0;
    }
    if (m_shader_buf) {
        std::free(m_shader_buf);
        m_shader_buf = nullptr;
        m_shader_buf_cap = 0;
    }
    if (m_raw_rgba) {
        std::free(m_raw_rgba);
        m_raw_rgba = nullptr;
        m_raw_rgba_cap = 0;
    }
    m_raw_w = m_raw_h = 0;
    m_renderer = nullptr;
    m_w = m_h = 0;
}

void VideoSinkSdl::on_frame(const Frontend::VideoFrame& f) {
    instance().upload(f);
}

void VideoSinkSdl::upload(const Frontend::VideoFrame& f) {
    if (!m_renderer || !f.data || f.width == 0 || f.height == 0) return;

    if (!m_tex || (int)f.width != m_w || (int)f.height != m_h) {
        if (m_tex) SDL_DestroyTexture(m_tex);
        m_tex = SDL_CreateTexture(m_renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING,
            (int)f.width, (int)f.height);
        if (!m_tex) {
            foyer::log::write("[video_sdl] SDL_CreateTexture failed: %s\n",
                SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(m_tex, SDL_ScaleModeNearest);
        m_w = (int)f.width;
        m_h = (int)f.height;
    }

    void* pixels = nullptr;
    int   pitch  = 0;
    if (SDL_LockTexture(m_tex, nullptr, &pixels, &pitch) != 0) {
        foyer::log::write("[video_sdl] SDL_LockTexture failed: %s\n",
            SDL_GetError());
        return;
    }
    auto* dst = static_cast<std::uint8_t*>(pixels);
    const std::size_t dst_pitch = (std::size_t)pitch;
    switch (f.format) {
        case RETRO_PIXEL_FORMAT_0RGB1555:
            convert_0rgb1555_to_rgba8(f.data, f.pitch, f.width, f.height,
                                       dst, dst_pitch);
            break;
        case RETRO_PIXEL_FORMAT_RGB565:
            convert_rgb565_to_rgba8(f.data, f.pitch, f.width, f.height,
                                     dst, dst_pitch);
            break;
        case RETRO_PIXEL_FORMAT_XRGB8888:
            convert_xrgb8888_to_rgba8(f.data, f.pitch, f.width, f.height,
                                       dst, dst_pitch);
            break;
        default:
            break;
    }
    // Cache the un-shaded RGBA for the pause-menu shader picker — so
    // selecting "none" can restore the raw frame into m_tex without
    // waiting for the next libretro frame. Done BEFORE the GL shader
    // path overwrites m_shader_buf with the readback.
    {
        const std::size_t raw_need = (std::size_t)f.width * (std::size_t)f.height * 4u;
        if (raw_need > m_raw_rgba_cap) {
            std::free(m_raw_rgba);
            m_raw_rgba = (unsigned char*)std::malloc(raw_need);
            m_raw_rgba_cap = m_raw_rgba ? raw_need : 0;
        }
        if (m_raw_rgba) {
            auto* src = static_cast<std::uint8_t*>(pixels);
            const std::size_t row = (std::size_t)f.width * 4u;
            for (unsigned y = 0; y < f.height; ++y) {
                std::memcpy(m_raw_rgba + (std::size_t)y * row,
                            src + (std::size_t)y * (std::size_t)pitch,
                            row);
            }
            m_raw_w = (int)f.width;
            m_raw_h = (int)f.height;
        }
    }

    SDL_UnlockTexture(m_tex);

    // ---- GPU shader path -------------------------------------------------
    // Run the shader chain ON THE GPU here, between libretro frames,
    // BEFORE the next SDL render begins. Every raw-GL state change is
    // bracketed with cleanup so SDL's state cache never sees us. The
    // chain output is read back into m_shader_buf and copied over the
    // SDL_Texture's pixels — one readback per frame is the price of
    // mixing raw GL with SDL2's renderer cleanly.
    if (shader_pipeline().has_borrowed_context()) {
        // Save the GL state we're about to mutate so SDL's cached
        // viewport / scissor / blend / depth don't drift. SDL2's
        // gles2 renderer caches viewport and never re-issues
        // glViewport on RenderCopy unless its own SetViewport API
        // is called, so leaving viewport at source-frame size
        // (e.g. 256x224) made SDL_RenderCopy splat the game into
        // a tiny bottom-left rectangle.
        GLint  saved_viewport[4]  = {0, 0, 0, 0};
        GLint  saved_scissor[4]   = {0, 0, 0, 0};
        GLboolean saved_scissor_on = GL_FALSE;
        GLboolean saved_blend_on   = GL_FALSE;
        GLboolean saved_depth_on   = GL_FALSE;
        GLboolean saved_cull_on    = GL_FALSE;
        GLint  saved_program       = 0;
        GLint  saved_vao           = 0;
        GLint  saved_tex2d         = 0;
        GLint  saved_active_unit   = 0;
        GLint  saved_fbo           = 0;
        GLint  saved_unpack_align  = 4;
        GLint  saved_pack_align    = 4;
        glGetIntegerv(GL_VIEWPORT, saved_viewport);
        glGetIntegerv(GL_SCISSOR_BOX, saved_scissor);
        saved_scissor_on = glIsEnabled(GL_SCISSOR_TEST);
        saved_blend_on   = glIsEnabled(GL_BLEND);
        saved_depth_on   = glIsEnabled(GL_DEPTH_TEST);
        saved_cull_on    = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_CURRENT_PROGRAM,           &saved_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,      &saved_vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,            &saved_active_unit);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,        &saved_tex2d);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,       &saved_fbo);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,          &saved_unpack_align);
        glGetIntegerv(GL_PACK_ALIGNMENT,            &saved_pack_align);

        const std::size_t need = (std::size_t)f.width * (std::size_t)f.height * 4u;
        if (need > m_shader_buf_cap) {
            std::free(m_shader_buf);
            m_shader_buf = (unsigned char*)std::malloc(need);
            m_shader_buf_cap = m_shader_buf ? need : 0;
        }

        // (Re)upload the libretro frame into a GL texture the shader
        // pipeline can sample. Allocate / resize lazily.
        if (m_gl_src_tex == 0 || (int)f.width != m_w || (int)f.height != m_h) {
            if (m_gl_src_tex) {
                glDeleteTextures(1, (const GLuint*)&m_gl_src_tex);
                m_gl_src_tex = 0;
            }
            GLuint t = 0;
            glGenTextures(1, &t);
            m_gl_src_tex = t;
            glBindTexture(GL_TEXTURE_2D, m_gl_src_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         (GLsizei)f.width, (GLsizei)f.height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, m_gl_src_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Pull the CPU buffer we just produced (locked SDL_Texture
        // pixel bytes) into the GL upload. Re-lock + read.
        // dst is no longer mapped — we need a separate staging copy.
        // Cheapest is to re-run the format conversion to a side
        // buffer, since SDL_LockTexture's mapping is opaque.
        // m_shader_buf serves double duty: source for GL upload AND
        // destination for the readback.
        const std::size_t dst_pitch_buf = (std::size_t)f.width * 4u;
        switch (f.format) {
            case RETRO_PIXEL_FORMAT_0RGB1555:
                convert_0rgb1555_to_rgba8(f.data, f.pitch, f.width, f.height,
                                           m_shader_buf, dst_pitch_buf);
                break;
            case RETRO_PIXEL_FORMAT_RGB565:
                convert_rgb565_to_rgba8(f.data, f.pitch, f.width, f.height,
                                         m_shader_buf, dst_pitch_buf);
                break;
            case RETRO_PIXEL_FORMAT_XRGB8888:
                convert_xrgb8888_to_rgba8(f.data, f.pitch, f.width, f.height,
                                           m_shader_buf, dst_pitch_buf);
                break;
            default: break;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        (GLsizei)f.width, (GLsizei)f.height,
                        GL_RGBA, GL_UNSIGNED_BYTE, m_shader_buf);

        // Skip the shader chain when no preset is active — the GL
        // source texture is still up-to-date though, so a later
        // reprocess_shader() call (e.g. from the paused shader
        // picker) can produce a live preview without waiting for
        // the next libretro frame.
        const unsigned out =
            shader_pipeline().active_borrowed()
                ? shader_pipeline().process_texture(
                      m_gl_src_tex, f.width, f.height)
                : 0u;
        if (out && shader_pipeline().readback_last_output(
                m_shader_buf, f.width, f.height)) {
            // Splat the readback into the SDL_Texture (overwriting
            // the format-converted CPU bytes from earlier in this
            // upload). Lock again.
            void* px = nullptr;
            int   p2 = 0;
            if (SDL_LockTexture(m_tex, nullptr, &px, &p2) == 0) {
                auto* d2 = (unsigned char*)px;
                const std::size_t row = (std::size_t)f.width * 4u;
                for (unsigned y = 0; y < f.height; ++y) {
                    std::memcpy(d2 + (std::size_t)y * (std::size_t)p2,
                                m_shader_buf + (std::size_t)y * row,
                                row);
                }
                SDL_UnlockTexture(m_tex);
            }
        }

        // Restore every piece of GL state we captured so SDL's
        // cache stays consistent with the actual GL pipeline.
        glViewport(saved_viewport[0], saved_viewport[1],
                   saved_viewport[2], saved_viewport[3]);
        glScissor(saved_scissor[0], saved_scissor[1],
                  saved_scissor[2], saved_scissor[3]);
        if (saved_scissor_on) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (saved_blend_on)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
        if (saved_depth_on)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
        if (saved_cull_on)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
        glUseProgram((GLuint)saved_program);
        glBindVertexArray((GLuint)saved_vao);
        glActiveTexture((GLenum)saved_active_unit);
        glBindTexture(GL_TEXTURE_2D, (GLuint)saved_tex2d);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
        glPixelStorei(GL_UNPACK_ALIGNMENT, saved_unpack_align);
        glPixelStorei(GL_PACK_ALIGNMENT,   saved_pack_align);
    }

    m_par = Frontend::instance().aspect_ratio();
    if (m_par <= 0.0f) m_par = (float)m_w / (float)m_h;
}

void VideoSinkSdl::reprocess_shader() {
    if (!m_renderer || !m_tex || m_w == 0 || m_h == 0) return;

    // Shader inactive (user picked "none" in the picker): restore the
    // un-shaded frame into m_tex from m_raw_rgba so the pause preview
    // matches what the user will see on resume. Without this, m_tex
    // still holds the last shaded readback and the frozen background
    // stays scanlined/CRT'd until the next retro_run.
    if (!shader_pipeline().active_borrowed()) {
        if (!m_raw_rgba || m_raw_w != m_w || m_raw_h != m_h) return;
        void* px = nullptr;
        int   p  = 0;
        if (SDL_LockTexture(m_tex, nullptr, &px, &p) == 0) {
            auto* d = (unsigned char*)px;
            const std::size_t row = (std::size_t)m_w * 4u;
            for (int y = 0; y < m_h; ++y) {
                std::memcpy(d + (std::size_t)y * (std::size_t)p,
                            m_raw_rgba + (std::size_t)y * row,
                            row);
            }
            SDL_UnlockTexture(m_tex);
        }
        return;
    }

    if (!m_gl_src_tex) return;

    // Same state-save / restore brackets as upload(). We're called
    // from a pause-menu MenuItem callback (so SDL is between
    // RenderCopy passes, but its state cache is still live).
    GLint  saved_viewport[4]  = {0, 0, 0, 0};
    GLint  saved_scissor[4]   = {0, 0, 0, 0};
    GLboolean saved_scissor_on = GL_FALSE;
    GLboolean saved_blend_on   = GL_FALSE;
    GLboolean saved_depth_on   = GL_FALSE;
    GLboolean saved_cull_on    = GL_FALSE;
    GLint  saved_program       = 0;
    GLint  saved_vao           = 0;
    GLint  saved_tex2d         = 0;
    GLint  saved_active_unit   = 0;
    GLint  saved_fbo           = 0;
    GLint  saved_unpack_align  = 4;
    GLint  saved_pack_align    = 4;
    glGetIntegerv(GL_VIEWPORT, saved_viewport);
    glGetIntegerv(GL_SCISSOR_BOX, saved_scissor);
    saved_scissor_on = glIsEnabled(GL_SCISSOR_TEST);
    saved_blend_on   = glIsEnabled(GL_BLEND);
    saved_depth_on   = glIsEnabled(GL_DEPTH_TEST);
    saved_cull_on    = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM,      &saved_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &saved_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE,       &saved_active_unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,   &saved_tex2d);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING,  &saved_fbo);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,     &saved_unpack_align);
    glGetIntegerv(GL_PACK_ALIGNMENT,       &saved_pack_align);

    const unsigned out = shader_pipeline().process_texture(
        m_gl_src_tex, (unsigned)m_w, (unsigned)m_h);
    if (out) {
        const std::size_t need = (std::size_t)m_w * (std::size_t)m_h * 4u;
        if (need > m_shader_buf_cap) {
            std::free(m_shader_buf);
            m_shader_buf = (unsigned char*)std::malloc(need);
            m_shader_buf_cap = m_shader_buf ? need : 0;
        }
        if (m_shader_buf && shader_pipeline().readback_last_output(
                m_shader_buf, (unsigned)m_w, (unsigned)m_h)) {
            void* px = nullptr;
            int   p2 = 0;
            if (SDL_LockTexture(m_tex, nullptr, &px, &p2) == 0) {
                auto* d2 = (unsigned char*)px;
                const std::size_t row = (std::size_t)m_w * 4u;
                for (int y = 0; y < m_h; ++y) {
                    std::memcpy(d2 + (std::size_t)y * (std::size_t)p2,
                                m_shader_buf + (std::size_t)y * row,
                                row);
                }
                SDL_UnlockTexture(m_tex);
            }
        }
    }

    glViewport(saved_viewport[0], saved_viewport[1],
               saved_viewport[2], saved_viewport[3]);
    glScissor(saved_scissor[0], saved_scissor[1],
              saved_scissor[2], saved_scissor[3]);
    if (saved_scissor_on) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (saved_blend_on)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (saved_depth_on)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
    if (saved_cull_on)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);
    glUseProgram((GLuint)saved_program);
    glBindVertexArray((GLuint)saved_vao);
    glActiveTexture((GLenum)saved_active_unit);
    glBindTexture(GL_TEXTURE_2D, (GLuint)saved_tex2d);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)saved_fbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT, saved_unpack_align);
    glPixelStorei(GL_PACK_ALIGNMENT,   saved_pack_align);
}

void VideoSinkSdl::draw(int screen_w, int screen_h) {
    if (!m_renderer || !m_tex || m_w == 0 || m_h == 0) return;

    auto fit = [&](float aspect) {
        float dw = (float)screen_w;
        float dh = dw / aspect;
        if (dh > (float)screen_h) {
            dh = (float)screen_h;
            dw = dh * aspect;
        }
        return std::pair{(int)dw, (int)dh};
    };
    int draw_w = 0, draw_h = 0;
    switch (m_aspect_mode) {
        case AspectMode::Display43: {
            auto [w, h] = fit(4.0f / 3.0f); draw_w = w; draw_h = h;
        } break;
        case AspectMode::Display169: {
            auto [w, h] = fit(16.0f / 9.0f); draw_w = w; draw_h = h;
        } break;
        case AspectMode::Stretch: {
            draw_w = screen_w; draw_h = screen_h;
        } break;
        case AspectMode::Integer1x: {
            draw_w = m_w; draw_h = m_h;
        } break;
        case AspectMode::Integer2x: {
            draw_w = m_w * 2; draw_h = m_h * 2;
        } break;
        case AspectMode::IntegerAuto: {
            int sx = screen_w / std::max(m_w, 1);
            int sy = screen_h / std::max(m_h, 1);
            int s  = std::min(sx, sy);
            if (s < 1) s = 1;
            draw_w = m_w * s; draw_h = m_h * s;
        } break;
        case AspectMode::DisplayCore:
        default: {
            const float a = m_par > 0.0f ? m_par : (float)m_w / (float)m_h;
            auto [w, h] = fit(a); draw_w = w; draw_h = h;
        } break;
    }

    SDL_Rect dst{
        (screen_w - draw_w) / 2,
        (screen_h - draw_h) / 2,
        draw_w, draw_h};
    SDL_RenderCopy(m_renderer, m_tex, nullptr, &dst);
}

}  // namespace foyer::libretro
