#pragma once

#include "dw4/model_motion.hpp"

#include <optional>
#include <string_view>
#include <variant>
#include <utility>
#include <vector>

namespace dw4 {
struct ModelCtrlCommand {
    std::uint32_t reference_index{};
    std::string name;
};
struct PadMotionCommand {
    std::uint32_t reference_index{};
    std::string name;
    float value{}, frames{};
};
struct ModelMutekiCommand {
    std::uint32_t reference_index{};
    std::string name;
    float frames{};
};
using ModelCommand = std::variant<MotionCommand,ModelCtrlCommand,PadMotionCommand,ModelMutekiCommand>;
// Exact MOVE/MOVEEX/CTRL/PDMOVE/MUTEKI records only. Unknown CTRL names are successful
// no-ops; PDMOVE selects Move parameters only for the exact name "Move".
ModelCommand decode_model_command(const IgbDocument& document, std::uint32_t reference_index);

// Actual work+48 igFloatList ownership. The logical count and backing allocation
// are distinct: original sequence entry clears the former, retaining the latter.
// Storage::size() describes represented addressable floats, not guest allocator
// capacity. No host vector growth rule is claimed to reproduce the guest heap.
class ModelPadMotionList {
public:
    using Storage=std::vector<float>;
    ModelPadMotionList() = default;
    explicit ModelPadMotionList(Storage values);
    // Explicit original/prepared shape. Consumers check the fields they use;
    // merely retaining or detaching a malformed shape does not repair it.
    ModelPadMotionList(std::int32_t count,std::shared_ptr<Storage> backing)
        : count_(count),backing_(std::move(backing)) {}
    ModelPadMotionList(const ModelPadMotionList&) = delete;
    ModelPadMotionList& operator=(const ModelPadMotionList&) = delete;
    std::int32_t size() const noexcept { return count_; }
    void clear() noexcept { count_=0; }
    std::shared_ptr<Storage> backing_owner() { return backing_; }
    std::shared_ptr<const Storage> backing_owner() const { return backing_; }
private:
    std::int32_t count_{};
    std::shared_ptr<Storage> backing_;
    friend class PadMotionParameters;
};

class PadMotionParameters {
public:
    using Values=std::array<float,4>;
    PadMotionParameters();
    explicit PadMotionParameters(std::nullptr_t) : PadMotionParameters(std::shared_ptr<ModelPadMotionList>{}) {}
    explicit PadMotionParameters(std::shared_ptr<ModelPadMotionList> actual_list);
    explicit PadMotionParameters(std::shared_ptr<std::shared_ptr<ModelPadMotionList>> actual_slot);
    // Copies detach actual list/backing values for validation. Assignments
    // publish into the destination's existing list/backing; move construction
    // transfers a binding. Assignment is not reference-field replacement.
    PadMotionParameters(const PadMotionParameters&);
    PadMotionParameters(PadMotionParameters&&) noexcept;
    PadMotionParameters& operator=(const PadMotionParameters&);
    PadMotionParameters& operator=(PadMotionParameters&&);
    PadMotionParameters detached_snapshot() const;
    void publish_snapshot(const PadMotionParameters&);
    std::shared_ptr<ModelPadMotionList> list_owner() { return slot_ ? *slot_ : nullptr; }
    std::shared_ptr<const ModelPadMotionList> list_owner() const { return slot_ ? *slot_ : nullptr; }
    std::int32_t size() const;
    void clear();
    // Explicit prepared contents, not the original PDMOVE command. Existing
    // owners and storage beyond these four floats remain intact.
    void set_values(const Values&);
    // Detached convenience view, derived from a logical four-element list.
    std::optional<Values> values() const;
    // Exact PDMOVE: initialize four zeros only when count==0, then independently
    // guard the selected value and frame stores by the actual logical count.
    void apply(const PadMotionCommand&);
    // 80309168 reads the chosen backing pair without consulting logical count.
    // A cleared list with retained storage therefore remains readable here.
    std::array<float,2> backing_pair(bool moving) const;
    bool operator==(const PadMotionParameters&) const;
private:
    // Null slot means a moved-from host view; a live slot containing null is
    // the distinct original null-reference case.
    std::shared_ptr<std::shared_ptr<ModelPadMotionList>> slot_;
    void require_owner() const;
    void prepare_storage(std::size_t size);
};

struct ModelControlFields {
    ModelControlFields() = default;
    ModelControlFields(std::shared_ptr<ModelPadMotionList> pad,std::shared_ptr<MotionChannelList> move)
        : pad_motion(std::move(pad)),movement(std::move(move)) {}
    std::int32_t mode{},phase{};
    std::optional<std::string> bullet_hit_script,wall_hit_script,floor_hit_script;
    std::uint8_t check_fall=1,wall_check=1,floor_check=1;
    float invulnerability_frames{};
    std::shared_ptr<ModelPadMotionList> pad_motion=std::make_shared<ModelPadMotionList>();
    std::shared_ptr<MotionChannelList> movement=std::make_shared<MotionChannelList>();
};
// Separate RAM-owned KEIKATIMER slot, never a member of canonical model work.
struct ModelRamTimerSlot { std::optional<MotionTimer> value; };

// Owned motion/control fields for one explicitly prepared ONE model. This is
// not the complete beModelCtrlInfoWork or original player construction routine.
struct ModelControlState {
private:
    std::shared_ptr<ModelControlFields> fields_;
    std::shared_ptr<ModelRamTimerSlot> timer_;
public:
    explicit ModelControlState(ModelMotionPose prepared_pose);
    ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelPadMotionList> actual_pad_list);
    ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelPadMotionList> actual_pad_list,
        std::shared_ptr<MotionChannelList> actual_movement_list);
    ModelControlState(ModelMotionPose prepared_pose,std::shared_ptr<ModelControlFields> fields,
        std::shared_ptr<ModelRamTimerSlot> timer_slot);
    ModelControlState(const ModelControlState&);
    ModelControlState(ModelControlState&&) noexcept = default;
    ModelControlState& operator=(const ModelControlState&);
    ModelControlState& operator=(ModelControlState&&);
    // Member copies detach matrix/list values. Publication retains existing
    // owners; binding another actual work list uses the constructor above.
    ModelControlState detached_snapshot() const { return *this; }
    void publish_snapshot(const ModelControlState& candidate) { *this=candidate; }
    ModelMotionPose pose;
    MotionChannels channels;
    PadMotionParameters pad_motion; // Actual work+48 list; Stop pair, then Move pair.
    std::shared_ptr<ModelControlFields> fields_owner();
    std::shared_ptr<const ModelControlFields> fields_owner() const;
    std::shared_ptr<ModelRamTimerSlot> timer_slot_owner();
    std::shared_ptr<const ModelRamTimerSlot> timer_slot_owner() const;
    std::optional<MotionTimer>& elapsed_timer();
    const std::optional<MotionTimer>& elapsed_timer() const;
    std::int32_t& mode();
    const std::int32_t& mode() const;
    std::int32_t& phase();
    const std::int32_t& phase() const;
    std::optional<std::string>& bullet_hit_script();
    const std::optional<std::string>& bullet_hit_script() const;
    std::optional<std::string>& wall_hit_script();
    const std::optional<std::string>& wall_hit_script() const;
    std::optional<std::string>& floor_hit_script();
    const std::optional<std::string>& floor_hit_script() const;
    std::uint8_t& check_fall();
    const std::uint8_t& check_fall() const;
    std::uint8_t& wall_check();
    const std::uint8_t& wall_check() const;
    std::uint8_t& floor_check();
    const std::uint8_t& floor_check() const;
    float& invulnerability_frames();
    const float& invulnerability_frames() const;
};
// MOVE/MOVEEX: ensure the timer, release position lock and synchronize heading,
// then configure the channels. MUTEKI ensures that same timer, then changes
// wall/floor flags or assigns its frame value. Existing timers never restart.
// CTRL and PDMOVE have no implicit timer/motion.
// Validation failures preserve the complete owned state.
void apply_model_command(ModelControlState& state, const ModelCommand& command, std::uint64_t world_time);

struct ModelControlDecision {
    std::string requested_sequence;
    bool dispatch_sequence{};
};
// 80309168 after the original input callback: preserve an existing request;
// otherwise apply modes 1/2/3. key_input is the original byte, including its
// distinct !=0 and ==1 branches. The caller supplies current/requested names
// after frame reset/input. A dispatch result still requires the real sequence
// callback; this function never pretends to execute its animation/actions.
ModelControlDecision update_model_control(ModelControlState& state, std::uint8_t key_input,
    std::string_view current_sequence, std::string_view requested_sequence);
// Motion part only, after control/sequence dispatch. Does not execute the outer
// attachment/position-lock, action wait, collision or player update callbacks.
ModelMotionStep advance_model_motion(ModelControlState& state, std::uint64_t world_time);
} // namespace dw4
