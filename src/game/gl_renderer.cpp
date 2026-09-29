#include "gl_renderer.hpp"

#include "dw4/textures.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
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
    X(PFNGLGENERATEMIPMAPPROC, GenerateMipmap)

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
    int u_lighting{}, u_light_dir{}, u_light_col{}, u_light_count{}, u_ambient{}, u_diffuse{}, u_emission{}, u_tex{};
    int u_fog{}, u_fog_color{}, u_fog_range{}, u_skinned{}, u_palette{};
    PFNGLUNIFORM2FPROC Uniform2f{};
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
uniform int u_skinned;
uniform mat4 u_palette[64];
uniform int u_use_vcol;
uniform vec4 u_color;
uniform int u_lighting;
uniform vec3 u_light_dir[4];
uniform vec3 u_light_col[4];
uniform int u_light_count;
uniform vec4 u_ambient, u_diffuse, u_emission;
out vec4 v_color;
out vec2 v_uv;
out float v_depth;
void main() {
    vec4 local = vec4(a_pos, 1.0);
    vec3 normal = a_normal;
    if (u_skinned != 0) {
        int i0 = int(a_indices.x + 0.5), i1 = int(a_indices.y + 0.5);
        vec4 p = u_palette[i0] * local * a_weights.x;
        vec3 nn = mat3(u_palette[i0]) * a_normal * a_weights.x;
        if (a_weights.y > 0.0) { p += u_palette[i1] * local * a_weights.y; nn += mat3(u_palette[i1]) * a_normal * a_weights.y; }
        local = vec4(p.xyz, 1.0);
        normal = nn;
    }
    vec4 world = u_model * local;
    vec4 eye = u_view * world;
    v_depth = -eye.z;
    gl_Position = u_proj * eye;
    vec4 base = u_use_vcol != 0 ? a_color : u_color;
    if (u_lighting != 0) {
        vec3 n = normalize(mat3(u_model) * normal);
        vec3 lit = u_emission.rgb + u_ambient.rgb * 0.35;
        for (int i = 0; i < u_light_count; ++i)
            lit += u_light_col[i] * max(dot(n, -normalize(u_light_dir[i])), 0.0);
        vec4 material = u_use_vcol != 0 ? a_color : u_diffuse;
        base = vec4(clamp(lit, 0.0, 1.0) * material.rgb, material.a);
    }
    v_color = base;
    v_uv = a_uv;
}
)";
const char* kFragment = R"(#version 330 core
in vec4 v_color;
in vec2 v_uv;
in float v_depth;
uniform int u_fog;
uniform vec4 u_fog_color;
uniform vec2 u_fog_range;
uniform sampler2D u_tex;
uniform int u_use_tex;
uniform vec4 u_modulate;
uniform int u_alpha_func;
uniform float u_alpha_ref;
out vec4 o_color;
void main() {
    vec4 c = v_color;
    if (u_use_tex != 0) c *= texture(u_tex, v_uv);
    c *= u_modulate;
    float a = c.a;
    bool pass = true;
    if (u_alpha_func == 0) pass = false;
    else if (u_alpha_func == 1) pass = a < u_alpha_ref;
    else if (u_alpha_func == 2) pass = abs(a - u_alpha_ref) < 0.002;
    else if (u_alpha_func == 3) pass = a <= u_alpha_ref;
    else if (u_alpha_func == 4) pass = a > u_alpha_ref;
    else if (u_alpha_func == 5) pass = abs(a - u_alpha_ref) >= 0.002;
    else if (u_alpha_func == 6) pass = a >= u_alpha_ref;
    if (!pass) discard;
    if (u_fog != 0) {
        float f = clamp((u_fog_range.y - v_depth) / max(u_fog_range.y - u_fog_range.x, 0.001), 0.0, 1.0);
        c.rgb = mix(u_fog_color.rgb, c.rgb, f);
    }
    o_color = c;
}
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
    U(lighting) U(light_dir) U(light_col) U(light_count) U(ambient) U(diffuse) U(emission) U(tex)
    U(fog) U(fog_color) U(fog_range) U(skinned) U(palette)
#undef U
    gl.Uniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(SDL_GL_GetProcAddress("glUniform2f"));
    glGenTextures(1, &white_texture_);
    glBindTexture(GL_TEXTURE_2D, white_texture_);
    const std::uint32_t white = 0xffffffffu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
}

GlRenderer::~GlRenderer() {
    clear_cache();
    if (white_texture_) glDeleteTextures(1, &white_texture_);
    if (program_) gl_->DeleteProgram(program_);
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

const GlRenderer::Mesh* GlRenderer::mesh(const engine::GeometryData* g) {
    if (auto found = meshes_.find(g); found != meshes_.end()) return &found->second;
    auto& gl = *gl_;
    Mesh m;
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
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(image.width), GLsizei(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        gl_->GenerateMipmap(GL_TEXTURE_2D);
        const auto& s = binding->sampler;
        auto wrap = [](int v) { return v == 0 ? GL_CLAMP_TO_EDGE : v == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT; };
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(s.wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(s.wrap_t));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, s.mag_filter == 0 ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, s.min_filter == 0 ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR);
    } catch (const std::exception& error) {
        std::cerr << "texture decode failed: " << error.what() << '\n';
        t.id = 0;
    }
    auto& stored = textures_.emplace(key, t).first->second;
    return stored.id ? &stored : nullptr;
}

void GlRenderer::render(const std::vector<engine::DrawItem>& items, const engine::FrameCamera& camera,
                        const engine::Vec4f& clear, const Viewport& screen, int window_width, int window_height) {
    auto& gl = *gl_;
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0, 0, 0, 1);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(screen.x, screen.y, screen.width, screen.height);
    glClearColor(clear.x, clear.y, clear.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.UseProgram(program_);
    gl.Uniform1i(gl.u_tex, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    const float sx = screen.width / 640.0f, sy = screen.height / 480.0f;
    for (const auto& item : items) {
        const auto* m = mesh(item.geometry);
        if (!m || !m->index_count) continue;
        const auto& s = item.state;
        if (s.viewport) {
            const auto& v = *s.viewport;
            const int x = screen.x + int(std::lround(v[0] * sx));
            const int y = screen.y + int(std::lround((480 - v[1] - v[3]) * sy));
            const int w = int(std::lround(v[2] * sx)), h = int(std::lround(v[3] * sy));
            glViewport(x, y, w, h);
            glScissor(x, y, w, h);
        } else {
            glViewport(screen.x, screen.y, screen.width, screen.height);
            glScissor(screen.x, screen.y, screen.width, screen.height);
        }
        const auto view = s.view.value_or(camera.view);
        const auto proj = s.projection.value_or(camera.projection);
        gl.UniformMatrix4fv(gl.u_model, 1, GL_FALSE, item.model.m.data());
        if (item.palette && !item.palette->empty()) {
            const auto count = std::min<std::size_t>(item.palette->size(), 64);
            gl.UniformMatrix4fv(gl.u_palette, GLsizei(count), GL_FALSE, item.palette->front().m.data());
            gl.Uniform1i(gl.u_skinned, 1);
        } else {
            gl.Uniform1i(gl.u_skinned, 0);
        }
        gl.UniformMatrix4fv(gl.u_view, 1, GL_FALSE, view.m.data());
        gl.UniformMatrix4fv(gl.u_proj, 1, GL_FALSE, proj.m.data());
        gl.Uniform4f(gl.u_color, s.color.x, s.color.y, s.color.z, s.color.w);
        const auto mod = s.modulate.value_or(engine::Vec4f{1, 1, 1, 1});
        gl.Uniform4f(gl.u_modulate, mod.x, mod.y, mod.z, mod.w);
        gl.Uniform1i(gl.u_use_vcol, m->has_colors ? 1 : 0);
        const Texture* tex = (s.texture_enabled && m->has_uvs) ? texture(s.texture) : nullptr;
        glBindTexture(GL_TEXTURE_2D, tex ? tex->id : white_texture_);
        gl.Uniform1i(gl.u_use_tex, tex ? 1 : 0);
        gl.Uniform1i(gl.u_alpha_func, s.alpha_test ? s.alpha_func : 7);
        gl.Uniform1f(gl.u_alpha_ref, s.alpha_ref);
        const bool lighting = s.lighting && m->has_normals && !s.lights.empty();
        gl.Uniform1i(gl.u_lighting, lighting ? 1 : 0);
        if (lighting) {
            float dirs[12]{}, cols[12]{};
            int count = 0;
            for (const auto* light : s.lights) {
                if (count >= 4) break;
                const engine::Attribute* attr = light;
                if (light->kind == engine::AttrKind::light_state) continue;
                dirs[count * 3] = attr->matrix(0, 0);
                dirs[count * 3 + 1] = attr->matrix(0, 1);
                dirs[count * 3 + 2] = attr->matrix(0, 2);
                cols[count * 3] = attr->color.x;
                cols[count * 3 + 1] = attr->color.y;
                cols[count * 3 + 2] = attr->color.z;
                ++count;
            }
            gl.Uniform3fv(gl.u_light_dir, 4, dirs);
            gl.Uniform3fv(gl.u_light_col, 4, cols);
            gl.Uniform1i(gl.u_light_count, count);
            gl.Uniform4f(gl.u_ambient, s.ambient.x, s.ambient.y, s.ambient.z, s.ambient.w);
            gl.Uniform4f(gl.u_diffuse, s.diffuse.x, s.diffuse.y, s.diffuse.z, s.diffuse.w);
            gl.Uniform4f(gl.u_emission, s.emission.x, s.emission.y, s.emission.z, s.emission.w);
        }
        const bool fog = s.fog && s.fog_params && !s.projection;
        gl.Uniform1i(gl.u_fog, fog ? 1 : 0);
        if (fog) {
            gl.Uniform4f(gl.u_fog_color, s.fog_params->color.x, s.fog_params->color.y, s.fog_params->color.z, 1);
            gl.Uniform2f(gl.u_fog_range, s.fog_params->f0, s.fog_params->f1);
        }
        if (s.blend_enabled) {
            glEnable(GL_BLEND);
            gl.BlendFuncSeparate(blend_factor(s.blend_src), blend_factor(s.blend_dst), GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            gl.BlendEquation(s.blend_equation == 1 ? GL_FUNC_SUBTRACT : GL_FUNC_ADD);
        } else {
            glDisable(GL_BLEND);
        }
        if (s.depth_test) { glEnable(GL_DEPTH_TEST); glDepthFunc(depth_func(s.depth_func)); }
        else glDisable(GL_DEPTH_TEST);
        glDepthMask(s.depth_write ? GL_TRUE : GL_FALSE);
        if (s.cull_enabled) {
            glEnable(GL_CULL_FACE);
            glCullFace(s.cull_mode == 0 ? GL_FRONT : s.cull_mode == 2 ? GL_FRONT_AND_BACK : GL_BACK);
        } else {
            glDisable(GL_CULL_FACE);
        }
        glColorMask(s.color_write, s.color_write, s.color_write, s.color_write);
        gl.BindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, window_width, window_height);
}

} // namespace dw4::game
