#include "shader.hpp"
#include "platform/log.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <yyjson.h>

// Static linkage for every stbi_* symbol. Some libretro cores
// (flycast, swanstation, ...) ship their own vendored stb_image.h
// with the implementation enabled inside the core's static lib.
// Without STB_IMAGE_STATIC the multiple definitions clash at link
// time when foyer_shared and the core archive get pulled into the
// same player nro.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <cstdio>
#include <cstring>
#include <vector>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace foyer::libretro {
namespace {

// ----------------------------- shaders -----------------------------------

constexpr const char* kVertexSrc = R"(#version 300 es
precision highp float;
out vec2 v_uv;
const vec2 POS[4] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0),
    vec2(-1.0,  1.0), vec2( 1.0,  1.0));
// UV mapping is NOT y-flipped. libretro frames arrive row-major top-first;
// glTexSubImage2D uploads row 0 to GL v=0 (which is the bottom edge by GL
// convention) so the source's top row ends up at the texture's v=0 row.
// Matching the clip-space Y mapping (UV[0]=(0,0) at clip (-1,-1)) makes
// the FBO bottom-left = source top-left, which is upside-down vs the
// source — then glReadPixels (also bottom-first) reads it back in the
// row order the libretro frontend expects.
const vec2 UV[4]  = vec2[](
    vec2( 0.0,  0.0), vec2( 1.0,  0.0),
    vec2( 0.0,  1.0), vec2( 1.0,  1.0));
void main() {
    gl_Position = vec4(POS[gl_VertexID], 0.0, 1.0);
    v_uv        = UV[gl_VertexID];
}
)";

// Standard preamble each fragment shader gets prepended. Mirrors the
// libretro slang preamble in spirit but spelled in GLES 3.0 syntax.
//   v_uv          — texcoord (0..1)
//   u_tex         — previous pass output (or original frame for pass 0)
//   u_size        — destination size in pixels
//   u_orig        — original emulator-frame size in pixels
//   u_frame       — frame counter (for animated shaders)
constexpr const char* kFragmentPreamble = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2  u_size;
uniform vec2  u_orig;
uniform float u_frame;
out vec4 fragColor;
)";

constexpr const char* kPresetNone = R"(
void main() { fragColor = texture(u_tex, v_uv); }
)";

constexpr const char* kPresetScanlines = R"(
void main() {
    vec4 c = texture(u_tex, v_uv);
    float row = floor(v_uv.y * u_size.y);
    float scan = mod(row, 2.0) < 1.0 ? 1.0 : 0.62;
    fragColor = vec4(c.rgb * scan, c.a);
}
)";

constexpr const char* kPresetCrtSimple = R"(
void main() {
    vec4 c = texture(u_tex, v_uv);
    float row  = floor(v_uv.y * u_size.y);
    float col  = floor(v_uv.x * u_size.x);
    float scan = mod(row, 2.0) < 1.0 ? 1.05 : 0.62;
    float ph   = mod(col, 3.0);
    vec3  mask = vec3(
        ph < 1.0 ? 1.0 : 0.78,
        (ph >= 1.0 && ph < 2.0) ? 1.0 : 0.78,
        ph >= 2.0 ? 1.0 : 0.78);
    fragColor = vec4(c.rgb * scan * mask, c.a);
}
)";

constexpr const char* kPresetLcdGrid = R"(
void main() {
    vec4 c = texture(u_tex, v_uv);
    float row = floor(v_uv.y * u_size.y);
    float col = floor(v_uv.x * u_size.x);
    bool x_lit = mod(col, 2.0) < 1.0;
    bool y_lit = mod(row, 2.0) < 1.0;
    float k = (x_lit && y_lit) ? 1.0 : 0.7;
    fragColor = vec4(c.rgb * k, c.a);
}
)";

constexpr const char* kPresetGbDmg = R"(
const vec3 PAL[4] = vec3[](
    vec3(0.608, 0.737, 0.059),
    vec3(0.545, 0.674, 0.059),
    vec3(0.188, 0.384, 0.188),
    vec3(0.059, 0.220, 0.059));
void main() {
    vec4 c = texture(u_tex, v_uv);
    float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));
    int bin = 3 - int(clamp(luma * 4.0, 0.0, 3.999));
    fragColor = vec4(PAL[bin], c.a);
}
)";

constexpr const char* kPresetGbaCorrect = R"(
void main() {
    vec4 c = texture(u_tex, v_uv);
    float r = c.r, g = c.g, b = c.b;
    float nr = 0.80 * r + 0.10 * g + 0.10 * b;
    float ng = 0.10 * r + 0.82 * g + 0.08 * b;
    float nb = 0.18 * r + 0.18 * g + 0.64 * b;
    fragColor = vec4(vec3(nr, ng, nb) * 1.10, c.a);
}
)";

struct BuiltIn {
    const char* name;
    const char* label;
    const char* body;
};
constexpr BuiltIn kBuiltIns[] = {
    { "none",        "None",                 kPresetNone       },
    { "scanlines",   "Scanlines",            kPresetScanlines  },
    { "crt_simple",  "CRT (simple)",         kPresetCrtSimple  },
    { "lcd_grid",    "LCD grid",             kPresetLcdGrid    },
    { "gb_dmg",      "Game Boy DMG",         kPresetGbDmg      },
    { "gba_correct", "GBA color correction", kPresetGbaCorrect },
};
constexpr int kBuiltInCount = sizeof(kBuiltIns) / sizeof(kBuiltIns[0]);

// ----------------------------- helpers ------------------------------------

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

unsigned compile_stage(unsigned type, const char* src) {
    const auto sh = glCreateShader(type);
    if (!sh) return 0;
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei n = 0;
        glGetShaderInfoLog(sh, sizeof(buf), &n, buf);
        foyer::log::write("[shader] compile failed (type=0x%X): %.*s\n",
            type, (int)n, buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

const BuiltIn* find_builtin(std::string_view name) {
    for (const auto& b : kBuiltIns) {
        if (name == b.name) return &b;
    }
    return nullptr;
}

std::string read_file(const std::string& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) return {};
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string strip_jsonc_comments(const std::string& in) {
    std::string out; out.reserve(in.size());
    bool in_str = false, escape = false;
    for (std::size_t i = 0; i < in.size(); i++) {
        const char c = in[i];
        if (in_str) {
            out.push_back(c);
            if (escape)         escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"')  in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; out.push_back(c); continue; }
        if (c == '/' && i + 1 < in.size() && in[i+1] == '/') {
            while (i < in.size() && in[i] != '\n') i++;
            if (i < in.size()) out.push_back(in[i]);
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::string resolve_fragment_source(const std::string& spec) {
    // Caller can give us:
    //   * a built-in name ("scanlines")
    //   * an absolute path
    //   * a path relative to /foyer/shaders/
    if (auto* b = find_builtin(spec)) return b->body;

    // Drop a leading "./" if present.
    std::string p = spec;
    if (p.size() >= 2 && p[0] == '.' && p[1] == '/') p = p.substr(2);

    if (file_exists(p)) return read_file(p);

    const std::string base = "/foyer/content/shaders/" + p;
    if (file_exists(base)) return read_file(base);

    // Maybe they passed a name without extension; try .glsl
    if (file_exists(base + ".glsl")) return read_file(base + ".glsl");

    return {};
}

} // namespace

ShaderPipeline::ShaderPipeline() = default;

ShaderPipeline::~ShaderPipeline() {
    if (m_egl_display) {
        eglMakeCurrent((EGLDisplay)m_egl_display, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        destroy_chain();
        for (auto& l : m_luts) {
            if (l.texture) glDeleteTextures(1, &l.texture);
        }
        if (m_vao)         glDeleteVertexArrays(1, &m_vao);
        if (m_in_tex)      glDeleteTextures(1, &m_in_tex);
        for (int i = 0; i < 2; i++) {
            if (m_pp_fbo[i]) glDeleteFramebuffers(1, &m_pp_fbo[i]);
            if (m_pp_tex[i]) glDeleteTextures(1, &m_pp_tex[i]);
        }
        if (m_egl_context) eglDestroyContext(
            (EGLDisplay)m_egl_display, (EGLContext)m_egl_context);
        if (m_egl_surface) eglDestroySurface(
            (EGLDisplay)m_egl_display, (EGLSurface)m_egl_surface);
    }
}

bool ShaderPipeline::init() {
    if (m_egl_context) return true;

    auto display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        foyer::log::write("[shader] eglGetDisplay failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    EGLint maj = 0, min = 0;
    if (!eglInitialize(display, &maj, &min)) {
        foyer::log::write("[shader] eglInitialize failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        foyer::log::write("[shader] eglBindAPI failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    // Switch's mesa-on-libnx doesn't advertise pbuffer-capable configs;
    // eglChooseConfig returned 0 with EGL_SUCCESS on hardware. Walk a
    // progressively-relaxed probe ladder (same shape as video_hw's
    // probe) and fall back to a surfaceless context if no pbuffer is
    // available — we render to FBOs only.
    struct Probe { EGLint renderable; EGLint surface_type; };
    const Probe probes[] = {
        { EGL_OPENGL_ES3_BIT, EGL_PBUFFER_BIT },
        { EGL_OPENGL_ES3_BIT, -1 },               // unfiltered
        { EGL_OPENGL_ES2_BIT, EGL_PBUFFER_BIT },
        { EGL_OPENGL_ES2_BIT, -1 },
    };
    EGLConfig cfg{};
    EGLint    n = 0;
    EGLint    picked_renderable  = 0;
    EGLint    picked_surface     = -1;
    bool      picked             = false;
    for (const auto& p : probes) {
        std::vector<EGLint> attrs = {
            EGL_RENDERABLE_TYPE, p.renderable,
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_ALPHA_SIZE,      8,
        };
        if (p.surface_type != -1) {
            attrs.push_back(EGL_SURFACE_TYPE);
            attrs.push_back(p.surface_type);
        }
        attrs.push_back(EGL_NONE);
        if (eglChooseConfig(display, attrs.data(), &cfg, 1, &n) && n >= 1) {
            picked_renderable = p.renderable;
            picked_surface    = p.surface_type;
            picked            = true;
            foyer::log::write(
                "[shader] config picked: gles=%s surface=%s\n",
                p.renderable == EGL_OPENGL_ES3_BIT ? "3" : "2",
                p.surface_type == EGL_PBUFFER_BIT ? "pbuffer" : "any");
            break;
        }
    }
    if (!picked) {
        foyer::log::write("[shader] eglChooseConfig: no config after %zu probes (%s)\n",
            sizeof(probes) / sizeof(probes[0]),
            egl_err_str(eglGetError()));
        return false;
    }

    // pbuffer first if the chosen config supports it; otherwise rely on
    // EGL_KHR_surfaceless_context (mesa-on-Switch ships it).
    EGLSurface surf = EGL_NO_SURFACE;
    if (picked_surface != EGL_WINDOW_BIT) {
        const EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
        surf = eglCreatePbufferSurface(display, cfg, pb_attrs);
        if (surf == EGL_NO_SURFACE) {
            foyer::log::write(
                "[shader] pbuffer unavailable (%s); trying surfaceless\n",
                egl_err_str(eglGetError()));
        }
    }
    const EGLint ctx_ver = picked_renderable == EGL_OPENGL_ES3_BIT ? 3 : 2;
    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, ctx_ver, EGL_NONE };
    auto ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        foyer::log::write("[shader] eglCreateContext(client_ver=%d) failed: %s\n",
            (int)ctx_ver, egl_err_str(eglGetError()));
        if (surf != EGL_NO_SURFACE) eglDestroySurface(display, surf);
        return false;
    }
    m_egl_display = (void*)display;
    m_egl_surface = (void*)surf;
    m_egl_context = (void*)ctx;
    if (!make_current()) return false;

    glGenVertexArrays(1, &m_vao);
    glGenTextures(1, &m_in_tex);
    glGenFramebuffers(2, m_pp_fbo);
    glGenTextures(2, m_pp_tex);

    foyer::log::write("[shader] EGL %d.%d ready\n", maj, min);
    return load_builtin("none");
}

bool ShaderPipeline::init_borrowed(void* egl_display,
                                   void* egl_context,
                                   void* egl_surface) {
    if (m_egl_context) return true;
    if (!egl_display || !egl_context) return false;
    m_egl_display = egl_display;
    m_egl_context = egl_context;
    m_egl_surface = egl_surface;

    // No make_current — the supplied context is already bound by
    // the player shell on this thread. Just allocate the GL
    // resources the pipeline needs.
    glGenVertexArrays(1, &m_vao);
    glGenTextures(1, &m_in_tex);
    glGenFramebuffers(2, m_pp_fbo);
    glGenTextures(2, m_pp_tex);

    foyer::log::write("[shader] init_borrowed ok (display=%p ctx=%p)\n",
        egl_display, egl_context);
    return load_builtin("none");
}

bool ShaderPipeline::make_current() {
    return eglMakeCurrent((EGLDisplay)m_egl_display,
                          (EGLSurface)m_egl_surface,
                          (EGLSurface)m_egl_surface,
                          (EGLContext)m_egl_context);
}

void ShaderPipeline::destroy_chain() {
    for (auto& p : m_chain) {
        if (p.program) glDeleteProgram(p.program);
    }
    m_chain.clear();
}

bool ShaderPipeline::build_program(const std::string& fragment_src, Pass& out) {
    out.program  = 0;
    out.params.clear();
    out.lut_locs.clear();

    auto vs = compile_stage(GL_VERTEX_SHADER, kVertexSrc);
    if (!vs) return false;

    const std::string full = std::string{kFragmentPreamble} + fragment_src;
    auto fs = compile_stage(GL_FRAGMENT_SHADER, full.c_str());
    if (!fs) { glDeleteShader(vs); return false; }

    auto prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[1024]; GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof(buf), &n, buf);
        foyer::log::write("[shader] link failed: %.*s\n", (int)n, buf);
        glDeleteProgram(prog);
        return false;
    }
    out.program   = prog;
    out.loc_tex   = glGetUniformLocation(prog, "u_tex");
    out.loc_size  = glGetUniformLocation(prog, "u_size");
    out.loc_orig  = glGetUniformLocation(prog, "u_orig");
    out.loc_frame = glGetUniformLocation(prog, "u_frame");
    return true;
}

bool ShaderPipeline::load_lut(const std::string& name,
                              const std::string& path, bool linear) {
    const auto bytes = read_file(path);
    if (bytes.empty()) {
        foyer::log::write("[shader] lut '%s' missing\n", path.c_str());
        return false;
    }
    int w = 0, h = 0, comp = 0;
    auto* px = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(bytes.data()),
        (int)bytes.size(), &w, &h, &comp, 4);
    if (!px) {
        foyer::log::write("[shader] lut '%s' decode failed\n", path.c_str());
        return false;
    }
    Lut l;
    l.name   = name;
    l.linear = linear;
    glGenTextures(1, &l.texture);
    glBindTexture(GL_TEXTURE_2D, l.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    foyer::log::write("[shader] lut '%s' (%s, %dx%d) loaded\n",
        name.c_str(), path.c_str(), w, h);
    m_luts.push_back(std::move(l));
    return true;
}

void ShaderPipeline::bind_pass_resources(Pass& pass) {
    glUseProgram(pass.program);
    // Texture unit 0 stays for u_tex (the input/previous-pass output).
    // LUTs occupy units 1+.
    int unit = 1;
    for (auto& lut : m_luts) {
        const std::string uname = "u_lut_" + lut.name;
        const int loc = glGetUniformLocation(pass.program, uname.c_str());
        if (loc < 0) continue;
        glUniform1i(loc, unit);
        pass.lut_locs[lut.name] = unit;
        unit++;
    }
    glUseProgram(0);
}

bool ShaderPipeline::load_builtin(std::string_view name) {
    auto* b = find_builtin(name);
    if (!b) return false;
    destroy_chain();
    Pass p;
    if (!build_program(b->body, p)) return false;
    m_chain.push_back(std::move(p));
    return true;
}

bool ShaderPipeline::load_glsl_file(const std::string& path) {
    const auto src = read_file(path);
    if (src.empty()) return false;
    destroy_chain();
    Pass p;
    if (!build_program(src, p)) return false;
    m_chain.push_back(std::move(p));
    return true;
}

bool ShaderPipeline::load_json_manifest(const std::string& path) {
    const auto raw = read_file(path);
    if (raw.empty()) return false;
    const auto stripped = strip_jsonc_comments(raw);

    auto* doc = yyjson_read(stripped.data(), stripped.size(), 0);
    if (!doc) {
        foyer::log::write("[shader] manifest parse failed: %s\n", path.c_str());
        return false;
    }
    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) { yyjson_doc_free(doc); return false; }

    // Free any LUTs from a previous preset before loading this one.
    for (auto& l : m_luts) {
        if (l.texture) glDeleteTextures(1, &l.texture);
    }
    m_luts.clear();
    destroy_chain();

    // LUTs first so per-pass uniform binding can pick them up.
    if (auto* luts = yyjson_obj_get(root, "luts");
        luts && yyjson_is_arr(luts)) {
        std::size_t i, max; yyjson_val* item;
        yyjson_arr_foreach(luts, i, max, item) {
            if (!yyjson_is_obj(item)) continue;
            std::string lname, lpath; bool linear = true;
            if (auto* x = yyjson_obj_get(item, "name");   x && yyjson_is_str(x))
                lname = yyjson_get_str(x);
            if (auto* x = yyjson_obj_get(item, "path");   x && yyjson_is_str(x))
                lpath = yyjson_get_str(x);
            if (auto* x = yyjson_obj_get(item, "filter"); x && yyjson_is_str(x))
                linear = std::strcmp(yyjson_get_str(x), "linear") == 0;
            if (lname.empty() || lpath.empty()) continue;
            if (lpath[0] != '/') lpath = "/foyer/content/shaders/" + lpath;
            load_lut(lname, lpath, linear);
        }
    }

    // Passes.
    auto* passes = yyjson_obj_get(root, "passes");
    if (!passes || !yyjson_is_arr(passes)) {
        yyjson_doc_free(doc);
        return false;
    }
    std::size_t i, max; yyjson_val* p_item;
    yyjson_arr_foreach(passes, i, max, p_item) {
        if (!yyjson_is_obj(p_item)) continue;

        std::string frag_spec;
        bool linear = false;
        if (auto* x = yyjson_obj_get(p_item, "fragment");
            x && yyjson_is_str(x)) frag_spec = yyjson_get_str(x);
        if (auto* x = yyjson_obj_get(p_item, "filter");
            x && yyjson_is_str(x))
            linear = std::strcmp(yyjson_get_str(x), "linear") == 0;
        if (frag_spec.empty()) continue;

        const auto src = resolve_fragment_source(frag_spec);
        if (src.empty()) {
            foyer::log::write("[shader] pass '%s' source not found\n",
                frag_spec.c_str());
            continue;
        }

        Pass pass;
        if (!build_program(src, pass)) continue;
        pass.filter_linear = linear;

        // Per-pass parameter values. Each declared key gets a uniform
        // location lookup; values are uploaded each frame in process().
        if (auto* params = yyjson_obj_get(p_item, "params");
            params && yyjson_is_obj(params)) {
            std::size_t j, jm; yyjson_val *k, *v;
            yyjson_obj_foreach(params, j, jm, k, v) {
                if (!yyjson_is_str(k)) continue;
                const char* key = yyjson_get_str(k);
                float val = 0.0f;
                if (yyjson_is_real(v)) val = (float)yyjson_get_real(v);
                else if (yyjson_is_int(v))  val = (float)yyjson_get_int(v);
                else continue;
                Pass::Param pp;
                pp.value = val;
                pp.loc   = glGetUniformLocation(pass.program, key);
                pass.params[key] = pp;
            }
        }

        bind_pass_resources(pass);
        m_chain.push_back(std::move(pass));
    }

    yyjson_doc_free(doc);
    return !m_chain.empty();
}

// Public entry point. Thread-safe queue-only: the actual GL work has
// to happen on the libretro_run thread (the only one that owns the
// EGL context), so just record the request here and let process()
// drain it on the next frame.
bool ShaderPipeline::set_preset(std::string_view name) {
    std::scoped_lock lk{m_pending_mu};
    m_pending_preset.assign(name);
    m_pending_preset_set = true;
    foyer::log::write("[shader] queued preset=%.*s\n",
        (int)name.size(), name.data());
    return true;
}

std::string ShaderPipeline::active() const {
    // m_active_name is only written from the libretro_run thread
    // (inside apply_preset_named, called from process()). The picker
    // reads it from the brls main thread; a torn read is harmless
    // because the picker just uses it to mark a row "Active".
    return m_active_name;
}

void ShaderPipeline::apply_pending_preset_locked() {
    std::string name;
    {
        std::scoped_lock lk{m_pending_mu};
        if (!m_pending_preset_set) return;
        name = std::move(m_pending_preset);
        m_pending_preset.clear();
        m_pending_preset_set = false;
    }
    apply_preset_named(name);
}

// Synchronous apply on the calling thread (libretro_run). Old public
// set_preset body, unchanged below the rename.
bool ShaderPipeline::apply_preset_named(std::string_view name) {
    if (!m_egl_context && !init()) return false;
    if (!make_current()) return false;

    // 1. Built-in name.
    if (find_builtin(name)) {
        if (!load_builtin(name)) return false;
        m_active_name = name;
        foyer::log::write("[shader] active=%.*s (built-in, 1 pass)\n",
            (int)name.size(), name.data());
        return true;
    }

    // 2. JSON multi-pass manifest.
    char buf[512];
    std::snprintf(buf, sizeof(buf), "/foyer/content/shaders/%.*s.json",
        (int)name.size(), name.data());
    if (file_exists(buf)) {
        if (load_json_manifest(buf)) {
            m_active_name = std::string{name};
            foyer::log::write("[shader] active=%s (manifest, %zu pass%s, %zu lut%s)\n",
                m_active_name.c_str(),
                m_chain.size(), m_chain.size() == 1 ? "" : "es",
                m_luts.size(),  m_luts.size()  == 1 ? "" : "s");
            return true;
        }
    }

    // 3. Single .glsl file.
    std::snprintf(buf, sizeof(buf), "/foyer/content/shaders/%.*s.glsl",
        (int)name.size(), name.data());
    if (file_exists(buf)) {
        if (load_glsl_file(buf)) {
            m_active_name = std::string{name};
            foyer::log::write("[shader] active=%s (single .glsl)\n", buf);
            return true;
        }
    }

    // Unknown — fall back to no-op pass-through.
    if (!load_builtin("none")) return false;
    m_active_name.clear();
    foyer::log::write("[shader] active=none (unknown name=%.*s)\n",
        (int)name.size(), name.data());
    return true;
}

bool ShaderPipeline::ensure_size(unsigned w, unsigned h) {
    if (w == m_w && h == m_h) return true;
    if (!make_current()) return false;

    auto setup = [&](unsigned tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    setup(m_in_tex);
    setup(m_pp_tex[0]);
    setup(m_pp_tex[1]);

    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_pp_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_pp_tex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            foyer::log::write("[shader] FBO %d incomplete\n", i);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_w = w;
    m_h = h;
    return true;
}

// CPU implementations of the built-in shaders. The GLES path crashes
// the player on Switch — mesa-on-nouveau and deko3d (the brls
// renderer) share the same nvhost channel under libnx, and even with
// eglMakeCurrent(NO_CONTEXT) released after each frame the GLES
// driver still corrupts the deko3d command pool inside ~1 second.
// Switch SW cores are 256×240-ish, so a per-pixel CPU loop is the
// cheapest possible fix and is plenty fast on the A57 cluster.
namespace {

inline void cpu_scanlines(std::uint8_t* p, unsigned w, unsigned h) {
    for (unsigned y = 0; y < h; ++y) {
        const bool odd = (y & 1u) != 0u;
        if (!odd) continue;  // even rows full, odd rows dim
        std::uint8_t* row = p + y * w * 4;
        for (unsigned x = 0; x < w; ++x) {
            row[x * 4 + 0] = static_cast<std::uint8_t>(row[x * 4 + 0] * 0.62f);
            row[x * 4 + 1] = static_cast<std::uint8_t>(row[x * 4 + 1] * 0.62f);
            row[x * 4 + 2] = static_cast<std::uint8_t>(row[x * 4 + 2] * 0.62f);
        }
    }
}

inline void cpu_crt_simple(std::uint8_t* p, unsigned w, unsigned h) {
    for (unsigned y = 0; y < h; ++y) {
        const float scan = (y & 1u) ? 0.62f : 1.05f;
        std::uint8_t* row = p + y * w * 4;
        for (unsigned x = 0; x < w; ++x) {
            const unsigned ph = x % 3u;
            const float mr = (ph == 0) ? 1.0f : 0.78f;
            const float mg = (ph == 1) ? 1.0f : 0.78f;
            const float mb = (ph == 2) ? 1.0f : 0.78f;
            auto cl = [&](float v, float k) {
                const float n = v * scan * k;
                return static_cast<std::uint8_t>(n < 0.0f ? 0 :
                                                 n > 255.0f ? 255 :
                                                 static_cast<int>(n));
            };
            row[x * 4 + 0] = cl(row[x * 4 + 0], mr);
            row[x * 4 + 1] = cl(row[x * 4 + 1], mg);
            row[x * 4 + 2] = cl(row[x * 4 + 2], mb);
        }
    }
}

inline void cpu_lcd_grid(std::uint8_t* p, unsigned w, unsigned h) {
    for (unsigned y = 0; y < h; ++y) {
        const bool y_lit = (y & 1u) == 0u;
        std::uint8_t* row = p + y * w * 4;
        for (unsigned x = 0; x < w; ++x) {
            const bool x_lit = (x & 1u) == 0u;
            const float k = (x_lit && y_lit) ? 1.0f : 0.70f;
            row[x * 4 + 0] = static_cast<std::uint8_t>(row[x * 4 + 0] * k);
            row[x * 4 + 1] = static_cast<std::uint8_t>(row[x * 4 + 1] * k);
            row[x * 4 + 2] = static_cast<std::uint8_t>(row[x * 4 + 2] * k);
        }
    }
}

inline void cpu_gb_dmg(std::uint8_t* p, unsigned w, unsigned h) {
    static const std::uint8_t kPal[4][3] = {
        { 155, 188,  15 },
        { 139, 172,  15 },
        {  48,  98,  48 },
        {  15,  56,  15 },
    };
    const std::uint8_t* total = p + w * h * 4;
    for (std::uint8_t* px = p; px < total; px += 4) {
        const float luma = 0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2];
        int bin = 3 - static_cast<int>((luma / 255.0f) * 4.0f);
        if (bin < 0) bin = 0; else if (bin > 3) bin = 3;
        px[0] = kPal[bin][0];
        px[1] = kPal[bin][1];
        px[2] = kPal[bin][2];
    }
}

inline void cpu_gba_correct(std::uint8_t* p, unsigned w, unsigned h) {
    const std::uint8_t* total = p + w * h * 4;
    for (std::uint8_t* px = p; px < total; px += 4) {
        const float r = px[0], g = px[1], b = px[2];
        const float nr = (0.80f * r + 0.10f * g + 0.10f * b) * 1.10f;
        const float ng = (0.10f * r + 0.82f * g + 0.08f * b) * 1.10f;
        const float nb = (0.18f * r + 0.18f * g + 0.64f * b) * 1.10f;
        auto cl = [](float v) {
            return static_cast<std::uint8_t>(v < 0.0f ? 0 :
                                             v > 255.0f ? 255 :
                                             static_cast<int>(v));
        };
        px[0] = cl(nr);
        px[1] = cl(ng);
        px[2] = cl(nb);
    }
}

}  // namespace

bool ShaderPipeline::process(std::uint8_t* pixels, unsigned w, unsigned h) {
    if (!pixels || w == 0 || h == 0) return false;
    // Drain any preset switch queued from the picker — under the CPU
    // path this is just a string move, no GL.
    apply_pending_preset_locked();

    if (m_active_name.empty() || m_active_name == "none") return true;

    // Route built-in presets through their CPU implementations. The
    // GLES pipeline is kept compiled but inert for now — see the
    // namespace comment above for why.
    if      (m_active_name == "scanlines")   cpu_scanlines  (pixels, w, h);
    else if (m_active_name == "crt_simple")  cpu_crt_simple (pixels, w, h);
    else if (m_active_name == "lcd_grid")    cpu_lcd_grid   (pixels, w, h);
    else if (m_active_name == "gb_dmg")      cpu_gb_dmg     (pixels, w, h);
    else if (m_active_name == "gba_correct") cpu_gba_correct(pixels, w, h);
    else return true;  // unknown name (e.g. user .glsl) — no-op for now
    return true;
}

bool ShaderPipeline::process_gles_unused(std::uint8_t* pixels, unsigned w, unsigned h) {
    if (!pixels || w == 0 || h == 0) return false;
    if (!m_egl_context && !init()) return false;
    if (!make_current()) return false;
    apply_pending_preset_locked();
    if (m_chain.empty()) return true;
    if (m_active_name.empty() && m_chain.size() == 1) return true;
    if (!ensure_size(w, h)) return false;

    // Upload the input frame to m_in_tex.
    glBindTexture(GL_TEXTURE_2D, m_in_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glBindVertexArray(m_vao);

    m_frame_count++;

    const std::size_t n = m_chain.size();
    for (std::size_t i = 0; i < n; i++) {
        auto& pass = m_chain[i];
        // Pass i samples from m_in_tex (i==0) or m_pp_tex[i&1] (else),
        // writes to m_pp_tex[(i+1)&1]. Last pass still writes to a
        // ping-pong target so we can glReadPixels from there.
        const unsigned src_tex = (i == 0) ? m_in_tex : m_pp_tex[i & 1];
        const unsigned dst_fbo = m_pp_fbo[(i + 1) & 1];

        // Per-pass filter: linear vs nearest sampling on the input.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        pass.filter_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        pass.filter_linear ? GL_LINEAR : GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
        glUseProgram(pass.program);

        if (pass.loc_tex   >= 0) glUniform1i(pass.loc_tex, 0);
        if (pass.loc_size  >= 0) glUniform2f(pass.loc_size, (float)w, (float)h);
        if (pass.loc_orig  >= 0) glUniform2f(pass.loc_orig, (float)w, (float)h);
        if (pass.loc_frame >= 0) glUniform1f(pass.loc_frame, (float)m_frame_count);

        // Per-pass scalar parameters.
        for (auto& [_, pp] : pass.params) {
            if (pp.loc >= 0) glUniform1f(pp.loc, pp.value);
        }
        // Bind LUT textures to the units the pass was set up for.
        for (auto& [name, unit] : pass.lut_locs) {
            for (auto& l : m_luts) {
                if (l.name == name) {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    glBindTexture(GL_TEXTURE_2D, l.texture);
                    break;
                }
            }
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Final output sits in m_pp_tex[n & 1] — bind that FBO and read.
    glBindFramebuffer(GL_FRAMEBUFFER, m_pp_fbo[n & 1]);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);

    const GLenum err = glGetError();

    // Flush the pipeline and release the EGL current binding. Switch
    // GLES (mesa-on-nouveau) and deko3d (the brls renderer) share the
    // same nvhost channel underneath; leaving the GLES context current
    // on the libretro thread starves deko3d frame submissions on the
    // brls main thread and we crash inside the core ~1 s later with
    // PC=0 (heap corruption from the driver's command-buffer pool).
    // glFinish forces the readback to complete before we hand the GPU
    // back; eglMakeCurrent(NO_CONTEXT) releases ownership cleanly.
    glFinish();
    eglMakeCurrent((EGLDisplay)m_egl_display,
                   EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    return err == GL_NO_ERROR;
}

std::vector<ShaderPipeline::PresetInfo> ShaderPipeline::available_presets() {
    std::vector<PresetInfo> out;
    out.reserve(kBuiltInCount + 8);
    for (const auto& b : kBuiltIns) {
        out.push_back({b.name, b.label});
    }
    if (auto* d = ::opendir("/foyer/content/shaders")) {
        while (auto* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() < 6) continue;
            std::string stem;
            const auto dot = n.find_last_of('.');
            if (dot == std::string::npos) continue;
            const auto ext = n.substr(dot);
            if (ext != ".glsl" && ext != ".json") continue;
            stem = n.substr(0, dot);
            if (find_builtin(stem)) continue;
            // Avoid duplicates if both .glsl and .json exist.
            bool dup = false;
            for (auto& p : out) if (p.name == stem) { dup = true; break; }
            if (dup) continue;
            out.push_back({stem, stem});
        }
        ::closedir(d);
    }
    return out;
}

unsigned ShaderPipeline::process_texture(unsigned src_tex,
                                         unsigned w, unsigned h) {
    if (!m_egl_context) return 0;
    if (src_tex == 0 || w == 0 || h == 0) return 0;

    apply_pending_preset_locked();
    if (m_chain.empty()) return 0;
    if (m_active_name.empty() || m_active_name == "none") return 0;

    // Ensure ping-pong textures + FBOs are sized for the current
    // frame. Reuses the same helper the CPU path used to (no-op if
    // size hasn't changed). NO make_current — caller owns the
    // context.
    if (m_w != w || m_h != h) {
        auto setup = [&](unsigned tex) {
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        setup(m_pp_tex[0]);
        setup(m_pp_tex[1]);
        for (int i = 0; i < 2; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_pp_fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, m_pp_tex[i], 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                foyer::log::write("[shader] process_texture: FBO %d incomplete\n", i);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return 0;
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_w = w;
        m_h = h;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glBindVertexArray(m_vao);
    m_frame_count++;

    const std::size_t n = m_chain.size();
    for (std::size_t i = 0; i < n; i++) {
        auto& pass = m_chain[i];
        // Pass 0 samples the caller's src_tex, subsequent passes
        // sample the previous ping-pong target. The destination is
        // always the other ping-pong texture.
        const unsigned src = (i == 0) ? src_tex : m_pp_tex[i & 1];
        const unsigned dst_fbo = m_pp_fbo[(i + 1) & 1];

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        pass.filter_linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        pass.filter_linear ? GL_LINEAR : GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
        glUseProgram(pass.program);
        if (pass.loc_tex   >= 0) glUniform1i(pass.loc_tex, 0);
        if (pass.loc_size  >= 0) glUniform2f(pass.loc_size,  (float)w, (float)h);
        if (pass.loc_orig  >= 0) glUniform2f(pass.loc_orig,  (float)w, (float)h);
        if (pass.loc_frame >= 0) glUniform1f(pass.loc_frame, (float)m_frame_count);
        for (auto& [_, pp] : pass.params) {
            if (pp.loc >= 0) glUniform1f(pp.loc, pp.value);
        }
        for (auto& [name, unit] : pass.lut_locs) {
            for (auto& l : m_luts) {
                if (l.name == name) {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    glBindTexture(GL_TEXTURE_2D, l.texture);
                    break;
                }
            }
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);

    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        foyer::log::write("[shader] process_texture gl_err=0x%x\n",
            (unsigned)err);
        return 0;
    }
    return m_pp_tex[n & 1];
}

ShaderPipeline& shader_pipeline() {
    static ShaderPipeline p;
    return p;
}

} // namespace foyer::libretro
