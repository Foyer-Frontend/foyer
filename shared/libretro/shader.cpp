#include "shader.hpp"
#include "platform/log.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstring>
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
const vec2 UV[4]  = vec2[](
    vec2( 0.0,  1.0), vec2( 1.0,  1.0),
    vec2( 0.0,  0.0), vec2( 1.0,  0.0));
void main() {
    gl_Position = vec4(POS[gl_VertexID], 0.0, 1.0);
    v_uv        = UV[gl_VertexID];
}
)";

// Standard preamble every fragment shader gets. Defines:
//   in  vec2 v_uv          — texcoord (0..1)
//   uniform sampler2D u_tex — emulator frame
//   uniform vec2 u_size    — frame size in pixels
//   out vec4 fragColor     — output
constexpr const char* kFragmentPreamble = R"(#version 300 es
precision highp float;
in vec2 v_uv;
uniform sampler2D u_tex;
uniform vec2 u_size;
out vec4 fragColor;
)";

// ----------------------------- built-ins ----------------------------------
// One GLSL ES 3.0 main() per preset. Real fragment shaders, not CPU
// approximations. They run on the GPU via switch-mesa's libGLESv2
// against an EGL pbuffer context — the same path video_hw.cpp uses
// for HW-render-callback cores.

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

constexpr const char* kPresetNone = R"(
void main() { fragColor = texture(u_tex, v_uv); }
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

// Compile a single shader stage; returns 0 on failure (logged).
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
    std::ifstream in{path};
    if (!in) return {};
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string user_shader_path(std::string_view name) {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "/foyer/shaders/%.*s.glsl",
        (int)name.size(), name.data());
    return std::string{buf};
}

} // namespace

ShaderPipeline::ShaderPipeline() = default;

ShaderPipeline::~ShaderPipeline() {
    if (m_egl_display) {
        eglMakeCurrent((EGLDisplay)m_egl_display, EGL_NO_SURFACE,
                       EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_program)  glDeleteProgram(m_program);
        if (m_vao)      glDeleteVertexArrays(1, &m_vao);
        if (m_vbo)      glDeleteBuffers(1, &m_vbo);
        if (m_fbo)      glDeleteFramebuffers(1, &m_fbo);
        if (m_in_tex)   glDeleteTextures(1, &m_in_tex);
        if (m_out_tex)  glDeleteTextures(1, &m_out_tex);
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
    const EGLint cfg_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_NONE
    };
    EGLConfig cfg{};
    EGLint    n = 0;
    if (!eglChooseConfig(display, cfg_attrs, &cfg, 1, &n) || n < 1) {
        foyer::log::write("[shader] eglChooseConfig failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    const EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    auto surf = eglCreatePbufferSurface(display, cfg, pb_attrs);
    if (surf == EGL_NO_SURFACE) {
        foyer::log::write("[shader] eglCreatePbufferSurface failed: %s\n",
            egl_err_str(eglGetError()));
        return false;
    }
    const EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    auto ctx = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        foyer::log::write("[shader] eglCreateContext failed: %s\n",
            egl_err_str(eglGetError()));
        eglDestroySurface(display, surf);
        return false;
    }
    m_egl_display = (void*)display;
    m_egl_surface = (void*)surf;
    m_egl_context = (void*)ctx;
    if (!make_current()) return false;

    // Empty VAO with constant attribs (vertex positions baked into
    // the vertex shader). VAO is still required by GLES3 to draw.
    glGenVertexArrays(1, &m_vao);

    glGenFramebuffers(1, &m_fbo);
    glGenTextures   (1, &m_in_tex);
    glGenTextures   (1, &m_out_tex);

    foyer::log::write("[shader] EGL %d.%d ready\n", maj, min);
    return compile_program(std::string{kPresetNone});
}

bool ShaderPipeline::make_current() {
    return eglMakeCurrent((EGLDisplay)m_egl_display,
                          (EGLSurface)m_egl_surface,
                          (EGLSurface)m_egl_surface,
                          (EGLContext)m_egl_context);
}

void ShaderPipeline::destroy_program() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_loc_tex = m_loc_size = -1;
}

bool ShaderPipeline::compile_program(const std::string& fragment_src) {
    if (!m_egl_context) return false;
    if (!make_current()) return false;

    destroy_program();

    auto vs = compile_stage(GL_VERTEX_SHADER, kVertexSrc);
    if (!vs) return false;

    const std::string full = std::string{kFragmentPreamble} + fragment_src;
    const char* p = full.c_str();
    auto fs = compile_stage(GL_FRAGMENT_SHADER, p);
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
    m_program  = prog;
    m_loc_tex  = glGetUniformLocation(prog, "u_tex");
    m_loc_size = glGetUniformLocation(prog, "u_size");
    return true;
}

bool ShaderPipeline::set_preset(std::string_view name) {
    if (!m_egl_context && !init()) return false;

    // Built-in lookup first.
    if (auto* b = find_builtin(name)) {
        if (!compile_program(b->body)) return false;
        m_active_name = b->name;
        foyer::log::write("[shader] active=%s (built-in)\n", b->name);
        return true;
    }
    // File-based fallback: /foyer/shaders/<name>.glsl
    const auto path = user_shader_path(name);
    if (file_exists(path)) {
        const auto src = read_file(path);
        if (!src.empty() && compile_program(src)) {
            m_active_name = std::string{name};
            foyer::log::write("[shader] active=%s (from %s)\n",
                m_active_name.c_str(), path.c_str());
            return true;
        }
    }
    // Unknown -> no-op pass-through.
    if (!compile_program(std::string{kPresetNone})) return false;
    m_active_name.clear();
    foyer::log::write("[shader] active=none (unknown name=%.*s)\n",
        (int)name.size(), name.data());
    return true;
}

bool ShaderPipeline::ensure_size(unsigned w, unsigned h) {
    if (w == m_w && h == m_h) return true;
    if (!make_current()) return false;
    glBindTexture(GL_TEXTURE_2D, m_in_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, m_out_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, m_out_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        foyer::log::write("[shader] FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_w = w;
    m_h = h;
    return true;
}

bool ShaderPipeline::process(std::uint8_t* pixels, unsigned w, unsigned h) {
    if (!pixels || w == 0 || h == 0) return false;
    if (m_active_name.empty() || m_active_name == "none") return true;
    if (!m_egl_context && !init()) return false;
    if (!make_current()) return false;
    if (!ensure_size(w, h)) return false;

    // Upload the input frame.
    glBindTexture(GL_TEXTURE_2D, m_in_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Render the fullscreen quad with the active fragment shader.
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    glUseProgram(m_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_in_tex);
    if (m_loc_tex  >= 0) glUniform1i(m_loc_tex, 0);
    if (m_loc_size >= 0) glUniform2f(m_loc_size, (float)w, (float)h);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Read back.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return glGetError() == GL_NO_ERROR;
}

std::vector<ShaderPipeline::PresetInfo> ShaderPipeline::available_presets() {
    std::vector<PresetInfo> out;
    out.reserve(kBuiltInCount + 4);
    for (const auto& b : kBuiltIns) {
        out.push_back({b.name, b.label});
    }
    // Discover *.glsl files under /foyer/shaders/. Display their bare
    // stem as the label so users see a recognisable list.
    if (auto* d = ::opendir("/foyer/shaders")) {
        while (auto* e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n.size() < 6 || n.substr(n.size() - 5) != ".glsl") continue;
            const auto stem = n.substr(0, n.size() - 5);
            // Skip if it shadows a built-in name (built-in wins).
            if (find_builtin(stem)) continue;
            out.push_back({stem, stem});
        }
        ::closedir(d);
    }
    return out;
}

ShaderPipeline& shader_pipeline() {
    static ShaderPipeline p;
    return p;
}

} // namespace foyer::libretro
