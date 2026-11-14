// Android only (docs/ANDROID.md, phase E): the second screen's own frame on
// the AYN Thor's lower screen. Java (LowerScreen.java) finds the display (a
// DisplayListener: FLAG_PRESENTATION, not private, not the game's own, not
// off), shows an android.app.Presentation there whose window is neither
// focusable nor touch modal (a touch never takes the controller's focus
// from the game) and hands its SurfaceView's surface and touches here. The
// game's thread draws into that surface with SDL's own EGL display, config
// and context after the window's swap: eglMakeCurrent on the lower surface,
// a second ImGui context (sharing the window's font atlas), eglSwapBuffers
// at swap interval 0 (the lower screen's 60 Hz never waits on the top's),
// then SDL's surface current again.
#include "screen2.hpp"

#include "host_view.hpp"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <jni.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace dw4::game {

namespace {

using Clock = std::chrono::steady_clock;

struct Touch {
    int action = 0; // 0 down, 1 move, 2 up, 3 cancel
    float x = 0, y = 0;
};

// The surface: Java's main thread hands it over and takes it back (surface
// changed / destroyed), the game's thread draws into it; g_surface_lock is
// held by the game's thread for its whole frame, so the surface's end
// (nativeSurfaceGone) waits until the frame is done.
std::timed_mutex g_surface_lock;
ANativeWindow* g_window = nullptr;          // a reference of ours (ANativeWindow_fromSurface)
EGLSurface g_surface = EGL_NO_SURFACE;      // made on the game's thread
EGLDisplay g_egl_display = EGL_NO_DISPLAY;  // SDL's, kept with the surface
std::atomic<bool> g_abandon{false};         // the surface went while a frame ran: the frame lets it go

// What Java reports and the touches, under a small lock of their own (the
// main thread never waits for a frame to hand over a touch).
std::mutex g_info_lock;
Screen2Display g_display;
std::vector<Touch> g_touches;
std::string g_shot;
std::atomic<long long> g_last_touch_ns{0};

// The game's thread only.
ImGuiContext* g_imgui = nullptr;
float g_scale = 0;
bool g_swap_interval_set = false;
bool g_drew_last = false;
Clock::time_point g_last_frame;
int g_wanted_sent = -1;
int g_gl_errors = 0, g_frames = 0, g_swaps = 0;
std::uint64_t g_digest = 0;                 // the last swapped picture's draw data (0: none)
bool g_logged_failure = false;

long long now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

// With g_surface_lock held.
void destroy_surface_locked() {
    if (g_surface != EGL_NO_SURFACE && g_egl_display != EGL_NO_DISPLAY) eglDestroySurface(g_egl_display, g_surface);
    g_surface = EGL_NO_SURFACE;
    g_swap_interval_set = false;
}
void release_window_locked() {
    destroy_surface_locked();
    if (g_window) ANativeWindow_release(g_window);
    g_window = nullptr;
    std::lock_guard info(g_info_lock);
    g_display.surface = false;
}

// The second ImGui context's end (its GL objects; the shared font atlas
// stays the window's).
void drop_imgui() {
    if (!g_imgui) return;
    ImGuiContext* top = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_imgui);
    if (ImGui::GetIO().BackendRendererUserData) ImGui_ImplOpenGL3_Shutdown();
    ImGui::SetCurrentContext(top == g_imgui ? nullptr : top);
    ImGui::DestroyContext(g_imgui);
    g_imgui = nullptr;
    g_scale = 0;
}

void log_line(const char* text) {
    std::printf("%s\n", text);
    std::fflush(stdout);
}

// The draw data's digest: vertices, indices and commands (with their
// textures), the size, and whether a texture waits for an upload (the font
// atlas baked new glyphs). Never 0.
std::uint64_t draw_digest(const ImDrawData* data, int width, int height) {
    std::uint64_t h = 0x9e3779b97f4a7c15ull ^ (std::uint64_t(std::uint32_t(width)) << 32 | std::uint32_t(height));
    const auto mix = [&h](const void* bytes, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(bytes);
        std::size_t i = 0;
        for (; i + 8 <= size; i += 8) {
            std::uint64_t word;
            std::memcpy(&word, p + i, 8);
            h = (h ^ word) * 0x100000001b3ull;
            h ^= h >> 29;
        }
        for (; i < size; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    };
    if (!data) return 1;
    for (const ImDrawList* list : data->CmdLists) {
        mix(list->VtxBuffer.Data, std::size_t(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        mix(list->IdxBuffer.Data, std::size_t(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            mix(&cmd.ClipRect, sizeof cmd.ClipRect);
            mix(&cmd.TexRef._TexData, sizeof cmd.TexRef._TexData);
            mix(&cmd.TexRef._TexID, sizeof cmd.TexRef._TexID);
            const std::uint32_t counts[3] = {cmd.VtxOffset, cmd.IdxOffset, cmd.ElemCount};
            mix(counts, sizeof counts);
        }
    }
    if (data->Textures)
        for (const ImTextureData* tex : *data->Textures)
            if (tex->Status != ImTextureStatus_OK) h = (h ^ std::uint64_t(tex->Status)) * 0x100000001b3ull + 1;
    return h ? h : 1;
}

// GL state the lower frame changes besides ImGui's (its backend restores its own).
struct GlState {
    GLint framebuffer = 0, viewport[4]{};
    GLfloat clear[4]{};
    GLboolean scissor = GL_FALSE;
    void save() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
    }
    void restore() const {
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(framebuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);
    }
};

} // namespace

Screen2Display screen2_display() {
    std::lock_guard info(g_info_lock);
    return g_display;
}

void screen2_request(bool wanted) {
    if (int(wanted) == g_wanted_sent) return;
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return; // (dw4_det: no Java side)
    g_wanted_sent = int(wanted);
    jclass type = env->GetObjectClass(activity);
    if (jmethodID method = type ? env->GetStaticMethodID(type, "setLowerScreenWanted", "(Z)V") : nullptr)
        env->CallStaticVoidMethod(type, method, jboolean(wanted ? JNI_TRUE : JNI_FALSE));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        log_line("second screen: DW4Activity.setLowerScreenWanted failed");
    }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

double screen2_seconds_since_touch() {
    const long long last = g_last_touch_ns.load();
    if (!last) return 1e9;
    return double(now_ns() - last) / 1e9;
}

void screen2_request_shot(const std::string& path) {
    std::lock_guard info(g_info_lock);
    g_shot = path;
}

bool screen2_frame(const std::function<void()>& input, const std::function<void()>& draw, int ui_scale_setting, bool now) {
    ImGuiContext* top = ImGui::GetCurrentContext();
    if (!top) return false;
    bool pending = false;
    int dpi = 160;
    {
        std::lock_guard info(g_info_lock);
        pending = !g_touches.empty() || !g_shot.empty();
        dpi = g_display.dpi > 0 ? g_display.dpi : 160;
    }
    // Every other frame (30 a second beside a 60 Hz game) unless something
    // waits: a touch (and half a second after it), the F1 menu shown here.
    if (!now && !pending && screen2_seconds_since_touch() > 0.5 && g_drew_last) {
        g_drew_last = false;
        return false;
    }
    std::unique_lock surface_lock(g_surface_lock);
    if (!g_window) return false;
    const auto start = Clock::now();
    auto egl_display = static_cast<EGLDisplay>(SDL_EGL_GetCurrentDisplay());
    auto config = static_cast<EGLConfig>(SDL_EGL_GetCurrentConfig());
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface top_draw = eglGetCurrentSurface(EGL_DRAW), top_read = eglGetCurrentSurface(EGL_READ);
    if (egl_display == EGL_NO_DISPLAY || !config || context == EGL_NO_CONTEXT) return false;
    if (g_surface == EGL_NO_SURFACE) {
        // (SDL_EGL_CreateSurface's way: the config's visual for the window.)
        EGLint format = 0;
        eglGetConfigAttrib(egl_display, config, EGL_NATIVE_VISUAL_ID, &format);
        ANativeWindow_setBuffersGeometry(g_window, 0, 0, format);
        g_surface = eglCreateWindowSurface(egl_display, config, g_window, nullptr);
        g_egl_display = egl_display;
        if (g_surface == EGL_NO_SURFACE) {
            if (!g_logged_failure) {
                std::fprintf(stderr, "second screen: eglCreateWindowSurface failed (EGL error 0x%x)\n", unsigned(eglGetError()));
                g_logged_failure = true;
            }
            return false;
        }
        g_logged_failure = false;
        g_digest = 0; // (a new surface: its first picture)
        const Screen2Display shown = screen2_display();
        std::printf("second screen: drawing on display %d \"%s\" (%dx%d, %d dpi)\n", shown.id, shown.name.c_str(), ANativeWindow_getWidth(g_window),
                    ANativeWindow_getHeight(g_window), shown.dpi);
        std::fflush(stdout);
    }
    // The panels' frame (ImGui only, SDL's surface stays current), then the
    // lower surface current, drawn and swapped only when the picture changed:
    // a panel that shows the same costs no surface switch and no swap.
    EGLint width = 0, height = 0;
    eglQuerySurface(egl_display, g_surface, EGL_WIDTH, &width);
    eglQuerySurface(egl_display, g_surface, EGL_HEIGHT, &height);
    if (!g_imgui) {
        g_imgui = ImGui::CreateContext(ImGui::GetIO().Fonts);
        ImGui::SetCurrentContext(g_imgui);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui_ImplOpenGL3_Init("#version 300 es");
        ImGui::SetCurrentContext(top);
        g_scale = 0;
        g_last_frame = start;
        g_digest = 0;
    }
    ImGui::SetCurrentContext(g_imgui);
    const float scale = ui_scale_factor(ui_scale_setting, float(dpi) / 160.0f, true);
    if (scale != g_scale) {
        apply_ui_style(scale);
        g_scale = scale;
        std::printf("second screen: menus at %d %% (%d dpi)\n", int(scale * 100.0f + 0.5f), dpi);
        std::fflush(stdout);
    }
    const float top_scale = exchange_ui_scale(scale);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = std::max(float(std::chrono::duration<double>(start - g_last_frame).count()), 1e-4f);
    g_last_frame = start;
    std::string shot;
    {
        std::lock_guard info(g_info_lock);
        for (const Touch& touch : g_touches) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(touch.x, touch.y);
            if (touch.action == 0) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            if (touch.action >= 2) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                // (No hover stays behind a finger.)
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            }
        }
        g_touches.clear();
        shot.swap(g_shot);
    }
    if (input) input();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    if (draw) draw();
    ImGui::Render();
    const std::uint64_t digest = draw_digest(ImGui::GetDrawData(), width, height);
    bool swapped = false, failed = false;
    if (digest != g_digest || !shot.empty()) {
        if (!eglMakeCurrent(egl_display, g_surface, g_surface, context)) {
            std::fprintf(stderr, "second screen: eglMakeCurrent failed (EGL error 0x%x); the surface is made again\n", unsigned(eglGetError()));
            eglMakeCurrent(egl_display, top_draw, top_read, context);
            destroy_surface_locked();
            failed = true;
        } else {
            if (!g_swap_interval_set) {
                // (The lower screen's swaps never wait for its display: the
                // game's pace is the top screen's.)
                eglSwapInterval(egl_display, 0);
                g_swap_interval_set = true;
            }
            GlState saved;
            saved.save();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!shot.empty()) {
                std::vector<std::uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                write_bmp(shot, width, height, pixels);
                std::printf("screenshot %s (the second screen)\n", shot.c_str());
                std::fflush(stdout);
            }
            static const bool gl_check = std::getenv("DW4_GL_CHECK") && std::atoi(std::getenv("DW4_GL_CHECK")) != 0;
            if (gl_check)
                for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
                    if (++g_gl_errors <= 20) std::fprintf(stderr, "second screen: GL error 0x%x (frame %d)\n", unsigned(error), g_frames);
            if (!eglSwapBuffers(egl_display, g_surface))
                std::fprintf(stderr, "second screen: eglSwapBuffers failed (EGL error 0x%x)\n", unsigned(eglGetError()));
            eglMakeCurrent(egl_display, top_draw, top_read, context);
            saved.restore();
            g_digest = digest;
            swapped = true;
            ++g_swaps;
        }
    }
    ++g_frames;
    exchange_ui_scale(top_scale);
    ImGui::SetCurrentContext(top);
    if (g_abandon.exchange(false)) release_window_locked();
    g_drew_last = true;
    frame_stats_screen2(std::chrono::duration<double, std::milli>(Clock::now() - start).count(), swapped);
    return !failed;
}

void screen2_device_reset() {
    std::lock_guard surface_lock(g_surface_lock);
    // (Its context first: the shared font atlas's texture is then the
    // window's alone, which its backend's restart makes again.)
    drop_imgui();
    destroy_surface_locked();
    g_digest = 0;
    if (g_window) log_line("second screen: made again after the lost OpenGL context");
}

void screen2_enter_background() {
    std::lock_guard surface_lock(g_surface_lock);
    if (g_surface == EGL_NO_SURFACE) return;
    destroy_surface_locked();
    log_line("second screen: its surface let go before SDL's pause");
}

void screen2_shutdown() {
    {
        std::lock_guard surface_lock(g_surface_lock);
        drop_imgui();
        destroy_surface_locked();
    }
    if (std::getenv("DW4_GL_CHECK"))
        std::printf("second screen: GL error check: %d errors in %d frames (%d drawn and swapped)\n", g_gl_errors, g_frames, g_swaps);
    screen2_request(false);
    g_wanted_sent = -1;
    std::fflush(stdout);
}

} // namespace dw4::game

// LowerScreen.java's natives (Java's main thread).
extern "C" {

// The Presentation's surface is there or changed (surfaceChanged).
JNIEXPORT void JNICALL Java_org_dw4pc_game_LowerScreen_nativeSurface(JNIEnv* env, jclass, jobject surface) {
    using namespace dw4::game;
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
    std::lock_guard surface_lock(g_surface_lock);
    if (window && window == g_window) {
        ANativeWindow_release(window); // (the same surface: EGL follows its size)
        return;
    }
    release_window_locked();
    g_window = window;
    std::lock_guard info(g_info_lock);
    g_display.surface = window != nullptr;
}

// The surface's end (surfaceDestroyed): waits for a frame that is drawing
// into it (at most 2 s; a frame that takes longer lets it go itself).
JNIEXPORT jboolean JNICALL Java_org_dw4pc_game_LowerScreen_nativeSurfaceGone(JNIEnv*, jclass) {
    using namespace dw4::game;
    if (g_surface_lock.try_lock_for(std::chrono::seconds(2))) {
        release_window_locked();
        g_surface_lock.unlock();
        return JNI_TRUE;
    }
    g_abandon.store(true);
    std::lock_guard info(g_info_lock);
    g_display.surface = false;
    return JNI_FALSE;
}

// The display the panels go to (id -1: none), for F1 > Settings.
JNIEXPORT void JNICALL Java_org_dw4pc_game_LowerScreen_nativeDisplay(JNIEnv* env, jclass, jint id, jstring name, jint width, jint height,
                                                                       jint dpi) {
    using namespace dw4::game;
    std::string text;
    if (name) {
        if (const char* chars = env->GetStringUTFChars(name, nullptr)) {
            text = chars;
            env->ReleaseStringUTFChars(name, chars);
        }
    }
    std::lock_guard info(g_info_lock);
    g_display.found = id >= 0;
    g_display.id = id;
    g_display.name = text;
    g_display.width = width;
    g_display.height = height;
    g_display.dpi = dpi;
}

// A touch on the panels (the first finger; surface pixels).
JNIEXPORT void JNICALL Java_org_dw4pc_game_LowerScreen_nativeTouch(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    using namespace dw4::game;
    g_last_touch_ns.store(now_ns());
    std::lock_guard info(g_info_lock);
    // (A long drag between two frames keeps its last moves only.)
    if (g_touches.size() >= 64) g_touches.erase(g_touches.begin(), g_touches.begin() + 32);
    g_touches.push_back({int(action), float(x), float(y)});
}

} // extern "C"
