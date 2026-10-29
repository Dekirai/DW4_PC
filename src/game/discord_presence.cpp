#include "discord_presence.hpp"

#include <array>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace dw4::game {

// One open pipe to the client (Windows named pipes; elsewhere none opens).
struct DiscordPresence::Pipe {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Pipe() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    bool open(const std::string& name) {
        const std::string path = "\\\\?\\pipe\\" + name;
        handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        return handle != INVALID_HANDLE_VALUE;
    }
    bool write(const std::vector<std::uint8_t>& bytes) {
        std::size_t done = 0;
        while (done < bytes.size()) {
            DWORD wrote = 0;
            if (!WriteFile(handle, bytes.data() + done, DWORD(bytes.size() - done), &wrote, nullptr) || wrote == 0) return false;
            done += wrote;
        }
        return true;
    }
    // Bytes waiting; -1 when the pipe broke.
    long long available() {
        DWORD bytes = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &bytes, nullptr)) return -1;
        return bytes;
    }
    bool read(std::uint8_t* out, std::size_t size) {
        std::size_t done = 0;
        while (done < size) {
            DWORD got = 0;
            if (!ReadFile(handle, out + done, DWORD(size - done), &got, nullptr) || got == 0) return false;
            done += got;
        }
        return true;
    }
#else
    bool open(const std::string&) { return false; }
    bool write(const std::vector<std::uint8_t>&) { return false; }
    long long available() { return -1; }
    bool read(std::uint8_t*, std::size_t) { return false; }
#endif
    // One whole frame when its header is waiting (nullopt: none yet);
    // `broken` when the pipe failed.
    std::optional<std::pair<discord::Opcode, std::string>> next(bool& broken) {
        broken = false;
        const long long waiting = available();
        if (waiting < 0) {
            broken = true;
            return std::nullopt;
        }
        if (waiting < static_cast<long long>(discord::kHeaderSize)) return std::nullopt;
        std::array<std::uint8_t, discord::kHeaderSize> header{};
        if (!read(header.data(), header.size())) {
            broken = true;
            return std::nullopt;
        }
        const auto decoded = discord::decode_header(header);
        if (!decoded) {
            broken = true;
            return std::nullopt;
        }
        std::string payload(decoded->length, '\0');
        if (decoded->length && !read(reinterpret_cast<std::uint8_t*>(payload.data()), payload.size())) {
            broken = true;
            return std::nullopt;
        }
        return std::make_pair(decoded->opcode, std::move(payload));
    }
};

namespace {

std::uint32_t process_id() {
#ifdef _WIN32
    return std::uint32_t(GetCurrentProcessId());
#else
    return std::uint32_t(getpid());
#endif
}

} // namespace

DiscordPresence::DiscordPresence() : DiscordPresence(Options{}) {}

DiscordPresence::DiscordPresence(Options options) : options_(std::move(options)) {
    worker_ = std::thread([this] { run(); });
}

DiscordPresence::~DiscordPresence() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void DiscordPresence::configure(bool enabled, std::string client_id) {
    {
        std::lock_guard lock(mutex_);
        if (enabled == enabled_ && client_id == client_id_) return;
        enabled_ = enabled;
        client_id_ = std::move(client_id);
        ++config_serial_;
    }
    wake_.notify_all();
}

void DiscordPresence::set_activity(std::optional<discord::Activity> activity) {
    {
        std::lock_guard lock(mutex_);
        if (activity == wanted_) return;
        wanted_ = std::move(activity);
        ++wanted_serial_;
    }
    wake_.notify_all();
}

DiscordPresence::State DiscordPresence::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::string DiscordPresence::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::uint64_t DiscordPresence::sent_count() const {
    std::lock_guard lock(mutex_);
    return sent_;
}

bool DiscordPresence::write_frame(discord::Opcode opcode, const std::string& json) {
    return pipe_ && pipe_->write(discord::encode_frame(opcode, json));
}

bool DiscordPresence::drain() {
    while (pipe_) {
        bool broken = false;
        auto frame = pipe_->next(broken);
        if (broken) return false;
        if (!frame) return true;
        if (frame->first == discord::Opcode::ping) {
            if (!write_frame(discord::Opcode::pong, frame->second)) return false;
        } else if (frame->first == discord::Opcode::close) {
            return false;
        }
        // FRAME replies (SET_ACTIVITY results, errors) need no answer.
    }
    return false;
}

void DiscordPresence::disconnect(bool say_close) {
    if (!pipe_) return;
    if (say_close) {
        std::string client;
        {
            std::lock_guard lock(mutex_);
            client = client_id_;
        }
        write_frame(discord::Opcode::close, discord::handshake_json(client));
    }
    pipe_.reset();
}

bool DiscordPresence::connect(std::unique_lock<std::mutex>& lock) {
    const std::string client = client_id_;
    const std::uint64_t config = config_serial_;
    lock.unlock();
    std::string refused;
    for (int n = 0; n < 10 && refused.empty(); ++n) {
        auto pipe = std::make_unique<Pipe>();
        if (!pipe->open(options_.pipe_prefix + std::to_string(n))) continue;
        pipe_ = std::move(pipe);
        if (!write_frame(discord::Opcode::handshake, discord::handshake_json(client))) {
            pipe_.reset();
            continue;
        }
        // The READY dispatch, or a CLOSE with the reason (an unknown
        // application id: 4000).
        const auto deadline = std::chrono::steady_clock::now() + options_.reply_timeout;
        bool ready = false;
        while (!ready && refused.empty() && std::chrono::steady_clock::now() < deadline) {
            bool broken = false;
            auto frame = pipe_->next(broken);
            if (broken) break;
            if (!frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (frame->first == discord::Opcode::frame && discord::json_string_field(frame->second, "evt") == "READY") ready = true;
            else if (frame->first == discord::Opcode::close) {
                refused = discord::json_string_field(frame->second, "message");
                if (const auto code = discord::json_int_field(frame->second, "code")) refused += " (" + std::to_string(*code) + ")";
                if (refused.empty()) refused = "closed";
            } else if (frame->first == discord::Opcode::ping) {
                write_frame(discord::Opcode::pong, frame->second);
            }
        }
        if (ready) {
            lock.lock();
            connected_config_ = config;
            sent_any_ = false;
            state_ = State::connected;
            status_ = "Connected to Discord";
            return true;
        }
        pipe_.reset();
    }
    lock.lock();
    if (!refused.empty()) {
        state_ = State::rejected;
        status_ = "Discord refused the connection: " + refused;
    } else {
        state_ = State::searching;
        status_ = "Looking for the Discord app (not running?)";
    }
    return false;
}

void DiscordPresence::run() {
    std::unique_lock lock(mutex_);
    while (!stop_) {
        if (!enabled_ || client_id_.empty()) {
            if (pipe_) {
                lock.unlock();
                disconnect(true);
                lock.lock();
            }
            state_ = State::off;
            status_ = enabled_ ? "No application id" : "Off";
            wake_.wait(lock, [&] { return stop_ || (enabled_ && !client_id_.empty()); });
            next_attempt_ = {};
            continue;
        }
        auto now = std::chrono::steady_clock::now();
        if (pipe_ && connected_config_ != config_serial_) {
            // A new application id: a new connection.
            lock.unlock();
            disconnect(true);
            lock.lock();
            next_attempt_ = {};
        }
        if (!pipe_ && now >= next_attempt_) {
            if (!connect(lock)) next_attempt_ = std::chrono::steady_clock::now() + options_.retry_interval;
            if (stop_) break;
            now = std::chrono::steady_clock::now();
        }
        if (pipe_) {
            lock.unlock();
            const bool alive = drain();
            lock.lock();
            if (!alive) {
                pipe_.reset();
                state_ = State::searching;
                status_ = "Discord closed the connection; looking again";
                next_attempt_ = now + options_.retry_interval;
                continue;
            }
            const bool changed = !sent_any_ || wanted_serial_ != sent_serial_;
            if (changed && (!sent_any_ || now - last_send_ >= options_.send_interval)) {
                const auto activity = wanted_;
                const auto serial = wanted_serial_;
                const auto nonce = ++nonce_;
                lock.unlock();
                const bool ok = write_frame(discord::Opcode::frame, discord::set_activity_json(process_id(), activity, nonce));
                lock.lock();
                if (ok) {
                    sent_serial_ = serial;
                    sent_any_ = true;
                    last_send_ = now;
                    ++sent_;
                } else {
                    pipe_.reset();
                    state_ = State::searching;
                    status_ = "Discord closed the connection; looking again";
                    next_attempt_ = now + options_.retry_interval;
                    continue;
                }
            }
        }
        wake_.wait_for(lock, std::chrono::milliseconds(200));
    }
    if (pipe_) {
        lock.unlock();
        disconnect(true);
    }
}

} // namespace dw4::game
