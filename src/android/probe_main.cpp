// The Android device probe (docs/ANDROID.md, phase A): the app's native
// library "main" until the game itself moves here in phase B. It opens an
// OpenGL ES 3 context through SDL, clears the screen to a test colour with a
// triangle from a "#version 300 es" shader and a vertex buffer on top (the
// screencap proof), and
// writes to logcat (tag DW4probe) what the renderer and the dual-screen work
// need to know about a device: GL version, renderer, uniform limits, the
// texture compression and depth clamp extensions, EGL, the displays Android's
// DisplayManager reports with their flags (through JNI, DW4Activity.java),
// SDL's paths and version, and the game controllers SDL sees.
//
// No game code and no game data. Arguments (intent extra "dw4args", see
// DW4Activity.java): --seconds N quits after N seconds.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <jni.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef DW4_PC_VERSION
#define DW4_PC_VERSION "unknown"
#endif

namespace {

constexpr const char* kTag = "DW4probe";

// The test colours (8-bit exact): the clear colour fills the screen, the
// triangle covers its centre (checked on a screencap, docs/ANDROID.md).
constexpr int kClear[3] = {0x20, 0x60, 0xC0};
constexpr int kTriangle[3] = {0xF0, 0xB0, 0x20};

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, ap);
    va_end(ap);
}

// logcat cuts an entry at about 4 KB: long text goes out line by line, and a
// long line in pieces at spaces.
void log_text(const char* label, const std::string& text) {
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        while (line.size() > 900) {
            std::size_t cut = line.rfind(' ', 900);
            if (cut == std::string::npos || cut == 0) cut = 900;
            logf("%s %s", label, line.substr(0, cut).c_str());
            line.erase(0, cut + 1);
        }
        if (!line.empty()) logf("%s %s", label, line.c_str());
        start = end + 1;
    }
}

const char* gl_string(GLenum name) {
    const auto* s = reinterpret_cast<const char*>(glGetString(name));
    return s ? s : "(null)";
}

GLint gl_int(GLenum name) {
    GLint v = -1;
    glGetIntegerv(name, &v);
    while (glGetError() != GL_NO_ERROR) {
    }
    return v;
}

std::set<std::string> gl_extensions() {
    std::set<std::string> out;
    const GLint n = gl_int(GL_NUM_EXTENSIONS);
    for (GLint i = 0; i < n; ++i) {
        const auto* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e) out.insert(e);
    }
    return out;
}

void log_gl() {
    logf("GL_VERSION %s", gl_string(GL_VERSION));
    logf("GL_RENDERER %s", gl_string(GL_RENDERER));
    logf("GL_VENDOR %s", gl_string(GL_VENDOR));
    logf("GL_SHADING_LANGUAGE_VERSION %s", gl_string(GL_SHADING_LANGUAGE_VERSION));
    logf("GL_MAJOR_VERSION.MINOR %d.%d", gl_int(GL_MAJOR_VERSION), gl_int(GL_MINOR_VERSION));
    // The renderer's bone palette needs about 295 vec4 in the vertex shader
    // today (the GLES minimum is 256): docs/ANDROID.md, phase B.
    logf("GL_MAX_VERTEX_UNIFORM_VECTORS %d", gl_int(GL_MAX_VERTEX_UNIFORM_VECTORS));
    logf("GL_MAX_FRAGMENT_UNIFORM_VECTORS %d", gl_int(GL_MAX_FRAGMENT_UNIFORM_VECTORS));
    logf("GL_MAX_VERTEX_UNIFORM_COMPONENTS %d", gl_int(GL_MAX_VERTEX_UNIFORM_COMPONENTS));
    logf("GL_MAX_UNIFORM_BLOCK_SIZE %d", gl_int(GL_MAX_UNIFORM_BLOCK_SIZE));
    logf("GL_MAX_VERTEX_UNIFORM_BLOCKS %d", gl_int(GL_MAX_VERTEX_UNIFORM_BLOCKS));
    logf("GL_MAX_TEXTURE_SIZE %d", gl_int(GL_MAX_TEXTURE_SIZE));
    logf("GL_MAX_TEXTURE_IMAGE_UNITS %d", gl_int(GL_MAX_TEXTURE_IMAGE_UNITS));
    logf("GL_MAX_VERTEX_ATTRIBS %d", gl_int(GL_MAX_VERTEX_ATTRIBS));
    logf("GL_MAX_SAMPLES %d", gl_int(GL_MAX_SAMPLES));
    logf("GL_MAX_COLOR_ATTACHMENTS %d", gl_int(GL_MAX_COLOR_ATTACHMENTS));

    // The alpha test's floor(a * 255.0 + 0.5) needs highp in the fragment shader.
    GLint range[2] = {0, 0};
    GLint precision = 0;
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &precision);
    logf("fragment highp float: range %d..%d precision %d", range[0], range[1], precision);
    glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, range, &precision);
    logf("fragment mediump float: range %d..%d precision %d", range[0], range[1], precision);

    const std::set<std::string> ext = gl_extensions();
    logf("GL_NUM_EXTENSIONS %zu", ext.size());
    static const char* const kWatched[] = {
        "GL_EXT_depth_clamp",
        "GL_KHR_texture_compression_astc_ldr",
        "GL_KHR_texture_compression_astc_hdr",
        "GL_OES_texture_compression_astc",
        "GL_EXT_texture_compression_bptc",
        "GL_EXT_texture_compression_s3tc",
        "GL_EXT_texture_compression_rgtc",
        "GL_EXT_texture_compression_dxt1",
        "GL_KHR_debug",
        "GL_EXT_color_buffer_float",
        "GL_EXT_clip_control",
        "GL_EXT_multisampled_render_to_texture",
        "GL_EXT_buffer_storage",
        "GL_OES_texture_float_linear",
    };
    for (const char* name : kWatched) logf("ext %s %s", name, ext.count(name) ? "yes" : "no");
    std::string all;
    for (const auto& e : ext) {
        if (!all.empty()) all += ' ';
        all += e;
    }
    log_text("GL_EXTENSIONS", all);

    // Compressed formats by family (ETC2 is core in ES 3.0).
    const GLint nfmt = gl_int(GL_NUM_COMPRESSED_TEXTURE_FORMATS);
    std::vector<GLint> fmts(nfmt > 0 ? std::size_t(nfmt) : 0);
    if (!fmts.empty()) glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, fmts.data());
    int astc = 0, bc7 = 0, s3tc = 0, etc2 = 0;
    std::string list;
    for (GLint f : fmts) {
        if (f >= 0x93B0 && f <= 0x93DD) ++astc;
        else if (f >= 0x8E8C && f <= 0x8E8F) ++bc7;
        else if (f >= 0x83F0 && f <= 0x83F3) ++s3tc;
        else if (f >= 0x9270 && f <= 0x9279) ++etc2;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%s0x%04X", list.empty() ? "" : " ", unsigned(f));
        list += buf;
    }
    logf("compressed formats %d: astc %d bptc(bc6h/bc7) %d s3tc(bc1-3) %d etc2/eac %d", nfmt, astc, bc7, s3tc, etc2);
    log_text("GL_COMPRESSED_TEXTURE_FORMATS", list);
}

void log_egl() {
    EGLDisplay dpy = SDL_EGL_GetCurrentDisplay();
    if (!dpy) {
        logf("EGL: no current display (%s)", SDL_GetError());
        return;
    }
    const char* ver = eglQueryString(dpy, EGL_VERSION);
    const char* ven = eglQueryString(dpy, EGL_VENDOR);
    const char* ext = eglQueryString(dpy, EGL_EXTENSIONS);
    logf("EGL_VERSION %s", ver ? ver : "(null)");
    logf("EGL_VENDOR %s", ven ? ven : "(null)");
    log_text("EGL_EXTENSIONS", ext ? ext : "");
}

// DisplayManager's displays with their flags: DW4Activity.displayReport().
void log_android_displays() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        logf("displays: no JNI environment or activity (%s)", SDL_GetError());
        return;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID mid = cls ? env->GetStaticMethodID(cls, "displayReport", "()Ljava/lang/String;") : nullptr;
    if (!mid) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        logf("displays: DW4Activity.displayReport() not found");
    } else {
        auto report = static_cast<jstring>(env->CallStaticObjectMethod(cls, mid));
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            logf("displays: displayReport() threw");
        } else if (report) {
            const char* chars = env->GetStringUTFChars(report, nullptr);
            log_text("display", chars ? chars : "");
            if (chars) env->ReleaseStringUTFChars(report, chars);
            env->DeleteLocalRef(report);
        }
    }
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

void log_sdl() {
    const int v = SDL_GetVersion();
    logf("DW4_PC %s probe, %s, clang %s, libc++ %d", DW4_PC_VERSION,
#if defined(__aarch64__)
         "arm64-v8a",
#elif defined(__x86_64__)
         "x86_64",
#else
         "other ABI",
#endif
         __clang_version__, _LIBCPP_VERSION);
    logf("SDL compiled %d.%d.%d, linked %d.%d.%d, revision %s", SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
         SDL_MICRO_VERSION, SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
         SDL_GetRevision());
    logf("Android SDK version %d, tablet %d, TV %d, DeX %d, Chromebook %d", SDL_GetAndroidSDKVersion(),
         SDL_IsTablet() ? 1 : 0, SDL_IsTV() ? 1 : 0, SDL_IsDeXMode() ? 1 : 0, SDL_IsChromebook() ? 1 : 0);
    const char* base = SDL_GetBasePath();
    logf("SDL_GetBasePath %s", base ? base : SDL_GetError());
    char* pref = SDL_GetPrefPath("DW4_PC", "DW4_PC");
    logf("SDL_GetPrefPath %s", pref ? pref : SDL_GetError());
    SDL_free(pref);
    const char* internal = SDL_GetAndroidInternalStoragePath();
    logf("SDL_GetAndroidInternalStoragePath %s", internal ? internal : SDL_GetError());
    const char* external = SDL_GetAndroidExternalStoragePath();
    logf("SDL_GetAndroidExternalStoragePath %s", external ? external : SDL_GetError());
    const Uint32 state = SDL_GetAndroidExternalStorageState();
    logf("SDL_GetAndroidExternalStorageState read %d write %d", (state & SDL_ANDROID_EXTERNAL_STORAGE_READ) ? 1 : 0,
         (state & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) ? 1 : 0);
    const char* cache = SDL_GetAndroidCachePath();
    logf("SDL_GetAndroidCachePath %s", cache ? cache : SDL_GetError());
    logf("SDL video driver %s, CPU cores %d, RAM %d MB, page %d", SDL_GetCurrentVideoDriver(), SDL_GetNumLogicalCPUCores(),
         SDL_GetSystemRAM(), SDL_GetSystemPageSize());

    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; i < count; ++i) {
        SDL_Rect r{};
        SDL_GetDisplayBounds(displays[i], &r);
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(displays[i]);
        logf("SDL display %u '%s' %dx%d at %d,%d scale %.2f refresh %.2f", unsigned(displays[i]),
             SDL_GetDisplayName(displays[i]) ? SDL_GetDisplayName(displays[i]) : "?", r.w, r.h, r.x, r.y,
             double(SDL_GetDisplayContentScale(displays[i])), mode ? double(mode->refresh_rate) : 0.0);
    }
    SDL_free(displays);
}

void log_gamepad(SDL_JoystickID id, const char* what) {
    logf("%s joystick %u '%s' vendor 0x%04X product 0x%04X gamepad %d type %d", what, unsigned(id),
         SDL_GetJoystickNameForID(id) ? SDL_GetJoystickNameForID(id) : "?", SDL_GetJoystickVendorForID(id),
         SDL_GetJoystickProductForID(id), SDL_IsGamepad(id) ? 1 : 0, int(SDL_GetGamepadTypeForID(id)));
}

GLuint compile(GLenum type, const char* src, const char* name) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char info[1024] = {};
    glGetShaderInfoLog(s, sizeof info, nullptr, info);
    logf("shader %s: %s %s", name, ok ? "compiled" : "FAILED", info);
    return s;
}

// The renderer's dialect for Android (docs/ANDROID.md phase B): GLSL ES 3.00
// with explicit highp; a triangle from a vertex buffer, as the renderer draws
// (the emulator's GLES translator draws nothing without an enabled attribute
// array).
GLuint triangle_program() {
    static const char* kVs = "#version 300 es\n"
                             "precision highp float;\n"
                             "layout(location = 0) in vec2 a_position;\n"
                             "void main() { gl_Position = vec4(a_position, 0.0, 1.0); }\n";
    static const char* kFs = "#version 300 es\n"
                             "precision highp float;\n"
                             "uniform vec3 u_colour;\n"
                             "out vec4 o_colour;\n"
                             "void main() { o_colour = vec4(u_colour, 1.0); }\n";
    const GLuint vs = compile(GL_VERTEX_SHADER, kVs, "vertex 300 es");
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFs, "fragment 300 es");
    const GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    logf("program 300 es: %s", ok ? "linked" : "link FAILED");
    glDeleteShader(vs);
    glDeleteShader(fs);
    return ok ? prog : 0;
}

const char* event_name(Uint32 type) {
    switch (type) {
    case SDL_EVENT_TERMINATING: return "TERMINATING";
    case SDL_EVENT_LOW_MEMORY: return "LOW_MEMORY";
    case SDL_EVENT_WILL_ENTER_BACKGROUND: return "WILL_ENTER_BACKGROUND";
    case SDL_EVENT_DID_ENTER_BACKGROUND: return "DID_ENTER_BACKGROUND";
    case SDL_EVENT_WILL_ENTER_FOREGROUND: return "WILL_ENTER_FOREGROUND";
    case SDL_EVENT_DID_ENTER_FOREGROUND: return "DID_ENTER_FOREGROUND";
    case SDL_EVENT_RENDER_DEVICE_RESET: return "RENDER_DEVICE_RESET";
    case SDL_EVENT_DISPLAY_ADDED: return "DISPLAY_ADDED";
    case SDL_EVENT_DISPLAY_REMOVED: return "DISPLAY_REMOVED";
    case SDL_EVENT_DISPLAY_ORIENTATION: return "DISPLAY_ORIENTATION";
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return "WINDOW_FOCUS_GAINED";
    case SDL_EVENT_WINDOW_FOCUS_LOST: return "WINDOW_FOCUS_LOST";
    default: return nullptr;
    }
}

// The application lifecycle events reach only an event watch. In SDL 3.4.16
// the Java side queues them (Android_SendLifecycleEvent, SDL_android.c), and
// the SDL thread delivers them from Android_PumpEvents (SDL_androidevents.c,
// also while it blocks in a pause) through SDL_SendAppEvent, which calls the
// event watchers and never queues them for SDL_PollEvent (SDL_events.c). So
// the watch runs on the SDL thread; phase B's lifecycle handling hooks in the
// same way. The log names the thread to show it.
SDL_ThreadID g_sdl_main_thread = 0;

bool SDLCALL lifecycle_watch(void*, SDL_Event* ev) {
    switch (ev->type) {
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_LOW_MEMORY:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND: {
        const SDL_ThreadID thread = SDL_GetCurrentThreadID();
        logf("watch %s on thread %llu (%s)", event_name(ev->type), static_cast<unsigned long long>(thread),
             thread == g_sdl_main_thread ? "the SDL main thread" : "another thread");
        break;
    }
    default:
        break;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    double quit_after = 0.0;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--seconds") == 0) quit_after = std::atof(argv[++i]);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        logf("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    g_sdl_main_thread = SDL_GetCurrentThreadID();
    logf("SDL main thread %llu", static_cast<unsigned long long>(g_sdl_main_thread));
    std::string args;
    for (int i = 1; i < argc; ++i) args += std::string(i > 1 ? " [" : "[") + argv[i] + "]";
    logf("arguments %d: %s", argc - 1, args.empty() ? "none" : args.c_str());
    SDL_AddEventWatch(lifecycle_watch, nullptr);
    log_sdl();
    log_android_displays();

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow("DW4_PC", 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!window) {
        logf("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        logf("SDL_GL_CreateContext (ES 3.0) failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);
    log_gl();
    log_egl();
    const GLuint program = triangle_program();
    const GLint colour_loc = program ? glGetUniformLocation(program, "u_colour") : -1;
    GLuint vao = 0;
    GLuint vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    static const GLfloat kTriangleXY[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};
    glBufferData(GL_ARRAY_BUFFER, sizeof kTriangleXY, kTriangleXY, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
    glBindVertexArray(0);

    int njoy = 0;
    SDL_JoystickID* joys = SDL_GetJoysticks(&njoy);
    logf("joysticks at start: %d", njoy);
    for (int i = 0; i < njoy; ++i) log_gamepad(joys[i], "present");
    SDL_free(joys);

    const Uint64 start = SDL_GetTicksNS();
    Uint64 window_start = start;
    int frames = 0;
    bool reported_first_frame = false;
    bool running = true;
    bool visible = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                logf("event QUIT");
                running = false;
            } else if (const char* name = event_name(ev.type)) {
                logf("event %s", name);
                if (ev.type == SDL_EVENT_DID_ENTER_BACKGROUND) visible = false;
                if (ev.type == SDL_EVENT_DID_ENTER_FOREGROUND) visible = true;
                if (ev.type == SDL_EVENT_DISPLAY_ADDED || ev.type == SDL_EVENT_DISPLAY_REMOVED) log_android_displays();
            } else if (ev.type == SDL_EVENT_JOYSTICK_ADDED) {
                log_gamepad(ev.jdevice.which, "added");
                if (SDL_IsGamepad(ev.jdevice.which)) SDL_OpenGamepad(ev.jdevice.which);
            } else if (ev.type == SDL_EVENT_JOYSTICK_REMOVED) {
                logf("removed joystick %u", unsigned(ev.jdevice.which));
            } else if (ev.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                logf("gamepad %u button %s", unsigned(ev.gbutton.which),
                     SDL_GetGamepadStringForButton(SDL_GamepadButton(ev.gbutton.button)));
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                logf("key %s scancode %d", SDL_GetKeyName(ev.key.key), int(ev.key.scancode));
            } else if (ev.type == SDL_EVENT_FINGER_DOWN) {
                logf("finger down %.3f %.3f", double(ev.tfinger.x), double(ev.tfinger.y));
            } else if (ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                logf("window pixel size %dx%d", ev.window.data1, ev.window.data2);
            }
        }
        if (!running) break;
        if (quit_after > 0.0 && double(SDL_GetTicksNS() - start) * 1e-9 >= quit_after) {
            logf("quitting after %.1f s (--seconds)", quit_after);
            break;
        }
        if (!visible) {
            SDL_Delay(50);
            continue;
        }
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(kClear[0] / 255.0f, kClear[1] / 255.0f, kClear[2] / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (program) {
            glUseProgram(program);
            glUniform3f(colour_loc, kTriangle[0] / 255.0f, kTriangle[1] / 255.0f, kTriangle[2] / 255.0f);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        if (!reported_first_frame) {
            reported_first_frame = true;
            unsigned char px[4] = {};
            glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            unsigned char corner[4] = {};
            glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            logf("first frame %dx%d: centre %02X%02X%02X corner %02X%02X%02X, glGetError 0x%04X", w, h, px[0], px[1],
                 px[2], corner[0], corner[1], corner[2], glGetError());
        }
        SDL_GL_SwapWindow(window);
        ++frames;
        const Uint64 now = SDL_GetTicksNS();
        if (now - window_start >= 5'000'000'000ull) {
            logf("frames: %d in %.2f s (%.2f ms per frame)", frames, double(now - window_start) * 1e-9,
                 double(now - window_start) * 1e-6 / frames);
            frames = 0;
            window_start = now;
        }
    }
    if (program) glDeleteProgram(program);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    logf("probe done");
    return 0;
}
