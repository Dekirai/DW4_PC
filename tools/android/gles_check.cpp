// gles_check: the renderer's OpenGL ES paths on a device without a window
// (docs/ANDROID.md, "GLES checks without the app"). A plain executable for
// `adb shell` (EGL pbuffer, no Activity, no SDL): it reads the renderer's
// shader sources from a copy of src/game/gl_renderer.cpp, compiles and links
// them with the renderer's OpenGL ES header, and runs the GL calls of the
// Graphics options and the texture streaming the way gl_renderer.cpp makes
// them, with glGetError after every step:
//
//   - the capabilities the renderer asks for (anisotropic filtering and its
//     maximum, BPTC / S3TC / ASTC, depth clamp, texture size, samples);
//   - every shader program (scene, movie, kept picture, sharpening);
//   - texture streaming: immutable storage (glTexStorage2D, RGBA8, a whole
//     mip chain) filled in bands of 256 KiB rows, sampled through a sampler
//     object with trilinear filtering, 16x anisotropy (clamped to the
//     maximum) and a LOD bias in the shader;
//   - the shadow map at every Shadow quality (128, 512, 1024 texels: an
//     RGBA8 colour attachment) drawn and read back;
//   - the Sharpening option's copy of a multisampled picture (the window's
//     resolve into a texture by a blit, then glCopyTexSubImage2D from it:
//     copy_window_es) and the kept picture's blit (keep_picture), with the
//     sharpening pass drawn from it;
//   - the skinning palette (RGBA32F, texelFetch);
//   - GL_DEPTH_CLAMP_EXT where the extension is there;
//   - with a third argument (frames, phase C): the cost of a whole frame of
//     the window (a scene stand-in of blended full-screen quads, then an
//     overlay quad as ImGui's) plain, with the Sharpening option's resolve,
//     copy and pass, with the kept picture's blit in the middle of the frame
//     (a tiler stores the window and loads it again for the overlay), and
//     with both: the GPU's own time (GL_EXT_disjoint_timer_query) and the
//     wall time to glFinish, median and 90th percentile, after warm-up frames
//     so the GPU's clock rises; the GPU clock is read before and after where
//     sysfs lets the shell read it.
//
// Build (NDK clang, see docs/ANDROID.md):
//   clang++ --target=aarch64-linux-android33 -std=c++20 -O1 -static-libstdc++ \
//           tools/android/gles_check.cpp -lEGL -lGLESv3 -o gles_check
// Run:
//   adb push gles_check gl_renderer.cpp /data/local/tmp/dw4c/
//   adb shell /data/local/tmp/dw4c/gles_check /data/local/tmp/dw4c/gl_renderer.cpp [samples] [frames]
// Exit code 0: every step without a GL error and every program linked.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_DEPTH_CLAMP_EXT
#define GL_DEPTH_CLAMP_EXT 0x864F
#endif

namespace {

int g_failures = 0;

bool check(const char* step) {
    GLenum first = GL_NO_ERROR;
    for (int i = 0; i < 8; ++i) {
        const GLenum e = glGetError();
        if (e == GL_NO_ERROR) break;
        if (first == GL_NO_ERROR) first = e;
    }
    if (first != GL_NO_ERROR) {
        std::printf("FAIL %s: GL error 0x%04x\n", step, first);
        ++g_failures;
        return false;
    }
    std::printf("ok   %s\n", step);
    return true;
}

bool has_extension(const char* name) {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i)
        if (std::strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i))), name) == 0) return true;
    return false;
}

// The raw string shaders of gl_renderer.cpp: const char* kName = R"(...)";
std::map<std::string, std::string> read_shaders(const char* path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    std::map<std::string, std::string> shaders;
    std::size_t at = 0;
    while ((at = text.find("const char* k", at)) != std::string::npos) {
        const std::size_t name_end = text.find(" = R\"(", at);
        const std::size_t line_end = text.find('\n', at);
        if (name_end == std::string::npos || name_end > line_end) {
            at = line_end;
            continue;
        }
        const std::string name = text.substr(at + 12, name_end - (at + 12));
        const std::size_t start = name_end + 6;
        const std::size_t end = text.find(")\"", start);
        if (end == std::string::npos) break;
        shaders[name] = text.substr(start, end - start);
        at = end;
    }
    return shaders;
}

// gl_renderer.cpp compile(): the "#version 330 core" line becomes ES 3.00
// with high precision.
std::string es_source(const std::string& source) {
    const std::string core = "#version 330 core\n";
    const std::string es = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
    return source.rfind(core, 0) == 0 ? es + source.substr(core.size()) : source;
}

GLuint shader(GLenum type, const std::string& source, const std::string& name) {
    const GLuint s = glCreateShader(type);
    const std::string text = es_source(source);
    const char* p = text.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::printf("FAIL compile %s: %s\n", name.c_str(), log);
        ++g_failures;
    }
    return s;
}

GLuint program(const std::map<std::string, std::string>& shaders, const char* vs, const char* fs) {
    const auto v = shaders.find(vs), f = shaders.find(fs);
    if (v == shaders.end() || f == shaders.end()) {
        std::printf("FAIL program %s + %s: source not found\n", vs, fs);
        ++g_failures;
        return 0;
    }
    const GLuint p = glCreateProgram();
    const GLuint a = shader(GL_VERTEX_SHADER, v->second, vs), b = shader(GL_FRAGMENT_SHADER, f->second, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glBindAttribLocation(p, 0, "a_pos"); // (the scene's; harmless for the strips)
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096]{};
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        std::printf("FAIL link %s + %s: %s\n", vs, fs, log);
        ++g_failures;
        return 0;
    }
    GLint uniforms = 0;
    glGetProgramiv(p, GL_ACTIVE_UNIFORMS, &uniforms);
    std::printf("ok   program %s + %s (%d active uniforms)\n", vs, fs, uniforms);
    return p;
}

// A full-screen strip with attribute 0 enabled (gl_renderer.cpp strip_vao:
// some GLES drivers draw nothing without one).
GLuint strip_vao() {
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const float points[8]{0, 0, 1, 0, 0, 1, 1, 1};
    glBufferData(GL_ARRAY_BUFFER, sizeof points, points, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindVertexArray(0);
    return vao;
}

double ms_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// A sysfs value the shell may read (the Adreno's clock), or "-".
std::string read_line(const char* path) {
    std::ifstream in(path);
    std::string line;
    if (!in || !std::getline(in, line)) return "-";
    return line;
}

std::string gpu_clock() {
    return "gpuclk " + read_line("/sys/class/kgsl/kgsl-3d0/gpuclk") + " Hz, devfreq " +
           read_line("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq") + " Hz, busy " + read_line("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage");
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::size_t(p * double(values.size() - 1) + 0.5)];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gles_check <copy of src/game/gl_renderer.cpp> [samples (4)] [frames (0: no frame costs)]\n");
        return 2;
    }
    const auto shaders = read_shaders(argv[1]);
    const int samples = argc > 2 ? std::atoi(argv[2]) : 4;
    const int bench_frames = argc > 3 ? std::atoi(argv[3]) : 0;
    std::printf("shader sources read: %zu\n", shaders.size());

    // EGL: an ES 3 context on a pbuffer the size of the Thor's upper screen
    // (1920 x 1080), multisampled as the game's window with msaa=4.
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (!eglInitialize(display, &major, &minor)) {
        std::printf("FAIL eglInitialize 0x%x\n", eglGetError());
        return 1;
    }
    std::printf("EGL %d.%d %s\n", major, minor, eglQueryString(display, EGL_VENDOR));
    const int width = 1920, height = 1080;
    EGLConfig config{};
    EGLint found = 0;
    for (int wanted : {samples, 0}) {
        const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                     EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
                                     EGL_SAMPLE_BUFFERS, wanted ? 1 : 0, EGL_SAMPLES, wanted, EGL_NONE};
        if (eglChooseConfig(display, attributes, &config, 1, &found) && found) break;
    }
    if (!found) {
        std::printf("FAIL no EGL config\n");
        return 1;
    }
    EGLint config_samples = 0;
    eglGetConfigAttrib(display, config, EGL_SAMPLES, &config_samples);
    const EGLint surface_attributes[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, surface_attributes);
    EGLContext context = EGL_NO_CONTEXT;
    for (int wanted_minor : {2, 1, 0}) {
        const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, wanted_minor, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (context != EGL_NO_CONTEXT) break;
    }
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !eglMakeCurrent(display, surface, surface, context)) {
        std::printf("FAIL EGL surface / context 0x%x\n", eglGetError());
        return 1;
    }
    std::printf("GL_VERSION  %s\nGL_RENDERER %s\nGLSL        %s\npbuffer %dx%d, %d samples\n",
                reinterpret_cast<const char*>(glGetString(GL_VERSION)), reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)), width, height, config_samples);

    // The capabilities gl_renderer.cpp asks for (create_objects).
    GLint max_texture = 0, max_samples = 0, vertex_vectors = 0, extensions = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vertex_vectors);
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    const bool aniso = has_extension("GL_EXT_texture_filter_anisotropic") || has_extension("GL_ARB_texture_filter_anisotropic");
    GLfloat max_aniso = 1.0f;
    if (aniso) glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso);
    const bool depth_clamp = has_extension("GL_EXT_depth_clamp");
    std::printf("caps: max texture %d, max samples %d, vertex uniform vectors %d, %d extensions\n", max_texture, max_samples,
                vertex_vectors, extensions);
    std::printf("caps: anisotropic %s (max %.0f), BPTC %s, S3TC %s, ASTC LDR %s, ASTC HDR %s, depth clamp %s, KHR_debug %s\n",
                aniso ? "yes" : "no", max_aniso, has_extension("GL_EXT_texture_compression_bptc") ? "yes" : "no",
                has_extension("GL_EXT_texture_compression_s3tc") ? "yes" : "no",
                has_extension("GL_KHR_texture_compression_astc_ldr") ? "yes" : "no",
                has_extension("GL_KHR_texture_compression_astc_hdr") ? "yes" : "no", depth_clamp ? "yes" : "no",
                has_extension("GL_KHR_debug") ? "yes" : "no");
    GLint formats = 0;
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &formats);
    std::printf("caps: %d compressed texture formats\n", formats);
    check("capabilities");

    // Every program the renderer links.
    const GLuint scene = program(shaders, "kVertex", "kFragment");
    const GLuint movie = program(shaders, "kMovieVertex", "kMovieFragment");
    const GLuint kept = program(shaders, "kKeptVertex", "kMovieFragment");
    const GLuint sharpen = program(shaders, "kKeptVertex", "kSharpenFragment");
    check("shader programs");
    const GLuint vao = strip_vao();

    // Texture streaming (begin_upload / continue_upload): immutable storage,
    // bands of 256 KiB, the whole mip chain; the pack's 4x level of a 512 x
    // 512 texture is 2048 x 2048.
    {
        const int size = 2048;
        int levels = 0;
        for (int s = size; s > 0; s >>= 1) ++levels;
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, levels, GL_RGBA8, size, size);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
        check("streaming: glTexStorage2D RGBA8 2048x2048, 12 levels");
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const auto started = std::chrono::steady_clock::now();
        std::size_t bytes = 0, bands = 0;
        std::vector<unsigned char> band;
        for (int level = 0; level < levels; ++level) {
            const int w = std::max(size >> level, 1), h = std::max(size >> level, 1);
            const int rows_per_band = std::max(1, int((256u << 10) / (unsigned(w) * 4)));
            for (int row = 0; row < h; row += rows_per_band) {
                const int rows = std::min(rows_per_band, h - row);
                band.assign(std::size_t(w) * rows * 4, static_cast<unsigned char>(level * 20));
                glTexSubImage2D(GL_TEXTURE_2D, level, 0, row, w, rows, GL_RGBA, GL_UNSIGNED_BYTE, band.data());
                bytes += band.size();
                ++bands;
            }
        }
        glFinish();
        std::printf("     %zu bands, %.1f MiB in %.1f ms\n", bands, double(bytes) / 1048576.0, ms_since(started));
        check("streaming: banded glTexSubImage2D of every level");
        // The sampler (GlRenderer::sampler): trilinear, anisotropy 16
        // clamped to the maximum; the LOD bias is the shader's.
        GLuint sampler = 0;
        glGenSamplers(1, &sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (aniso) glSamplerParameterf(sampler, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(16.0f, 1.0f, std::max(max_aniso, 1.0f)));
        check(aniso ? "sampler: trilinear, anisotropy 16" : "sampler: trilinear (no anisotropic filtering)");
        if (movie) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            glUseProgram(movie);
            glActiveTexture(GL_TEXTURE0);
            glBindSampler(0, sampler);
            glUniform1i(glGetUniformLocation(movie, "u_tex"), 0);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            glBindSampler(0, 0);
            glFinish();
            check("draw with the streamed texture through the sampler");
        }
        glDeleteSamplers(1, &sampler);
        glDeleteTextures(1, &texture);
    }
    // The block formats where the GPU has them (caps_.bc7 / bc1 / bc3): the
    // same storage, bands of block rows (the last band may end at the edge
    // of the level, the smallest levels are one block).
    {
        struct Format {
            const char* name;
            const char* extension;
            GLenum format;
            std::size_t block_bytes;
        };
        const Format formats_used[] = {{"BC7", "GL_EXT_texture_compression_bptc", 0x8E8C, 16},
                                       {"BC1", "GL_EXT_texture_compression_s3tc", 0x83F1, 8},
                                       {"BC3", "GL_EXT_texture_compression_s3tc", 0x83F3, 16}};
        for (const auto& f : formats_used) {
            if (!has_extension(f.extension)) {
                std::printf("info %s: no %s (the renderer decodes it to RGBA8 on its loading thread)\n", f.name, f.extension);
                continue;
            }
            const int size = 2048;
            int levels = 0;
            for (int s = size; s > 0; s >>= 1) ++levels;
            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexStorage2D(GL_TEXTURE_2D, levels, f.format, size, size);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
            char step[128];
            std::snprintf(step, sizeof step, "streaming: glTexStorage2D %s 2048x2048, 12 levels", f.name);
            check(step);
            const auto started = std::chrono::steady_clock::now();
            std::size_t bytes = 0, bands = 0;
            std::vector<unsigned char> band;
            for (int level = 0; level < levels; ++level) {
                const int w = std::max(size >> level, 1), h = std::max(size >> level, 1);
                const std::size_t row_bytes = std::size_t((w + 3) / 4) * f.block_bytes;
                const int block_rows = int(std::max<std::size_t>(1, (256u << 10) / std::max<std::size_t>(row_bytes, 1)));
                for (int row = 0; row < h; row += block_rows * 4) {
                    const int rows = std::min(h - row, block_rows * 4);
                    band.assign(std::size_t((rows + 3) / 4) * row_bytes, 0x5A);
                    glCompressedTexSubImage2D(GL_TEXTURE_2D, level, 0, row, w, rows, f.format, GLsizei(band.size()), band.data());
                    bytes += band.size();
                    ++bands;
                }
            }
            glFinish();
            std::printf("     %zu bands, %.1f MiB in %.1f ms\n", bands, double(bytes) / 1048576.0, ms_since(started));
            std::snprintf(step, sizeof step, "streaming: banded glCompressedTexSubImage2D %s of every level", f.name);
            check(step);
            glDeleteTextures(1, &texture);
        }
    }
    {
        // A format the GPU does not take (BC7 without BPTC): the error the
        // renderer's refuse_upload expects, never reached through its caps.
        if (!has_extension("GL_EXT_texture_compression_bptc")) {
            GLuint refused = 0;
            glGenTextures(1, &refused);
            glBindTexture(GL_TEXTURE_2D, refused);
            glTexStorage2D(GL_TEXTURE_2D, 1, 0x8E8C /* COMPRESSED_RGBA_BPTC_UNORM */, 64, 64);
            const GLenum e = glGetError();
            std::printf("info BC7 storage without BPTC: GL error 0x%04x (the renderer decodes BC7 on its loading thread instead)\n", e);
            while (glGetError() != GL_NO_ERROR) {
            }
            glDeleteTextures(1, &refused);
        }
    }

    // The skinning palette: RGBA32F 4 x 64, uploaded per item.
    {
        GLuint palette = 0;
        glGenTextures(1, &palette);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, palette);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 64, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        std::vector<float> matrices(4 * 64 * 4, 0.5f);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 64, GL_RGBA, GL_FLOAT, matrices.data());
        glActiveTexture(GL_TEXTURE0);
        check("skinning palette RGBA32F 4x64");
        glDeleteTextures(1, &palette);
    }

    // The shadow map at every Shadow quality (render_shadow_map: an RGBA8
    // colour attachment, the game's 128 x 128 times 1, 4, 8).
    for (int scale : {1, 4, 8}) {
        const int size = 128 * scale;
        GLuint texture = 0, fbo = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glViewport(0, 0, size, size);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        const int border = 2 * scale;
        glViewport(border, border, size - 2 * border, size - 2 * border);
        if (kept) {
            glUseProgram(kept);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
        }
        std::vector<unsigned char> pixels(std::size_t(size) * size * 4);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        char step[96];
        std::snprintf(step, sizeof step, "shadow map %dx%d (scale %d): %s, drawn, read back", size, size, scale,
                      complete ? "complete" : "INCOMPLETE");
        if (!complete) ++g_failures;
        check(step);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &texture);
    }

    // The multisampled window: something drawn into it, then the
    // Sharpening option's copy (copy_window_es: a blit resolves the window's
    // rectangle into a window-sized texture, glCopyTexSubImage2D copies it
    // from there) and the kept picture's blit (keep_picture).
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(0.2f, 0.4f, 0.8f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        check("window: cleared");
        // The 4:3 picture inside the 16:9 window (as the game's view).
        const int x = 240, y = 0, w = 1440, h = 1080;
        GLuint resolve_texture = 0, resolve_fbo = 0, sharpen_texture = 0;
        glGenTextures(1, &resolve_texture);
        glBindTexture(GL_TEXTURE_2D, resolve_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, x + w, y + h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glGenTextures(1, &sharpen_texture);
        glBindTexture(GL_TEXTURE_2D, sharpen_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        const auto started = std::chrono::steady_clock::now();
        glDisable(GL_SCISSOR_TEST);
        glGenFramebuffers(1, &resolve_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_texture, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        check("sharpening: the window's rectangle resolved by a blit");
        glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo);
        glBindTexture(GL_TEXTURE_2D, sharpen_texture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        check("sharpening: glCopyTexSubImage2D from the resolved texture");
        if (sharpen) {
            glViewport(x, y, w, h);
            glUseProgram(sharpen);
            glUniform1i(glGetUniformLocation(sharpen, "u_tex"), 0);
            glUniform1f(glGetUniformLocation(sharpen, "u_sharpness"), 1.0f);
            glUniform2i(glGetUniformLocation(sharpen, "u_origin"), x, y);
            glUniform2i(glGetUniformLocation(sharpen, "u_size"), w, h);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
            check("sharpening: the pass drawn into the window");
        }
        glFinish();
        std::printf("     the sharpening's resolve, copy and pass: %.2f ms\n", ms_since(started));
        // keep_picture: the whole window blitted into the kept texture.
        GLuint kept_texture = 0, kept_fbo = 0;
        glGenTextures(1, &kept_texture);
        glBindTexture(GL_TEXTURE_2D, kept_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const auto kept_started = std::chrono::steady_clock::now();
        glGenFramebuffers(1, &kept_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, kept_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, kept_texture, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glFinish();
        std::printf("     the kept picture's blit: %.2f ms\n", ms_since(kept_started));
        check("kept picture: the window blitted into its texture");
        unsigned char centre[4]{};
        glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre);
        std::printf("     window centre after the sharpening pass: %u %u %u %u\n", centre[0], centre[1], centre[2], centre[3]);
        check("window read back");
        // The steady cost per frame (the first use above includes the
        // driver's set-up): 60 rounds of a drawn window, the sharpening's
        // resolve, copy and pass, and the kept picture's blit.
        if (sharpen && kept) {
            double sharpen_ms = 0, kept_ms = 0;
            const int rounds = 60;
            for (int i = 0; i < rounds; ++i) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, width, height);
                glClearColor(0.1f * float(i % 10), 0.4f, 0.8f, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glFinish();
                auto at = std::chrono::steady_clock::now();
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo);
                glBindTexture(GL_TEXTURE_2D, sharpen_texture);
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(x, y, w, h);
                glUseProgram(sharpen);
                glBindVertexArray(vao);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glBindVertexArray(0);
                glFinish();
                sharpen_ms += ms_since(at);
                at = std::chrono::steady_clock::now();
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, kept_fbo);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glFinish();
                kept_ms += ms_since(at);
            }
            std::printf("     per frame over %d rounds: sharpening %.2f ms, kept picture %.2f ms\n", rounds, sharpen_ms / rounds,
                        kept_ms / rounds);
            check("sharpening and kept picture, 60 rounds");
        }
        // Whole frames (phase C): what the kept picture and the Sharpening
        // option cost inside a frame, where a tiler must store the window
        // before the blit and load it again for what is drawn after it.
        if (bench_frames > 0 && sharpen && kept) {
            const bool timer = has_extension("GL_EXT_disjoint_timer_query");
            constexpr GLenum kTimeElapsed = 0x88BF, kGpuDisjoint = 0x8FBB;
            GLuint query = 0;
            if (timer) glGenQueries(1, &query);
            std::printf("frames: %d per case after %d warm-up frames, timer query %s; %s\n", bench_frames, bench_frames / 2,
                        timer ? "yes" : "no", gpu_clock().c_str());
            const auto frame = [&](bool sharpening, bool keep) {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, width, height);
                glClearColor(0.1f, 0.2f, 0.3f, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                // The scene's stand-in: three blended full-screen quads of a
                // 1920 x 1080 texture over the 4:3 picture.
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glViewport(x, y, w, h);
                glUseProgram(kept);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, kept_texture);
                glUniform1i(glGetUniformLocation(kept, "u_tex"), 0);
                glBindVertexArray(vao);
                for (int i = 0; i < 3; ++i) glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glBindVertexArray(0);
                glDisable(GL_BLEND);
                if (sharpening) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                    glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo);
                    glBindTexture(GL_TEXTURE_2D, sharpen_texture);
                    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glViewport(x, y, w, h);
                    glUseProgram(sharpen);
                    glUniform1i(glGetUniformLocation(sharpen, "u_tex"), 0);
                    glUniform1f(glGetUniformLocation(sharpen, "u_sharpness"), 1.0f);
                    glUniform2i(glGetUniformLocation(sharpen, "u_origin"), x, y);
                    glUniform2i(glGetUniformLocation(sharpen, "u_size"), w, h);
                    glBindVertexArray(vao);
                    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                    glBindVertexArray(0);
                }
                if (keep) {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, kept_fbo);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                }
                // The menus' stand-in after it: one blended quad over a
                // quarter of the window.
                glEnable(GL_BLEND);
                glViewport(width / 4, height / 4, width / 2, height / 2);
                glUseProgram(kept);
                glBindTexture(GL_TEXTURE_2D, kept_texture);
                glBindVertexArray(vao);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glBindVertexArray(0);
                glDisable(GL_BLEND);
            };
            struct Case {
                const char* name;
                bool sharpening, keep;
            };
            const Case cases[] = {{"plain", false, false}, {"kept picture", false, true}, {"sharpening", true, false}, {"both", true, true}};
            double plain_gpu = 0, plain_wall = 0;
            for (const auto& c : cases) {
                for (int i = 0; i < bench_frames / 2; ++i) frame(c.sharpening, c.keep);
                glFinish();
                std::vector<double> gpu, wall;
                int disjoint_frames = 0;
                for (int i = 0; i < bench_frames; ++i) {
                    const auto at = std::chrono::steady_clock::now();
                    if (timer) glBeginQuery(kTimeElapsed, query);
                    frame(c.sharpening, c.keep);
                    if (timer) glEndQuery(kTimeElapsed);
                    glFinish();
                    wall.push_back(ms_since(at));
                    if (timer) {
                        GLuint ns = 0;
                        glGetQueryObjectuiv(query, GL_QUERY_RESULT, &ns);
                        GLint disjoint = 0;
                        glGetIntegerv(kGpuDisjoint, &disjoint);
                        if (disjoint) ++disjoint_frames;
                        else gpu.push_back(double(ns) / 1e6);
                    }
                }
                const double gpu_median = percentile(gpu, 0.5), wall_median = percentile(wall, 0.5);
                if (!c.sharpening && !c.keep) plain_gpu = gpu_median, plain_wall = wall_median;
                std::printf("     frame %-13s GPU median %.3f ms p90 %.3f (+%.3f), wall median %.3f ms p90 %.3f (+%.3f)%s\n", c.name,
                            gpu_median, percentile(gpu, 0.9), gpu_median - plain_gpu, wall_median, percentile(wall, 0.9),
                            wall_median - plain_wall, disjoint_frames ? " (some timer results disjoint)" : "");
                char step[64];
                std::snprintf(step, sizeof step, "frames: %s", c.name);
                check(step);
            }
            std::printf("frames done: %s\n", gpu_clock().c_str());
            if (timer) glDeleteQueries(1, &query);
        }
        glDeleteFramebuffers(1, &resolve_fbo);
        glDeleteFramebuffers(1, &kept_fbo);
        glDeleteTextures(1, &resolve_texture);
        glDeleteTextures(1, &sharpen_texture);
        glDeleteTextures(1, &kept_texture);
    }

    if (depth_clamp) {
        glEnable(GL_DEPTH_CLAMP_EXT);
        glDisable(GL_DEPTH_CLAMP_EXT);
        check("GL_DEPTH_CLAMP_EXT on and off");
    }
    (void)scene;

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    std::printf("%s: %d failures\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
