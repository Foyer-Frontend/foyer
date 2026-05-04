#include "video.hpp"
#include "platform/log.hpp"
#include "shader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <nanovg.h>

namespace foyer::libretro {
namespace {

inline std::uint8_t expand5(std::uint32_t v) {
    return (std::uint8_t)((v << 3) | (v >> 2));
}
inline std::uint8_t expand6(std::uint32_t v) {
    return (std::uint8_t)((v << 2) | (v >> 4));
}

// Reference: libretro spec.
//   RETRO_PIXEL_FORMAT_0RGB1555  - 16-bit, big-endian XRGB1555
//   RETRO_PIXEL_FORMAT_RGB565    - 16-bit, RGB565
//   RETRO_PIXEL_FORMAT_XRGB8888  - 32-bit, native-endian X8R8G8B8
void convert_0rgb1555_to_rgba8(const void* src_v, std::size_t pitch,
                               unsigned w, unsigned h,
                               std::uint8_t* dst, std::size_t dst_pitch) {
    const auto* src = static_cast<const std::uint8_t*>(src_v);
    for (unsigned y = 0; y < h; y++) {
        const auto* sp = reinterpret_cast<const std::uint16_t*>(src + y * pitch);
        auto*       dp = dst + y * dst_pitch;
        for (unsigned x = 0; x < w; x++) {
            const std::uint16_t px = sp[x];
            const auto r = (px >> 10) & 0x1F;
            const auto g = (px >>  5) & 0x1F;
            const auto b = (px >>  0) & 0x1F;
            dp[0] = expand5(r);
            dp[1] = expand5(g);
            dp[2] = expand5(b);
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
            const auto r = (px >> 11) & 0x1F;
            const auto g = (px >>  5) & 0x3F;
            const auto b = (px >>  0) & 0x1F;
            dp[0] = expand5(r);
            dp[1] = expand6(g);
            dp[2] = expand5(b);
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

} // namespace

VideoSinkImpl& VideoSinkImpl::instance() {
    static VideoSinkImpl s;
    return s;
}

void VideoSinkImpl::init(NVGcontext* vg) {
    m_vg = vg;
    instance().m_vg = vg;
    Frontend::instance().set_video_sink(&VideoSinkImpl::on_frame);
}

void VideoSinkImpl::shutdown() {
    if (m_image > 0 && m_vg) {
        nvgDeleteImage(m_vg, m_image);
        m_image = 0;
    }
    std::free(m_rgba_buf);
    m_rgba_buf = nullptr;
    m_rgba_cap = 0;
}

void VideoSinkImpl::on_frame(const Frontend::VideoFrame& f) {
    instance().upload(f);
}

void VideoSinkImpl::upload(const Frontend::VideoFrame& f) {
    if (!m_vg || !f.data || !f.width || !f.height) return;

    const std::size_t need = (std::size_t)f.width * f.height * 4u;
    if (need > m_rgba_cap) {
        std::free(m_rgba_buf);
        m_rgba_buf = std::malloc(need);
        m_rgba_cap = m_rgba_buf ? need : 0;
        // Geometry changed → recreate texture next path.
        if (m_image > 0) {
            nvgDeleteImage(m_vg, m_image);
            m_image = 0;
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

    // Apply the active post-process shader pass (if any) BEFORE the
    // nanovg blit. shader_pipeline().process() is a no-op when the
    // active preset is "none" or empty, so this stays free for users
    // who haven't picked a shader.
    shader_pipeline().process(dst, f.width, f.height);

    if (m_image > 0 && (int)f.width == m_w && (int)f.height == m_h) {
        nvgUpdateImage(m_vg, m_image, dst);
    } else {
        if (m_image > 0) nvgDeleteImage(m_vg, m_image);
        m_image = nvgCreateImageRGBA(m_vg, (int)f.width, (int)f.height,
                                     NVG_IMAGE_NEAREST, dst);
        m_w = (int)f.width;
        m_h = (int)f.height;
        m_par = Frontend::instance().aspect_ratio();
        if (m_par <= 0.f) m_par = (float)m_w / (float)m_h;
    }
}

void VideoSinkImpl::draw(NVGcontext* vg, float screen_w, float screen_h) {
    if (!m_vg || m_image <= 0 || m_w <= 0 || m_h <= 0) return;

    auto fit_aspect = [&](float aspect) {
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
            auto [w, h] = fit_aspect(4.0f / 3.0f);
            draw_w = w; draw_h = h;
        } break;
        case AspectMode::Display169: {
            auto [w, h] = fit_aspect(16.0f / 9.0f);
            draw_w = w; draw_h = h;
        } break;
        case AspectMode::Stretch: {
            draw_w = screen_w;
            draw_h = screen_h;
        } break;
        case AspectMode::Integer1x: {
            draw_w = (float)m_w;
            draw_h = (float)m_h;
        } break;
        case AspectMode::Integer2x: {
            draw_w = (float)(m_w * 2);
            draw_h = (float)(m_h * 2);
        } break;
        case AspectMode::IntegerAuto: {
            int sx = (int)(screen_w / (float)m_w);
            int sy = (int)(screen_h / (float)m_h);
            int s  = std::min(sx, sy);
            if (s < 1) s = 1;
            draw_w = (float)(m_w * s);
            draw_h = (float)(m_h * s);
        } break;
        case AspectMode::DisplayCore:
        default: {
            const float a = m_par > 0.f ? m_par : (float)m_w / (float)m_h;
            auto [w, h] = fit_aspect(a);
            draw_w = w; draw_h = h;
        } break;
    }

    const float x = (screen_w - draw_w) * 0.5f;
    const float y = (screen_h - draw_h) * 0.5f;

    auto pat = nvgImagePattern(vg, x, y, draw_w, draw_h, 0.f, m_image, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, draw_w, draw_h);
    nvgFillPaint(vg, pat);
    nvgFill(vg);
}

} // namespace foyer::libretro
