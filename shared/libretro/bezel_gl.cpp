#include "bezel_gl.hpp"
#include "library/config.hpp"
#include "platform/log.hpp"

#include <GLES3/gl3.h>

// stb_image: ship our own private impl in this TU with static
// linkage. shader.cpp also has STB_IMAGE_IMPLEMENTATION but its
// `STBI_NO_STDIO` flag turns off the file-path entry points we need
// here, AND its `STB_IMAGE_STATIC` makes the symbols file-local.
// Vendoring a second private copy keeps both TUs happy without
// colliding with cores that ship their own (flycast, swanstation
// etc. ship STATIC too).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace foyer::libretro {
namespace {

std::string g_folder;
std::string g_stem;

GLuint   g_tex     = 0;
GLuint   g_program = 0;
GLuint   g_vao     = 0;
GLint    g_loc_tex = -1;
bool     g_resolved = false;
std::string g_resolved_path;

bool exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string resolve_path() {
    const bool enabled = foyer::library::config().show_bezels;
    foyer::log::write(
        "[bezel_gl] resolve folder=%s stem=%s show_bezels=%d\n",
        g_folder.c_str(), g_stem.c_str(), (int)enabled);
    if (!enabled) return {};
    char buf[512];

    if (!g_folder.empty() && !g_stem.empty()) {
        const std::string bundle_dir =
            "/foyer/assets/system/" + g_folder + "/" + g_stem + "/";
        const char* bundle_candidates[] = {
            "bezel-16-9(wor).png", "bezel-16-9(us).png",
            "bezel-16-9(eu).png",  "bezel-16-9(jp).png",
            "bezel-16-9.png",
            "bezel(wor).png",      "bezel(us).png",
            "bezel(eu).png",       "bezel(jp).png",
            "bezel.png",
        };
        for (const char* name : bundle_candidates) {
            const std::string path = bundle_dir + name;
            if (exists(path)) {
                foyer::log::write("[bezel_gl] using bundle %s\n", path.c_str());
                return path;
            }
        }
    }
    if (!g_folder.empty() && !g_stem.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/content/bezels/%s/%s.png", g_folder.c_str(), g_stem.c_str());
        if (exists(buf)) return std::string{buf};
    }
    if (!g_folder.empty()) {
        std::snprintf(buf, sizeof(buf),
            "/foyer/content/bezels/%s.png", g_folder.c_str());
        if (exists(buf)) return std::string{buf};
    }
    return {};
}

constexpr const char* kVS = R"(#version 300 es
precision highp float;
out vec2 v_uv;
const vec2 kCorners[4] = vec2[](
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0));
const vec2 kUV[4] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(0.0, 1.0),
    vec2(1.0, 1.0));
void main() {
    gl_Position = vec4(kCorners[gl_VertexID], 0.0, 1.0);
    v_uv        = kUV[gl_VertexID];
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
        foyer::log::write("[bezel_gl] shader compile failed: %.*s\n",
            (int)n, buf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

void ensure_loaded() {
    if (g_resolved) return;
    g_resolved      = true;
    g_resolved_path = resolve_path();
    if (g_resolved_path.empty()) return;

    int w = 0, h = 0, n = 0;
    stbi_uc* pix = stbi_load(g_resolved_path.c_str(), &w, &h, &n, 4);
    if (!pix || w <= 0 || h <= 0) {
        foyer::log::write("[bezel_gl] stbi_load failed for %s\n",
            g_resolved_path.c_str());
        if (pix) stbi_image_free(pix);
        g_resolved_path.clear();
        return;
    }

    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pix);
    foyer::log::write("[bezel_gl] uploaded %s (%dx%d)\n",
        g_resolved_path.c_str(), w, h);
}

}  // namespace

void bezel_gl_set_rom_id(const std::string& system_folder, const std::string& rom_stem) {
    foyer::log::write("[bezel_gl] set folder=\"%s\" stem=\"%s\"\n",
        system_folder.c_str(), rom_stem.c_str());
    g_folder = system_folder;
    g_stem   = rom_stem;
    bezel_gl_invalidate();
}

bool bezel_gl_init() {
    if (g_program) return true;
    GLuint vs = compile_stage(GL_VERTEX_SHADER, kVS);
    if (!vs) return false;
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, kFS);
    if (!fs) { glDeleteShader(vs); return false; }

    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(g_program);
        g_program = 0;
        foyer::log::write("[bezel_gl] program link failed\n");
        return false;
    }
    g_loc_tex = glGetUniformLocation(g_program, "u_tex");
    glGenVertexArrays(1, &g_vao);
    return true;
}

void bezel_gl_draw(float /*screen_w*/, float /*screen_h*/) {
    ensure_loaded();
    if (g_tex == 0 || g_program == 0) return;

    glUseProgram(g_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    if (g_loc_tex >= 0) glUniform1i(g_loc_tex, 0);
    glBindVertexArray(g_vao);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void bezel_gl_invalidate() {
    if (g_tex) glDeleteTextures(1, &g_tex);
    g_tex = 0;
    g_resolved = false;
    g_resolved_path.clear();
}

void bezel_gl_shutdown() {
    bezel_gl_invalidate();
    if (g_vao)     { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
    if (g_program) { glDeleteProgram(g_program); g_program = 0; }
}

}  // namespace foyer::libretro
