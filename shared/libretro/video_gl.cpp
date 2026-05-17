#include "video_gl.hpp"
#include "platform/log.hpp"
#include "shader.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace foyer::libretro {
namespace {

// Pixel-format converters. Duplicated from video.cpp on purpose — that
// file goes away with Phase 5; pulling them into a shared header now
// would touch every player target. The 5/6 expand helpers are the
// libretro-spec reference mapping.
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

// mesa-on-Switch (libnx + nouveau) miscompiles GLES3
// `const vec2[](...)` global arrays — gl_VertexID then indexes
// uninitialised storage and every fragment ends up at the origin.
// Bit-twiddle 0..3 -> {(0,0),(1,0),(0,1),(1,1)} instead.
constexpr const char* kVS = R"(#version 300 es
precision highp float;
uniform vec2 u_pos;     // NDC top-left
uniform vec2 u_scale;   // NDC size
out vec2 v_uv;
void main() {
    vec2 c = vec2(float(gl_VertexID & 1),
                  float((gl_VertexID >> 1) & 1));
    vec2 ndc;
    ndc.x = u_pos.x + c.x * u_scale.x;
    ndc.y = u_pos.y - c.y * u_scale.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = vec2(c.x, c.y);
}
)";

constexpr const char* kFS = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 fragColor;
void main() {
    fragColor = texture(u_tex, v_uv);
}
)";

GLuint compile_stage(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof(buf), &n, buf);
        foyer::log::write("[video_gl] shader compile failed (type=0x%X): %.*s\n",
            (unsigned)type, (int)n, buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

}  // namespace

VideoSinkGl& VideoSinkGl::instance() {
    static VideoSinkGl s;
    return s;
}

bool VideoSinkGl::init() {
    if (m_program) return true;
    GLuint vs = compile_stage(GL_VERTEX_SHADER, kVS);
    if (!vs) return false;
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, kFS);
    if (!fs) { glDeleteShader(vs); return false; }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char buf[1024]; GLsizei n = 0;
        glGetProgramInfoLog(m_program, sizeof(buf), &n, buf);
        foyer::log::write("[video_gl] link failed: %.*s\n", (int)n, buf);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    m_loc_pos   = glGetUniformLocation(m_program, "u_pos");
    m_loc_scale = glGetUniformLocation(m_program, "u_scale");
    m_loc_tex   = glGetUniformLocation(m_program, "u_tex");

    glGenVertexArrays(1, &m_vao);

    Frontend::instance().set_video_sink(&VideoSinkGl::on_frame);

    foyer::log::write("[video_gl] init ok prog=%u vao=%u loc_pos=%d loc_scale=%d loc_tex=%d err=0x%x\n",
        (unsigned)m_program, (unsigned)m_vao,
        m_loc_pos, m_loc_scale, m_loc_tex,
        (unsigned)glGetError());
    return true;
}

void VideoSinkGl::shutdown() {
    if (m_tex)     { glDeleteTextures(1, &m_tex); m_tex = 0; }
    if (m_vao)     { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    if (m_rgba_buf) { std::free(m_rgba_buf); m_rgba_buf = nullptr; m_rgba_cap = 0; }
    m_w = m_h = 0;
}

void VideoSinkGl::on_frame(const Frontend::VideoFrame& f) {
    instance().upload(f);
}

void VideoSinkGl::upload(const Frontend::VideoFrame& f) {
    if (!f.data || f.width == 0 || f.height == 0) return;

    const std::size_t need = (std::size_t)f.width * (std::size_t)f.height * 4u;
    if (need > m_rgba_cap) {
        std::free(m_rgba_buf);
        m_rgba_buf = std::malloc(need);
        m_rgba_cap = m_rgba_buf ? need : 0;
        if (m_tex) {
            // geometry changed -> drop the GL texture so we re-create
            // it at the new size below.
            glDeleteTextures(1, &m_tex);
            m_tex = 0;
        }
    }
    if (!m_rgba_buf) return;
    auto* dst = static_cast<std::uint8_t*>(m_rgba_buf);
    const std::size_t dst_pitch = (std::size_t)f.width * 4u;

    switch (f.format) {
        case RETRO_PIXEL_FORMAT_0RGB1555:
            convert_0rgb1555_to_rgba8(f.data, f.pitch, f.width, f.height, dst, dst_pitch);
            break;
        case RETRO_PIXEL_FORMAT_RGB565:
            convert_rgb565_to_rgba8(f.data, f.pitch, f.width, f.height, dst, dst_pitch);
            break;
        case RETRO_PIXEL_FORMAT_XRGB8888:
            convert_xrgb8888_to_rgba8(f.data, f.pitch, f.width, f.height, dst, dst_pitch);
            break;
        default:
            return;
    }

    if (m_tex == 0 || (int)f.width != (int)m_w || (int)f.height != (int)m_h) {
        if (m_tex) glDeleteTextures(1, &m_tex);
        glGenTextures(1, &m_tex);
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)f.width, (GLsizei)f.height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, dst);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_w = f.width;
        m_h = f.height;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            (GLsizei)f.width, (GLsizei)f.height,
            GL_RGBA, GL_UNSIGNED_BYTE, dst);
    }
    m_par = Frontend::instance().aspect_ratio();
    if (m_par <= 0.f) m_par = (float)m_w / (float)m_h;
}

void VideoSinkGl::draw(float screen_w, float screen_h) {
    static int s_draw_log_budget = 4;
    if (s_draw_log_budget > 0) {
        foyer::log::write("[video_gl] draw screen=%.0fx%.0f tex=%u src=%ux%u prog=%u\n",
            screen_w, screen_h, (unsigned)m_tex, m_w, m_h, (unsigned)m_program);
        --s_draw_log_budget;
    }
    if (!m_program || !m_tex || m_w == 0 || m_h == 0) return;

    auto fit = [&](float aspect) {
        float dw = screen_w;
        float dh = screen_w / aspect;
        if (dh > screen_h) {
            dh = screen_h;
            dw = screen_h * aspect;
        }
        return std::pair{dw, dh};
    };
    float draw_w = 0.f, draw_h = 0.f;
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
            draw_w = (float)m_w; draw_h = (float)m_h;
        } break;
        case AspectMode::Integer2x: {
            draw_w = (float)(m_w * 2); draw_h = (float)(m_h * 2);
        } break;
        case AspectMode::IntegerAuto: {
            int sx = (int)(screen_w / (float)m_w);
            int sy = (int)(screen_h / (float)m_h);
            int s  = std::min(sx, sy);
            if (s < 1) s = 1;
            draw_w = (float)(m_w * s); draw_h = (float)(m_h * s);
        } break;
        case AspectMode::DisplayCore:
        default: {
            const float a = m_par > 0.f ? m_par : (float)m_w / (float)m_h;
            auto [w, h] = fit(a); draw_w = w; draw_h = h;
        } break;
    }

    const float x = (screen_w - draw_w) * 0.5f;
    const float y = (screen_h - draw_h) * 0.5f;

    // Pixel space -> NDC: pixel (0..sw, 0..sh) maps to (-1..+1, +1..-1)
    // (top-left origin in screen, top-left = +1 Y in NDC).
    const float nx = (x / screen_w) * 2.0f - 1.0f;
    const float ny = 1.0f - (y / screen_h) * 2.0f;
    const float nw = (draw_w / screen_w) * 2.0f;
    const float nh = (draw_h / screen_h) * 2.0f;

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    if (m_loc_tex   >= 0) glUniform1i(m_loc_tex, 0);
    if (m_loc_pos   >= 0) glUniform2f(m_loc_pos, nx, ny);
    if (m_loc_scale >= 0) glUniform2f(m_loc_scale, nw, nh);
    glBindVertexArray(m_vao);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    const GLenum err = glGetError();
    static int s_err_log_budget = 4;
    if (s_err_log_budget > 0) {
        foyer::log::write("[video_gl] draw err=0x%x ndc=(%.2f,%.2f) scale=(%.2f,%.2f)\n",
            (unsigned)err, nx, ny, nw, nh);
        --s_err_log_budget;
    }
    glBindVertexArray(0);
}

}  // namespace foyer::libretro
