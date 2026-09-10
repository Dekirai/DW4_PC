#pragma once

#include "dw4/animation_combiner.hpp"
#include "dw4/action_actors.hpp"
#include "dw4/motion_timer.hpp"

#include <functional>
#include <string_view>

namespace dw4 {
struct ModelActionRequest;
class ActionError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class ActionOpcode { motion, joint, fade, wait, flash, unsupported };
struct ActionCommand {
    std::uint32_t reference{};
    ActionOpcode opcode=ActionOpcode::unsupported;
    std::string type, name, interpolation, loop;
    float first{}, second{};
};
struct ActionDefinition {
    std::uint32_t reference{};
    std::string name;
    std::vector<ActionCommand> commands;
    bool command_list_present=true; // Native-command placeholders can store null.
};
struct ActionRamDefinition {
    std::uint32_t reference{}, node_reference{}, combiner_reference{};
    AnimationStateClock node_clock;
};
class ActionProgram {
public:
    const std::string& source() const noexcept { return source_; }
    const std::string& name() const noexcept { return name_; }
    std::uint32_t reference() const noexcept { return reference_; }
    const std::vector<ActionDefinition>& actions() const noexcept { return actions_; }
    const std::vector<ActionRamDefinition>& rams() const noexcept { return rams_; }
    const std::vector<std::pair<std::uint32_t,std::string>>& animations() const noexcept { return animations_; }
    bool same_definition(const ActionProgram& other) const noexcept { return definition_identity_==other.definition_identity_; }
private:
    friend ActionProgram decode_action_program(const IgbDocument&,std::uint32_t,std::string);
    std::string source_, name_;
    std::uint32_t reference_{};
    std::vector<ActionDefinition> actions_;
    std::vector<ActionRamDefinition> rams_;
    std::vector<std::pair<std::uint32_t,std::string>> animations_;
    std::shared_ptr<const std::uint8_t> definition_identity_=std::make_shared<const std::uint8_t>(std::uint8_t{0});
};
// Own the selected ACTION2 info's ordered commands and prepared node references.
// The animation profile requires its explicit database and one combiner per RAM.
ActionProgram decode_action_program(const IgbDocument& document,std::uint32_t info_reference,std::string source);

struct ActionBroadcast {
    std::string source, info_name, action;
    std::int32_t ram_no{};
};
struct ActionServices {
    // Shared original random lower bound (WAITCHECK name[3]=='S'), initially BSS zero.
    float random_lower{};
    // Required only by a random wait or a completed/yielded action dispatch.
    // Callbacks must implement the owning runtime's random/event service.
    std::function<std::uint32_t()> random_word;
    std::function<void(const ActionBroadcast&)> broadcast;
    // Optional resumable delivery. When present, takes precedence over broadcast.
    // False retains the original pending event; the service owns any completed
    // recipient prefix and must resume it without duplicating side effects.
    std::function<bool(const ActionBroadcast&)> try_broadcast;
};
struct ActionColor {
    std::array<float,4> channels{1,1,1,1};
    std::uint32_t packed=0xffffffffu; // Original ABGR8, rounded and clamped separately.
};
// Owned ACTION2 attribute overrides, distinct from inherited scene attributes.
struct ActionAppearance {
    std::optional<ActionColor> color;
    std::optional<bool> blend, texture, lighting;
};
struct ActionWork {
    AnimationMotionWork motion;
    std::int32_t joint_state{}, fade_state{}, wait_state{}, flash_state{};
    std::int64_t joint_start{}, joint_end{};
    bool joint_ended{}, frozen{};
    float fade_limit{}, fade_add{}, fade_value{};
};
enum class ActionStop { end, waiting, unsupported_command, random_required, broadcast_required, command_failed };
struct ActionResult {
    ActionStop stop=ActionStop::end;
    std::uint32_t commands_executed{};
    std::optional<std::uint32_t> command_reference;
};

// ACTION2 execution on explicitly prepared animation/node state. Owns command
// cursors, timers and attribute overrides. An explicitly attached actor group
// adds original activation/NDSP ownership; traversal and rendering remain separate.
class ActionRuntime {
public:
    ActionRuntime(ActionProgram program, const AnimationSourceRuntime& sources,
        std::int32_t ram_no, AnimationCombiner combiner);
    ActionRuntime(ActionProgram program, const AnimationSourceRuntime& sources,
        std::int32_t ram_no, AnimationCombiner combiner, ActionActorGroup actors);
    const ActionProgram& program() const noexcept { return *program_; }
    const ActionWork& work() const noexcept { return work_; }
    const ActionAppearance& appearance() const noexcept { return appearance_; }
    const AnimationCombiner& combiner() const noexcept { return combiner_; }
    const std::map<std::string,MotionTimer>& timers() const noexcept { return timers_; }
    const AnimationStateClock& node_clock() const noexcept { return node_; }
    const ActionActorGroup* actors() const noexcept { return actors_ ? &*actors_ : nullptr; }
    bool active() const noexcept { return active_; }
    bool visible() const noexcept { return visible_; }
    bool command_end() const noexcept;
    // 802F1828..802F1868: checked messenger query without an activity gate.
    // Nullopt means selected command work/list is not represented, not false.
    // This does not advance work, clocks, animation or pending broadcasts.
    std::optional<bool> query_command_end() const noexcept;
    // A dispatch can stop while delivering an older event before accepting a
    // new action. Owners must distinguish that phase from an accepted dispatch.
    bool broadcast_pending() const noexcept { return pending_broadcast_.has_value(); }
    // Opaque host continuation stamp, not an original game field. Copies retain
    // the same exact state token; mutating entry points replace it before effects.
    // Retaining the shared token makes comparisons immune to address reuse.
    std::shared_ptr<const void> state_identity() const noexcept { return state_identity_; }
    std::string_view current_action() const noexcept;
    std::uint32_t command_index() const noexcept { return cursor_; }
    std::uint64_t node_time(std::uint64_t system_time) const;
    ActionResult dispatch(AnimationSourceRuntime& sources,ActionServices& services,
        std::string_view name,std::uint64_t system_time);
    // Route a recovered model sequence ACTION to this exact source/info/RAM.
    ActionResult dispatch(AnimationSourceRuntime& sources,ActionServices& services,
        const ModelActionRequest& request,std::uint64_t system_time);
    ActionResult update(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t system_time);
    // Retry a native service/error boundary explicitly. This is not an original
    // action restart: completed commands and their cursor remain committed.
    ActionResult resume(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t system_time);
    SkinPose pose(AnimationSourceRuntime& sources,const SkinningData& skin,std::uint64_t system_time);
    // Explicit traversal of one prepared actor through this node's clock. The
    // Caller supplies parent traversal time and current view attributes. The
    // model-view matrix already includes outer/node transforms, not actor transform.
    void traverse_actor(AnimationSourceRuntime& sources,std::size_t actor_index,
        const AnimationMatrix& model_view,const AnimationMatrix& inverse_view,std::uint64_t parent_time);
    // Original active-gated cached query: no sampling or traversal is implied.
    std::optional<AnimationMatrix> bone_matrix(std::string_view name) const;
    void freeze(bool frozen,std::uint64_t system_time);
private:
    std::shared_ptr<const void> state_identity_=std::make_shared<const std::uint8_t>(std::uint8_t{0});
    std::shared_ptr<const ActionProgram> program_;
    std::int32_t ram_no_{};
    AnimationCombiner combiner_;
    AnimationStateClock node_;
    ActionWork work_;
    ActionAppearance appearance_;
    std::optional<ActionActorGroup> actors_;
    std::map<std::string,MotionTimer> timers_;
    std::optional<std::size_t> current_;
    std::optional<ActionBroadcast> pending_broadcast_;
    std::optional<ActionStop> suspended_;
    std::uint32_t cursor_{};
    bool active_{}, visible_{}, running_{}, dispatch_pending_{};
    void begin_state_change();
    ActionResult run(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t time);
    ActionResult deliver(ActionServices& services,ActionResult result);
    ActionResult finish_dispatch(ActionServices& services,ActionResult result);
    ActionResult status() const;
    bool command(AnimationSourceRuntime& sources,ActionServices& services,const ActionCommand&,std::uint64_t time);
    void joint_update(std::uint64_t time);
    void fade_update(std::uint64_t time);
    void flash_update(std::uint64_t time);
    bool wait_ready(AnimationSourceRuntime& sources,std::uint64_t time);
    void deactivate();
};
// Prepared commands only: NDSP remains an explicit missing-actor boundary.
ActionRuntime decode_action_runtime(const IgbDocument& document,ActionProgram program,
    const AnimationSourceRuntime& sources,std::int32_t ram_no);
// Own every actor in this RAM's prepared graph and execute its activation/NDSP.
ActionRuntime decode_action_actor_runtime(const IgbDocument& document,ActionProgram program,
    const AnimationSourceRuntime& sources,std::int32_t ram_no);
} // namespace dw4
