#include "gl_renderer.hpp"
#include "texture_pack.hpp"

#include "dw4/engine/gx_color.hpp"
#include "dw4/textures.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace dw4::game {

#define DW4_GAME_GL(X) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) X(PFNGLBINDBUFFERPROC, BindBuffer) X(PFNGLBUFFERDATAPROC, BufferData) \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, DisableVertexAttribArray) X(PFNGLVERTEXATTRIB4FPROC, VertexAttrib4f) \
    X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader) X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
    X(PFNGLUNIFORM1IPROC, Uniform1i) X(PFNGLUNIFORM1FPROC, Uniform1f) X(PFNGLUNIFORM4FPROC, Uniform4f) \
    X(PFNGLUNIFORM3FVPROC, Uniform3fv) X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) X(PFNGLBLENDEQUATIONPROC, BlendEquation) \
    X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap) X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers)

struct GlRenderer::Impl {
#define DECLARE(type, name) type name{};
    DW4_GAME_GL(DECLARE)
#undef DECLARE
    Impl() {
#define LOAD(type, name) name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); \
        if (!name) throw std::runtime_error("Game renderer requires gl" #name);
        DW4_GAME_GL(LOAD)
#undef LOAD
    }
    int u_model{}, u_view{}, u_proj{}, u_color{}, u_modulate{}, u_use_vcol{}, u_use_tex{}, u_alpha_func{}, u_alpha_ref{};
    int u_lighting{}, u_light_dir{}, u_light_col{}, u_light_count{}, u_diffuse{}, u_tex{};
    int u_light_ambient{};
    int u_fog{}, u_fog_color{}, u_fog_range{}, u_skinned{}, u_palette{}, u_tex_matrix{};
    int u_shadow_matrix{}, u_shadow_tex{}, u_shadow_pass{};
    int u_texgen{}, u_pixel_center{}, u_normal{};
    int u_id_mode{}, u_id{};
    PFNGLUNIFORM2FPROC Uniform2f{};
    // GX samples a pixel at 7/12 of its width and height instead of GL's 1/2
    // (unless antialiasing); Dolphin's VertexShaderManager compensates with
    // pixel_center_correction = 7/12 - 1/2, moving every vertex 1/12 of a
    // render pixel left and up. Clip-space offset for the current viewport.
    void pixel_center(int width, int height) {
        constexpr float kCorrection = 7.0f / 12.0f - 0.5f;
        if (width > 0 && height > 0) Uniform2f(u_pixel_center, -kCorrection * 2.0f / float(width), kCorrection * 2.0f / float(height));
    }
};

namespace {
const char* kVertex = R"(#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec4 a_color;
layout(location=3) in vec2 a_uv;
layout(location=4) in vec2 a_weights;
layout(location=5) in vec2 a_indices;
uniform mat4 u_model, u_view, u_proj;
uniform mat4 u_normal;
uniform int u_skinned;
uniform mat4 u_palette[64];
uniform mat4 u_tex_matrix;
uniform int u_texgen;
uniform int u_use_vcol;
uniform vec4 u_color;
uniform int u_lighting;
uniform vec3 u_light_dir[4];
uniform vec3 u_light_col[4];
uniform int u_light_count;
uniform vec3 u_light_ambient;
uniform vec4 u_diffuse;
uniform mat4 u_shadow_matrix;
uniform vec2 u_pixel_center;
out vec4 v_color;
out vec2 v_uv;
out float v_depth;
out vec4 v_shadow;
void main() {
    vec4 local = vec4(a_pos, 1.0);
    vec3 normal = a_normal;
    if (u_skinned != 0) {
        // A blended igVertexArray (visual context flag 0x10, 0x800FAD00) is
        // skinned on the CPU in the immediate draw 0x800EDD24. Positions sum
        // w_k * (p * M[i_k]) over the weights up to the first zero weight
        // (0x800F00FC..0x800F01B4; one weight per vertex: p * M[i0] alone,
        // 0x800F01BC, Mesh sets that weight to 1). The normal (flag 0x20,
        // 0x800F03D8..0x800F044C) goes through the 3x3 of the FIRST blend
        // index only (lbzx at 0x800F03FC), unweighted and not normalised on
        // the CPU (the XF's normal matrix and normalisation follow, see the
        // lighting below); so do the NBT vectors (flag 0x200,
        // 0x800F045C..0x800F0588) and the pre-blended buffer path
        // (0x800FA968, normals through 0x800ED4A0).
        // A vertex between two bones keeps the first bone's shading
        // (s_weregaru's shoulders and tail, the wings of s_impdrafm/pm).
        int i0 = int(a_indices.x + 0.5), i1 = int(a_indices.y + 0.5);
        vec4 p = vec4(0.0);
        if (a_weights.x != 0.0) {
            p = u_palette[i0] * local * a_weights.x;
            if (a_weights.y != 0.0) p += u_palette[i1] * local * a_weights.y;
        }
        local = vec4(p.xyz, 1.0);
        normal = mat3(u_palette[i0]) * a_normal;
    }
    vec4 world = u_model * local;
    v_shadow = u_shadow_matrix * world;
    vec4 eye = u_view * world;
    v_depth = -eye.z;
    gl_Position = u_proj * eye;
    // GX pixel centre at 7/12 (see Impl::pixel_center): GL y points up.
    gl_Position.xy += gl_Position.w * u_pixel_center;
    vec4 base = u_use_vcol != 0 ? a_color : u_color;
    if (u_lighting != 0) {
        // GX channel as Alchemy sets it up (800F6DC0): the material colour
        // times the clamped sum of the ambient register and each light's
        // colour times its diffuse term (the registers come in prepared).
        // The normal goes through the normal matrix the modelview flush
        // loads (u_normal, see the draw loop) and is normalised before the
        // lighting, as in Dolphin's vertex shader (_normal = normalize(N *
        // rawnormal): the lighting needs the unit normal, only the emboss
        // texgen sees the matrix's scale). A uniformly scaled model is lit
        // like an unscaled one.
        vec3 n = normalize(mat3(u_normal) * normal);
        vec3 lit = u_light_ambient;
        for (int i = 0; i < u_light_count; ++i)
            lit += u_light_col[i] * max(dot(n, -normalize(u_light_dir[i])), 0.0);
        vec4 material = u_use_vcol != 0 ? a_color : u_diffuse;
        base = vec4(clamp(lit, 0.0, 1.0) * material.rgb, material.a);
    }
    v_color = base;
    if (u_texgen == 2) {
        // igTextureCoordSourceAttr source 2 (800FCEA8 case 2): GX texgen
        // from the normal through the modelview rotation (translation 0),
        // normalised, then the post matrix MTXTrans(0.5, 0.5, 1) *
        // MTXScale(0.5, -0.5, 0) (GX_PTTEXMTX0); the texture matrix stack is
        // not used.
        vec3 nv = normalize(mat3(u_view) * (mat3(u_model) * normal));
        v_uv = vec2(0.5 + 0.5 * nv.x, 0.5 - 0.5 * nv.y);
    } else {
        v_uv = (u_tex_matrix * vec4(a_uv, 0.0, 1.0)).xy;
    }
}
)";
const char* kFragment = R"(#version 330 core
in vec4 v_color;
in vec2 v_uv;
in float v_depth;
in vec4 v_shadow;
uniform sampler2D u_shadow_tex;
uniform int u_shadow_pass;
uniform int u_fog;
uniform vec4 u_fog_color;
uniform vec2 u_fog_range;
uniform sampler2D u_tex;
uniform int u_use_tex;
uniform vec4 u_modulate;
uniform int u_alpha_func;
uniform float u_alpha_ref;
uniform int u_id_mode;
uniform vec4 u_id;
out vec4 o_color;
void main() {
    // Receiver pass (801FE190): the light's map is the multiplier (white
    // where lit, _shadowFactor inside a shadow), MODULATE with white.
    // v_shadow is already the map coordinate; outside the light box the
    // clamped lookup lands on the map's white border (GX has no range test).
    if (u_shadow_pass == 2) {
        o_color = vec4(vec3(texture(u_shadow_tex, v_shadow.xy / v_shadow.w).r), 1.0);
        return;
    }
    vec4 c = v_color;
    if (u_use_tex != 0) c *= texture(u_tex, v_uv);
    c *= u_modulate;
    // GX alpha compare (800F8FB0): the 8-bit TEV alpha against the 8-bit
    // reference (int)(255.0 * ref); IG functions map 1:1 (table 0x80490530).
    float a = floor(c.a * 255.0 + 0.5);
    float r = floor(u_alpha_ref * 255.0);
    bool pass = true;
    if (u_alpha_func == 0) pass = false;
    else if (u_alpha_func == 1) pass = a < r;
    else if (u_alpha_func == 2) pass = a == r;
    else if (u_alpha_func == 3) pass = a <= r;
    else if (u_alpha_func == 4) pass = a > r;
    else if (u_alpha_func == 5) pass = a != r;
    else if (u_alpha_func == 6) pass = a >= r;
    if (!pass) discard;
    // Dev item-id pass (set_id_pass): the draw item's number.
    if (u_id_mode != 0) { o_color = u_id; return; }
    // Shadow map pass: casters in the _shadowColor grey (u_modulate) on the
    // white map, unlit and untextured.
    if (u_shadow_pass != 0) { o_color = vec4(u_modulate.rgb, 1.0); return; }
    if (u_fog != 0) {
        float f = clamp((u_fog_range.y - v_depth) / max(u_fog_range.y - u_fog_range.x, 0.001), 0.0, 1.0);
        c.rgb = mix(u_fog_color.rgb, c.rgb, f);
    }
    o_color = c;
}
)";

// The movie picture: a screen-filling strip from the vertex index.
const char* kMovieVertex = R"(#version 330 core
out vec2 v_uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    v_uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// The kept picture: the window as copied, rows bottom first.
const char* kKeptVertex = R"(#version 330 core
out vec2 v_uv;
void main() {
    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kMovieFragment = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_tex;
out vec4 o_color;
void main() { o_color = vec4(texture(u_tex, v_uv).rgb, 1.0); }
)";

unsigned compile(GlRenderer::Impl& gl, unsigned type, const char* source) {
    const unsigned shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    int ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl.GetShaderInfoLog(shader, sizeof log, nullptr, log);
        throw std::runtime_error(std::string("shader: ") + log);
    }
    return shader;
}

GLenum blend_factor(int value) {
    switch (value) {
    case 0: return GL_ZERO;
    case 1: return GL_ONE;
    case 2: return GL_SRC_COLOR;
    case 3: return GL_ONE_MINUS_SRC_COLOR;
    case 4: return GL_SRC_ALPHA;
    case 5: return GL_ONE_MINUS_SRC_ALPHA;
    case 6: return GL_DST_COLOR;
    case 7: return GL_ONE_MINUS_DST_COLOR;
    case 8: return GL_DST_ALPHA;
    case 9: return GL_ONE_MINUS_DST_ALPHA;
    case 10: return GL_SRC_ALPHA_SATURATE;
    default: return GL_ONE;
    }
}

GLenum depth_func(int value) {
    static const GLenum table[] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return value >= 0 && value < 8 ? table[value] : GL_LEQUAL;
}
} // namespace

GlRenderer::GlRenderer() : gl_(std::make_unique<Impl>()) {
    auto& gl = *gl_;
    const unsigned vs = compile(gl, GL_VERTEX_SHADER, kVertex);
    const unsigned fs = compile(gl, GL_FRAGMENT_SHADER, kFragment);
    program_ = gl.CreateProgram();
    gl.AttachShader(program_, vs);
    gl.AttachShader(program_, fs);
    gl.LinkProgram(program_);
    int ok = 0;
    gl.GetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) throw std::runtime_error("game shader link failed");
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
#define U(name) gl.u_##name = gl.GetUniformLocation(program_, "u_" #name);
    U(model) U(view) U(proj) U(color) U(modulate) U(use_vcol) U(use_tex) U(alpha_func) U(alpha_ref)
    U(lighting) U(light_dir) U(light_col) U(light_count) U(diffuse) U(tex) U(light_ambient)
    U(fog) U(fog_color) U(fog_range) U(skinned) U(palette) U(tex_matrix)
    U(shadow_matrix) U(shadow_tex) U(shadow_pass) U(texgen) U(pixel_center) U(normal) U(id_mode) U(id)
#undef U
    gl.UseProgram(program_);
    const engine::Mat4 identity{};
    gl.UniformMatrix4fv(gl.u_normal, 1, GL_FALSE, identity.m.data());
    gl.UseProgram(0);
    gl.Uniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(SDL_GL_GetProcAddress("glUniform2f"));
    glGenTextures(1, &white_texture_);
    glBindTexture(GL_TEXTURE_2D, white_texture_);
    const std::uint32_t white = 0xffffffffu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
}

GlRenderer::~GlRenderer() {
    clear_cache();
    if (shadow_fbo_) gl_->DeleteFramebuffers(1, &shadow_fbo_);
    if (shadow_texture_) glDeleteTextures(1, &shadow_texture_);
    if (white_texture_) glDeleteTextures(1, &white_texture_);
    if (movie_texture_) glDeleteTextures(1, &movie_texture_);
    if (kept_texture_) glDeleteTextures(1, &kept_texture_);
    if (kept_program_) gl_->DeleteProgram(kept_program_);
    if (movie_vao_) gl_->DeleteVertexArrays(1, &movie_vao_);
    if (movie_program_) gl_->DeleteProgram(movie_program_);
    if (program_) gl_->DeleteProgram(program_);
}

void GlRenderer::draw_movie(const std::uint8_t* rgba, int width, int height, const Viewport& screen, int window_width,
                            int window_height) {
    auto& gl = *gl_;
    if (!movie_program_) {
        const unsigned vs = compile(gl, GL_VERTEX_SHADER, kMovieVertex);
        const unsigned fs = compile(gl, GL_FRAGMENT_SHADER, kMovieFragment);
        movie_program_ = gl.CreateProgram();
        gl.AttachShader(movie_program_, vs);
        gl.AttachShader(movie_program_, fs);
        gl.LinkProgram(movie_program_);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        int ok = 0;
        gl.GetProgramiv(movie_program_, GL_LINK_STATUS, &ok);
        if (!ok) throw std::runtime_error("movie shader link failed");
        if (!movie_vao_) gl.GenVertexArrays(1, &movie_vao_);
        glGenTextures(1, &movie_texture_);
    }
    glBindTexture(GL_TEXTURE_2D, movie_texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != movie_width_ || height != movie_height_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        movie_width_ = width;
        movie_height_ = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(screen.x, screen.y, screen.width, screen.height);
    gl.UseProgram(movie_program_);
    gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, movie_texture_);
    gl.Uniform1i(gl.GetUniformLocation(movie_program_, "u_tex"), 0);
    gl.BindVertexArray(movie_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindVertexArray(0);
    gl.UseProgram(0);
    glViewport(0, 0, window_width, window_height);
}

void GlRenderer::keep_picture(int window_width, int window_height) {
    if (window_width <= 0 || window_height <= 0) return;
    if (!kept_texture_) glGenTextures(1, &kept_texture_);
    glBindTexture(GL_TEXTURE_2D, kept_texture_);
    if (window_width != kept_width_ || window_height != kept_height_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, window_width, window_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        kept_width_ = window_width;
        kept_height_ = window_height;
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, window_width, window_height);
}

bool GlRenderer::draw_kept_picture(int window_width, int window_height) {
    if (!kept_texture_ || window_width != kept_width_ || window_height != kept_height_) return false;
    auto& gl = *gl_;
    if (!kept_program_) {
        const unsigned vs = compile(gl, GL_VERTEX_SHADER, kKeptVertex);
        const unsigned fs = compile(gl, GL_FRAGMENT_SHADER, kMovieFragment);
        kept_program_ = gl.CreateProgram();
        gl.AttachShader(kept_program_, vs);
        gl.AttachShader(kept_program_, fs);
        gl.LinkProgram(kept_program_);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        int ok = 0;
        gl.GetProgramiv(kept_program_, GL_LINK_STATUS, &ok);
        if (!ok) throw std::runtime_error("kept picture shader link failed");
    }
    if (!movie_vao_) gl.GenVertexArrays(1, &movie_vao_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, window_width, window_height);
    gl.UseProgram(kept_program_);
    gl.ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, kept_texture_);
    gl.Uniform1i(gl.GetUniformLocation(kept_program_, "u_tex"), 0);
    gl.BindVertexArray(movie_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindVertexArray(0);
    gl.UseProgram(0);
    return true;
}

void GlRenderer::clear_cache() {
    auto& gl = *gl_;
    for (auto& [_, m] : meshes_) {
        gl.DeleteBuffers(1, &m.vbo);
        gl.DeleteBuffers(1, &m.ibo);
        gl.DeleteVertexArrays(1, &m.vao);
    }
    meshes_.clear();
    for (auto& [_, t] : textures_) if (t.id) glDeleteTextures(1, &t.id);
    textures_.clear();
}

void GlRenderer::release(Mesh& m) {
    auto& gl = *gl_;
    gl.DeleteBuffers(1, &m.vbo);
    gl.DeleteBuffers(1, &m.ibo);
    gl.DeleteVertexArrays(1, &m.vao);
    m = Mesh{};
}

const GlRenderer::Mesh* GlRenderer::mesh(const engine::GeometryData* g) {
    // The cache is keyed by address; a different serial means a new geometry
    // (e.g. replaced text digits) now lives at a freed address.
    if (auto found = meshes_.find(g); found != meshes_.end()) {
        if (found->second.serial == g->serial.value) {
            found->second.last_frame = frame_;
            return &found->second;
        }
        release(found->second);
        meshes_.erase(found);
    }
    auto& gl = *gl_;
    Mesh m;
    m.serial = g->serial.value;
    m.last_frame = frame_;
    m.has_colors = !g->colors.empty();
    m.has_uvs = !g->texcoords.empty();
    m.has_normals = !g->normals.empty();
    const std::size_t n = g->positions.size();
    // Interleaved: pos(12) normal(12) color(4) uv(8) weights(8) indices(8) = 52 bytes
    constexpr std::size_t stride = 52;
    std::vector<std::uint8_t> data(n * stride, 0);
    const std::size_t wc = g->weight_count;
    for (std::size_t i = 0; i < n; ++i) {
        auto* p = data.data() + i * stride;
        std::memcpy(p, &g->positions[i], 12);
        if (m.has_normals) std::memcpy(p + 12, &g->normals[i], 12);
        const std::uint32_t color = m.has_colors ? g->colors[i] : 0xffffffffu;
        std::memcpy(p + 24, &color, 4);
        if (m.has_uvs) std::memcpy(p + 28, &g->texcoords[i * 2], 8);
        float weights[2]{1.0f, 0.0f}, indices[2]{0.0f, 0.0f};
        for (std::size_t k = 0; k < wc && k < 2; ++k) {
            weights[k] = g->weights[i * wc + k];
            indices[k] = float(g->blend_indices[i * wc + k]);
        }
        // One weight per vertex: 0x800F01BC transforms by the bone without
        // reading the weight.
        if (wc == 1) weights[0] = 1.0f;
        std::memcpy(p + 36, weights, 8);
        std::memcpy(p + 44, indices, 8);
    }
    gl.GenVertexArrays(1, &m.vao);
    gl.BindVertexArray(m.vao);
    gl.GenBuffers(1, &m.vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, m.vbo);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size()), data.data(), GL_STATIC_DRAW);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(24));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(28));
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(36));
    gl.EnableVertexAttribArray(5);
    gl.VertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(44));
    gl.GenBuffers(1, &m.ibo);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(g->indices.size() * 4), g->indices.data(), GL_STATIC_DRAW);
    m.index_count = static_cast<int>(g->indices.size());
    gl.BindVertexArray(0);
    return &meshes_.emplace(g, m).first->second;
}

const GlRenderer::Texture* GlRenderer::texture(const engine::Attribute* binding) {
    if (!binding || !binding->image) return nullptr;
    const auto key = std::make_pair(static_cast<const void*>(binding->image.document()), binding->image.reference());
    if (auto found = textures_.find(key); found != textures_.end()) return found->second.id ? &found->second : nullptr;
    Texture t;
    try {
        const auto image = decode_igb_texture(*binding->image.document(), binding->image.reference());
        int width = int(image.width), height = int(image.height);
        const std::uint8_t* pixels = image.rgba.data();
        std::optional<TextureImage> replaced;
        if (pack_) {
            // Texture packs: dumps and replacements by pixel hash. CMPR
            // textures keep the hash of the earlier decode with black
            // transparent texels, so existing packs still match them.
            std::uint64_t hash;
            if (image.block_compressed) {
                auto earlier = image.rgba;
                for (std::size_t i = 0; i + 3 < earlier.size(); i += 4)
                    if (earlier[i + 3] == 0) earlier[i] = earlier[i + 1] = earlier[i + 2] = 0;
                hash = TexturePack::hash(earlier.data(), earlier.size(), width, height);
            } else {
                hash = TexturePack::hash(pixels, image.rgba.size(), width, height);
            }
            const std::string name(binding->image.str(22));
            pack_->dump(name, pixels, width, height, hash);
            replaced = pack_->replacement(hash);
            if (replaced) {
                width = replaced->width;
                height = replaced->height;
                pixels = replaced->rgba.data();
            }
        }
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        // The GX texture object (800FE744) is mipmapped only when the texture
        // carries mip images: GXInitTexObj(mipmap = count != 0) at 0x800FEDE4,
        // GXInitTexObjLOD(max_lod = count) at 0x800FEE50. Without them GX
        // samples level 0 only, whatever the min filter says. The game's IGBs
        // never store mip images (igTextureAttr _imageCount 1, empty
        // _imageMipMaps), so the original's textures stay sharp when shrunk.
        int levels = 0;
        if (!replaced) {
            if (const auto mips = binding->texture.ref(16)) {
                int w = width, h = height;
                for (const auto& mip : mips.elements()) {
                    if (!mip || (w <= 1 && h <= 1)) break;
                    w = std::max(w / 2, 1), h = std::max(h / 2, 1);
                    const auto level = decode_igb_texture(*mip.document(), mip.reference());
                    if (int(level.width) != w || int(level.height) != h) break;
                    glTexImage2D(GL_TEXTURE_2D, levels + 1, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, level.rgba.data());
                    ++levels;
                }
            }
        } else if (width > int(image.width) || height > int(image.height)) {
            // A larger texture-pack replacement is not original data; mipmap
            // it so it does not shimmer when drawn at the original size.
            gl_->GenerateMipmap(GL_TEXTURE_2D);
            levels = 1000;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels);
        const auto& s = binding->sampler;
        // GX wraps (texture_sampler): 0 CLAMP, 1 REPEAT.
        auto wrap = [](int v) { return v == 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT; };
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(s.wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(s.wrap_t));
        // IG_GFX_TEXTURE_FILTER: 0 NEAREST, 1 LINEAR, 2 NEAREST_MIPMAP_NEAREST,
        // 3 NEAREST_MIPMAP_LINEAR, 4 LINEAR_MIPMAP_NEAREST, 5 LINEAR_MIPMAP_LINEAR
        // (min -> GX table 0x80414A40, mag table 0x80414A58 = nearest for 0/2/3).
        static const GLenum min_filters[] = {GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                                             GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR};
        const bool mag_nearest = s.mag_filter == 0 || s.mag_filter == 2 || s.mag_filter == 3;
        GLenum min_filter = s.min_filter >= 0 && s.min_filter < 6 ? min_filters[s.min_filter] : GL_LINEAR;
        if (replaced && levels) min_filter = GL_LINEAR_MIPMAP_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    } catch (const std::exception& error) {
        std::cerr << "texture decode failed: " << error.what() << '\n';
        t.id = 0;
    }
    auto& stored = textures_.emplace(key, t).first->second;
    return stored.id ? &stored : nullptr;
}

// igProjectiveShadowShader: the casters from the light into the map.
void GlRenderer::render_shadow_map() {
    auto& gl = *gl_;
    shadow_ready_ = false;
    if (!shadow_ || !shadow_->active || shadow_->casters.empty()) return;
    if (!shadow_fbo_ || shadow_width_ != shadow_->width || shadow_height_ != shadow_->height) {
        if (!shadow_texture_) glGenTextures(1, &shadow_texture_);
        glBindTexture(GL_TEXTURE_2D, shadow_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, shadow_->width, shadow_->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (!shadow_fbo_) gl.GenFramebuffers(1, &shadow_fbo_);
        gl.BindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shadow_texture_, 0);
        const bool complete = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete) return;
        shadow_width_ = shadow_->width;
        shadow_height_ = shadow_->height;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, shadow_width_, shadow_height_);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // 801FD040 (0x801FD1D0): the casters go into _shadowInnerViewport, the
    // map inset by the border on every side.
    const int border = shadow_->border;
    glViewport(border, border, shadow_width_ - 2 * border, shadow_height_ - 2 * border);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.UseProgram(program_);
    gl.pixel_center(shadow_width_ - 2 * border, shadow_height_ - 2 * border);
    gl.Uniform1i(gl.u_shadow_pass, 1);
    gl.Uniform1i(gl.u_lighting, 0);
    gl.Uniform1i(gl.u_fog, 0);
    gl.Uniform1i(gl.u_use_tex, 0);
    gl.Uniform1i(gl.u_alpha_func, 7);
    // 801FDCCC (0x801FDD10..0x801FDD3C): _shadowColor = (f, f, f, 1) with
    // f = _shadowFactor, pushed while _useShadowFactor (default 1, never
    // serialized) is set; it is an igColorAttr (setColor, vtable +0x80), so
    // GX gets f rounded to 8 bits.
    const float shadow_grey = engine::gx_round(shadow_->factor);
    gl.Uniform4f(gl.u_modulate, shadow_grey, shadow_grey, shadow_grey, 1);
    gl.UniformMatrix4fv(gl.u_view, 1, GL_FALSE, shadow_->light_view.m.data());
    gl.UniformMatrix4fv(gl.u_proj, 1, GL_FALSE, shadow_->light_projection.m.data());
    const engine::Mat4 identity{};
    gl.UniformMatrix4fv(gl.u_tex_matrix, 1, GL_FALSE, identity.m.data());
    gl.Uniform1i(gl.u_texgen, 0);
    for (const auto& item : shadow_->casters) {
        const auto* m = mesh(item.geometry);
        if (!m || !m->index_count) continue;
        gl.UniformMatrix4fv(gl.u_model, 1, GL_FALSE, item.model.m.data());
        if (item.palette && !item.palette->empty()) {
            const auto count = std::min<std::size_t>(item.palette->size(), 64);
            gl.UniformMatrix4fv(gl.u_palette, GLsizei(count), GL_FALSE, item.palette->front().m.data());
            gl.Uniform1i(gl.u_skinned, 1);
        } else {
            gl.Uniform1i(gl.u_skinned, 0);
        }
        // 801FDCCC locks (801D5190) only igLightingStateAttr, igDepthStateAttr,
        // the unit-0 igTextureStateAttr, _shadowColor and the viewport; every
        // caster keeps its own blending (always added, as in the main pass)
        // and its own culling. An additive one (stagBS06 f_devl31 ring1 /
        // ring2: SRC_ALPHA, ONE) adds its grey to the white map and casts
        // nothing.
        const auto& s = item.state;
        if (s.blend_enabled) {
            glEnable(GL_BLEND);
            gl.BlendFuncSeparate(blend_factor(s.blend_src), blend_factor(s.blend_dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            gl.BlendEquation(GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        if (s.cull_enabled) {
            glEnable(GL_CULL_FACE);
            glCullFace(s.cull_mode == 1 ? GL_FRONT : s.cull_mode == 2 ? GL_FRONT_AND_BACK : GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        gl.BindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    gl.Uniform1i(gl.u_shadow_pass, 0);
    if (static const bool trace = std::getenv("DW4_TRACE_SHADOW") != nullptr; trace && frame_ % 120 == 0) {
        std::vector<std::uint8_t> pixels(std::size_t(shadow_width_ * shadow_height_) * 4);
        glReadPixels(0, 0, shadow_width_, shadow_height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::size_t dark = 0;
        int x0 = shadow_width_, y0 = shadow_height_, x1 = -1, y1 = -1;
        for (int y = 0; y < shadow_height_; ++y)
            for (int x = 0; x < shadow_width_; ++x)
                if (pixels[std::size_t(y * shadow_width_ + x) * 4] < 255) {
                    ++dark;
                    x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
                }
        std::cerr << "shadow map: " << dark << " caster texels in x " << x0 << ".." << x1 << " y " << y0 << ".." << y1 << std::endl;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    shadow_ready_ = true;
}

// The receivers multiply the map over the opaque scene drawn so far.
void GlRenderer::render_shadow_receivers(const engine::FrameCamera& camera) {
    auto& gl = *gl_;
    if (!shadow_ready_ || shadow_->receivers.empty()) return;
    // 801FE504: the texture matrix is light view * projection * the bias
    // (0.5, 0.5) * the border matrix flt_804F8CC8 (scale (W - 2b) / W, offset
    // b / W). GL's map rows run bottom-up, so t keeps +0.5 where GX's bias
    // flips it.
    const float border = float(shadow_->border);
    const float inner_s = (float(shadow_width_) - 2.0f * border) / float(shadow_width_);
    const float inner_t = (float(shadow_height_) - 2.0f * border) / float(shadow_height_);
    engine::Mat4 bias;
    bias(0, 0) = 0.5f * inner_s;
    bias(1, 1) = 0.5f * inner_t;
    bias(3, 0) = 0.5f * inner_s + border / float(shadow_width_);
    bias(3, 1) = 0.5f * inner_t + border / float(shadow_height_);
    const auto matrix = shadow_->light_view * shadow_->light_projection * bias;
    gl.UniformMatrix4fv(gl.u_shadow_matrix, 1, GL_FALSE, matrix.m.data());
    gl.ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadow_texture_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.Uniform1i(gl.u_shadow_pass, 2);
    gl.Uniform1i(gl.u_skinned, 0);
    glEnable(GL_BLEND);
    gl.BlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ZERO, GL_ONE);
    gl.BlendEquation(GL_FUNC_ADD);
    glDisable(GL_DEPTH_CLAMP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    gl.UniformMatrix4fv(gl.u_view, 1, GL_FALSE, camera.view.m.data());
    gl.UniformMatrix4fv(gl.u_proj, 1, GL_FALSE, camera.projection.m.data());
    for (const auto& item : shadow_->receivers) {
        const auto* m = mesh(item.geometry);
        if (!m || !m->index_count) continue;
        // 801FE190 pushes the texture, colour, texture coordinate source and
        // function, the map, the texture matrix, the blend state and function
        // and a disabled alpha test, but no depth, cull or offset attribute:
        // each receiver is drawn again on its own vertices with its own depth
        // test, function, write and culling (GX has no polygon offset). A
        // decal receiver (dng307b2's boss arena emblem, 0.1 above the floor
        // and drawn first as a whole square) so writes its depth and keeps the
        // floor receiver from shading its pixels a second time, and a blended
        // item drawn later behind a receiver stays hidden (stagBS05's arena
        // receiver, y 10.2-10.8, covers the boss gate's centre tile at y
        // 10.53-10.77).
        const auto& s = item.state;
        if (s.depth_test) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(depth_func(s.depth_func));
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(s.depth_write ? GL_TRUE : GL_FALSE);
        if (s.cull_enabled) {
            glEnable(GL_CULL_FACE);
            glCullFace(s.cull_mode == 1 ? GL_FRONT : s.cull_mode == 2 ? GL_FRONT_AND_BACK : GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        gl.UniformMatrix4fv(gl.u_model, 1, GL_FALSE, item.model.m.data());
        gl.BindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.Uniform1i(gl.u_shadow_pass, 0);
}

void GlRenderer::render(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& screen_camera,
                        const engine::Vec4f& clear, const Viewport& screen, const Viewport& wide, int window_width,
                        int window_height) {
    auto& gl = *gl_;
    if (!id_pass_) render_shadow_map();
    // Dev item-id pass: no blending and no multisampling (every sample of a
    // pixel gets the item's exact number), no shadow receivers.
    const bool multisample = glIsEnabled(GL_MULTISAMPLE);
    if (id_pass_) {
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_DITHER);
    }
    // Widescreen: perspective projections keep their vertical field of view
    // and widen horizontally to the wide area.
    const float widen = wide.width > screen.width && wide.height > 0 && screen.height > 0
                            ? (float(screen.width) / float(screen.height)) / (float(wide.width) / float(wide.height))
                            : 1.0f;
    auto widened = [&](engine::Mat4 projection) {
        for (int r = 0; r < 4; ++r) projection(r, 0) *= widen;
        return projection;
    };
    engine::FrameCamera camera = screen_camera;
    camera.projection = widened(screen_camera.projection);
    // A 2D item reaching every edge of the picture (fade, backdrop).
    auto full_screen = [&](const engine::DrawItem& item, const engine::Mat4& view, const engine::Mat4& projection) {
        const auto& positions = item.geometry->positions;
        if (positions.empty() || positions.size() > 64) return false;
        const auto to_ndc = item.model * view * projection;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (const auto& p : positions) {
            const auto v = engine::transform_point(p, to_ndc);
            x0 = std::min(x0, v.x);
            x1 = std::max(x1, v.x);
            y0 = std::min(y0, v.y);
            y1 = std::max(y1, v.y);
        }
        return x0 <= -0.99f && x1 >= 0.99f && y0 <= -0.99f && y1 >= 0.99f;
    };
    bool receivers_drawn = false;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(wide.x, wide.y, wide.width, wide.height);
    // The GX copy clear colour each picture starts from is igClearAttr's
    // packed colour (apply 800BD7D0 -> 800EC01C), which the attribute packs
    // with 8012E1D8 (0x800BD8DC / 0x800BD924): rounded. beBkColor's float
    // setClearColor (802F29E8 -> 800EBF28) would truncate, but the clear
    // Dolphin shows is the rounded one: stagML14's 0.031 / 0.117 / 0.031
    // background is 8 / 30 / 8, not 7 / 29 / 7.
    if (id_pass_) glClearColor(0, 0, 0, 1);
    else glClearColor(engine::gx_round(clear.x), engine::gx_round(clear.y), engine::gx_round(clear.z), 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.UseProgram(program_);
    gl.Uniform1i(gl.u_id_mode, id_pass_ ? 1 : 0);
    gl.Uniform1i(gl.u_tex, 0);
    gl.Uniform1i(gl.u_shadow_tex, 1);
    gl.Uniform1i(gl.u_shadow_pass, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    const float sx = screen.width / 640.0f, sy = screen.height / 480.0f;
    if (id_pass_) receivers_drawn = true;
    std::uint32_t item_number = 0;
    for (const auto& item : items) {
        ++item_number;
        const auto* m = mesh(item.geometry);
        if (!m || !m->index_count) continue;
        const auto& s = item.state;
        if (id_pass_)
            gl.Uniform4f(gl.u_id, float(item_number & 0xFF) / 255.0f, float((item_number >> 8) & 0xFF) / 255.0f,
                         float((item_number >> 16) & 0xFF) / 255.0f, 1.0f);
        // igProjectiveShadowShader draws its receivers in a compound package
        // (801FE348 opens it with neither flag, 802062E0), which the sorter's
        // flush appends after the opaque packages and before the transparent
        // ones (Engine::render puts the blended items last).
        if (!receivers_drawn && s.blend_enabled) {
            glViewport(wide.x, wide.y, wide.width, wide.height);
            gl.pixel_center(wide.width, wide.height);
            glScissor(wide.x, wide.y, wide.width, wide.height);
            render_shadow_receivers(camera);
            receivers_drawn = true;
        }
        const auto view = s.view.value_or(screen_camera.view);
        const auto authored = s.projection.value_or(screen_camera.projection);
        const bool flat = authored(2, 3) == 0.0f;
        const bool on_wide = widen != 1.0f && !s.viewport && (!flat || full_screen(item, view, authored));
        const auto proj = !flat && !s.viewport ? widened(authored) : authored;
        if (on_wide) {
            glViewport(wide.x, wide.y, wide.width, wide.height);
            gl.pixel_center(wide.width, wide.height);
            glScissor(wide.x, wide.y, wide.width, wide.height);
        } else if (s.viewport) {
            const auto& v = *s.viewport;
            const int x = screen.x + int(std::lround(v[0] * sx));
            const int y = screen.y + int(std::lround((480 - v[1] - v[3]) * sy));
            const int w = int(std::lround(v[2] * sx)), h = int(std::lround(v[3] * sy));
            glViewport(x, y, w, h);
            gl.pixel_center(w, h);
            glScissor(x, y, w, h);
        } else {
            glViewport(screen.x, screen.y, screen.width, screen.height);
            gl.pixel_center(screen.width, screen.height);
            glScissor(screen.x, screen.y, screen.width, screen.height);
        }
        // 2D layers (orthographic, e.g. the FONT camera with near 10) place
        // geometry in front of the near plane (item names at z -5); GX draws
        // it, so clamp depth there instead of clipping.
        if (proj(2, 3) == 0.0f && proj(3, 3) == 1.0f) glEnable(GL_DEPTH_CLAMP);
        else glDisable(GL_DEPTH_CLAMP);
        gl.UniformMatrix4fv(gl.u_model, 1, GL_FALSE, item.model.m.data());
        // The modelview flush 800F805C (via 800F84EC) loads the position
        // matrix and, as the normal matrix, the transpose of its inverse:
        // PSMTXInverse 8025B5E4, PSMTXTranspose 8025B594, GXLoadNrmMtxImm
        // 802585D0. With a rotation and a uniform scale that is the
        // model's own rotation; a non-uniform scale (stage props such as
        // tower_insideShape2 at 1.5 / 1 / 1.5, or 0.84 / 0.84 / 1) tilts the
        // normals the other way than the plain matrix would. The view is a
        // rotation, so the world matrix stands in for the modelview. A
        // singular matrix fails the inverse (it returns 0 at 0x8025B650) and
        // the load is skipped: the previous normal matrix stays.
        if (engine::Mat4 inverse; engine::invert(item.model, inverse)) {
            engine::Mat4 normal;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) normal(r, c) = inverse(c, r);
            gl.UniformMatrix4fv(gl.u_normal, 1, GL_FALSE, normal.m.data());
        }
        const auto tex_matrix = s.texture_matrix.value_or(dw4::engine::Mat4{});
        gl.UniformMatrix4fv(gl.u_tex_matrix, 1, GL_FALSE, tex_matrix.m.data());
        const int texgen = s.texcoord_source == 2 && m->has_normals ? 2 : 0;
        gl.Uniform1i(gl.u_texgen, texgen);
        if (item.palette && !item.palette->empty()) {
            const auto count = std::min<std::size_t>(item.palette->size(), 64);
            gl.UniformMatrix4fv(gl.u_palette, GLsizei(count), GL_FALSE, item.palette->front().m.data());
            gl.Uniform1i(gl.u_skinned, 1);
        } else {
            gl.Uniform1i(gl.u_skinned, 0);
        }
        gl.UniformMatrix4fv(gl.u_view, 1, GL_FALSE, view.m.data());
        gl.UniformMatrix4fv(gl.u_proj, 1, GL_FALSE, proj.m.data());
        // Constant colours reach GX as 8-bit register values (gx_color.hpp):
        // igColorAttr colours, the FADESET / FLASHSET override among them,
        // rounded like igColorAttr::setColor packs them (8012E1D8). A fade
        // at 127.54 / 255 is drawn at 128 / 255 as in the original.
        const auto color = engine::gx_round(s.color);
        gl.Uniform4f(gl.u_color, color.x, color.y, color.z, color.w);
        const auto mod = engine::gx_round(s.modulate.value_or(engine::Vec4f{1, 1, 1, 1}));
        gl.Uniform4f(gl.u_modulate, mod.x, mod.y, mod.z, mod.w);
        gl.Uniform1i(gl.u_use_vcol, m->has_colors ? 1 : 0);
        const Texture* tex = (s.texture_enabled && (m->has_uvs || texgen)) ? texture(s.texture) : nullptr;
        glBindTexture(GL_TEXTURE_2D, tex ? tex->id : white_texture_);
        gl.Uniform1i(gl.u_use_tex, tex ? 1 : 0);
        gl.Uniform1i(gl.u_alpha_func, s.alpha_test ? s.alpha_func : 7);
        gl.Uniform1f(gl.u_alpha_ref, s.alpha_ref);
        const bool lighting = s.lighting && m->has_normals && !s.lights.empty();
        gl.Uniform1i(gl.u_lighting, lighting ? 1 : 0);
        if (lighting) {
            // 800F6DC0 builds the GX registers in 8 bits: the ambient register
            // is emission + sum(light ambient * material ambient) (fmadds,
            // 0x800F6F30..0x800F6F40), a light's colour is light diffuse,
            // times the material diffuse unless the material colour comes
            // from the vertices (modes 2/3, mat_src VTX); each channel is
            // clamped to 1 and truncated (fctiwz of c * 255: the light
            // colours at 0x800F7228..0x800F727C, gx_trunc_light; the ambient
            // register at 0x800F7604..0x800F7678, gx_trunc). A light diffuse
            // above 1 (the lobby's 1.1) adds no more than 1.
            const auto reg = [](float v) { return engine::gx_trunc(v); };
            const auto light_reg = [](float v) { return engine::gx_trunc_light(v); };
            const engine::Vec4f tint = m->has_colors ? engine::Vec4f{1, 1, 1, 1} : s.diffuse;
            float dirs[12]{}, cols[12]{};
            engine::Vec3f ambient{s.emission.x, s.emission.y, s.emission.z};
            int count = 0;
            for (const auto* light : s.lights) {
                if (count >= 4) break;
                const engine::Attribute* attr = light;
                if (light->kind == engine::AttrKind::light_state) continue;
                ambient.x = std::fma(attr->ambient.x, s.ambient.x, ambient.x);
                ambient.y = std::fma(attr->ambient.y, s.ambient.y, ambient.y);
                ambient.z = std::fma(attr->ambient.z, s.ambient.z, ambient.z);
                dirs[count * 3] = attr->matrix(0, 0);
                dirs[count * 3 + 1] = attr->matrix(0, 1);
                dirs[count * 3 + 2] = attr->matrix(0, 2);
                cols[count * 3] = light_reg(attr->color.x * tint.x);
                cols[count * 3 + 1] = light_reg(attr->color.y * tint.y);
                cols[count * 3 + 2] = light_reg(attr->color.z * tint.z);
                ++count;
            }
            const float ambient_reg[3] = {reg(ambient.x), reg(ambient.y), reg(ambient.z)};
            gl.Uniform3fv(gl.u_light_dir, 4, dirs);
            gl.Uniform3fv(gl.u_light_col, 4, cols);
            gl.Uniform3fv(gl.u_light_ambient, 1, ambient_reg);
            gl.Uniform1i(gl.u_light_count, count);
            // The material colour channels as the flush converts them, each
            // clamped to 1 and truncated (0x800F76E0..0x800F7780; the alpha
            // goes into the ALPHA0 registers, 0x800F7788..0x800F77F0).
            const auto diffuse = engine::gx_trunc(s.diffuse);
            gl.Uniform4f(gl.u_diffuse, diffuse.x, diffuse.y, diffuse.z, diffuse.w);
        }
        const bool fog = s.fog && s.fog_params && !s.projection;
        gl.Uniform1i(gl.u_fog, fog ? 1 : 0);
        if (fog) {
            // The fog flush 800F9604 hands GXSetFog the colour as fctiwz of
            // 255 * c (0x800F9638..0x800F966C): truncated to 8 bits.
            const auto& f = s.fog_params->color;
            gl.Uniform4f(gl.u_fog_color, engine::gx_trunc_light(f.x), engine::gx_trunc_light(f.y), engine::gx_trunc_light(f.z), 1);
            gl.Uniform2f(gl.u_fog_range, s.fog_params->f0, s.fog_params->f1);
        }
        if (s.blend_enabled && !id_pass_) {
            // The GX visual context's blend flush 800F8EA8 calls
            // GXSetBlendMode(enabled ? GX_BM_BLEND : GX_BM_NONE, src, dst,
            // GX_LO_COPY): it never selects GX_BM_SUBTRACT, so every blended
            // item adds (the igBlendFunctionAttr equation is dropped, see the
            // blend_function case in scene_graph.cpp).
            glEnable(GL_BLEND);
            gl.BlendFuncSeparate(blend_factor(s.blend_src), blend_factor(s.blend_dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            gl.BlendEquation(GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        if (s.depth_test) { glEnable(GL_DEPTH_TEST); glDepthFunc(depth_func(s.depth_func)); }
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(s.depth_write ? GL_TRUE : GL_FALSE);
        if (s.cull_enabled) {
            // IG_GFX_CULL_FACE_MODE (enum pairs at 0x8048F570): BACK = 0,
            // FRONT = 1; front faces wind counter-clockwise as in OpenGL.
            glEnable(GL_CULL_FACE);
            glCullFace(s.cull_mode == 1 ? GL_FRONT : s.cull_mode == 2 ? GL_FRONT_AND_BACK : GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glColorMask(s.color_write, s.color_write, s.color_write, s.color_write);
        gl.BindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    if (!receivers_drawn) {
        glViewport(wide.x, wide.y, wide.width, wide.height);
        gl.pixel_center(wide.width, wide.height);
        glScissor(wide.x, wide.y, wide.width, wide.height);
        render_shadow_receivers(camera);
    }
    glDisable(GL_DEPTH_CLAMP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, window_width, window_height);
    if (id_pass_) {
        gl.Uniform1i(gl.u_id_mode, 0);
        if (multisample) glEnable(GL_MULTISAMPLE);
        glEnable(GL_DITHER);
    }
    // Drop buffers of geometry not drawn for two seconds (replaced text).
    ++frame_;
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (frame_ - it->second.last_frame > 120) {
            release(it->second);
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace dw4::game
