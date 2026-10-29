#include "discord_presence.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
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

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

std::uint32_t process_id() {
#ifdef _WIN32
    return std::uint32_t(GetCurrentProcessId());
#else
    return std::uint32_t(getpid());
#endif
}

enum class Io { ok, failed, timeout, stopped };

#ifdef _WIN32
using StopHandle = HANDLE;
using Count = DWORD;
#else
using StopHandle = void*;
using Count = unsigned;
#endif

// One open pipe to the client (Windows named pipes, opened for overlapped
// transfers; elsewhere none opens). `stop` is the worker's stop event: a
// transfer waiting on the client ends as soon as it is set, except the
// last CLOSE, which only its own timeout bounds.
struct Pipe {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    HANDLE stop = nullptr;
    OVERLAPPED overlapped{};
#endif
    // A cancelled transfer did not finish: the pipe (and its OVERLAPPED)
    // must outlive it, so it is never freed.
    bool pending{};

#ifdef _WIN32
    explicit Pipe(HANDLE stop_event) : event(CreateEventA(nullptr, TRUE, FALSE, nullptr)), stop(stop_event) {}
    ~Pipe() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (event) CloseHandle(event);
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    // false: not open; `exists` tells a busy pipe (every instance in use)
    // from a missing one.
    bool open(const std::string& name, bool& exists) {
        const std::string path = "\\\\?\\pipe\\" + name;
        handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        exists = handle != INVALID_HANDLE_VALUE || GetLastError() == ERROR_PIPE_BUSY;
        return handle != INVALID_HANDLE_VALUE && event;
    }
    // One overlapped ReadFile / WriteFile, waited for at most `timeout`.
    Io transfer(bool writing, void* data, Count size, Count& done, milliseconds timeout, bool interruptible) {
        done = 0;
        overlapped = {};
        overlapped.hEvent = event;
        ResetEvent(event);
        const BOOL started = writing ? WriteFile(handle, data, size, nullptr, &overlapped) : ReadFile(handle, data, size, nullptr, &overlapped);
        if (!started && GetLastError() != ERROR_IO_PENDING) return Io::failed;
        HANDLE waits[2] = {event, stop};
        const DWORD count = interruptible && stop ? 2 : 1;
        const DWORD waited = WaitForMultipleObjects(count, waits, FALSE, DWORD(std::max<long long>(0, timeout.count())));
        if (waited == WAIT_OBJECT_0) {
            if (!GetOverlappedResult(handle, &overlapped, &done, FALSE) || done == 0) return Io::failed;
            return Io::ok;
        }
        // Timed out or stopped: cancel and wait for the cancellation (named
        // pipes cancel at once).
        CancelIoEx(handle, &overlapped);
        if (!GetOverlappedResultEx(handle, &overlapped, &done, 1000, FALSE) && GetLastError() == WAIT_TIMEOUT) pending = true;
        return waited == WAIT_OBJECT_0 + 1 ? Io::stopped : Io::timeout;
    }
    // Bytes waiting; -1 when the pipe broke.
    long long available() {
        DWORD bytes = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &bytes, nullptr)) return -1;
        return bytes;
    }
#else
    explicit Pipe(void*) {}
    bool open(const std::string&, bool& exists) {
        exists = false;
        return false;
    }
    Io transfer(bool, void*, Count, Count& done, milliseconds, bool) {
        done = 0;
        return Io::failed;
    }
    long long available() { return -1; }
#endif

    // Writes all bytes within `timeout`.
    Io write(const std::vector<std::uint8_t>& bytes, milliseconds timeout, bool interruptible) {
        const auto deadline = Clock::now() + timeout;
        std::size_t done = 0;
        while (done < bytes.size()) {
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) return Io::timeout;
            Count wrote = 0;
            const Io io = transfer(true, const_cast<std::uint8_t*>(bytes.data() + done), Count(bytes.size() - done), wrote, left, interruptible);
            if (io != Io::ok) return io;
            done += wrote;
        }
        return Io::ok;
    }
    // Reads exactly `size` bytes within `timeout`.
    Io read(std::uint8_t* out, std::size_t size, milliseconds timeout) {
        const auto deadline = Clock::now() + timeout;
        std::size_t done = 0;
        while (done < size) {
            const auto left = std::chrono::duration_cast<milliseconds>(deadline - Clock::now());
            if (left.count() <= 0) return Io::timeout;
            Count got = 0;
            const Io io = transfer(false, out + done, Count(size - done), got, left, true);
            if (io != Io::ok) return io;
            done += got;
        }
        return Io::ok;
    }
    // One whole frame when its header is waiting (nullopt with Io::ok: none
    // yet). Its payload must follow within `timeout`.
    std::optional<std::pair<discord::Opcode, std::string>> next(Io& io, milliseconds timeout) {
        io = Io::ok;
        const long long waiting = available();
        if (waiting < 0) {
            io = Io::failed;
            return std::nullopt;
        }
        if (waiting < static_cast<long long>(discord::kHeaderSize)) return std::nullopt;
        std::array<std::uint8_t, discord::kHeaderSize> header{};
        if ((io = read(header.data(), header.size(), timeout)) != Io::ok) return std::nullopt;
        const auto decoded = discord::decode_header(header);
        if (!decoded) {
            io = Io::failed;
            return std::nullopt;
        }
        std::string payload(decoded->length, '\0');
        if (decoded->length && (io = read(reinterpret_cast<std::uint8_t*>(payload.data()), payload.size(), timeout)) != Io::ok)
            return std::nullopt;
        return std::make_pair(decoded->opcode, std::move(payload));
    }
};

std::string seconds_text(milliseconds wait) { return std::to_string((wait.count() + 999) / 1000) + " s"; }

} // namespace

struct DiscordPresence::Shared {
    explicit Shared(Options o) : options(std::move(o)) {
#ifdef _WIN32
        stop_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
#endif
    }
    ~Shared() {
        drop_pipe();
#ifdef _WIN32
        if (stop_event) CloseHandle(stop_event);
#endif
    }
    Shared(const Shared&) = delete;
    Shared& operator=(const Shared&) = delete;

    void run();
    void connect(std::unique_lock<std::mutex>& lock);
    void disconnect(bool say_close, bool quitting);
    Io write_frame(discord::Opcode opcode, const std::string& json, milliseconds timeout, bool interruptible = true);
    // Reads the waiting frames (answers PING); not ok when the pipe broke,
    // the client closed it or a frame stalled.
    Io drain();
    // A failed or dropped connection (lock held): wait before the next
    // attempt, twice as long with every failure in a row.
    void failed(const std::string& what);
    void drop_pipe() {
        if (pipe && pipe->pending) static_cast<void>(pipe.release());
        pipe.reset();
    }

    Options options;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::condition_variable finished_signal;
    std::atomic<bool> stop{};
    bool finished{};
    bool enabled{};
    std::string client_id;
    std::uint64_t config_serial{};
    std::optional<discord::Activity> wanted;
    std::uint64_t wanted_serial{};
    State state = State::off;
    std::string status = "Off";
    std::uint64_t sent{};
    int failure_count{};
    std::uint64_t connection_count{};
    Clock::time_point next_attempt{};
    StopHandle stop_event = nullptr;
    // Worker only.
    std::unique_ptr<Pipe> pipe;
    std::uint64_t connected_config{};
    std::uint64_t sent_serial{};
    bool sent_any{};
    std::uint64_t nonce{};
    Clock::time_point last_send{};
};

DiscordPresence::DiscordPresence() : DiscordPresence(Options{}) {}

DiscordPresence::DiscordPresence(Options options) : shared_(std::make_shared<Shared>(std::move(options))) {
    worker_ = std::thread([shared = shared_] {
        shared->run();
        std::lock_guard lock(shared->mutex);
        shared->finished = true;
        shared->finished_signal.notify_all();
    });
}

DiscordPresence::~DiscordPresence() {
    std::unique_lock lock(shared_->mutex);
    shared_->stop = true;
#ifdef _WIN32
    if (shared_->stop_event) SetEvent(shared_->stop_event);
#endif
    shared_->wake.notify_all();
    // The worker leaves any transfer at once (the stop event) and gives the
    // CLOSE frame close_timeout; should it still be busy after
    // shutdown_timeout on top, it finishes alone: the thread holds its own
    // reference to everything it touches.
    const bool done = shared_->finished_signal.wait_for(lock, shared_->options.close_timeout + shared_->options.shutdown_timeout,
                                                        [&] { return shared_->finished; });
    lock.unlock();
    if (worker_.joinable()) {
        if (done) worker_.join();
        else worker_.detach();
    }
}

void DiscordPresence::configure(bool enabled, std::string client_id) {
    {
        std::lock_guard lock(shared_->mutex);
        if (enabled == shared_->enabled && client_id == shared_->client_id) return;
        shared_->enabled = enabled;
        shared_->client_id = std::move(client_id);
        ++shared_->config_serial;
        // A new setting starts without the backoff of the old one.
        shared_->failure_count = 0;
        shared_->next_attempt = {};
    }
    shared_->wake.notify_all();
}

void DiscordPresence::set_activity(std::optional<discord::Activity> activity) {
    {
        std::lock_guard lock(shared_->mutex);
        if (activity == shared_->wanted) return;
        shared_->wanted = std::move(activity);
        ++shared_->wanted_serial;
    }
    shared_->wake.notify_all();
}

DiscordPresence::State DiscordPresence::state() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->state;
}

std::string DiscordPresence::status() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->status;
}

std::uint64_t DiscordPresence::sent_count() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->sent;
}

int DiscordPresence::failures() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->failure_count;
}

std::uint64_t DiscordPresence::connections() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->connection_count;
}

Io DiscordPresence::Shared::write_frame(discord::Opcode opcode, const std::string& json, milliseconds timeout, bool interruptible) {
    if (!pipe) return Io::failed;
    return pipe->write(discord::encode_frame(opcode, json), timeout, interruptible);
}

Io DiscordPresence::Shared::drain() {
    while (pipe) {
        Io io = Io::ok;
        auto frame = pipe->next(io, options.io_timeout);
        if (io != Io::ok) return io;
        if (!frame) return Io::ok;
        if (frame->first == discord::Opcode::ping) {
            if (const Io pong = write_frame(discord::Opcode::pong, frame->second, options.io_timeout); pong != Io::ok) return pong;
        } else if (frame->first == discord::Opcode::close) {
            return Io::failed;
        }
        // FRAME replies (SET_ACTIVITY results, errors) need no answer.
    }
    return Io::failed;
}

void DiscordPresence::Shared::disconnect(bool say_close, bool quitting) {
    if (!pipe) return;
    if (say_close) {
        std::string client;
        {
            std::lock_guard lock(mutex);
            client = client_id;
        }
        // When quitting the stop event is set: the CLOSE is not interrupted
        // by it, only bounded by close_timeout.
        write_frame(discord::Opcode::close, discord::handshake_json(client), quitting ? options.close_timeout : options.io_timeout, !quitting);
    }
    drop_pipe();
}

void DiscordPresence::Shared::failed(const std::string& what) {
    ++failure_count;
    const milliseconds cap = std::max(options.max_retry_interval, options.retry_interval);
    milliseconds wait = options.retry_interval;
    for (int i = 1; i < failure_count && wait < cap; ++i) wait *= 2;
    wait = std::min(wait, cap);
    next_attempt = Clock::now() + wait;
    status = what + "; trying again in " + seconds_text(wait);
}

void DiscordPresence::Shared::connect(std::unique_lock<std::mutex>& lock) {
    const std::string client = client_id;
    const std::uint64_t config = config_serial;
    // The first attempt after switching on: the F1 status no longer says
    // "Off" while the handshake waits for an answer (up to reply_timeout).
    if (state == State::off) {
        state = State::searching;
        status = "Connecting to Discord";
    }
    lock.unlock();
    std::string refused, trouble;
    bool any_pipe = false, stopped = false;
    for (int n = 0; n < 10 && refused.empty() && !stopped && !stop; ++n) {
        auto candidate = std::make_unique<Pipe>(stop_event);
        bool exists = false;
        if (!candidate->open(options.pipe_prefix + std::to_string(n), exists)) {
            any_pipe = any_pipe || exists;
            continue;
        }
        any_pipe = true;
        pipe = std::move(candidate);
        const Io hello = write_frame(discord::Opcode::handshake, discord::handshake_json(client), options.io_timeout);
        if (hello != Io::ok) {
            if (hello == Io::timeout) trouble = "Discord did not take the handshake";
            stopped = hello == Io::stopped;
            drop_pipe();
            continue;
        }
        // The READY dispatch, or a CLOSE with the reason (an unknown
        // application id: 4000).
        const auto deadline = Clock::now() + options.reply_timeout;
        bool ready = false;
        while (!ready && refused.empty() && !stopped && Clock::now() < deadline) {
            Io io = Io::ok;
            auto frame = pipe->next(io, options.io_timeout);
            if (io == Io::stopped) {
                stopped = true;
                break;
            }
            if (io != Io::ok) {
                trouble = io == Io::timeout ? "Discord sent half a message" : "Discord closed the connection";
                break;
            }
            if (!frame) {
#ifdef _WIN32
                stopped = WaitForSingleObject(stop_event, 10) == WAIT_OBJECT_0;
#endif
                continue;
            }
            if (frame->first == discord::Opcode::frame && discord::json_string_field(frame->second, "evt") == "READY") ready = true;
            else if (frame->first == discord::Opcode::close) {
                refused = discord::json_string_field(frame->second, "message");
                if (const auto code = discord::json_int_field(frame->second, "code")) refused += " (" + std::to_string(*code) + ")";
                if (refused.empty()) refused = "closed";
            } else if (frame->first == discord::Opcode::ping) {
                write_frame(discord::Opcode::pong, frame->second, options.io_timeout);
            }
        }
        if (ready) {
            lock.lock();
            connected_config = config;
            sent_any = false;
            failure_count = 0;
            ++connection_count;
            state = State::connected;
            status = "Connected to Discord";
            return;
        }
        if (!stopped && refused.empty() && trouble.empty()) trouble = "Discord did not answer";
        drop_pipe();
    }
    lock.lock();
    if (stop || stopped) return;
    if (!refused.empty()) {
        state = State::rejected;
        failed("Discord refused the connection: " + refused);
    } else if (!trouble.empty()) {
        state = State::searching;
        failed(trouble);
    } else {
        // No client runs (or every pipe was busy): look again at the plain
        // interval, so a Discord started later is found soon.
        state = State::searching;
        if (!any_pipe) failure_count = 0;
        next_attempt = Clock::now() + options.retry_interval;
        status = "Looking for the Discord app (not running?)";
    }
}

void DiscordPresence::Shared::run() {
    std::unique_lock lock(mutex);
    while (!stop) {
        if (!enabled || client_id.empty()) {
            if (pipe) {
                lock.unlock();
                disconnect(true, false);
                lock.lock();
            }
            state = State::off;
            status = enabled ? "No application id" : "Off";
            wake.wait(lock, [&] { return stop || (enabled && !client_id.empty()); });
            next_attempt = {};
            continue;
        }
        auto now = Clock::now();
        if (pipe && connected_config != config_serial) {
            // A new application id: a new connection.
            lock.unlock();
            disconnect(true, false);
            lock.lock();
            next_attempt = {};
        }
        if (!pipe && now >= next_attempt) {
            connect(lock);
            if (stop) break;
            now = Clock::now();
        }
        if (pipe) {
            lock.unlock();
            const Io alive = drain();
            if (alive != Io::ok && alive != Io::stopped) drop_pipe();
            lock.lock();
            if (alive == Io::stopped) break;
            if (alive != Io::ok) {
                state = State::searching;
                failed(alive == Io::timeout ? "Discord stopped answering" : "Discord closed the connection");
                continue;
            }
            const bool changed = !sent_any || wanted_serial != sent_serial;
            if (changed && (!sent_any || now - last_send >= options.send_interval)) {
                const auto activity = wanted;
                const auto serial = wanted_serial;
                const auto id = ++nonce;
                lock.unlock();
                const Io io = write_frame(discord::Opcode::frame, discord::set_activity_json(process_id(), activity, id), options.io_timeout);
                if (io != Io::ok && io != Io::stopped) drop_pipe();
                lock.lock();
                if (io == Io::stopped) break;
                if (io == Io::ok) {
                    sent_serial = serial;
                    sent_any = true;
                    last_send = now;
                    ++sent;
                } else {
                    state = State::searching;
                    failed(io == Io::timeout ? "Discord stopped reading" : "Discord closed the connection");
                    continue;
                }
            }
        }
        // Until the next attempt, a change or the stop.
        auto until = Clock::now() + milliseconds(200);
        if (!pipe && next_attempt > Clock::now()) until = std::min(next_attempt, Clock::now() + milliseconds(1000));
        wake.wait_until(lock, until);
    }
    if (pipe) {
        lock.unlock();
        disconnect(true, true);
        lock.lock();
    }
}

} // namespace dw4::game
