#pragma once

#include "dw4/controllers.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace dw4 {
class MessengerError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

// One shared argument/result object. Selection clears it; sending does not.
// Finite scalar/vector values retain their words. Matrix PSQ copies flush tiny
// values to signed zero. Non-finite payloads are an explicit host rejection.
// Object IDs identify externally owned RAMs by loaded source occurrence and local
// reference. Their owners must survive registration, cached rows and delivery.
class MessengerBuffer {
public:
    std::uint8_t enabled_only() const noexcept { return enabled_only_; }
    void set_enabled_only(std::uint8_t value) noexcept { enabled_only_=value; }
    void clear() noexcept;
    void add_bool(std::uint8_t value);
    void add_int(std::int32_t value);
    void add_float(float value);
    void add_int64(std::int64_t value);
    void add_vector(std::array<float,3> value);
    void add_matrix(std::array<float,16> value);
    void add_object(std::optional<ControllerObjectId> value);
    void add_string(std::optional<std::string> value);
    const auto& booleans() const noexcept { return booleans_; }
    const auto& integers() const noexcept { return integers_; }
    const auto& floats() const noexcept { return floats_; }
    const auto& integers64() const noexcept { return integers64_; }
    const auto& vectors() const noexcept { return vectors_; }
    const auto& matrices() const noexcept { return matrices_; }
    const auto& objects() const noexcept { return objects_; }
    const auto& strings() const noexcept { return strings_; }
private:
    std::uint8_t enabled_only_{};
    std::size_t entries_{}, bytes_{};
    std::vector<std::uint8_t> booleans_;
    std::vector<std::int32_t> integers_;
    std::vector<float> floats_;
    std::vector<std::int64_t> integers64_;
    std::vector<std::array<float,3>> vectors_;
    std::vector<std::array<float,16>> matrices_;
    std::vector<std::optional<ControllerObjectId>> objects_;
    std::vector<std::optional<std::string>> strings_;
    void check_append(std::size_t bytes) const;
};

struct MessengerMessage {
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::optional<std::string> command;
};
struct MessengerDelivery {
    std::uint64_t dispatch_id{};
    std::size_t row_index{};
    ControllerObjectId recipient;
    // A live cache record, not a per-call snapshot. Nested selection/send can
    // change its index, command, or all fields if the cache entry is evicted.
    const MessengerMessage& message;
};
struct MessengerCallResult {
    bool completed{}; // False stops before claiming this callback completed.
    std::uint32_t value{}; // Structured hook: only its low byte selects fallback.
    std::string diagnostic;
};
class NativeMessenger;
struct MessengerManager {
    std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)> structured;
    std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&,std::string_view)> fallback;
};
struct MessengerServices {
    // Read current RAM state. Nonzero means active; called only when needed.
    // Nullopt explicitly suspends until the live RAM service is available.
    std::function<std::optional<std::uint8_t>(ControllerObjectId)> active;
    // Capture the RAM's current parent manager before the structured hook.
    // The returned pair is retained through fallback and explicit suspension.
    // An empty structured hook means resolution is unavailable; retry the
    // resolver on resume. A resolved hook can return value zero for fallback.
    std::function<MessengerManager(ControllerObjectId)> manager;
};
enum class MessengerStop { completed, service_required, faulted };
struct MessengerResult {
    MessengerStop stop=MessengerStop::completed;
    std::size_t recipients_completed{};
    std::optional<ControllerObjectId> recipient;
    std::string diagnostic;
};

// A native service continuation, not an original queued message. Already
// completed recipients and hook/fallback phases are never replayed by resume.
// A callback that suspends after partial internal effects owns that continuation.
// Throwing faults this handle because completion of those effects is uncertain.
class MessengerDispatch {
public:
    MessengerDispatch(MessengerDispatch&&) noexcept;
    MessengerDispatch& operator=(MessengerDispatch&&) noexcept;
    ~MessengerDispatch();
    std::uint64_t id() const noexcept;
    bool completed() const noexcept;
private:
    friend class NativeMessenger;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit MessengerDispatch(std::unique_ptr<Impl> impl);
};

class NativeMessenger {
public:
    // Fresh native context with lazy original selection-buffer preparation.
    // This does not claim the original system/manager startup has executed.
    NativeMessenger();
    NativeMessenger(NativeMessenger&&) noexcept;
    NativeMessenger& operator=(NativeMessenger&&) noexcept;
    ~NativeMessenger();
    void register_recipient(ControllerObjectId recipient,std::string category,
        std::optional<std::string> name,std::int32_t index);
    void select(std::string category,std::optional<std::string> name,std::int32_t index);
    MessengerBuffer& buffer();
    const MessengerBuffer& buffer() const;
    const MessengerMessage& selected() const;
    std::size_t registration_count() const noexcept;
    std::size_t registration_text_bytes() const noexcept;
    MessengerDispatch begin_send(std::string command);
    MessengerDispatch begin_send_to(std::string category,std::optional<std::string> name,
        std::int32_t index,std::string command);
    MessengerResult resume(MessengerDispatch& dispatch,const MessengerServices& services);
    // Original cleanup/reprepare boundary: discard registrations, cached rows
    // and the buffer. Invalidates outstanding handles. Forbidden in a callback.
    void reset();
private:
    friend struct MessengerDispatch::Impl;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
// Host bounds: 65,536 registration rows; 8 MiB registration text; 64 cache
// records; 256 live handles; 64 nested resume calls; 32,768 shared typed values
// and 8 MiB buffer storage; 65,535 bytes/string. Embedded NULs reject.
// No source-file filtering, deduplication or automatic nested-buffer restoration.
// Deferred messages and explicit original buffer push/pop are separate services.
} // namespace dw4
