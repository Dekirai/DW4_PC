#include "gl_renderer.hpp"
#include "button_art.hpp"
#include "texture_pack.hpp"
#include "texture_stream.hpp"

#include "dw4/engine/gx_color.hpp"
#include "dw4/textures.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

// Extension enums (GL 3.3 core has these only as extensions).
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif

namespace dw4::game {

#define DW4_GAME_GL(X) \
    X(PFNGLGENSAMPLERSPROC, GenSamplers) X(PFNGLDELETESAMPLERSPROC, DeleteSamplers) X(PFNGLBINDSAMPLERPROC, BindSampler) \
    X(PFNGLSAMPLERPARAMETERIPROC, SamplerParameteri) X(PFNGLSAMPLERPARAMETERFPROC, SamplerParameterf) \
    X(PFNGLCOMPRESSEDTEXIMAGE2DPROC, CompressedTexImage2D) X(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, CompressedTexSubImage2D) \
    X(PFNGLUNIFORM2IPROC, Uniform2i) \
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
    int u_id_mode{}, u_id{}, u_lod_bias{};
    PFNGLUNIFORM2FPROC Uniform2f{};
    PFNGLTEXSTORAGE2DPROC TexStorage2D{}; // GL 4.2 / ARB_texture_storage (null without)
    // GX samples a pixel at 7/12 of its width and height instead of GL's 1/2
    // (unless antialiasing); Dolphin's VertexShaderManager compensates with
    // pixel_center_correction = 7/12 - 1/2, moving every vertex 1/12 of a
    // render pixel left and up. Clip-space offset for the current viewport.
    void pixel_center(int width, int height) {
        constexpr float kCorrection = 7.0f / 12.0f - 0.5f;
        if (width > 0 && height > 0) Uniform2f(u_pixel_center, -kCorrection * 2.0f / float(width), kCorrection * 2.0f / float(height));
    }
};

// A replacement going up in bands (GlRenderer::stream_textures).
struct GlRenderer::Upload {
    TexturePayload payload;
    GpuTexture texture;
    GLenum format{};             // 0: RGBA8
    std::size_t block_bytes{};
    std::size_t first{};         // the payload level that is GL level 0
    std::size_t level{};         // the payload level going up
    std::uint32_t row{};         // its next texel row
    bool whole_levels{};         // no immutable storage: each level whole
    GLenum error{};              // the first GL error while it went up
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
// The Graphics options' mip LOD bias for a mipmapped texture (0 for the
// others). In the shader rather than the sampler: GLES 3 samplers have no
// LOD bias, the shader's texture() bias works on both.
uniform float u_lod_bias;
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
    if (u_use_tex != 0) c *= texture(u_tex, v_uv, u_lod_bias);
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

// The Sharpening option: contrast adaptive sharpening of the game picture
// (written after the published description of AMD FidelityFX CAS, MIT: a
// cross-shaped negative lobe whose weight follows how far the 3x3
// neighbourhood is from clipping, so edges sharpen without halos and flat
// areas stay as they are). The picture comes copied into u_tex.
const char* kSharpenFragment = R"(#version 330 core
uniform sampler2D u_tex;
uniform float u_sharpness;
uniform ivec2 u_origin;
uniform ivec2 u_size;
out vec4 o_color;
vec3 at(ivec2 p) { return texelFetch(u_tex, clamp(p, ivec2(0), u_size - 1), 0).rgb; }
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) - u_origin;
    vec3 a = at(p + ivec2(-1, 1)), b = at(p + ivec2(0, 1)), c = at(p + ivec2(1, 1));
    vec3 d = at(p + ivec2(-1, 0)), e = at(p), f = at(p + ivec2(1, 0));
    vec3 g = at(p + ivec2(-1, -1)), h = at(p + ivec2(0, -1)), i = at(p + ivec2(1, -1));
    // Soft minimum and maximum: the cross plus the whole 3x3 (each 0..2).
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    mn += min(mn, min(min(a, c), min(g, i)));
    vec3 mx = max(max(max(d, e), max(f, b)), h);
    mx += max(mx, max(max(a, c), max(g, i)));
    vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, vec3(1e-5)), 0.0, 1.0));
    // The lobe: -1/8 (soft) .. -1/5 (strong).
    vec3 w = amp * (-1.0 / mix(8.0, 5.0, u_sharpness));
    o_color = vec4(clamp(((b + d + f + h) * w + e) / (1.0 + 4.0 * w), 0.0, 1.0), 1.0);
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

// The first GL error raised since the last call (GL_NO_ERROR for none); the
// error flags are cleared (a few at most: one per kind).
GLenum take_gl_error() {
    GLenum first = GL_NO_ERROR;
    for (int i = 0; i < 8; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        if (first == GL_NO_ERROR) first = error;
    }
    return first;
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
    U(shadow_matrix) U(shadow_tex) U(shadow_pass) U(texgen) U(pixel_center) U(normal) U(id_mode) U(id) U(lod_bias)
#undef U
    gl.UseProgram(program_);
    const engine::Mat4 identity{};
    gl.UniformMatrix4fv(gl.u_normal, 1, GL_FALSE, identity.m.data());
    gl.Uniform1f(gl.u_lod_bias, 0.0f);
    gl.UseProgram(0);
    gl.Uniform2f = reinterpret_cast<PFNGLUNIFORM2FPROC>(SDL_GL_GetProcAddress("glUniform2f"));
    glGenTextures(1, &white_texture_);
    glBindTexture(GL_TEXTURE_2D, white_texture_);
    const std::uint32_t white = 0xffffffffu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    // What packs and the filtering options can use (GL 3.3 core has BPTC,
    // S3TC and anisotropic filtering only as extensions; BPTC is core from 4.2).
    int major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    caps_.bc7 = SDL_GL_ExtensionSupported("GL_ARB_texture_compression_bptc") || major > 4 || (major == 4 && minor >= 2);
    if (SDL_GL_ExtensionSupported("GL_ARB_texture_storage") || major > 4 || (major == 4 && minor >= 2))
        gl.TexStorage2D = reinterpret_cast<PFNGLTEXSTORAGE2DPROC>(SDL_GL_GetProcAddress("glTexStorage2D"));
    caps_.bc1 = caps_.bc3 = SDL_GL_ExtensionSupported("GL_EXT_texture_compression_s3tc");
    caps_.anisotropic =
        SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic") || SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic");
    if (caps_.anisotropic) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &caps_.max_anisotropy);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &caps_.max_texture_size);
    caps_.memory_info = SDL_GL_ExtensionSupported("GL_NVX_gpu_memory_info");
    while (glGetError() != GL_NO_ERROR) {
    }
    // Test switches: DW4_TEXTURE_CPU_DECODE=1 decodes the block formats on
    // the loading thread as for a GPU without them; DW4_TEXTURE_UPLOAD_MS
    // sets the pass's upload time (default 1.5 ms).
    if (const char* cpu = std::getenv("DW4_TEXTURE_CPU_DECODE")) cpu_decode_ = std::atoi(cpu) != 0;
    if (const char* ms = std::getenv("DW4_TEXTURE_UPLOAD_MS")) upload_budget_ms_ = std::clamp(std::atof(ms), 0.1, 100.0);
    if (const char* trace = std::getenv("DW4_TRACE_TEXTURES")) trace_ = std::atoi(trace);
    // DW4_TEXTURE_TEST_REFUSE=<16 hex digits>: that pack texture's upload
    // raises a GL error, as a driver refusing it would (test of the fallback
    // to the game's own texture).
    if (const char* refuse = std::getenv("DW4_TEXTURE_TEST_REFUSE")) refuse_hash_ = std::strtoull(refuse, nullptr, 16);
    streamer_ = std::make_unique<TextureStreamer>();
}

GlRenderer::~GlRenderer() {
    // (The loading thread first: it holds no GL objects.)
    streamer_.reset();
    clear_cache();
    drop_originals();
    drop_replacements();
    drop_samplers();
    if (sharpen_texture_) glDeleteTextures(1, &sharpen_texture_);
    if (sharpen_program_) gl_->DeleteProgram(sharpen_program_);
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

std::optional<float> GlRenderer::read_depth(int x, int y, int window_width, int window_height) {
    if (x < 0 || y < 0 || x >= window_width || y >= window_height) return std::nullopt;
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i) {
    }
    float depth = 1.0f;
    glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    if (glGetError() != GL_NO_ERROR) return std::nullopt;
    return depth;
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
    for (auto& [_, split] : splits_) release(split);
    splits_.clear();
    for (auto& [_, t] : textures_) delete_texture(t.owned);
    textures_.clear();
    owned_bytes_ = 0;
    for (auto& [_, id] : title_variants_)
        if (id) glDeleteTextures(1, &id);
    title_variants_.clear();
}

// ------------------------------------------------------------ texture packs

bool GlRenderer::streaming() const {
    return pack_ && pack_->replacement_count() > 0 && graphics_.texture_quality != TextureQuality::original;
}

void GlRenderer::delete_texture(GpuTexture& texture) {
    if (texture.id) glDeleteTextures(1, &texture.id);
    texture = GpuTexture{};
}

void GlRenderer::drop_originals() {
    for (auto& [_, t] : originals_) delete_texture(t);
    originals_.clear();
    for (auto& [_, t] : textures_) delete_texture(t.owned);
    owned_bytes_ = 0;
}

void GlRenderer::drop_replacements() {
    if (upload_) {
        delete_texture(upload_->texture);
        upload_.reset();
    }
    for (auto& [_, t] : replacements_) delete_texture(t);
    replacements_.clear();
    failed_uploads_.clear();
    for (auto& [_, id] : title_variants_)
        if (id) glDeleteTextures(1, &id);
    title_variants_.clear();
}

void GlRenderer::drop_samplers() {
    for (auto& [_, id] : samplers_) gl_->DeleteSamplers(1, &id);
    samplers_.clear();
}

void GlRenderer::configure_streamer() {
    TextureStreamer::Config config;
    if (streaming()) config.pack = pack_->shared_index();
    config.target_scale = target_scale(graphics_.texture_quality);
    config.gpu = {caps_.bc1 && !cpu_decode_, caps_.bc3 && !cpu_decode_, caps_.bc7 && !cpu_decode_};
    // The title's PRESS START: the prompts redraw its START from its pixels.
    config.rgba_hashes = {kTitlePressStartHash};
    streamer_->configure(std::move(config));
}

void GlRenderer::refresh_pack_flags() {
    // Records hashed before know at once whether the pack has their texture;
    // records drawn while nothing was hashed (Original, or no pack) are
    // keyed from the scan of their document the loading thread now makes,
    // so a running stage switches to the pack's textures without a load.
    const bool on = streaming();
    for (auto& [_, t] : textures_) {
        t.in_pack = t.keyed && on && pack_->index().textures.count(t.pack_hash) != 0;
        t.rekey = on && !t.keyed;
        t.asked_frame = 0;
    }
}

void GlRenderer::set_texture_pack(TexturePack* pack) {
    pack_ = pack;
    drop_replacements();
    configure_streamer();
    refresh_pack_flags();
}

void GlRenderer::set_graphics(const GraphicsOptions& requested) {
    const GraphicsOptions options = requested.clamped();
    const GraphicsOptions old = graphics_;
    graphics_ = options;
    if (original_mipmaps(old.texture_filter) != original_mipmaps(options.texture_filter)) drop_originals();
    if (old.texture_filter != options.texture_filter) drop_samplers();
    if (old.texture_quality != options.texture_quality) {
        drop_replacements();
        configure_streamer();
        refresh_pack_flags();
    }
}

GlRenderer::TextureStats GlRenderer::texture_stats() const {
    TextureStats s = stats_;
    s.originals = originals_.size();
    s.original_bytes = owned_bytes_;
    for (const auto& [_, t] : textures_)
        if (t.owned.id) ++s.originals;
    for (const auto& [_, t] : originals_) s.original_bytes += t.bytes;
    s.replacements = replacements_.size();
    s.replacement_bytes = 0;
    for (const auto& [_, t] : replacements_) s.replacement_bytes += t.bytes;
    const auto streamed = streamer_->stats();
    s.pending = streamed.queued;
    s.images_scanned = streamed.images_scanned;
    s.loads = streamed.loads;
    s.failed += streamed.failures;
    s.scan_ms = streamed.scan_ms;
    s.load_ms = streamed.load_ms;
    if (s.last_error.empty()) s.last_error = streamed.last_error;
    if (caps_.memory_info) {
        GLint kb = -1;
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &kb);
        s.gpu_available_kb = kb;
        while (glGetError() != GL_NO_ERROR) {
        }
    }
    return s;
}

void GlRenderer::reset_texture_stats() {
    stats_ = TextureStats{};
    note_bytes();
}

void GlRenderer::note_bytes() {
    std::size_t total = owned_bytes_;
    for (const auto& [_, t] : originals_) total += t.bytes;
    for (const auto& [_, t] : replacements_) total += t.bytes;
    stats_.peak_bytes = std::max(stats_.peak_bytes, total);
}

void GlRenderer::stream_textures(const std::vector<std::shared_ptr<const IgbDocument>>* documents) {
    if (documents) streamer_->set_documents(*documents);
    // Uploads within the pass's time budget: a replacement goes up in bands
    // of rows (a large one over several passes) and is drawn once it is
    // complete. Each pass moves at least one piece (a texture's storage or
    // a band); after that a piece goes only while the measured rates say it
    // ends within the budget, so a pass ends near the budget, not one band
    // past it.
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::microseconds(std::int64_t(upload_budget_ms_ * 1000.0));
    bool moved = false;
    for (;;) {
        if (!upload_) {
            if (moved && std::chrono::steady_clock::now() + std::chrono::microseconds(std::int64_t(begin_upload_ms_ * 1000.0)) > deadline)
                break;
            auto payload = streamer_->take();
            if (!payload) break;
            const auto begun = std::chrono::steady_clock::now();
            begin_upload(std::move(*payload));
            if (!upload_) continue;
            moved = true;
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
            begin_upload_ms_ = std::clamp(0.75 * begin_upload_ms_ + 0.25 * ms, 0.001, 100.0);
        }
        const bool done = continue_upload(deadline, moved);
        if (done) finish_upload();
        if (!done || std::chrono::steady_clock::now() >= deadline) break;
    }
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    stats_.upload_ms += elapsed;
    pass_texture_ms_ += elapsed;
    stats_.longest_upload_pass_ms = std::max(stats_.longest_upload_pass_ms, elapsed);
    // The memory budget: the least recently drawn textures known by hash go
    // (never one drawn in the last two frames); a replacement may be read
    // again when it is drawn later.
    const std::size_t budget = std::size_t(graphics_.texture_memory_mb) << 20;
    std::size_t total = owned_bytes_;
    for (const auto& [_, t] : originals_) total += t.bytes;
    for (const auto& [_, t] : replacements_) total += t.bytes;
    if (total > budget) {
        std::vector<BudgetEntry> entries;
        std::unordered_map<std::uint64_t, std::pair<bool, std::uint64_t>> by_slot; // slot -> (replacement, hash)
        for (const auto& [hash, t] : originals_) {
            entries.push_back({t.slot, t.bytes, t.last_frame});
            by_slot[t.slot] = {false, hash};
        }
        for (const auto& [hash, t] : replacements_) {
            entries.push_back({t.slot, t.bytes, t.last_frame});
            by_slot[t.slot] = {true, hash};
        }
        const std::size_t shared_budget = budget > owned_bytes_ ? budget - owned_bytes_ : 0;
        for (const auto slot : choose_evictions(std::move(entries), shared_budget, frame_ >= 2 ? frame_ - 2 : 0)) {
            const auto [replacement, hash] = by_slot[slot];
            auto& map = replacement ? replacements_ : originals_;
            if (auto found = map.find(hash); found != map.end()) {
                delete_texture(found->second);
                map.erase(found);
                if (replacement) streamer_->forget(hash);
                ++stats_.evictions;
            }
        }
    }
    note_bytes();
}

void GlRenderer::begin_upload(TexturePayload payload) {
    if (payload.generation != streamer_->generation() || payload.levels.empty() || !streaming()) return;
    auto up = std::make_unique<Upload>();
    switch (payload.encoding) {
    case TextureEncoding::bc1: up->format = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; up->block_bytes = 8; break;
    case TextureEncoding::bc3: up->format = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; up->block_bytes = 16; break;
    case TextureEncoding::bc7: up->format = GL_COMPRESSED_RGBA_BPTC_UNORM; up->block_bytes = 16; break;
    case TextureEncoding::rgba8: break;
    default:
        failed_uploads_[payload.hash] = true;
        ++stats_.failed;
        return;
    }
    // Levels larger than the GPU takes are left out.
    while (up->first + 1 < payload.levels.size() && (int(payload.levels[up->first].width) > caps_.max_texture_size ||
                                                      int(payload.levels[up->first].height) > caps_.max_texture_size))
        ++up->first;
    up->level = up->first;
    const auto& top = payload.levels[up->first];
    const GLsizei levels = GLsizei(payload.levels.size() - up->first);
    take_gl_error(); // (errors of earlier calls are not this texture's)
    glGenTextures(1, &up->texture.id);
    glBindTexture(GL_TEXTURE_2D, up->texture.id);
    // Immutable storage for every level at once (GL 4.2 / ARB_texture_storage),
    // then the rows in bands; without it each level goes up whole.
    if (gl_->TexStorage2D)
        gl_->TexStorage2D(GL_TEXTURE_2D, levels, up->format ? up->format : GL_RGBA8, GLsizei(top.width), GLsizei(top.height));
    else
        up->whole_levels = true;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    if (const GLenum error = take_gl_error()) {
        // (The driver refused the format or the size: the game's own stays.)
        glDeleteTextures(1, &up->texture.id);
        refuse_upload(payload.hash, error);
        return;
    }
    up->texture.levels = int(levels);
    up->texture.width = int(top.width);
    up->texture.height = int(top.height);
    for (std::size_t i = up->first; i < payload.levels.size(); ++i) up->texture.bytes += payload.levels[i].size;
    up->payload = std::move(payload);
    upload_ = std::move(up);
}

bool GlRenderer::continue_upload(std::chrono::steady_clock::time_point deadline, bool& moved) {
    auto& up = *upload_;
    take_gl_error(); // (errors of the frame's draws are not this texture's)
    glBindTexture(GL_TEXTURE_2D, up.texture.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    constexpr std::size_t kBandBytes = std::size_t(256) << 10;
    while (up.level < up.payload.levels.size()) {
        const auto& l = up.payload.levels[up.level];
        const GLint gl_level = GLint(up.level - up.first);
        const std::uint8_t* bytes = up.payload.data.data() + l.offset;
        // The next piece: the whole level without immutable storage, else a
        // band of rows (block rows of four texel rows for the compressed
        // formats; the last band may end at the edge of the level, which
        // compressed sub-images allow).
        std::uint32_t rows = l.height - up.row;
        std::size_t offset = 0, size = l.size;
        if (!up.whole_levels) {
            if (up.format) {
                const std::size_t row_bytes = std::size_t((l.width + 3) / 4) * up.block_bytes;
                const auto block_rows = std::uint32_t(std::max<std::size_t>(1, kBandBytes / std::max<std::size_t>(row_bytes, 1)));
                rows = std::min(l.height - up.row, block_rows * 4);
                offset = std::size_t(up.row / 4) * row_bytes;
                size = std::size_t((rows + 3) / 4) * row_bytes;
            } else {
                const std::size_t row_bytes = std::size_t(l.width) * 4;
                rows = std::min(l.height - up.row, std::uint32_t(std::max<std::size_t>(1, kBandBytes / row_bytes)));
                offset = std::size_t(up.row) * row_bytes;
                size = std::size_t(rows) * row_bytes;
            }
        }
        const auto before = std::chrono::steady_clock::now();
        if (moved && before + std::chrono::nanoseconds(std::int64_t(double(size) * upload_ns_per_byte_)) > deadline) break;
        if (up.whole_levels) {
            if (up.format)
                gl_->CompressedTexImage2D(GL_TEXTURE_2D, gl_level, up.format, GLsizei(l.width), GLsizei(l.height), 0, GLsizei(l.size), bytes);
            else
                glTexImage2D(GL_TEXTURE_2D, gl_level, GL_RGBA8, GLsizei(l.width), GLsizei(l.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, bytes);
            ++up.level;
        } else {
            if (up.format)
                gl_->CompressedTexSubImage2D(GL_TEXTURE_2D, gl_level, 0, GLint(up.row), GLsizei(l.width), GLsizei(rows), up.format,
                                             GLsizei(size), bytes + offset);
            else
                glTexSubImage2D(GL_TEXTURE_2D, gl_level, 0, GLint(up.row), GLsizei(l.width), GLsizei(rows), GL_RGBA, GL_UNSIGNED_BYTE,
                                bytes + offset);
            up.row += rows;
            if (up.row >= l.height) {
                up.row = 0;
                ++up.level;
            }
        }
        const auto after = std::chrono::steady_clock::now();
        moved = true;
        // (The rate from pieces large enough to time; tiny mip levels cost
        // little either way.)
        if (size >= 16384)
            upload_ns_per_byte_ = std::clamp(0.75 * upload_ns_per_byte_ +
                                                 0.25 * std::chrono::duration<double, std::nano>(after - before).count() / double(size),
                                             0.01, 1000.0);
        if (after >= deadline) break;
    }
    if (const GLenum error = take_gl_error(); error && !up.error) up.error = error;
    return up.level >= up.payload.levels.size();
}

void GlRenderer::refuse_upload(std::uint64_t hash, unsigned error) {
    failed_uploads_[hash] = true;
    ++stats_.failed;
    char text[160];
    std::snprintf(text, sizeof text, "the GPU refused pack texture %016llx (GL error 0x%04x): the game's own texture is drawn",
                  (unsigned long long)hash, error);
    stats_.last_error = text;
    std::cerr << "textures: " << text << '\n';
}

void GlRenderer::finish_upload() {
    auto up = std::move(upload_);
    if (refuse_hash_ && up->payload.hash == refuse_hash_) {
        // (The test switch: an invalid parameter value, GL_INVALID_ENUM.)
        glBindTexture(GL_TEXTURE_2D, up->texture.id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x1234);
        if (const GLenum error = take_gl_error(); error && !up->error) up->error = error;
    }
    if (up->error) {
        delete_texture(up->texture);
        refuse_upload(up->payload.hash, up->error);
        return;
    }
    up->texture.slot = next_slot_++;
    up->texture.last_frame = frame_;
    if (up->first == 0) up->texture.rgba = std::move(up->payload.rgba);
    ++stats_.uploads;
    stats_.upload_bytes += up->texture.bytes;
    auto& stored = replacements_[up->payload.hash];
    delete_texture(stored);
    stored = std::move(up->texture);
    // (A title variant made from the game's own picture is made again.)
    if (up->payload.hash == kTitlePressStartHash) {
        for (auto& [_, id] : title_variants_)
            if (id) glDeleteTextures(1, &id);
        title_variants_.clear();
    }
}

unsigned GlRenderer::sampler(const Texture& texture, const GpuTexture& gpu) {
    // IG_GFX_TEXTURE_FILTER: 0 NEAREST, 1 LINEAR, 2 NEAREST_MIPMAP_NEAREST,
    // 3 NEAREST_MIPMAP_LINEAR, 4 LINEAR_MIPMAP_NEAREST, 5 LINEAR_MIPMAP_LINEAR
    // (min -> GX table 0x80414A40, mag table 0x80414A58 = nearest for 0/2/3).
    // A texture without mip levels samples its full-size image only, as GX
    // does whatever the min filter says; one with levels (a pack's, or the
    // game's own with Trilinear and above) blends them, with the options'
    // anisotropy and LOD bias.
    static const GLenum min_filters[] = {GL_NEAREST, GL_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR,
                                         GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR};
    const bool mip = gpu.levels > 1;
    const int min_index = mip ? 5 : (texture.min_filter >= 0 && texture.min_filter < 6 ? texture.min_filter : 1);
    const unsigned key = unsigned(texture.wrap_s != 0) | unsigned(texture.wrap_t != 0) << 1 | unsigned(texture.mag_nearest) << 2 |
                         unsigned(min_index) << 3 | unsigned(mip) << 6;
    if (const auto found = samplers_.find(key); found != samplers_.end()) return found->second;
    auto& gl = *gl_;
    unsigned id = 0;
    gl.GenSamplers(1, &id);
    // GX wraps (texture_sampler): 0 CLAMP, 1 REPEAT.
    gl.SamplerParameteri(id, GL_TEXTURE_WRAP_S, texture.wrap_s == 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    gl.SamplerParameteri(id, GL_TEXTURE_WRAP_T, texture.wrap_t == 0 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    gl.SamplerParameteri(id, GL_TEXTURE_MAG_FILTER, texture.mag_nearest ? GL_NEAREST : GL_LINEAR);
    gl.SamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GLint(min_filters[min_index]));
    // (The LOD bias is the shader's: u_lod_bias, render().)
    if (mip && caps_.anisotropic)
        gl.SamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY,
                             std::clamp(float(anisotropy(graphics_.texture_filter)), 1.0f, std::max(caps_.max_anisotropy, 1.0f)));
    samplers_[key] = id;
    return id;
}

GlRenderer::Texture* GlRenderer::texture(const engine::Attribute* binding) {
    if (!binding || !binding->image) return nullptr;
    const auto key = std::make_pair(static_cast<const void*>(binding->image.document()), binding->image.reference());
    if (auto found = textures_.find(key); found != textures_.end()) {
        found->second.binding = binding;
        return found->second.failed ? nullptr : &found->second;
    }
    Texture t;
    t.binding = binding;
    const auto& s = binding->sampler;
    t.wrap_s = s.wrap_s;
    t.wrap_t = s.wrap_t;
    t.min_filter = s.min_filter;
    t.mag_nearest = s.mag_filter == 0 || s.mag_filter == 2 || s.mag_filter == 3;
    if (const auto mips = binding->texture.ref(16)) t.authored_mips = !mips.elements().empty();
    // With a pack streaming, the loading thread scanned the stage's
    // documents: the hashes come from it, without a decode here.
    if (streaming()) key_from_scan(t);
    return &textures_.emplace(key, std::move(t)).first->second;
}

bool GlRenderer::key_from_scan(Texture& t) {
    const auto scanned = streamer_->lookup(t.binding->image.document(), t.binding->image.reference());
    if (!scanned) return false;
    t.keyed = true;
    t.scanned = true;
    t.pack_hash = scanned->pack_hash;
    t.pixel_hash = scanned->pixel_hash;
    t.width = scanned->width;
    t.height = scanned->height;
    t.in_pack = scanned->in_pack;
    t.title_press = t.title_press || (t.width == 256 && t.height == 128 && t.pixel_hash == kTitlePressStartHash &&
                                      t.binding->image.str(22) == "mojigc.png");
    return true;
}

const GlRenderer::GpuTexture* GlRenderer::resolve(Texture& t, bool& replaced) {
    replaced = false;
    if (t.failed) return nullptr;
    if (t.rekey) {
        // The pack or the quality changed while this record had no hashes:
        // keyed from its document's scan, which the loading thread makes
        // now (the game's own picture is drawn until then). A document the
        // thread does not scan is decoded and hashed here once.
        if (!streaming() || t.keyed || key_from_scan(t)) {
            t.rekey = false;
        } else if (!streamer_->pending(t.binding->image.document())) {
            t.rekey = false;
            if (t.owned.id) {
                owned_bytes_ -= std::min(owned_bytes_, t.owned.bytes);
                delete_texture(t.owned);
            }
        }
    }
    if (t.in_pack && streaming()) {
        if (auto found = replacements_.find(t.pack_hash); found != replacements_.end()) {
            found->second.last_frame = frame_;
            replaced = true;
            return &found->second;
        }
        // (A replacement the GPU refused: the game's own, not counted as missing.)
        if (!failed_uploads_.count(t.pack_hash)) {
            ++stats_.missing_draws;
            if (trace_ >= 2 && t.asked_frame == 0)
                std::printf("textures: frame %llu draws %s (%016llx, %ux%u) before its pack texture (%s)\n", (unsigned long long)frame_,
                            std::string(t.binding->image.str(22)).c_str(), (unsigned long long)t.pack_hash, t.width, t.height,
                            t.scanned ? "read ahead, not uploaded yet" : "its document was not scanned yet");
            // (Asked again now and then: an evicted replacement is read again.)
            if (t.asked_frame == 0 || frame_ - t.asked_frame >= 30) {
                streamer_->request(t.pack_hash, t.width, t.height);
                t.asked_frame = frame_ ? frame_ : 1;
            }
        }
    }
    // (The title's PRESS START keeps its decoded pixels for the prompts: a
    // record without them decodes once.)
    if (t.keyed && !t.authored_mips && !(t.title_press && t.original.empty())) {
        if (auto found = originals_.find(t.pixel_hash); found != originals_.end()) {
            found->second.last_frame = frame_;
            return &found->second;
        }
    }
    if (t.owned.id) {
        t.owned.last_frame = frame_;
        return &t.owned;
    }
    return upload_original(t);
}

const GlRenderer::GpuTexture* GlRenderer::upload_original(Texture& t) {
    const auto* binding = t.binding;
    const auto work_started = std::chrono::steady_clock::now();
    struct Timed {
        double& total;
        std::chrono::steady_clock::time_point started;
        ~Timed() { total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); }
    } timed{pass_texture_ms_, work_started};
    try {
        auto image = decode_igb_texture(*binding->image.document(), binding->image.reference());
        const int width = int(image.width), height = int(image.height);
        t.width = image.width;
        t.height = image.height;
        const bool dumping = pack_ && pack_->dumping();
        if (!t.keyed && (streaming() || dumping)) {
            // Texture packs: dumps and replacements by pixel hash. CMPR
            // textures keep the hash of the earlier decode with black
            // transparent texels, so existing packs still match them
            // (TexturePack::image_hash, shared with tools/dw4_texpack).
            t.pack_hash = TexturePack::image_hash(image);
            t.pixel_hash = image.block_compressed ? TexturePack::hash(image.rgba.data(), image.rgba.size(), width, height) : t.pack_hash;
            t.keyed = true;
            t.in_pack = streaming() && pack_->index().textures.count(t.pack_hash) != 0;
            if (t.in_pack) {
                if (trace_ >= 2)
                    std::printf("textures: frame %llu hashes %s (%016llx) itself: its document was not scanned yet\n", (unsigned long long)frame_,
                                std::string(binding->image.str(22)).c_str(), (unsigned long long)t.pack_hash);
                streamer_->request(t.pack_hash, t.width, t.height);
                t.asked_frame = frame_ ? frame_ : 1;
            }
        }
        if (dumping) pack_->dump(std::string(binding->image.str(22)), image.rgba.data(), width, height, t.pack_hash);
        // The title's PRESS START (the USA mojigc.png, or a texture pack's
        // replacement of it): the prompts draw its START with player 1's
        // picture (button_art.hpp title_press_start; a replacement whose
        // START does not stand where the original's does is left alone).
        if (image.width == 256 && image.height == 128 && binding->image.str(22) == "mojigc.png" &&
            (t.keyed ? t.pixel_hash : TexturePack::hash(image.rgba.data(), image.rgba.size(), 256, 128)) == kTitlePressStartHash) {
            t.title_press = true;
            t.width_px = width;
            t.height_px = height;
            t.original = image.rgba;
            t.nearest = t.mag_nearest;
        }
        GpuTexture g;
        glGenTextures(1, &g.id);
        glBindTexture(GL_TEXTURE_2D, g.id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        g.bytes = image.rgba.size();
        g.width = width;
        g.height = height;
        // The GX texture object (800FE744) is mipmapped only when the texture
        // carries mip images: GXInitTexObj(mipmap = count != 0) at 0x800FEDE4,
        // GXInitTexObjLOD(max_lod = count) at 0x800FEE50. Without them GX
        // samples level 0 only, whatever the min filter says. The game's IGBs
        // never store mip images (igTextureAttr _imageCount 1, empty
        // _imageMipMaps), so the original's textures stay sharp when shrunk.
        // Trilinear filtering and above (an option) give them a mip chain
        // (texture_codec.hpp mip_chain: colours by alpha, cut-outs keep a
        // binary alpha).
        int levels = 0;
        if (t.authored_mips) {
            if (const auto mips = binding->texture.ref(16)) {
                int w = width, h = height;
                for (const auto& mip : mips.elements()) {
                    if (!mip || (w <= 1 && h <= 1)) break;
                    w = std::max(w / 2, 1), h = std::max(h / 2, 1);
                    const auto level = decode_igb_texture(*mip.document(), mip.reference());
                    if (int(level.width) != w || int(level.height) != h) break;
                    glTexImage2D(GL_TEXTURE_2D, levels + 1, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, level.rgba.data());
                    g.bytes += level.rgba.size();
                    ++levels;
                }
            }
        } else if (original_mipmaps(graphics_.texture_filter) && (width > 1 || height > 1)) {
            std::vector<TextureLevel> chain_levels;
            const auto kind = classify_alpha(image.rgba);
            const auto chain = mip_chain(std::move(image.rgba), image.width, image.height, kind, chain_levels);
            for (std::size_t i = 1; i < chain_levels.size(); ++i) {
                const auto& l = chain_levels[i];
                glTexImage2D(GL_TEXTURE_2D, GLint(i), GL_RGBA8, GLsizei(l.width), GLsizei(l.height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                             chain.data() + l.offset);
                g.bytes += l.size;
            }
            levels = int(chain_levels.size()) - 1;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels);
        g.levels = levels + 1;
        g.slot = next_slot_++;
        g.last_frame = frame_;
        // Shared by pixel hash when a pack streams (the stage's copies of
        // one picture, and the next stages', upload once), else the
        // binding's own until the stage changes.
        if (t.keyed && !t.authored_mips) {
            auto& stored = originals_[t.pixel_hash];
            delete_texture(stored);
            stored = std::move(g);
            note_bytes();
            return &stored;
        }
        owned_bytes_ += g.bytes;
        t.owned = std::move(g);
        note_bytes();
        return &t.owned;
    } catch (const std::exception& error) {
        std::cerr << "texture decode failed: " << error.what() << '\n';
        t.failed = true;
        return nullptr;
    }
}

void GlRenderer::sharpen(const Viewport& area) {
    if (area.width <= 0 || area.height <= 0) return;
    auto& gl = *gl_;
    if (!sharpen_program_) {
        const unsigned vs = compile(gl, GL_VERTEX_SHADER, kKeptVertex);
        const unsigned fs = compile(gl, GL_FRAGMENT_SHADER, kSharpenFragment);
        sharpen_program_ = gl.CreateProgram();
        gl.AttachShader(sharpen_program_, vs);
        gl.AttachShader(sharpen_program_, fs);
        gl.LinkProgram(sharpen_program_);
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        int ok = 0;
        gl.GetProgramiv(sharpen_program_, GL_LINK_STATUS, &ok);
        if (!ok) throw std::runtime_error("sharpening shader link failed");
    }
    if (!movie_vao_) gl.GenVertexArrays(1, &movie_vao_);
    if (!sharpen_texture_) glGenTextures(1, &sharpen_texture_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, sharpen_texture_);
    if (area.width != sharpen_width_ || area.height != sharpen_height_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, area.width, area.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        sharpen_width_ = area.width;
        sharpen_height_ = area.height;
    }
    // (The window's picture, multisampling resolved as keep_picture's copy.)
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, area.x, area.y, area.width, area.height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_CLAMP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glViewport(area.x, area.y, area.width, area.height);
    gl.UseProgram(sharpen_program_);
    gl.Uniform1i(gl.GetUniformLocation(sharpen_program_, "u_tex"), 0);
    gl.Uniform1f(gl.GetUniformLocation(sharpen_program_, "u_sharpness"), sharpness(graphics_.sharpening));
    gl.Uniform2i(gl.GetUniformLocation(sharpen_program_, "u_origin"), area.x, area.y);
    gl.Uniform2i(gl.GetUniformLocation(sharpen_program_, "u_size"), area.width, area.height);
    gl.BindVertexArray(movie_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl.BindVertexArray(0);
    gl.UseProgram(program_);
}

void GlRenderer::release(PromptSplit& split) {
    auto& gl = *gl_;
    if (split.text_vbo) gl.DeleteBuffers(1, &split.text_vbo);
    if (split.text_ibo) gl.DeleteBuffers(1, &split.text_ibo);
    if (split.text_vao) gl.DeleteVertexArrays(1, &split.text_vao);
    if (split.icon_vbo) gl.DeleteBuffers(1, &split.icon_vbo);
    if (split.icon_ibo) gl.DeleteBuffers(1, &split.icon_ibo);
    if (split.icon_vao) gl.DeleteVertexArrays(1, &split.icon_vao);
    split = PromptSplit{};
}

void GlRenderer::set_prompt_glyphs(const PromptGlyphs& glyphs) {
    glyphs_ = glyphs;
    for (auto& [_, split] : splits_) release(split);
    splits_.clear();
}

namespace {
// The vertex layout of GlRenderer::mesh (52 bytes) on a vertex array.
template <class Gl>
void vertex_layout(Gl& gl) {
    constexpr GLsizei stride = 52;
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
}

// Quads in the vertex layout above (white, unweighted), in layout_text's
// winding.
struct QuadBuffer {
    static constexpr std::size_t kStride = 52;
    std::vector<std::uint8_t> data;
    std::vector<std::uint32_t> indices;
    // corners: x, y, z, u, v in layout_text's order (top left, top right,
    // bottom right, bottom left).
    void add(const std::array<std::array<float, 5>, 4>& corners) {
        const auto base = std::uint32_t(data.size() / kStride);
        for (const auto& c : corners) {
            std::uint8_t vertex[kStride]{};
            std::memcpy(vertex, c.data(), 12);
            const std::uint32_t white = 0xffffffffu;
            std::memcpy(vertex + 24, &white, 4);
            std::memcpy(vertex + 28, &c[3], 8);
            const float weight = 1.0f;
            std::memcpy(vertex + 36, &weight, 4);
            data.insert(data.end(), vertex, vertex + kStride);
        }
        indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    }
};

template <class Gl>
void upload(Gl& gl, const QuadBuffer& quads, unsigned& vao, unsigned& vbo, unsigned& ibo) {
    gl.GenVertexArrays(1, &vao);
    gl.BindVertexArray(vao);
    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(quads.data.size()), quads.data.data(), GL_STATIC_DRAW);
    vertex_layout(gl);
    gl.GenBuffers(1, &ibo);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(quads.indices.size() * 4), quads.indices.data(), GL_STATIC_DRAW);
}
} // namespace

// layout_text (ui.cpp) makes four vertices and six indices per glyph, in
// text order; prompt_layout.hpp decides which become pictures and where the
// rest goes. Kept per geometry; laid out again when its player's START
// picture has another width (another family).
const GlRenderer::PromptSplit* GlRenderer::prompt_split(const engine::GeometryData* g) {
    if (!g || !prompts_) return nullptr;
    PromptSource::Line line;
    bool line_known = false;
    if (auto found = splits_.find(g); found != splits_.end()) {
        auto& split = found->second;
        if (split.serial == g->serial.value) {
            if (split.none) return nullptr;
            if (!split.words || prompts_->atlas_word(split.player).aspect == split.word_aspect) return &split;
            line = {split.player, split.align};
            line_known = true;
        }
        release(split);
        splits_.erase(found);
    }
    if (!line_known) line = prompts_->line_info(g);
    const float aspect = prompts_->atlas_word(line.player).aspect;
    const PromptLayout layout = layout_prompts(*g, glyphs_, aspect, line.align);
    PromptSplit split;
    split.serial = g->serial.value;
    split.player = line.player;
    split.align = line.align;
    split.word_aspect = aspect;
    split.words = layout.words > 0;
    // DW4_TRACE_PROMPTS=1 (test runs): every text line with prompts.
    static const bool trace = std::getenv("DW4_TRACE_PROMPTS") != nullptr;
    if (trace && !layout.pictures.empty()) {
        std::printf("prompts: geometry %llu, %zu quads, player %d, align %d, %zu pictures, %d words\n", (unsigned long long)g->serial.value,
                    g->positions.size() / 4, line.player, line.align, layout.pictures.size(), layout.words);
        for (const auto& p : layout.pictures) std::printf("  picture %d x %.3f..%.3f y %.3f..%.3f\n", p.control, p.x0, p.x1, p.y0, p.y1);
        std::fflush(stdout);
    }
    auto& stored = splits_[g];
    if (layout.pictures.empty()) {
        split.none = true;
        stored = split;
        return nullptr;
    }
    QuadBuffer text, icons;
    for (const auto& t : layout.text) {
        std::array<std::array<float, 5>, 4> corners{};
        for (std::size_t k = 0; k < 4; ++k) {
            const auto& p = g->positions[t.quad * 4 + k];
            corners[k] = {p.x + t.shift, p.y, p.z, g->texcoords[(t.quad * 4 + k) * 2], g->texcoords[(t.quad * 4 + k) * 2 + 1]};
        }
        text.add(corners);
    }
    for (const auto& p : layout.pictures) {
        // uv: u0, v top, u1, v bottom; y points up in the text's space.
        const auto uv = p.control >= 0 ? prompts_->atlas_cell(p.control) : prompts_->atlas_word(line.player).uv;
        icons.add({{{p.x0, p.y1, p.z, uv[0], uv[1]}, {p.x1, p.y1, p.z, uv[2], uv[1]}, {p.x1, p.y0, p.z, uv[2], uv[3]}, {p.x0, p.y0, p.z, uv[0], uv[3]}}});
    }
    auto& gl = *gl_;
    if (!text.indices.empty()) {
        upload(gl, text, split.text_vao, split.text_vbo, split.text_ibo);
        split.text_count = int(text.indices.size());
    }
    upload(gl, icons, split.icon_vao, split.icon_vbo, split.icon_ibo);
    split.icon_count = int(icons.indices.size());
    gl.BindVertexArray(0);
    stored = split;
    return &stored;
}

// The title's PRESS START with player 1's START picture, made from the
// pixels drawn (the pack's replacement when it carries them, else the
// game's own); 0 when there is none (the texture is drawn as it is).
unsigned GlRenderer::title_texture(const Texture& t, const GpuTexture* replacement) {
    const bool from_pack = replacement && !replacement->rgba.empty();
    if (!from_pack && t.original.empty()) return 0;
    const std::uint64_t key = prompts_->art_key(0) ^ (from_pack ? 0x9e3779b97f4a7c15ull : 0);
    for (std::size_t i = 0; i < title_variants_.size(); ++i)
        if (title_variants_[i].first == key) {
            // (The latest first.)
            std::rotate(title_variants_.begin(), title_variants_.begin() + std::ptrdiff_t(i), title_variants_.begin() + std::ptrdiff_t(i) + 1);
            return title_variants_.front().second;
        }
    // Three variants at most (8 x the original: about 11 MB each with mipmaps).
    while (title_variants_.size() >= 3) {
        if (title_variants_.back().second) glDeleteTextures(1, &title_variants_.back().second);
        title_variants_.pop_back();
    }
    unsigned id = 0;
    std::vector<std::uint8_t> pixels;
    int width = 0, height = 0;
    const bool nearest = !from_pack && t.nearest;
    if (from_pack ? prompts_->title_variant(replacement->rgba, replacement->width, replacement->height, false, pixels, width, height)
                  : prompts_->title_variant(t.original, t.width_px, t.height_px, nearest, pixels, width, height)) {
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        gl_->GenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    title_variants_.insert(title_variants_.begin(), {key, id});
    return id;
}

void GlRenderer::release(Mesh& m) {
    auto& gl = *gl_;
    for (auto it = splits_.begin(); it != splits_.end(); ++it)
        if (it->second.serial == m.serial) {
            release(it->second);
            splits_.erase(it);
            break;
        }
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

// igProjectiveShadowShader: the casters from the light into the map.
void GlRenderer::render_shadow_map() {
    auto& gl = *gl_;
    shadow_ready_ = false;
    if (!shadow_ || !shadow_->active || shadow_->casters.empty()) return;
    // Shadow quality (an option): the map with 4x / 8x the texels per side
    // and its border scaled alike, so the light box maps to the same part
    // of it (only the texel size changes; the GPU's limit caps it).
    int scale = shadow_scale(graphics_.shadow_quality);
    while (scale > 1 && std::max(shadow_->width, shadow_->height) * scale > caps_.max_texture_size) scale /= 2;
    const int map_width = shadow_->width * scale, map_height = shadow_->height * scale;
    shadow_border_ = shadow_->border * scale;
    if (!shadow_fbo_ || shadow_width_ != map_width || shadow_height_ != map_height) {
        if (!shadow_texture_) glGenTextures(1, &shadow_texture_);
        glBindTexture(GL_TEXTURE_2D, shadow_texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, map_width, map_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
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
        shadow_width_ = map_width;
        shadow_height_ = map_height;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, shadow_width_, shadow_height_);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    // 801FD040 (0x801FD1D0): the casters go into _shadowInnerViewport, the
    // map inset by the border on every side.
    const int border = shadow_border_;
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
    const float border = float(shadow_border_);
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
    // The mip LOD bias of the texture bound (u_lod_bias: 0 for textures
    // without mip levels and those sampled with their own parameters).
    float bound_bias = 0.0f;
    gl.Uniform1f(gl.u_lod_bias, 0.0f);
    auto set_bias = [&](float bias) {
        if (bias != bound_bias) gl.Uniform1f(gl.u_lod_bias, bound_bias = bias);
    };
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
        Texture* tex = (s.texture_enabled && (m->has_uvs || texgen)) ? texture(s.texture) : nullptr;
        // The pack's replacement once it is on the GPU, else the game's own.
        bool replaced = false;
        const GpuTexture* gpu = tex ? resolve(*tex, replaced) : nullptr;
        if (!gpu) tex = nullptr;
        // The controller prompts: the font's button glyphs in each line's
        // player's pictures, the title's PRESS START in player 1's
        // (presentation only).
        unsigned bound = gpu ? gpu->id : white_texture_;
        unsigned bound_sampler = gpu ? sampler(*tex, *gpu) : 0;
        const PromptSplit* split = nullptr;
        unsigned prompt_atlas = 0;
        if (tex && prompts_ && !id_pass_ && prompts_->active()) {
            if (glyphs_.valid && s.texture->image.document() == glyphs_.document && s.texture->image.reference() == glyphs_.reference) {
                split = prompt_split(item.geometry);
                // (A player on the game's own pictures: the line as it is.)
                if (split && (prompt_atlas = prompts_->atlas_texture(split->player)) == 0) split = nullptr;
            } else if (tex->title_press && prompts_->atlas_texture(0) != 0) {
                // (The variant samples with its own parameters.)
                if (const unsigned variant = title_texture(*tex, replaced ? gpu : nullptr)) {
                    bound = variant;
                    bound_sampler = 0;
                }
            }
        }
        gl.BindSampler(0, bound_sampler);
        glBindTexture(GL_TEXTURE_2D, bound);
        set_bias(gpu && bound_sampler && gpu->levels > 1 ? graphics_.lod_bias : 0.0f);
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
        if (split) {
            // (The split's own buffers; the text part with the line's mesh state.)
            if (split->text_count) {
                gl.BindVertexArray(split->text_vao);
                glDrawElements(GL_TRIANGLES, split->text_count, GL_UNSIGNED_INT, nullptr);
            }
            gl.BindSampler(0, 0);
            glBindTexture(GL_TEXTURE_2D, prompt_atlas);
            set_bias(0.0f);
            gl.BindVertexArray(split->icon_vao);
            glDrawElements(GL_TRIANGLES, split->icon_count, GL_UNSIGNED_INT, nullptr);
            continue;
        }
        gl.BindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, nullptr);
    }
    gl.BindVertexArray(0);
    set_bias(0.0f);
    if (!receivers_drawn) {
        glViewport(wide.x, wide.y, wide.width, wide.height);
        gl.pixel_center(wide.width, wide.height);
        glScissor(wide.x, wide.y, wide.width, wide.height);
        render_shadow_receivers(camera);
    }
    // (Later draws of the frame, the movie and the kept picture, sample with
    // their textures' own parameters.)
    gl.BindSampler(0, 0);
    // Sharpening (an option) over the game picture, before the menus.
    if (!id_pass_ && graphics_.sharpening != Sharpening::off) sharpen(wide);
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
