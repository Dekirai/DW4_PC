#pragma once

#include "dw4/controllers.hpp"
#include "dw4/collision_math.hpp"
#include "dw4/game_data.hpp"

#include <functional>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace dw4 {
class GameDataList;
class DatabaseDefinition;
class MessengerError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
using MessengerBox = std::shared_ptr<const CollisionBox>;
using MessengerDataList = std::shared_ptr<GameDataList>;
using MessengerDatabase = std::shared_ptr<const DatabaseDefinition>;
// Native generated data and transient geometry retain their actual owners;
// neither object is encoded as a serialized controller reference.
using MessengerObject = GameDataObject;

// One shared argument/result object. Selection clears it; sending does not.
// Finite scalar/vector values retain their words. Matrix PSQ copies flush tiny
// values to signed zero. Non-finite payloads are an explicit host rejection.
// Object IDs identify serialized RAMs or separately allocated native owners.
// Their owners must survive registration, cached rows and delivery.
class MessengerBuffer {
public:
    MessengerBuffer()=default;
    ~MessengerBuffer() { clearing_=true; }
    MessengerBuffer(const MessengerBuffer&)=default;
    MessengerBuffer(MessengerBuffer&&)=delete;
    MessengerBuffer& operator=(const MessengerBuffer&)=delete;
    MessengerBuffer& operator=(MessengerBuffer&&)=delete;
    std::uint8_t enabled_only() const noexcept { return enabled_only_; }
    void set_enabled_only(std::uint8_t value);
    void clear();
    // Original integer-list reset; preserve the other seven typed lists and gate.
    void clear_integers();
    void add_bool(std::uint8_t value);
    void add_int(std::int32_t value);
    void add_float(float value);
    void add_int64(std::int64_t value);
    void add_vector(std::array<float,3> value);
    void add_matrix(std::array<float,16> value);
    void add_object(std::optional<ControllerObjectId> value);
    void add_box(MessengerBox value);
    void add_data_list(MessengerDataList value);
    void add_database(MessengerDatabase value);
    // Preserve an already represented object's exact type and shared identity.
    // This supports original opaque object transfers such as PLAYERNAME.
    void add_game_object(GameDataObject value);
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
    friend class NativeMessenger;
    std::uint8_t enabled_only_{};
    bool clearing_{};
    // A retained mutable reference cannot bypass a deferred invocation's pause.
    // Detached packet owners outlive this non-owning context guard after reset.
    std::weak_ptr<const bool> suspended_;
    std::size_t entries_{}, bytes_{};
    std::vector<std::uint8_t> booleans_;
    std::vector<std::int32_t> integers_;
    std::vector<float> floats_;
    std::vector<std::int64_t> integers64_;
    std::vector<std::array<float,3>> vectors_;
    std::vector<std::array<float,16>> matrices_;
    std::vector<MessengerObject> objects_;
    std::vector<std::optional<std::string>> strings_;
    void check_append(std::size_t bytes) const;
    void check_mutation() const;
};

struct MessengerMessage {
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::optional<std::string> command;
};
struct MessengerRegistration {
    ControllerObjectId recipient;
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
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

// Includes inactive reusable records in physical queue order. Packet owners are
// live, read-only observations, not copies of their current argument lists.
struct MessengerDeferredRecord {
    bool active{};
    std::uint64_t deadline{};
    MessengerMessage message;
    std::shared_ptr<const MessengerBuffer> arguments;
};
// One original manager-update invocation with a retained native service pause.
// Its initial clock/count survive retries. Abandoning an unfinished invocation
// does not replay delivery; the context retains it until reset.
class MessengerDeferredUpdate {
public:
    MessengerDeferredUpdate(MessengerDeferredUpdate&&) noexcept;
    MessengerDeferredUpdate& operator=(MessengerDeferredUpdate&&) noexcept;
    ~MessengerDeferredUpdate();
    std::uint64_t id() const noexcept;
    std::size_t messages_completed() const noexcept;
    bool completed() const noexcept;
private:
    friend class NativeMessenger;
    struct Impl;
    std::shared_ptr<Impl> impl_;
    explicit MessengerDeferredUpdate(std::shared_ptr<Impl> impl);
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
    // Preserve all ordered occurrences. Rejection leaves registrations unchanged.
    void register_recipients(std::span<const MessengerRegistration> registrations);
    void select(std::string category,std::optional<std::string> name,std::int32_t index);
    MessengerBuffer& buffer();
    const MessengerBuffer& buffer() const;
    std::shared_ptr<const MessengerBuffer> buffer_owner() const;
    // Explicit original buffer-object rotation. Push preserves the current
    // object's identity and installs a spare without clearing it. Pop clears
    // the child, restores its parent and leaves selection/cache state alone.
    // A suspended caller owns the matching pop; exceptions do not restore it.
    void push_buffer();
    void pop_buffer();
    std::size_t buffer_depth() const noexcept;
    const MessengerMessage& selected() const;
    std::size_t registration_count() const noexcept;
    bool has_category(std::string_view category) const noexcept;
    std::size_t registration_text_bytes() const noexcept;
    MessengerDispatch begin_send(std::string command);
    MessengerDispatch begin_send_to(std::string category,std::optional<std::string> name,
        std::int32_t index,std::string command);
    MessengerResult resume(MessengerDispatch& dispatch,const MessengerServices& services);
    // Preparation clears CURRENT and writes the inactive tail's keys without
    // selecting a cache record. Commit reloads the live tail/current packet,
    // swaps packet owners and uses (time + delay * 16666666) modulo 2^64.
    void prepare_deferred(std::string category,std::optional<std::string> name,std::int32_t index);
    void schedule_deferred(std::string command,std::uint64_t delay,std::uint64_t context_time);
    void defer_to(std::string category,std::optional<std::string> name,std::int32_t index,
        std::string command,std::uint64_t delay,std::uint64_t context_time);
    void set_deferred_pause(std::uint32_t raw_value,std::uint64_t context_time);
    std::uint64_t deferred_pause_anchor() const noexcept;
    std::vector<MessengerDeferredRecord> deferred_snapshot() const;
    MessengerDeferredUpdate begin_deferred_update(std::uint64_t context_time);
    MessengerResult resume(MessengerDeferredUpdate& update,const MessengerServices& services);
    // Original cleanup/reprepare boundary: discard registrations, cached rows
    // and all five buffers. Invalidates outstanding handles. Forbidden in a callback.
    void reset();
private:
    friend struct MessengerDispatch::Impl;
    friend struct MessengerDeferredUpdate::Impl;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
// Host bounds: 65,536 registration rows; 8 MiB registration text; 64 cache
// records; 256 live handles; 64 nested resume calls; 32,768 shared typed values
// and 8 MiB storage per buffer; 65,535 bytes/string. Embedded NULs reject.
// Original preparation owns five stack buffers and 32 reusable deferred packet
// records. Deferred preparation can grow to the explicit host bound of 65,536.
// Signed deadline order and equal-time FIFO are preserved. Recursive updates
// and insertion before a currently delivered head reject as unsafe host cases.
// No source-file filtering, deduplication or automatic nested-buffer restoration.
} // namespace dw4
