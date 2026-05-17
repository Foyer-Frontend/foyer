#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
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
    //
    // Standalone init (creates its own EGL context). Used by the
    // legacy CPU shader path under PLAYER_BRLS — that path never
    // actually calls into GL, so init() is only kept here to keep
    // the API surface stable while the brls player gets retired.
    bool init();

    // Borrowed init: takes an EGL display + context + surface owned
    // by the caller (the ImGui player shell). The pipeline does NOT
    // call eglCreateContext / eglCreatePbufferSurface — it just
    // sticks the handles into m_egl_* and runs the same GL-resource
    // bring-up as init() (VAO, FBOs, ping-pong textures). Must be
    // called while the supplied context is already current on the
    // calling thread. Used by PLAYER_IMGUI; share the one context
    // we own for everything (no nvhost contention).
    bool init_borrowed(void* egl_display,
                       void* egl_context,
                       void* egl_surface);

    // Switch to a preset by name. Resolution order: built-in →
    // <name>.json (multi-pass) → <name>.glsl (single-pass) →
    // fall through to a no-op chain. Safe to call from any thread —
    // the actual GL work is deferred to the libretro_run thread on
    // the next process() call.
    bool set_preset(std::string_view name);

    // Currently active preset name. "" if no shader is active. Read
    // from any thread (the picker reads it to mark "Active").
    std::string active() const;

    // Process one frame in place. `pixels` is RGBA8 of size w*h*4.
    // Returns false on any GL error; pixels are unchanged in that
    // case. No-op (returns true unchanged) when the active preset
    // is "none" or the chain is empty.
    //
    // CPU implementation path for PLAYER_BRLS (mesa-on-Switch GLES
    // can't coexist with deko3d on the same nvhost channel). The
    // ImGui shell should call process_texture() instead.
    bool process(std::uint8_t* pixels, unsigned w, unsigned h);

    // GL-native processing path for PLAYER_IMGUI. Takes a source
    // GLES texture (RGBA8, the libretro frame upload), runs the
    // active chain, returns the GL texture handle holding the final
    // output. Zero CPU readback. The returned texture is owned by
    // the pipeline (one of m_pp_tex[]) — caller must NOT delete it.
    // Returns 0 when no shader is active OR on any GL error (caller
    // falls back to sampling `src_tex` directly).
    unsigned process_texture(unsigned src_tex, unsigned w, unsigned h);

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

    // set_preset is called from the brls main thread (picker click),
    // but every GL call needs to happen on the libretro_run thread
    // that owns the EGL context. eglMakeCurrent allows the context to
    // be current on only one thread at a time, so doing make_current
    // from main while process() runs on libretro_run yanks the
    // context out from under the in-flight GL calls and we crash
    // inside the core ~1 frame later (PC=0). Queue the preset name
    // here instead; process() applies it on the libretro_run thread
    // at the start of the next frame.
    std::mutex   m_pending_mu;
    std::string  m_pending_preset;
    bool         m_pending_preset_set = false;

    bool make_current();
    bool ensure_size(unsigned w, unsigned h);
    void destroy_chain();
    bool build_program(const std::string& fragment_src, Pass& out);
    bool load_lut(const std::string& name, const std::string& path,
                  bool linear);

    // Apply m_pending_preset (drained under m_pending_mu) on the
    // current thread. Called from process() at the start of every
    // frame so the GL work always lands on the libretro_run thread
    // that owns the EGL context.
    void apply_pending_preset_locked();

    // Synchronous preset apply. Runs the resolution chain (built-in
    // -> .json -> .glsl) and does the GL work to populate m_chain.
    // Must be called on the thread that owns the EGL context.
    bool apply_preset_named(std::string_view name);

    // Old full-GLES process path. Kept compiled (referenced in
    // shader.cpp) but unused: the brls player runs a CPU shader
    // loop in process(), and the ImGui player will own the GL
    // context end-to-end so Phase 3 will rewrite this into a
    // process_texture(GLuint) entry point. The declaration lives
    // here only to satisfy the linker for the dormant body in
    // shader.cpp.
    bool process_gles_unused(std::uint8_t* pixels, unsigned w, unsigned h);

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
