#pragma once

#include "dw4/motion.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace dw4 {
// Original work+4C/+44 and player-work+128/+12C. WCHECK assignment and
// ordinary sequence entry preserve the three scalar fields.
struct ModelWaitState {
    std::optional<std::string> name;
    std::uint8_t control_enabled{};
    std::int32_t attack_state{}, attack_mode{};
    bool operator==(const ModelWaitState&) const = default;
};
struct ModelWaitServices {
    // Original selected player's held logical buttons, including A=0x4000.
    // The owner supplies qualification; no preview input merging is implied.
    std::function<std::optional<std::uint32_t>()> held_buttons;
    std::function<std::optional<bool>()> action_complete; // ACTION ISCOMEND boolean[0].
    std::function<bool()> resume_commands; // Retained live sequence cursor, 8030C3AC.
    std::function<bool(std::string_view)> dispatch_sequence; // Original controller+8C.
};
enum class ModelWaitStop {
    complete, held_buttons_required, action_completion_required,
    commands_required, sequence_required
};
struct ModelWaitResult {
    ModelWaitStop stop = ModelWaitStop::complete;
    bool complete() const noexcept { return stop==ModelWaitStop::complete; }
};

// Prepared 8030BDB8 / player fallback 8034D214 only. No clocks, player
// construction, sequence-entry callback or frame scheduler are implied.
// Missing/nullopt/false services mean no NEW effect and can be resumed. A
// service that performs partial work must retain its own continuation. Throws
// fault this owner so a possibly completed effect can never replay automatically.
class ModelWaitRuntime {
public:
    ModelWaitRuntime() = default;
    ModelWaitRuntime(const ModelWaitRuntime&) = delete;
    ModelWaitRuntime& operator=(const ModelWaitRuntime&) = delete;
    ModelWaitRuntime(ModelWaitRuntime&&);
    ModelWaitRuntime& operator=(ModelWaitRuntime&&);
    bool pending() const noexcept { return phase_!=Phase::none; }
    bool faulted() const noexcept { return faulted_; }
    // Null name skips the original caller's wait branch. Common motion waits
    // query these prepared channels synchronously; services are only required
    // for the branches actually reached. A complete result can retain a wait.
    ModelWaitResult update(ModelWaitState&,const MotionChannels&,
        const ModelWaitServices& = {});
    // The state must remain alive at the same address throughout a pending
    // pass. Callbacks may mutate it; subsequent original operations use live
    // fields. Completed sends/queries and cleared names are never restored.
    ModelWaitResult resume(ModelWaitState&,const ModelWaitServices& = {});
private:
    enum class Phase { none, player_pad, player_branch, action_query,
        commands, sequence_then_query, sequence };
    enum class Query { common, attack_one, attack_two };
    ModelWaitState* state_{};
    Phase phase_ = Phase::none;
    Query query_ = Query::common;
    std::uint32_t held_{};
    bool processing_{}, faulted_{};
    void require_usable() const;
    void transfer(ModelWaitRuntime&&);
    ModelWaitResult process(const ModelWaitServices&);
    ModelWaitResult finish() noexcept;
};
} // namespace dw4
