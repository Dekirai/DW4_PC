#include "net_log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <ctime>
#include <system_error>

#ifdef _WIN32
#include <share.h>
#endif

namespace dw4::game::online {

namespace {
std::FILE* open_append(const std::filesystem::path& path) {
    std::FILE* f = nullptr;
#ifdef _WIN32
    // (Shared for reading: the player can open or copy the log while the game runs.)
    f = _wfsopen(path.c_str(), L"ab", _SH_DENYWR);
#else
    f = std::fopen(path.c_str(), "ab");
#endif
    return f;
}
} // namespace

std::filesystem::path rotated_log_name(const std::filesystem::path& path, int n) {
    auto name = path.stem().string() + "." + std::to_string(n) + path.extension().string();
    return path.parent_path() / name;
}

OnlineLog::~OnlineLog() { close(); }

bool OnlineLog::open(const std::filesystem::path& path, std::uintmax_t max_bytes, int keep) {
    close();
    path_ = path;
    max_bytes_ = std::max<std::uintmax_t>(max_bytes, 4096);
    keep_ = std::max(keep, 1);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    // Opened first: a file another game holds (two games from one folder) is
    // left alone, its older logs too, and this game writes another name
    // (OnlineRun::open_log). Round 62 review: the second game rotated first,
    // deleted online.3.log, moved .1 and .2 on, failed to move the held
    // online.log and left no online.1.log.
    file_ = open_append(path);
    if (!file_) return false;
    written_ = std::filesystem::file_size(path, ec);
    if (ec) written_ = 0;
    if (written_ > max_bytes_) rotate();
    return file_ != nullptr;
}

void OnlineLog::close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
}

void OnlineLog::rotate() {
    const bool was_open = file_ != nullptr;
    close();
    std::error_code ec;
    // The current file moves aside first: when it cannot (another program
    // holds it), nothing older is touched and this game appends on; the next
    // try comes after another max_bytes.
    const auto aside = path_.parent_path() / (path_.filename().string() + ".rotating");
    std::filesystem::rename(path_, aside, ec);
    if (!ec) {
        std::filesystem::remove(rotated_log_name(path_, keep_), ec);
        for (int n = keep_ - 1; n >= 1; --n) {
            const auto from = rotated_log_name(path_, n);
            if (std::filesystem::exists(from, ec)) std::filesystem::rename(from, rotated_log_name(path_, n + 1), ec);
        }
        std::filesystem::rename(aside, rotated_log_name(path_, 1), ec);
    }
    written_ = 0;
    if (was_open) file_ = open_append(path_);
}

void OnlineLog::line(const char* format, ...) {
    if (!file_) return;
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof text, format, args);
    va_end(args);
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &local);
    const int n = std::fprintf(file_, "%s.%03d %s\n", stamp, ms, text);
    std::fflush(file_);
    if (n > 0) written_ += std::uintmax_t(n);
    if (written_ > max_bytes_) rotate();
}

} // namespace dw4::game::online
