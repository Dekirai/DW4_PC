#include "mesh_renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4::viewer {
namespace {
// Resolve core entry points through the current SDL context, including Windows
// where opengl32.dll exports only OpenGL 1.1 directly.
#define DW4_GL_FUNCTIONS(X) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLBUFFERSUBDATAPROC, BufferSubData) \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLCREATESHADERPROC, CreateShader) X(PFNGLSHADERSOURCEPROC, ShaderSource) \
    X(PFNGLCOMPILESHADERPROC, CompileShader) X(PFNGLGETSHADERIVPROC, GetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) X(PFNGLATTACHSHADERPROC, AttachShader) \
    X(PFNGLLINKPROGRAMPROC, LinkProgram) X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
    X(PFNGLUSEPROGRAMPROC, UseProgram) X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) X(PFNGLUNIFORM1IPROC, Uniform1i) \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture) X(PFNGLBINDSAMPLERPROC, BindSampler) \
    X(PFNGLBLENDFUNCSEPARATEPROC, BlendFuncSeparate) \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, BlendEquationSeparate) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers) X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers) X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, RenderbufferStorageMultisample) \
    X(PFNGLGETRENDERBUFFERPARAMETERIVPROC, GetRenderbufferParameteriv) X(PFNGLBLITFRAMEBUFFERPROC, BlitFramebuffer)

struct Gl {
#define DECLARE(type, name) type name{};
    DW4_GL_FUNCTIONS(DECLARE)
#undef DECLARE
    Gl() {
#define LOAD(type, name) name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name)); \
        if (!name) throw std::runtime_error("Mesh renderer requires gl" #name);
        DW4_GL_FUNCTIONS(LOAD)
#undef LOAD
    }
};
#undef DW4_GL_FUNCTIONS

void check_gl(const char* operation) {
    const auto error = glGetError();
    if (error != GL_NO_ERROR)
        throw std::runtime_error(std::string(operation) + ": OpenGL error " + std::to_string(error));
}
void discard_prior_errors() {
    for (int i = 0; i < 16; ++i) if (glGetError() == GL_NO_ERROR) return;
    throw std::runtime_error("OpenGL error state did not clear; the graphics context may be unavailable");
}

struct SavedState {
    Gl& gl;
    GLint draw_fbo{}, read_fbo{}, program{}, vao{}, array_buffer{}, renderbuffer{}, texture{};
    GLint active_texture{}, texture_zero{}, sampler_zero{}, unpack_buffer{}, unpack_alignment{}, unpack_row_length{}, unpack_skip_rows{}, unpack_skip_pixels{};
    GLint blend_source_rgb{}, blend_dest_rgb{}, blend_source_alpha{}, blend_dest_alpha{}, blend_equation_rgb{}, blend_equation_alpha{};
    GLint viewport[4]{}, polygon[2]{}, depth_function{};
    GLfloat clear_color[4]{}, line_width{};
    GLdouble clear_depth{};
    GLboolean depth_mask{}, color_mask[4]{};
    const std::array<GLenum, 7> capabilities{GL_DEPTH_TEST, GL_CULL_FACE, GL_BLEND,
        GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_FRAMEBUFFER_SRGB, GL_POLYGON_OFFSET_FILL};
    std::array<GLboolean, 7> enabled{};
    explicit SavedState(Gl& functions) : gl(functions) {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer); glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        gl.ActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture_zero);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler_zero);
        gl.ActiveTexture(static_cast<GLenum>(active_texture));
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment); glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpack_row_length);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpack_skip_rows); glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpack_skip_pixels);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_source_rgb); glGetIntegerv(GL_BLEND_DST_RGB, &blend_dest_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_source_alpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dest_alpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_equation_rgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_equation_alpha);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_POLYGON_MODE, polygon); glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear_color); glGetFloatv(GL_LINE_WIDTH, &line_width);
        glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask); glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        for (std::size_t i = 0; i < capabilities.size(); ++i) enabled[i] = glIsEnabled(capabilities[i]);
    }
    ~SavedState() {
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fbo));
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(read_fbo));
        gl.UseProgram(static_cast<GLuint>(program)); gl.BindVertexArray(static_cast<GLuint>(vao));
        gl.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(array_buffer));
        gl.BindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(renderbuffer));
        gl.ActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture_zero));
        gl.BindSampler(0, static_cast<GLuint>(sampler_zero));
        gl.ActiveTexture(static_cast<GLenum>(active_texture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpack_buffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment); glPixelStorei(GL_UNPACK_ROW_LENGTH, unpack_row_length);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpack_skip_rows); glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpack_skip_pixels);
        gl.BlendFuncSeparate(static_cast<GLenum>(blend_source_rgb), static_cast<GLenum>(blend_dest_rgb),
            static_cast<GLenum>(blend_source_alpha), static_cast<GLenum>(blend_dest_alpha));
        gl.BlendEquationSeparate(static_cast<GLenum>(blend_equation_rgb), static_cast<GLenum>(blend_equation_alpha));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        // Core profile has one mode and requires GL_FRONT_AND_BACK. Retain room
        // for a second query result for drivers that also return the back mode.
        glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(polygon[0]));
        glDepthFunc(static_cast<GLenum>(depth_function)); glDepthMask(depth_mask);
        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]); glLineWidth(line_width);
        glClearDepth(clear_depth);
        for (std::size_t i = 0; i < capabilities.size(); ++i) {
            if (enabled[i]) glEnable(capabilities[i]); else glDisable(capabilities[i]);
        }
    }
    void forget_texture(GLuint name) {
        if (texture == static_cast<GLint>(name)) texture = 0;
        if (texture_zero == static_cast<GLint>(name)) texture_zero = 0;
    }
};

GLint wrap_mode(MeshTextureWrap mode) {
    switch (mode) {
    case MeshTextureWrap::repeat: return GL_REPEAT;
    case MeshTextureWrap::clamp_to_edge: return GL_CLAMP_TO_EDGE;
    case MeshTextureWrap::mirrored_repeat: return GL_MIRRORED_REPEAT;
    }
    throw std::runtime_error("Unknown mesh texture wrap mode");
}
GLint filter_mode(MeshTextureFilter mode) {
    switch (mode) {
    case MeshTextureFilter::nearest: return GL_NEAREST;
    case MeshTextureFilter::linear: return GL_LINEAR;
    }
    throw std::runtime_error("Unknown mesh texture filter mode");
}

std::uint64_t image_bytes(const RgbaImage& image) {
    if (!image.width || !image.height || image.width > 16384 || image.height > 16384)
        throw std::runtime_error("Texture upload dimensions or RGBA byte count are invalid");
    const auto size = std::uint64_t(image.width) * image.height * 4;
    if (size > 64u * 1024u * 1024u || size != image.rgba.size())
        throw std::runtime_error("Texture upload dimensions or RGBA byte count are invalid");
    return size;
}

constexpr const char* vertex_source = R"GLSL(#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texture_coordinate;
uniform mat4 view_projection;
uniform mat4 model_view;
uniform int prepared_draw;
out vec3 local_position;
out vec2 uv;
void main() {
    vec4 point = vec4(position, 1.0);
    if (prepared_draw != 0) point = model_view * point;
    local_position = point.xyz;
    uv = texture_coordinate;
    gl_Position = view_projection * point;
}
)GLSL";
constexpr const char* fragment_source = R"GLSL(#version 330 core
in vec3 local_position;
in vec2 uv;
uniform int draw_mode;
uniform sampler2D base_texture;
out vec4 color;
void main() {
    if (draw_mode == 1) { color = vec4(0.30, 0.90, 0.79, 1.0); return; }
    if (draw_mode == 3) { color = vec4(1.0, 0.72, 0.22, 1.0); return; }
    if (draw_mode == 2) {
        color = texture(base_texture, uv);
        if (color.a == 0.0) discard;
        return;
    }
    vec3 normal = cross(dFdx(local_position), dFdy(local_position));
    float length_squared = dot(normal, normal);
    normal = length_squared > 1e-16 ? normal * inversesqrt(length_squared) : vec3(0.0, 1.0, 0.0);
    float light = 0.28 + 0.72 * abs(dot(normal, normalize(vec3(0.35, 0.7, 1.0))));
    color = vec4(vec3(0.30, 0.69, 0.82) * light, 1.0);
}
)GLSL";
}

class MeshRenderer::Impl {
public:
    struct DrawBatch {
        std::size_t first_index{}, index_count{};
        GLuint texture{};
        bool allow_solid_fallback{};
        std::size_t first_vertex{}, vertex_count{};
    };
    Gl gl;
    GLuint program{}, vao{}, vertex_buffer{}, index_buffer{}, base_texture{};
    GLuint draw_fbo{}, resolve_fbo{}, texture{}, depth_buffer{}, color_buffer{};
    GLint matrix_location{-1}, mode_location{-1}, texture_location{-1};
    GLint model_view_location{-1}, prepared_location{-1};
    bool has_uv{};
    bool batch_mode{};
    bool prepared{};
    std::vector<DrawBatch> batches;
    std::vector<GLuint> batch_textures;
    std::vector<Vec2> batch_uvs;
    std::array<double, 3> center{};
    double radius = 1;
    std::vector<float> vertices;
    int width{}, height{}, sample_count{}, requested_sample_count{-1};
    std::size_t index_count{};
    std::uint64_t frames{};
    ~Impl() { release_mesh(); release_targets(); if (program) gl.DeleteProgram(program); }

    void release_mesh() {
        release_texture();
        if (vao) gl.DeleteVertexArrays(1, &vao);
        if (vertex_buffer) gl.DeleteBuffers(1, &vertex_buffer);
        if (index_buffer) gl.DeleteBuffers(1, &index_buffer);
        vao = vertex_buffer = index_buffer = 0;
        index_count = 0; frames = 0; has_uv = false; batch_mode = false; prepared = false;
        batches.clear();
        batch_uvs.clear();
        vertices.clear(); center = {}; radius = 1;
    }
    void release_texture() {
        if (base_texture) glDeleteTextures(1, &base_texture);
        base_texture = 0;
        for (const auto image : batch_textures) glDeleteTextures(1, &image);
        batch_textures.clear();
        for (auto& batch : batches) batch.texture = 0;
    }
    GLuint upload_texture(const RgbaImage& image, const MeshTextureOptions& options) {
        const auto wrap_u = wrap_mode(options.wrap_u), wrap_v = wrap_mode(options.wrap_v);
        const auto min_filter = filter_mode(options.min_filter), mag_filter = filter_mode(options.mag_filter);
        GLint maximum = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        if (maximum <= 0 || image.width > static_cast<std::uint32_t>(maximum)
            || image.height > static_cast<std::uint32_t>(maximum))
            throw std::runtime_error("Mesh image exceeds the driver's texture size limit");
        GLuint name{};
        try {
            gl.ActiveTexture(GL_TEXTURE0); gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glGenTextures(1, &name); glBindTexture(GL_TEXTURE_2D, name);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_u); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_v);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height),
                0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            check_gl("Uploading mesh texture");
        } catch (...) { if (name) glDeleteTextures(1, &name); throw; }
        return name;
    }
    void release_targets() {
        if (draw_fbo) gl.DeleteFramebuffers(1, &draw_fbo);
        if (resolve_fbo) gl.DeleteFramebuffers(1, &resolve_fbo);
        if (texture) glDeleteTextures(1, &texture);
        if (depth_buffer) gl.DeleteRenderbuffers(1, &depth_buffer);
        if (color_buffer) gl.DeleteRenderbuffers(1, &color_buffer);
        draw_fbo = resolve_fbo = texture = depth_buffer = color_buffer = 0;
        width = height = sample_count = 0; requested_sample_count = -1;
    }
    GLuint compile(GLenum type, const char* source) {
        const auto shader = gl.CreateShader(type);
        if (!shader) throw std::runtime_error("Could not create mesh shader");
        gl.ShaderSource(shader, 1, &source, nullptr); gl.CompileShader(shader);
        GLint okay = 0; gl.GetShaderiv(shader, GL_COMPILE_STATUS, &okay);
        if (!okay) {
            std::array<char, 4096> message{};
            gl.GetShaderInfoLog(shader, static_cast<GLsizei>(message.size()), nullptr, message.data());
            gl.DeleteShader(shader);
            throw std::runtime_error("Mesh shader compilation failed: " + std::string(message.data()));
        }
        return shader;
    }
    void ensure_program() {
        if (program) return;
        GLuint vertex = 0, fragment = 0;
        try {
            vertex = compile(GL_VERTEX_SHADER, vertex_source); fragment = compile(GL_FRAGMENT_SHADER, fragment_source);
            program = gl.CreateProgram();
            if (!program) throw std::runtime_error("Could not create mesh program");
            gl.AttachShader(program, vertex); gl.AttachShader(program, fragment); gl.LinkProgram(program);
            GLint okay = 0; gl.GetProgramiv(program, GL_LINK_STATUS, &okay);
            if (!okay) {
                std::array<char, 4096> message{};
                gl.GetProgramInfoLog(program, static_cast<GLsizei>(message.size()), nullptr, message.data());
                throw std::runtime_error("Mesh program link failed: " + std::string(message.data()));
            }
            matrix_location = gl.GetUniformLocation(program, "view_projection");
            mode_location = gl.GetUniformLocation(program, "draw_mode");
            texture_location = gl.GetUniformLocation(program, "base_texture");
            model_view_location = gl.GetUniformLocation(program, "model_view");
            prepared_location = gl.GetUniformLocation(program, "prepared_draw");
            if (matrix_location < 0 || mode_location < 0 || texture_location < 0 || model_view_location < 0 || prepared_location < 0)
                throw std::runtime_error("Mesh shader uniforms are unavailable");
            gl.DeleteShader(vertex); gl.DeleteShader(fragment);
        } catch (...) {
            if (vertex) gl.DeleteShader(vertex);
            if (fragment) gl.DeleteShader(fragment);
            if (program) gl.DeleteProgram(program);
            program = 0; throw;
        }
    }
    void complete_framebuffer() {
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("The graphics driver could not create a complete mesh framebuffer");
    }
    void targets(int requested_width, int requested_height, int requested_samples, SavedState& saved) {
        GLint maximum_samples = 0, maximum_size = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &maximum_samples); glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maximum_size);
        const auto samples = std::clamp(requested_samples, 0, std::max(0, std::min(8, maximum_samples)));
        const auto desired_samples = samples >= 8 ? 8 : samples >= 4 ? 4 : samples >= 2 ? 2 : 0;
        // Multisample storage may round a supported request upward. Cache the
        // request separately from the actual count reported to the interface.
        if (width == requested_width && height == requested_height && requested_sample_count == desired_samples) return;
        if (requested_width > maximum_size || requested_height > maximum_size)
            throw std::runtime_error("Mesh viewport exceeds the graphics driver's renderbuffer size limit");
        saved.forget_texture(texture);
        release_targets();
        try {
            gl.GenFramebuffers(1, &draw_fbo);
            gl.ActiveTexture(GL_TEXTURE0); gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, requested_width, requested_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl.BindFramebuffer(GL_FRAMEBUFFER, draw_fbo);
            gl.GenRenderbuffers(1, &depth_buffer); gl.BindRenderbuffer(GL_RENDERBUFFER, depth_buffer);
            if (desired_samples) gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, desired_samples, GL_DEPTH_COMPONENT24, requested_width, requested_height);
            else gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, requested_width, requested_height);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_buffer);
            if (desired_samples) {
                gl.GenRenderbuffers(1, &color_buffer); gl.BindRenderbuffer(GL_RENDERBUFFER, color_buffer);
                gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, desired_samples, GL_RGBA8, requested_width, requested_height);
                gl.GetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &sample_count);
                gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_buffer);
                complete_framebuffer();
                gl.GenFramebuffers(1, &resolve_fbo); gl.BindFramebuffer(GL_FRAMEBUFFER, resolve_fbo);
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            } else {
                gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
            }
            complete_framebuffer(); check_gl("Allocating mesh viewport");
            width = requested_width; height = requested_height; requested_sample_count = desired_samples;
        } catch (...) { release_targets(); throw; }
    }
};

MeshRenderer::MeshRenderer() = default;
MeshRenderer::~MeshRenderer() = default;
void MeshRenderer::shutdown() { impl_.reset(); }
void MeshRenderer::clear() { if (impl_) impl_->release_mesh(); }
bool MeshRenderer::has_mesh() const noexcept { return impl_ && impl_->index_count != 0; }
bool MeshRenderer::has_texture() const noexcept {
    return has_mesh() && textured_batch_count() == batch_count();
}
std::size_t MeshRenderer::batch_count() const noexcept {
    return has_mesh() ? (impl_->batch_mode ? impl_->batches.size() : 1) : 0;
}
std::size_t MeshRenderer::textured_batch_count() const noexcept {
    if (!has_mesh()) return 0;
    if (!impl_->batch_mode) return impl_->has_uv && impl_->base_texture ? 1 : 0;
    return static_cast<std::size_t>(std::count_if(impl_->batches.begin(), impl_->batches.end(),
        [](const Impl::DrawBatch& batch) { return batch.texture != 0; }));
}
std::size_t MeshRenderer::texture_count() const noexcept {
    return has_mesh() ? (impl_->batch_mode ? impl_->batch_textures.size() : impl_->base_texture ? 1 : 0) : 0;
}
void MeshRenderer::clear_texture() { if (impl_) impl_->release_texture(); }
std::size_t MeshRenderer::triangle_count() const noexcept { return impl_ ? impl_->index_count / 3 : 0; }
std::uint64_t MeshRenderer::rendered_frames() const noexcept { return impl_ ? impl_->frames : 0; }
int MeshRenderer::samples() const noexcept { return impl_ ? impl_->sample_count : 0; }

void MeshRenderer::upload(const MeshData& mesh) {
    upload_impl(mesh,false);
}
void MeshRenderer::upload_prepared(const MeshData& mesh) {
    upload_impl(mesh,true);
}
void MeshRenderer::upload_impl(const MeshData& mesh,bool prepared) {
    clear();
    if (mesh.positions.empty() || mesh.positions.size() > 1048576 || mesh.indices.empty()
        || mesh.indices.size() > 3145728 || mesh.indices.size() % 3)
        throw std::runtime_error("Mesh upload requires bounded triangle indices and positions");
    if (!mesh.texcoords.empty() && mesh.texcoords.size() != mesh.positions.size())
        throw std::runtime_error("Mesh texture coordinates must match the vertex count");
    std::array<double, 3> low{}, high{};
    bool first = true;
    for (const auto& position : mesh.positions) {
        const std::array<double, 3> value{position.x, position.y, position.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(value[axis])) throw std::runtime_error("Mesh upload contains a non-finite coordinate");
            low[axis] = first ? value[axis] : std::min(low[axis], value[axis]);
            high[axis] = first ? value[axis] : std::max(high[axis], value[axis]);
        }
        first = false;
    }
    std::array<double, 3> center{};
    double radius_squared = 0;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        center[axis] = (low[axis] + high[axis]) * 0.5;
        const auto half_extent = (high[axis] - low[axis]) * 0.5;
        radius_squared += half_extent * half_extent;
    }
    const auto radius = std::sqrt(radius_squared);
    if (radius == 0) throw std::runtime_error("Mesh has no spatial extent to display");
    std::vector<float> vertices;
    vertices.reserve(mesh.positions.size() * 5);
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const auto& position = mesh.positions[i];
        vertices.push_back(prepared ? position.x : static_cast<float>((position.x - center[0]) / radius));
        vertices.push_back(prepared ? position.y : static_cast<float>((position.y - center[1]) / radius));
        vertices.push_back(prepared ? position.z : static_cast<float>((position.z - center[2]) / radius));
        const auto uv = mesh.texcoords.empty() ? Vec2{} : mesh.texcoords[i];
        if (!std::isfinite(uv.u) || !std::isfinite(uv.v)) throw std::runtime_error("Mesh upload contains a non-finite UV coordinate");
        vertices.push_back(uv.u); vertices.push_back(uv.v);
    }
    for (const auto index : mesh.indices)
        if (index >= mesh.positions.size()) throw std::runtime_error("Mesh upload index exceeds the vertex buffer");
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto& state = *impl_;
    SavedState saved(state.gl); discard_prior_errors();
    try {
        state.ensure_program();
        state.gl.GenVertexArrays(1, &state.vao); state.gl.BindVertexArray(state.vao);
        state.gl.GenBuffers(1, &state.vertex_buffer); state.gl.BindBuffer(GL_ARRAY_BUFFER, state.vertex_buffer);
        state.gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
        state.gl.EnableVertexAttribArray(0); state.gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        state.gl.EnableVertexAttribArray(1); state.gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));
        state.gl.GenBuffers(1, &state.index_buffer); state.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.index_buffer);
        state.gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
        check_gl("Uploading mesh geometry");
        state.index_count = mesh.indices.size();
        state.has_uv = !mesh.texcoords.empty();
        state.center = center; state.radius = radius;
        state.prepared = prepared;
        state.vertices = std::move(vertices);
    } catch (...) { state.release_mesh(); throw; }
}

void MeshRenderer::upload_batches(std::span<const MeshBatch> batches) {
    clear();
    constexpr std::size_t maximum_vertices = 4194304;
    constexpr std::size_t maximum_indices = 4194304 * 3;
    constexpr std::uint64_t maximum_images = 256u * 1024u * 1024u;
    constexpr auto no_image = std::numeric_limits<std::size_t>::max();
    if (batches.empty() || batches.size() > 4096)
        throw std::runtime_error("Scene upload requires between 1 and 4096 batches");
    struct ImageUpload { const RgbaImage* image; MeshTextureOptions options; };
    std::vector<ImageUpload> images;
    std::vector<std::size_t> image_indices;
    image_indices.reserve(batches.size());
    std::uint64_t total_image_bytes{};
    std::size_t total_vertices{}, total_indices{};
    std::array<double, 3> low{}, high{};
    bool first = true;
    for (const auto& batch : batches) {
        if (!batch.mesh) throw std::runtime_error("Scene batch has no source mesh");
        const auto& mesh = *batch.mesh;
        if (mesh.positions.empty() || mesh.positions.size() > maximum_vertices - total_vertices
            || mesh.indices.empty() || mesh.indices.size() % 3
            || mesh.indices.size() > maximum_indices - total_indices)
            throw std::runtime_error("Scene upload exceeds its aggregate vertex or triangle limit");
        total_vertices += mesh.positions.size(); total_indices += mesh.indices.size();
        if ((!mesh.texcoords.empty() && mesh.texcoords.size() != mesh.positions.size())
            || (batch.texture && mesh.texcoords.empty()))
            throw std::runtime_error("Textured scene batches require one UV pair per vertex");
        for (const auto& position : mesh.positions) {
            const std::array<double, 3> value{position.x, position.y, position.z};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (!std::isfinite(value[axis])) throw std::runtime_error("Scene upload contains a non-finite coordinate");
                low[axis] = first ? value[axis] : std::min(low[axis], value[axis]);
                high[axis] = first ? value[axis] : std::max(high[axis], value[axis]);
            }
            first = false;
        }
        for (const auto& uv : mesh.texcoords)
            if (!std::isfinite(uv.u) || !std::isfinite(uv.v))
                throw std::runtime_error("Scene upload contains a non-finite UV coordinate");
        for (const auto index : mesh.indices)
            if (index >= mesh.positions.size()) throw std::runtime_error("Scene index exceeds its own batch's vertex buffer");
        auto image_index = no_image;
        if (batch.texture) {
            const auto bytes = image_bytes(*batch.texture);
            wrap_mode(batch.texture_options.wrap_u); wrap_mode(batch.texture_options.wrap_v);
            filter_mode(batch.texture_options.min_filter); filter_mode(batch.texture_options.mag_filter);
            for (std::size_t i = 0; i < images.size(); ++i)
                if (images[i].image == batch.texture && images[i].options == batch.texture_options) { image_index = i; break; }
            if (image_index == no_image) {
                if (bytes > maximum_images - total_image_bytes)
                    throw std::runtime_error("Scene upload exceeds the 256-MiB unique texture limit");
                total_image_bytes += bytes;
                image_index = images.size(); images.push_back({batch.texture, batch.texture_options});
            }
        }
        image_indices.push_back(image_index);
    }
    std::array<double, 3> center{};
    double radius_squared{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        center[axis] = (low[axis] + high[axis]) * 0.5;
        const auto half_extent = (high[axis] - low[axis]) * 0.5;
        radius_squared += half_extent * half_extent;
    }
    const auto radius = std::sqrt(radius_squared);
    if (!std::isfinite(radius) || radius == 0) throw std::runtime_error("Scene has no finite spatial extent to display");
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Impl::DrawBatch> draws;
    std::vector<Vec2> uvs;
    vertices.reserve(total_vertices * 5); indices.reserve(total_indices); draws.reserve(batches.size());
    uvs.reserve(total_vertices);
    std::uint32_t vertex_base{};
    for (const auto& batch : batches) {
        const auto& mesh = *batch.mesh;
        draws.push_back({indices.size(), mesh.indices.size(), 0, batch.allow_solid_fallback, vertex_base, mesh.positions.size()});
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            const auto& position = mesh.positions[i];
            vertices.push_back(static_cast<float>((position.x - center[0]) / radius));
            vertices.push_back(static_cast<float>((position.y - center[1]) / radius));
            vertices.push_back(static_cast<float>((position.z - center[2]) / radius));
            const auto uv = mesh.texcoords.empty() ? Vec2{} : mesh.texcoords[i];
            vertices.push_back(uv.u); vertices.push_back(uv.v);
            uvs.push_back(uv);
        }
        for (const auto index : mesh.indices) indices.push_back(vertex_base + index);
        vertex_base += static_cast<std::uint32_t>(mesh.positions.size());
    }
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto& state = *impl_;
    SavedState saved(state.gl); discard_prior_errors();
    try {
        state.ensure_program();
        state.batch_textures.reserve(images.size());
        state.gl.GenVertexArrays(1, &state.vao); state.gl.BindVertexArray(state.vao);
        state.gl.GenBuffers(1, &state.vertex_buffer); state.gl.BindBuffer(GL_ARRAY_BUFFER, state.vertex_buffer);
        state.gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
        state.gl.EnableVertexAttribArray(0); state.gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        state.gl.EnableVertexAttribArray(1); state.gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
            reinterpret_cast<const void*>(3 * sizeof(float)));
        state.gl.GenBuffers(1, &state.index_buffer); state.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.index_buffer);
        state.gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)), indices.data(), GL_STATIC_DRAW);
        check_gl("Uploading shared scene geometry");
        for (const auto& image : images) state.batch_textures.push_back(state.upload_texture(*image.image, image.options));
        for (std::size_t i = 0; i < draws.size(); ++i)
            if (image_indices[i] != no_image) draws[i].texture = state.batch_textures[image_indices[i]];
        state.batches = std::move(draws);
        state.index_count = indices.size(); state.batch_mode = true;
        state.center = center; state.radius = radius;
        // Retain immutable UV pairs for bounded per-batch pose uploads, without
        // keeping a second CPU copy of every normalized scene position/index.
        state.batch_uvs = std::move(uvs);
    } catch (...) { state.release_mesh(); throw; }
}

void MeshRenderer::update_positions(std::span<const Vec3> positions) {
    if (!has_mesh()) throw std::runtime_error("No mesh is uploaded for a pose update");
    auto& state = *impl_;
    if (state.batch_mode) throw std::runtime_error("Static scene batches do not support single-mesh pose updates");
    if (positions.size() != state.vertices.size() / 5)
        throw std::runtime_error("Pose vertex count must match the uploaded mesh");
    auto updated = state.vertices;
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const std::array<double, 3> position{positions[i].x, positions[i].y, positions[i].z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto normalized = state.prepared ? position[axis] : (position[axis] - state.center[axis]) / state.radius;
            if (!std::isfinite(normalized) || std::abs(normalized) > std::numeric_limits<float>::max())
                throw std::runtime_error("Pose contains a non-finite or unrepresentable coordinate");
            updated[i * 5 + axis] = static_cast<float>(normalized);
        }
    }
    SavedState saved(state.gl); discard_prior_errors();
    state.gl.BindBuffer(GL_ARRAY_BUFFER, state.vertex_buffer);
    state.gl.BufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(updated.size() * sizeof(float)), updated.data());
    check_gl("Updating mesh pose");
    state.vertices = std::move(updated);
}

void MeshRenderer::update_batch_positions(std::size_t batch_index, std::span<const Vec3> positions) {
    if (!has_mesh()) throw std::runtime_error("No scene is uploaded for a batch pose update");
    auto& state = *impl_;
    if (!state.batch_mode) {
        if (batch_index != 0) throw std::runtime_error("A single uploaded mesh has only batch zero");
        update_positions(positions);
        return;
    }
    if (batch_index >= state.batches.size()) throw std::runtime_error("Pose batch index exceeds the uploaded scene");
    const auto& batch = state.batches[batch_index];
    if (positions.size() != batch.vertex_count)
        throw std::runtime_error("Pose vertex count must match the selected scene batch");
    std::vector<float> updated(positions.size() * 5);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const std::array<double, 3> position{positions[i].x, positions[i].y, positions[i].z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto normalized = (position[axis] - state.center[axis]) / state.radius;
            if (!std::isfinite(normalized) || std::abs(normalized) > std::numeric_limits<float>::max())
                throw std::runtime_error("Batch pose contains a non-finite or unrepresentable coordinate");
            updated[i * 5 + axis] = static_cast<float>(normalized);
        }
        const auto& uv = state.batch_uvs[batch.first_vertex + i];
        updated[i * 5 + 3] = uv.u;
        updated[i * 5 + 4] = uv.v;
    }
    // Validation and staging complete before the sole subrange write. Adjacent
    // vertex ranges and all index/material objects are left intact.
    SavedState saved(state.gl); discard_prior_errors();
    state.gl.BindBuffer(GL_ARRAY_BUFFER, state.vertex_buffer);
    state.gl.BufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(batch.first_vertex * 5 * sizeof(float)),
        static_cast<GLsizeiptr>(updated.size() * sizeof(float)), updated.data());
    check_gl("Updating scene batch pose");
}

std::optional<MeshCanvasPoint> MeshRenderer::project_to_canvas(const Vec3& point,
    const OrbitCamera& camera, float aspect) const {
    if (!has_mesh()) return std::nullopt;
    const auto& state = *impl_;
    if (state.prepared) throw std::runtime_error("Prepared geometry requires explicit model-view and projection for canvas coordinates");
    const std::array<double, 4> position{(point.x - state.center[0]) / state.radius,
        (point.y - state.center[1]) / state.radius, (point.z - state.center[2]) / state.radius, 1};
    for (auto value : position)
        if (!std::isfinite(value)) throw std::runtime_error("Projected point must be finite");
    const auto matrix = camera.view_projection(aspect);
    std::array<double, 4> clip{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            clip[row] += matrix[column * 4 + row] * position[column];
    if (!std::isfinite(clip[3]) || clip[3] <= 0) return std::nullopt;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (!std::isfinite(clip[axis]) || std::abs(clip[axis]) > clip[3]) return std::nullopt;
    return MeshCanvasPoint{static_cast<float>(0.5 + 0.5 * clip[0] / clip[3]),
        static_cast<float>(0.5 - 0.5 * clip[1] / clip[3])};
}

void MeshRenderer::focus_camera(OrbitCamera& camera, const MeshBounds& source_bounds) const {
    if (!has_mesh()) throw std::runtime_error("No mesh is uploaded for camera focus");
    const auto& state = *impl_;
    if (state.prepared) throw std::runtime_error("Prepared geometry requires explicit camera placement");
    const std::array<double,3> low{source_bounds.min.x,source_bounds.min.y,source_bounds.min.z};
    const std::array<double,3> high{source_bounds.max.x,source_bounds.max.y,source_bounds.max.z};
    std::array<float,3> target{};
    std::array<double,3> extents{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(low[axis]) || !std::isfinite(high[axis]) || low[axis] > high[axis])
            throw std::runtime_error("Camera focus bounds must be finite and ordered");
        const auto normalized = ((low[axis] + high[axis]) * 0.5 - state.center[axis]) / state.radius;
        if (!std::isfinite(normalized) || std::abs(normalized) > 10000.0)
            throw std::runtime_error("Camera focus bounds exceed the normalized target range");
        target[axis] = static_cast<float>(normalized);
        extents[axis] = (high[axis] - low[axis]) * 0.5 / state.radius;
    }
    constexpr double half_fov_sine = 0.3826834323650898; // Half of the camera's 45-degree vertical field of view.
    const auto distance = std::max(0.05,1.1 * std::hypot(extents[0],extents[1],extents[2]) / half_fov_sine);
    if (!std::isfinite(distance) || distance > std::numeric_limits<float>::max())
        throw std::runtime_error("Camera focus distance is not representable");
    // All source conversion checks precede the camera's atomic target change.
    camera.focus(target,static_cast<float>(distance));
}

void MeshRenderer::set_texture(const RgbaImage& image, const MeshTextureOptions& options) {
    if (impl_ && impl_->batch_mode)
        throw std::runtime_error("Scene materials must be supplied through upload_batches");
    clear_texture();
    if (!has_mesh() || !impl_->has_uv) throw std::runtime_error("A textured mesh requires a UV pair for every vertex");
    image_bytes(image);
    auto& state = *impl_;
    SavedState saved(state.gl); discard_prior_errors();
    try { state.base_texture = state.upload_texture(image, options); }
    catch (...) { state.release_texture(); throw; }
}

std::uint32_t MeshRenderer::render(int width, int height, const OrbitCamera& camera,
    MeshDrawMode mode, int requested_samples, std::optional<std::size_t> occluded_batch) {
    if (impl_ && impl_->prepared) throw std::runtime_error("Prepared geometry requires explicit model-view and projection matrices");
    constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    return render_matrices(width,height,identity,camera.view_projection(static_cast<float>(width)/std::max(height,1)),mode,requested_samples,occluded_batch);
}
std::uint32_t MeshRenderer::render_prepared(int width,int height,
    const std::array<float,16>& model_view,const std::array<float,16>& projection,MeshDrawMode mode,int requested_samples) {
    if (!impl_ || !impl_->prepared) throw std::runtime_error("Explicit matrix rendering requires prepared source coordinates");
    return render_matrices(width,height,model_view,projection,mode,requested_samples,std::nullopt);
}
std::uint32_t MeshRenderer::render_matrices(int width,int height,
    const std::array<float,16>& model_view,const std::array<float,16>& matrix,
    MeshDrawMode mode,int requested_samples,std::optional<std::size_t> occluded_batch) {
    if (!has_mesh()) throw std::runtime_error("No mesh is uploaded");
    if (occluded_batch && *occluded_batch >= batch_count())
        throw std::runtime_error("Occluded batch index is outside the uploaded scene");
    if (mode == MeshDrawMode::textured) {
        if (impl_->batch_mode) {
            for (const auto& batch : impl_->batches)
                if (!batch.texture && !batch.allow_solid_fallback)
                    throw std::runtime_error("Scene Textured mode requires a verified image and UVs for every batch");
        } else if (!has_texture()) throw std::runtime_error("No verified mesh texture and UVs are uploaded");
    }
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || std::uint64_t(width) * height > 8388608)
        throw std::runtime_error("Mesh viewport exceeds the 4096-axis / 8-megapixel limit");
    for (const auto value:model_view) if (!std::isfinite(value)) throw std::runtime_error("Prepared model-view contains a non-finite value");
    for (const auto value:matrix) if (!std::isfinite(value)) throw std::runtime_error("Prepared projection contains a non-finite value");
    auto& state = *impl_;
    SavedState saved(state.gl); discard_prior_errors();
    state.targets(width, height, requested_samples, saved);
    state.gl.BindFramebuffer(GL_FRAMEBUFFER, state.draw_fbo);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (state.sample_count) glEnable(GL_MULTISAMPLE); else glDisable(GL_MULTISAMPLE);
    glClearColor(0.035f, 0.055f, 0.075f, 1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, mode == MeshDrawMode::wireframe ? GL_LINE : GL_FILL); glLineWidth(1);
    state.gl.UseProgram(state.program);
    state.gl.UniformMatrix4fv(state.matrix_location, 1, GL_FALSE, matrix.data());
    state.gl.UniformMatrix4fv(state.model_view_location, 1, GL_FALSE, model_view.data());
    state.gl.Uniform1i(state.prepared_location,state.prepared ? 1 : 0);
    state.gl.Uniform1i(state.mode_location, mode == MeshDrawMode::wireframe ? 1 : mode == MeshDrawMode::textured ? 2 : 0);
    if (mode == MeshDrawMode::textured) {
        state.gl.ActiveTexture(GL_TEXTURE0); state.gl.BindSampler(0, 0);
        state.gl.Uniform1i(state.texture_location, 0);
        // Basic inspection compositing, not the original material/alpha state.
        glEnable(GL_BLEND); state.gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        state.gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    state.gl.BindVertexArray(state.vao);
    const auto draw_silhouette = [&](std::size_t count, const void* offset) {
        // Test against static depth before this batch writes its own front
        // surfaces. Otherwise rear faces would highlight an unobstructed actor.
        glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        state.gl.Uniform1i(state.mode_location, 3);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, offset);
        glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
        glPolygonMode(GL_FRONT_AND_BACK, mode == MeshDrawMode::wireframe ? GL_LINE : GL_FILL);
        if (mode == MeshDrawMode::textured) glEnable(GL_BLEND);
        state.gl.Uniform1i(state.mode_location, mode == MeshDrawMode::wireframe ? 1 : mode == MeshDrawMode::textured ? 2 : 0);
    };
    if (state.batch_mode) {
        for (std::size_t index = 0; index < state.batches.size(); ++index) {
            const auto& batch = state.batches[index];
            const auto offset = static_cast<std::uintptr_t>(batch.first_index * sizeof(std::uint32_t));
            if (occluded_batch && *occluded_batch == index)
                draw_silhouette(batch.index_count, reinterpret_cast<const void*>(offset));
            if (mode == MeshDrawMode::textured) {
                state.gl.Uniform1i(state.mode_location, batch.texture ? 2 : 0);
                glBindTexture(GL_TEXTURE_2D, batch.texture);
            }
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.index_count), GL_UNSIGNED_INT,
                reinterpret_cast<const void*>(offset));
        }
    } else {
        if (occluded_batch) draw_silhouette(state.index_count, nullptr);
        if (mode == MeshDrawMode::textured) glBindTexture(GL_TEXTURE_2D, state.base_texture);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state.index_count), GL_UNSIGNED_INT, nullptr);
    }
    if (state.sample_count) {
        state.gl.BindFramebuffer(GL_READ_FRAMEBUFFER, state.draw_fbo);
        state.gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, state.resolve_fbo);
        state.gl.BlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    check_gl("Rendering mesh viewport");
    ++state.frames;
    return state.texture;
}
} // namespace dw4::viewer
