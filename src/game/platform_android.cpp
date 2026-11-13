// Android only (docs/ANDROID.md): stdout and stderr into logcat.
#include "app_paths.hpp"

#include <android/log.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <thread>

namespace dw4::game {

namespace {
// One reader per stream: lines (without the newline) at INFO for stdout,
// WARN for stderr; a line longer than logcat's limit goes in pieces.
void pump(int fd, int priority) {
    std::string line;
    char buffer[1024];
    for (;;) {
        const ssize_t got = read(fd, buffer, sizeof buffer);
        if (got <= 0) break;
        for (ssize_t i = 0; i < got; ++i) {
            if (buffer[i] == '\n' || line.size() >= 3000) {
                __android_log_write(priority, "DW4", line.c_str());
                line.clear();
                if (buffer[i] == '\n') continue;
            }
            line.push_back(buffer[i]);
        }
    }
    if (!line.empty()) __android_log_write(priority, "DW4", line.c_str());
}

void route(FILE* stream, int fd, int priority) {
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
    std::thread(pump, pipe_fds[0], priority).detach();
}
} // namespace

void route_output_to_logcat() {
    static bool routed = false;
    if (routed) return;
    routed = true;
    route(stdout, STDOUT_FILENO, ANDROID_LOG_INFO);
    route(stderr, STDERR_FILENO, ANDROID_LOG_WARN);
}

} // namespace dw4::game
