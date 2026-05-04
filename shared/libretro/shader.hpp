#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace foyer::libretro {

// Real GLES3 multi-pass post-process. EGL pbuffer + ping-pong FBOs +
// optional LUT textures. Each pass is a fullscreen-quad fragment
// shader sampling from the previous pass's output (or the original
// frame for pass 0); the final pass reads back to the CPU buffer the
// caller hands in.
//
// Three ways to spell a "preset":
//   * Built-in name — "scanlines", "crt_simple", "lcd_grid",
//     "gb_dmg", "gba_correct", "none". GLSL ES 3.0 string literals
//     baked into the player binary; one pass each.
//   * Single .glsl file at /foyer/shaders/<name>.glsl. One pass.
//   * JSON manifest at /foyer/shaders/<name>.json. Multi-pass with
//     per-pass filter / scale / parameters and shared LUTs:
//       {
//         "passes": [
//           { "fragment": "scanlines.glsl",   "filter": "nearest",
//             "params": { "intensity": 0.7 } },
//           { "fragment": "gba_correct.glsl", "filter": "linear" }
//         ],
//         "luts": [
//           { "name": "palette", "path": "palette.png",
//             "filter": "linear" }
//         ]
//       }
//     Fragment paths resolve relative to /foyer/shaders/. Built-in
//     names are also valid (e.g. "scanlines" without ".glsl"). LUTs
//     bind to a uniform sampler called u_lut_<name> in every pass.
//
// The pipeline is structured so a single-pass and a 6-pass chain hit
// the same call site in video.cpp; only the count of pass-vector
// entries differs.
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

    // Switch to a preset by name. Resolution order: built-in →
    // <name>.json (multi-pass) → <name>.glsl (single-pass) →
    // fall through to a no-op chain.
    bool set_preset(std::string_view name);

    // Currently active preset name. "" if no shader is active.
    const std::string& active() const { return m_active_name; }

    // Process one frame in place. `pixels` is RGBA8 of size w*h*4.
    // Returns false on any GL error; pixels are unchanged in that
    // case. No-op (returns true unchanged) when the active preset
    // is "none" or the chain is empty.
    bool process(std::uint8_t* pixels, unsigned w, unsigned h);

    struct PresetInfo { std::string name; std::string label; };
    static std::vector<PresetInfo> available_presets();

private:
    // One stage in the chain.
    struct Pass {
        unsigned program   = 0;
        int      loc_tex   = -1;     // u_tex   — previous-pass output
        int      loc_size  = -1;     // u_size  — destination size
        int      loc_orig  = -1;     // u_orig  — original frame size
        int      loc_frame = -1;     // u_frame — frame counter
        bool     filter_linear = false;
        // Param uniform locations keyed by name. Each pass may define
        // its own #pragma-style parameters; the JSON manifest fills
        // them with concrete float values applied per-frame.
        struct Param { int loc; float value; };
        std::unordered_map<std::string, Param> params;
        // Per-pass LUT sampler bindings, keyed by LUT name. Maps to
        // the GL texture handle owned by m_luts.
        std::unordered_map<std::string, int> lut_locs;
    };

    struct Lut {
        std::string name;
        unsigned    texture = 0;
        bool        linear  = true;
    };

    // EGL/GL state. void* so the header doesn't drag in <EGL/egl.h>.
    void*    m_egl_display = nullptr;
    void*    m_egl_context = nullptr;
    void*    m_egl_surface = nullptr;

    unsigned m_vao         = 0;
    unsigned m_in_tex      = 0;
    // Two ping-pong textures + matching FBOs. Pass i reads from
    // [i & 1] and writes to [(i & 1) ^ 1]. Pass 0 reads from
    // m_in_tex which holds the freshly-uploaded source.
    unsigned m_pp_fbo[2]   = { 0, 0 };
    unsigned m_pp_tex[2]   = { 0, 0 };
    unsigned m_w           = 0;
    unsigned m_h           = 0;

    std::vector<Pass> m_chain;
    std::vector<Lut>  m_luts;

    std::string  m_active_name;
    std::uint64_t m_frame_count = 0;

    bool make_current();
    bool ensure_size(unsigned w, unsigned h);
    void destroy_chain();
    bool build_program(const std::string& fragment_src, Pass& out);
    bool load_lut(const std::string& name, const std::string& path,
                  bool linear);

    // High-level loaders for each preset variant. Each fully populates
    // m_chain + m_luts on success and returns true.
    bool load_builtin(std::string_view name);
    bool load_glsl_file(const std::string& path);
    bool load_json_manifest(const std::string& path);

    // Helper used by load_json_manifest: bind LUT samplers and apply
    // parameter defaults to a freshly-built pass.
    void bind_pass_resources(Pass& pass);
};

// Singleton accessor — one pipeline per process is enough.
ShaderPipeline& shader_pipeline();

} // namespace foyer::libretro
