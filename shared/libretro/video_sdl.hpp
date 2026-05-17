#pragma once
//
// shared/libretro/video_sdl — SDL2 sibling of video.hpp. Receives
// libretro frames, uploads to a streaming SDL_Texture, draws aspect-
// fit via SDL_RenderCopy. Used by PLAYER_PLUTONIUM.
//
// The CPU shader path runs inside upload() — process(uint8_t*) on
// the RGBA buffer before SDL_UpdateTexture. Same model that worked
// under PLAYER_BRLS. GPU shader is parked: every attempt to mix
// raw GL with SDL2's gles2 renderer on switch-sdl2 poisoned SDL's
// state cache and broke the menu text rendering.

#include "frontend.hpp"
#include "aspect.hpp"

#include <SDL2/SDL.h>

#include <cstdint>

namespace foyer::libretro {

struct VideoSinkSdl {
    static VideoSinkSdl& instance();

    bool init(SDL_Renderer* renderer);
    void shutdown();

    static void on_frame(const Frontend::VideoFrame& f);

    // Draw into the current SDL render target.
    void draw(int screen_w, int screen_h);

    bool has_frame() const { return m_tex != nullptr; }
    int  width()    const { return m_w; }
    int  height()   const { return m_h; }
    SDL_Texture* texture() const { return m_tex; }

    // Re-run the shader chain on the last uploaded libretro frame
    // and refresh the SDL_Texture. Used by the pause menu's shader
    // picker so the user sees the new shader applied to the frozen
    // frame BEFORE resuming.
    void reprocess_shader();

    void set_aspect(AspectMode m) { m_aspect_mode = m; }
    AspectMode aspect() const { return m_aspect_mode; }

private:
    void upload(const Frontend::VideoFrame& f);

    SDL_Renderer* m_renderer    = nullptr;
    SDL_Texture*  m_tex         = nullptr;
    int           m_w           = 0;
    int           m_h           = 0;
    float         m_par         = 1.0f;
    AspectMode    m_aspect_mode = AspectMode::DisplayCore;

    // GPU shader scratch: a GL texture for the libretro upload that
    // ShaderPipeline samples, plus a CPU buffer the chain reads back
    // into so we can copy it into the SDL_Texture for SDL_RenderCopy.
    // All raw GL happens inside upload() (between libretro frames,
    // never during SDL's render pass), with full state cleanup
    // before SDL sees the renderer again.
    unsigned        m_gl_src_tex     = 0;
    unsigned char*  m_shader_buf     = nullptr;
    std::size_t     m_shader_buf_cap = 0;

    // Format-converted RGBA8 of the last libretro frame. Kept separate
    // from m_shader_buf (which gets overwritten by readback) so the
    // pause-menu shader picker can stamp the un-shaded frame back into
    // m_tex when the user selects "none" while paused.
    unsigned char*  m_raw_rgba       = nullptr;
    std::size_t     m_raw_rgba_cap   = 0;
    int             m_raw_w          = 0;
    int             m_raw_h          = 0;
};

}  // namespace foyer::libretro
