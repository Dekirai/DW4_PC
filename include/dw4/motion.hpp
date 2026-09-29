#pragma once

#include "dw4/igb_document.hpp"
#include "dw4/motion_timer.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dw4 {
struct ModelMotionPose;
struct ModelMotionStep;
class ModelRamTimerAccess;
class MotionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class MotionMode : std::int32_t {
    accelerate_positive = 0, accelerate_negative = 1,
    stop_positive = 2, stop_negative = 3,
    impulse_positive = 4, impulse_negative = 5
};

// Owned beModelCtrlMoveObject fields. Values and time use original nominal
// frame units, not seconds. Stop parameters are consumed only by stop_motion.
struct MotionState {
    bool active{};
    float end_time{};
    MotionMode mode{};
    float value{}, acceleration{}, speed{};
    bool stop_on_sequence{};
    float stop_value{}, stop_frames{};
    bool operator==(const MotionState&) const = default;
};
struct MotionSetup {
    MotionMode mode{};
    float value{}, frames{}, end_time{};
};
struct MotionStop {
    bool on_sequence{};
    float value{}, frames{};
};

// GDJEB2 8030A1F4 / 8030A254 / 8030A2D8. Configure preserves speed in
// modes 0..3 and stored stop parameters. Stop changes 0->2 or 1->3; other
// modes deactivate immediately. Advance subtracts expiry time BEFORE its
// delta<=3 clamp and integrates the updated speed. Exact zero does not expire
// or deactivate. Signed finite parameters/deltas keep their original meaning.
// Finite zero-frame parameters are valid: preserve derived infinity/NaN and
// ordered comparisons (NaN does not trigger a speed clamp or zero crossing).
// Invalid modes or non-finite authored parameters/deltas throw, preserving
// state. NaN payloads, FP exception flags and guest subnormal modes are not
// modeled. Check a displacement before passing it to a rendering boundary.
void configure_motion(MotionState& state, MotionSetup setup);
void stop_motion(MotionState& state);
float advance_motion(MotionState& state, float delta_frames);
// Actual A2D8 reached reads/publications. Inactive records ignore all payloads;
// an active record retains its expiry/stop prefix if later used fields fail.
// Unrecognized mode follows the original unchanged-speed branch. Numerical
// output is checked at the eventual matrix boundary, not on unrelated fields.
float advance_motion_actual(MotionState& state,float delta_frames);

struct MotionCommand {
    std::uint32_t reference_index{};
    std::string type, name;
    // Unknown names on exact MOVE are verified no-ops. MOVEEX validates its
    // four-byte control code before deriving channel/mode from the original.
    std::optional<std::uint32_t> channel;
    MotionSetup setup;
    std::optional<MotionStop> stop;
};
// Reads one exact beModelCtrlMOVE or beModelCtrlMOVEEX object, never infers a
// command from names alone. Bounded local little-endian schema, name<=64 bytes.
// This is parameter decoding, not sequence selection or whole handler startup.
MotionCommand decode_motion_command(const IgbDocument& document, std::uint32_t reference);

struct MotionStep {
    std::array<float, 3> translation{}; // Original local X/Y/Z displacement.
    float rotation{}; // Channel 3 scalar displacement, before degrees->radians.
    bool translation_updated{}; // At least one translation channel began active.
};

// Actual beModelCtrlMoveObjectList ownership. Fresh work owns an allocated empty
// list; its first MOVE creates four distinct beModelCtrlMoveObject allocations.
// Mutable bindings can supply original owners. Prepared whole-list consumers
// validate shape; actual reached-field consumers allow repeated owners and
// check only the represented slots they consume. Count and backing currently
// share one representation, without spare physical slots beyond size.
class MotionChannelList {
public:
    using Entries = std::vector<std::shared_ptr<MotionState>>;
    MotionChannelList() = default;
    explicit MotionChannelList(Entries entries) : entries_(std::move(entries)) {}
    MotionChannelList(const MotionChannelList&) = delete;
    MotionChannelList& operator=(const MotionChannelList&) = delete;
    std::size_t size() const noexcept { return entries_.size(); }
    Entries& mutable_entries() noexcept { return entries_; }
    std::shared_ptr<MotionState> entry_owner(std::size_t index) { return entries_.at(index); }
    std::shared_ptr<const MotionState> entry_owner(std::size_t index) const { return entries_.at(index); }
private:
    Entries entries_;
};

class MotionChannels {
public:
    using States = std::array<MotionState,4>;
    MotionChannels();
    explicit MotionChannels(std::nullptr_t) : MotionChannels(std::shared_ptr<MotionChannelList>{}) {}
    explicit MotionChannels(std::shared_ptr<MotionChannelList> actual_list);
    // Bind the actual reference field, including a known null value. Each
    // operation resolves its live value; list_owner() captures one current list.
    explicit MotionChannels(std::shared_ptr<std::shared_ptr<MotionChannelList>> actual_slot);
    // Validation candidates detach each distinct entry while preserving raw list
    // shape/aliases; copying does not consume movement or repair malformed data.
    // Publication preserves the destination list and compatible entry owners;
    // assigning an empty candidate clears entries only. Move construction
    // transfers a binding. Raw copies are bounded to 4096 entries.
    MotionChannels(const MotionChannels&);
    MotionChannels(MotionChannels&&) noexcept = default;
    MotionChannels& operator=(const MotionChannels&);
    MotionChannels& operator=(MotionChannels&&);
    // Applies only the recovered channel effects of 8030A6BC. The caller must
    // separately establish its timer, model work and attachment lifecycle.
    void apply(const MotionCommand& command);
    MotionStep advance(float delta_frames); // Original order: rotation, X, Y, Z.
    MotionStep advance(MotionTimer& timer, std::uint64_t world_time);
    // 8030A448: rotation channel, model turning, then local translation. Uses
    // an explicit owned model pose; no attachment, collision or trace callbacks.
    ModelMotionStep advance_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time);
    // Actual RAM named-timer calls preserve completed prefixes. The translated
    // byte clears before the list count check; nonempty lists query frames
    // before consuming channel fields. Turning performs its own per-axis calls.
    ModelMotionStep advance_model(ModelMotionPose& pose, ModelRamTimerAccess& timer);
    // 8030EA1C..8030EA74: stop flagged XYZ channels using their stored stop
    // parameters; always deactivate rotation and clear only its speed.
    void on_sequence_change();
    // Actual entry captures the count, then consumes each current list entry.
    // Completed stops survive a later invalid/null entry; no four-axis precheck.
    void on_sequence_change_actual();
    // Detached values for inspection/validation, never authoritative storage.
    // An empty list returns zero values without allocating entry objects.
    States states() const;
    bool allocated() const; // True means four entries, not merely a list owner.
    std::shared_ptr<MotionChannelList> list_owner();
    std::shared_ptr<const MotionChannelList> list_owner() const;
private:
    std::shared_ptr<std::shared_ptr<MotionChannelList>> slot_;
    void require_slot() const;
    void require_owner() const;
    void validate_shape() const;
    void publish(const States&);
    void publish_from(const MotionChannels&);
};

// 80128DC8: column-major 4x4, including the fourth translation component.
// Retains model-basis scale/rotation and original Y, fused X, fused Z, add-T
// arithmetic order. This performs no collision response or matrix normalization.
// This native matrix boundary rejects non-finite inputs/results atomically;
// the scalar state above preserves the original arithmetic that produced them.
void translate_model_local(std::array<float, 16>& matrix, const std::array<float, 3>& translation);

// 8030C85C / 801292D4: model basis for applied Euler turn angles (radians),
// keeping the original exact-zero branches, single rounding and FMA order.
// Translation XYZ is retained; the other fourth-column components reset.
void original_model_turn_basis(std::array<float, 16>& matrix, const std::array<float, 3>& angles);
} // namespace dw4
