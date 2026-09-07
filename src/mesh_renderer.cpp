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
    X(PFNGLBUFFERDATAPROC, BufferData) X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
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
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture); glGetIntegerv(GL_VIEWPORT, viewport);
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
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
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
};

constexpr const char* vertex_source = R"GLSL(#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 view_projection;
out vec3 local_position;
void main() {
    local_position = position;
    gl_Position = view_projection * vec4(position, 1.0);
}
)GLSL";
constexpr const char* fragment_source = R"GLSL(#version 330 core
in vec3 local_position;
uniform int wireframe;
out vec4 color;
void main() {
    if (wireframe != 0) { color = vec4(0.30, 0.90, 0.79, 1.0); return; }
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
    Gl gl;
    GLuint program{}, vao{}, vertex_buffer{}, index_buffer{};
    GLuint draw_fbo{}, resolve_fbo{}, texture{}, depth_buffer{}, color_buffer{};
    GLint matrix_location{-1}, wire_location{-1};
    int width{}, height{}, sample_count{}, requested_sample_count{-1};
    std::size_t index_count{};
    std::uint64_t frames{};
    ~Impl() { release_mesh(); release_targets(); if (program) gl.DeleteProgram(program); }

    void release_mesh() {
        if (vao) gl.DeleteVertexArrays(1, &vao);
        if (vertex_buffer) gl.DeleteBuffers(1, &vertex_buffer);
        if (index_buffer) gl.DeleteBuffers(1, &index_buffer);
        vao = vertex_buffer = index_buffer = 0;
        index_count = 0; frames = 0;
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
            wire_location = gl.GetUniformLocation(program, "wireframe");
            if (matrix_location < 0 || wire_location < 0) throw std::runtime_error("Mesh shader uniforms are unavailable");
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
        if (saved.texture == static_cast<GLint>(texture)) saved.texture = 0;
        release_targets();
        try {
            glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, requested_width, requested_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            gl.GenFramebuffers(1, &draw_fbo); gl.BindFramebuffer(GL_FRAMEBUFFER, draw_fbo);
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
std::size_t MeshRenderer::triangle_count() const noexcept { return impl_ ? impl_->index_count / 3 : 0; }
std::uint64_t MeshRenderer::rendered_frames() const noexcept { return impl_ ? impl_->frames : 0; }
int MeshRenderer::samples() const noexcept { return impl_ ? impl_->sample_count : 0; }

void MeshRenderer::upload(const MeshData& mesh) {
    clear();
    if (mesh.positions.empty() || mesh.positions.size() > 1048576 || mesh.indices.empty()
        || mesh.indices.size() > 3145728 || mesh.indices.size() % 3)
        throw std::runtime_error("Mesh upload requires bounded triangle indices and positions");
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
    vertices.reserve(mesh.positions.size() * 3);
    for (const auto& position : mesh.positions) {
        vertices.push_back(static_cast<float>((position.x - center[0]) / radius));
        vertices.push_back(static_cast<float>((position.y - center[1]) / radius));
        vertices.push_back(static_cast<float>((position.z - center[2]) / radius));
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
        state.gl.EnableVertexAttribArray(0); state.gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        state.gl.GenBuffers(1, &state.index_buffer); state.gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, state.index_buffer);
        state.gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
        check_gl("Uploading mesh geometry");
        state.index_count = mesh.indices.size();
    } catch (...) { state.release_mesh(); throw; }
}

std::uint32_t MeshRenderer::render(int width, int height, const OrbitCamera& camera,
    MeshDrawMode mode, int requested_samples) {
    if (!has_mesh()) throw std::runtime_error("No mesh is uploaded");
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || std::uint64_t(width) * height > 8388608)
        throw std::runtime_error("Mesh viewport exceeds the 4096-axis / 8-megapixel limit");
    const auto matrix = camera.view_projection(static_cast<float>(width) / height);
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
    state.gl.Uniform1i(state.wire_location, mode == MeshDrawMode::wireframe ? 1 : 0);
    state.gl.BindVertexArray(state.vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state.index_count), GL_UNSIGNED_INT, nullptr);
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
