// Android only (docs/ANDROID.md): stdout and stderr into logcat, the
// language packs from the APK's assets.
#include "app_paths.hpp"

#include <android/log.h>
#include <jni.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace dw4::game {

namespace {
// stderr carries the engine's trace ("[frame] stage change -> ...") and
// SDL's echo of its own log next to the real problems, so a stderr line is a
// warning (or an error) only when its words say so: logcat's W/DW4 and E/DW4
// are the problems alone, everything else is I/DW4 like stdout.
int stderr_line_priority(const std::string& line) {
    // (main's catch: the reason the game stops, before its message box.)
    if (line.rfind("dw4_game: ", 0) == 0) return ANDROID_LOG_ERROR;
    std::string lower(line);
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    for (const char* word : {"fatal", "crash", "abort", "exception"})
        if (lower.find(word) != std::string::npos) return ANDROID_LOG_ERROR;
    for (const char* word : {"error", "fail", "cannot", "could not", "can't", "unable", "warning", "refused", "invalid", "missing",
                             "not found", "unsupported", "corrupt"})
        if (lower.find(word) != std::string::npos) return ANDROID_LOG_WARN;
    return ANDROID_LOG_INFO;
}

// One reader per stream: lines (without the newline) into logcat (stdout at
// INFO, stderr by stderr_line_priority); a line longer than logcat's limit
// goes in pieces.
void pump(int fd, bool from_stderr) {
    std::string line;
    char buffer[1024];
    auto write_line = [&] { __android_log_write(from_stderr ? stderr_line_priority(line) : ANDROID_LOG_INFO, "DW4", line.c_str()); };
    for (;;) {
        const ssize_t got = read(fd, buffer, sizeof buffer);
        if (got <= 0) break;
        for (ssize_t i = 0; i < got; ++i) {
            if (buffer[i] == '\n' || line.size() >= 3000) {
                write_line();
                line.clear();
                if (buffer[i] == '\n') continue;
            }
            line.push_back(buffer[i]);
        }
    }
    if (!line.empty()) write_line();
}

void route(FILE* stream, int fd, bool from_stderr) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) return;
    std::fflush(stream);
    setvbuf(stream, nullptr, _IOLBF, 0);
    if (dup2(pipe_fds[1], fd) < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return;
    }
    close(pipe_fds[1]);
    std::thread(pump, pipe_fds[0], from_stderr).detach();
}
} // namespace

void route_output_to_logcat() {
    static bool routed = false;
    if (routed) return;
    routed = true;
    route(stdout, STDOUT_FILENO, false);
    route(stderr, STDERR_FILENO, true);
}

namespace {
// SDL_EnumerateDirectory's names, collected (a relative path that is no
// folder of the internal storage is read from the APK's assets).
SDL_EnumerationResult SDLCALL collect_name(void* names, const char*, const char* name) {
    static_cast<std::vector<std::string>*>(names)->emplace_back(name);
    return SDL_ENUM_CONTINUE;
}

std::vector<std::string> asset_names(const std::string& folder) {
    std::vector<std::string> names;
    if (!SDL_EnumerateDirectory(folder.c_str(), collect_name, &names)) names.clear();
    return names;
}

bool same_contents(const std::filesystem::path& path, const void* data, std::size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string have{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return have.size() == size && std::equal(have.begin(), have.end(), static_cast<const char*>(data));
}
} // namespace

std::string android_property(const char* name) {
    char value[PROP_VALUE_MAX] = {};
    if (__system_property_get(name, value) <= 0) return {};
    return value;
}

void android_set_frame_rate(float hz) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    // (The activity's own class: FindClass on this thread would look in the
    // system class loader.)
    jclass type = env->GetObjectClass(activity);
    if (jmethodID method = type ? env->GetStaticMethodID(type, "setFrameRate", "(F)V") : nullptr)
        env->CallStaticVoidMethod(type, method, jfloat(hz));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        std::fprintf(stderr, "display: DW4Activity.setFrameRate(%.0f) failed\n", double(hz));
    }
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

void android_show_text_input(int input_type, int x, int y, int w, int h) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass type = env->GetObjectClass(activity);
    if (jmethodID method = type ? env->GetStaticMethodID(type, "showTextInputAgain", "(IIIII)V") : nullptr)
        env->CallStaticVoidMethod(type, method, jint(input_type), jint(x), jint(y), jint(w), jint(h));
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

void android_restart_text_input() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return;
    jclass type = env->GetObjectClass(activity);
    if (jmethodID method = type ? env->GetStaticMethodID(type, "restartTextInput", "()V") : nullptr) env->CallStaticVoidMethod(type, method);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (type) env->DeleteLocalRef(type);
    env->DeleteLocalRef(activity);
}

void install_bundled_language_packs(const std::filesystem::path& lang_root) {
    if (lang_root.empty()) return;
    int written = 0;
    for (const auto& code : asset_names("lang")) {
        for (const auto& name : asset_names("lang/" + code)) {
            // (Never a game.tsv: the game text is the player's own file.)
            if (name == "game.tsv") continue;
            const std::string asset = "lang/" + code + "/" + name;
            std::size_t size = 0;
            void* data = SDL_LoadFile(asset.c_str(), &size);
            if (!data) continue;
            const auto target = lang_root / detail::utf8_path(code.c_str()) / detail::utf8_path(name.c_str());
            if (!same_contents(target, data, size)) {
                std::error_code error;
                std::filesystem::create_directories(target.parent_path(), error);
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (out.write(static_cast<const char*>(data), std::streamsize(size)) && out.flush())
                    ++written;
                else
                    std::fprintf(stderr, "language: %s could not be written to %s\n", asset.c_str(), target.c_str());
            }
            SDL_free(data);
        }
    }
    if (written) std::printf("language: %d language pack files from the APK into %s\n", written, lang_root.c_str());
}

namespace {
std::atomic<bool> g_game_paused{false};
} // namespace

void android_note_game_paused(bool paused) { g_game_paused.store(paused); }

} // namespace dw4::game

// DW4Activity.nativeGamePaused: whether the game's thread has reached SDL's
// pause (DID_ENTER_BACKGROUND seen by the lifecycle watch; SDL lets go of the
// OpenGL context right after it), for the surface's end to wait for.
extern "C" JNIEXPORT jboolean JNICALL Java_org_dw4pc_game_DW4Activity_nativeGamePaused(JNIEnv*, jclass) {
    return dw4::game::g_game_paused.load() ? JNI_TRUE : JNI_FALSE;
}
