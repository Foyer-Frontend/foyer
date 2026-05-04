#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace foyer::libretro {

// Real GLES3 fragment-shader post-process. EGL pbuffer + headless FBO,
// uploads the emulator's RGBA8 frame, runs a fullscreen-quad pass with
// the active fragment shader, reads pixels back. Caller (video sink)
// then pushes the post-processed pixels into nanovg the same way it
// does the raw frame today.
//
// Shaders come from one of two places:
//   * Built-in presets compiled into the player binary as GLSL ES
//     string literals (scanlines, CRT, LCD, GB-DMG, GBA correction).
//   * User .glsl files at /foyer/shaders/<name>.glsl — one fragment
//     shader per file, no header / preset manifest.
//
// Single-pass is the v1; the multi-pass slang-shader pipeline in
// docs/SHADERS_PLAN.md remains the long-term target. The interface
// here is structured so a future ShaderPipeline that holds a chain
// of ShaderPasses doesn't need a different call site in video.cpp.
class ShaderPipeline {
public:
    ShaderPipeline();
    ~ShaderPipeline();

    ShaderPipeline(const ShaderPipeline&)            = delete;
    ShaderPipeline& operator=(const ShaderPipeline&) = delete;

    // Bring up EGL + GLES3 + the fixed fullscreen-quad VAO. Returns
    // false on any failure; the caller should treat that as "shaders
    // unavailable" and skip the post-process step entirely.
    bool init();

    // Switch to a preset by name. Looks up the name in the built-in
    // table first, falls back to /foyer/shaders/<name>.glsl, falls
    // back to "none" if neither is found. Recompiles the fragment
    // shader; expensive, so only call when the user picks a new
    // preset (not per frame).
    bool set_preset(std::string_view name);

    // Currently active preset name. "" if no shader is active.
    const std::string& active() const { return m_active_name; }

    // Process one frame in place. `pixels` is RGBA8 of size w*h*4.
    // Returns false on any GL error; pixels are unchanged in that
    // case. No-op (returns true unchanged) when the active preset
    // is "none".
    bool process(std::uint8_t* pixels, unsigned w, unsigned h);

    // Names + labels for the Settings cycle picker. Includes both
    // built-in presets and any *.glsl files under /foyer/shaders/.
    struct PresetInfo { std::string name; std::string label; };
    static std::vector<PresetInfo> available_presets();

private:
    // EGL/GL state. void* so the header doesn't drag in <EGL/egl.h>.
    void*    m_egl_display = nullptr;
    void*    m_egl_context = nullptr;
    void*    m_egl_surface = nullptr;

    // GL objects.
    unsigned m_program     = 0;
    unsigned m_vao         = 0;
    unsigned m_vbo         = 0;
    unsigned m_fbo         = 0;
    unsigned m_in_tex      = 0;
    unsigned m_out_tex     = 0;
    int      m_loc_tex     = -1;
    int      m_loc_size    = -1;
    unsigned m_w           = 0;
    unsigned m_h           = 0;

    // Latest applied preset. "" + m_program == 0 means no-op mode.
    std::string m_active_name;

    bool make_current();
    bool ensure_size(unsigned w, unsigned h);
    bool compile_program(const std::string& fragment_src);
    void destroy_program();
};

// Singleton accessor — one pipeline per process is enough.
ShaderPipeline& shader_pipeline();

} // namespace foyer::libretro
