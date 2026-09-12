#pragma once

#include "dw4/action_event_tables.hpp"
#include "dw4/actor_scene.hpp"
#include "dw4/messenger.hpp"
#include "dw4/model_collision.hpp"
#include "dw4/model_script_context.hpp"
#include "dw4/model_player_work.hpp"
#include "dw4/player_runtime_binding.hpp"
#include "dw4/pad.hpp"

namespace dw4 {
class ModelStatusWork;
struct NativeControllerServices {
    std::function<std::optional<std::uint8_t>()> active;
    std::function<MessengerManager()> manager;
};
struct PreparedControllerRegistration {
    ControllerObjectId id;
    NativeControllerServices services;
    // Required for a serialized RAM; a native allocation has no source file.
    std::optional<std::string> source;
};
// Explicit actual fields of a prepared KIND_PLAYER MODELCTRL RAM. Parent info
// is fixed to the binding's verified info owner; its nullable name, RAM ordinal,
// raw active byte and work-presence field remain live. This does not execute
// the omitted PC status-allocation or virtual+C4 prelude.
struct ModelQueryOwner {
    ControllerObjectId parent_info;
    std::string source;
    std::shared_ptr<std::optional<std::string>> parent_name;
    std::shared_ptr<std::int32_t> ram_no;
    std::shared_ptr<std::uint8_t> active;
    std::shared_ptr<bool> work_available;
};
struct PlayerWaitServices {
    // Four already processed original pad slots. Null preserves the missing
    // input service boundary; disconnected slots carry the original zero input.
    const OriginalPads* pads{};
    std::function<bool()> resume_commands;
    // Full outer sequence dispatch, including required player entry callbacks.
    // Selecting/running a prepared sequence alone does not implement this hook.
    std::function<bool(std::string_view)> dispatch_sequence;
};
struct ModelHitboxServices {
    // Request mode supplies the original debug color selection. False retains
    // this display operation; the service owns any committed internal prefix.
    std::function<bool(ControllerObjectId,const ModelHitboxCheckRequest&,const CollisionBox&)> debug_box;
    ModelLuaDispatch lua;
};
enum class ModelHitboxStop { complete, debug_required, collision_registration_required, collision_delivery_required, lua_required };
struct ModelHitboxResult {
    ModelHitboxStop stop = ModelHitboxStop::complete;
    std::uint64_t entry_id{};
    std::string diagnostic;
};
// Explicitly prepared model work and loaded event-consumer RAMs. This owns
// ordinary sound/effect/HITBOX queues and synchronous messenger continuations,
// not original player creation, a frame scheduler or complete external managers.
// Object IDs distinguish repeated source loads; message matching does not.
class ActionEventRuntime {
public:
    ActionEventRuntime();
    ~ActionEventRuntime();
    ActionEventRuntime(const ActionEventRuntime&) = delete;
    ActionEventRuntime& operator=(const ActionEventRuntime&) = delete;
    ActionEventRuntime(ActionEventRuntime&&) = delete;
    ActionEventRuntime& operator=(ActionEventRuntime&&) = delete;

    ControllerObjectId register_model(PlayerRuntimeBinding binding,ModelControlState model,
        bool active,std::optional<ModelSubcommandOwner> attachment = {});
    // Consume one actual factory/prepared work allocation. Scene-node matrix
    // and named RAM timer remain independent owners. This installs the checked
    // binding, not the original mutable RAM graph or complete GENERATE tail.
    ControllerObjectId register_model(PlayerRuntimeBinding binding,std::shared_ptr<ModelPlayerWork> work,
        std::shared_ptr<ModelMotionPose::Matrix> node,std::shared_ptr<ModelRamTimerSlot> timer,bool active);
    std::shared_ptr<ModelPlayerWork> model_work(ControllerObjectId id);
    std::shared_ptr<const ModelPlayerWork> model_work(ControllerObjectId id) const;
    // Attach these exact live fields once to an existing checked model binding.
    // The initial RAM number must match the binding's selected occurrence.
    void prepare_model_queries(ControllerObjectId model,ModelQueryOwner owner);
    // Attach the actual PC virtual+C4 status fields to the same prepared ONE.
    // The caller retains this owner; the model holds a weak reference to keep
    // prepared work lifetime explicit. First preparation may
    // supply a missing status service, but cannot replace published work.
    void prepare_model_status(ControllerObjectId model,std::shared_ptr<ModelStatusWork> work);
    // Explicit original PLAYER registration on an already prepared KIND_PLAYER
    // owner. Each call appends an occurrence using the then-current name/RAM;
    // cached recipient rows retain their existing original snapshot semantics.
    void register_player(ControllerObjectId model);
    // Retain this exact live ACTION owner under its actual qualified RAM.
    // One runtime cannot belong to multiple RAMs or repeated source loads.
    ControllerObjectId register_action(ControllerObjectId model,std::shared_ptr<ActionRuntime> action);
    // Initial checked scene attachment can satisfy a suspended ACTION query.
    // The actual loaded owner cannot be replaced after publication.
    void prepare_action_scene(ControllerObjectId action,ActorSceneAction scene);
    // Register every RAM occurrence, including aliases, in the supplied order.
    // active is caller-prepared RAM state, not inferred from a loaded export.
    void register_consumer(ActionEventTableProgram program,bool active);
    // Attach the real providers of a generated native RAM. This does not select
    // or change the shared buffer; category rows are registered separately.
    // Existing providers cannot be replaced through another registration.
    void register_native_controller(ControllerObjectId id,NativeControllerServices services);
    // Register a newly generated provider and its category row atomically.
    void register_native_controller(ControllerObjectId id,NativeControllerServices services,
        std::string category,std::optional<std::string> name,std::int32_t index);
    // Atomic installation of new prepared providers and ordered category rows.
    // Existing provider owners are never replaced; additional rows may refer
    // to existing owners. This does not select or change the shared buffer.
    void register_controllers(std::span<const PreparedControllerRegistration> controllers,
        std::span<const MessengerRegistration> rows);
    void set_active(ControllerObjectId id,bool active);
    const PlayerRuntimeBinding& binding(ControllerObjectId id) const;
    const ModelSequenceRuntime& sequence(ControllerObjectId id) const;
    const ModelSubcommandQueue& subcommands(ControllerObjectId id) const;
    void set_label(ControllerObjectId id,std::optional<std::string> sequence,std::optional<std::string> label);
    // Publish prepared control values into existing work; preserve its owner,
    // sequence/wait cursors and actual node/list references. This is not a
    // fresh ONE work replacement or original player creation.
    void prepare_model_state(ControllerObjectId id,ModelControlState state);
    // Prepared combination of the original controller/debug-node gates.
    void set_hitbox_debug(ControllerObjectId id,bool enabled);
    ModelHitboxResult hitbox_result(ControllerObjectId id) const;
    // Attach the exact authored HIT graph to this prepared ONE work once. Adds
    // its enabled collision categories in original order, using the bound name
    // and RAM ordinal. MODELCTRL registration already belongs to register_model.
    // This does not implement the original registration tail virtual or create
    // HP/status/script owners; HP reference presence is explicit prepared work.
    // The caller prepares the base command path after virtual+C4 declines;
    // original PC CHKBULLET is verified to take that path.
    void prepare_collision_target(ControllerObjectId id,ModelHitBodyDefinition definition,bool hp_reference_present);
    const ModelCollisionRuntime& collision_target(ControllerObjectId id) const;
    // Create this validated target's default ONE script work once, backed by
    // its live matrix. The script runtime must retain the returned owner. HP
    // wrappers start at original zero defaults, not invented player HP.
    std::shared_ptr<ModelScriptWork> prepare_target_script_work(ControllerObjectId id);
    // Missing services can be supplied between retries of the same delivery.
    void set_collision_services(ModelCollisionServices services);

    // Only the owned sequence-selection and subcommand-cleanup effects. Other
    // original entry callbacks (collision, action waits, player work) remain
    // required. The new name/cursor/control reset is visible during cleanup;
    // fall/channel effects follow cleanup. No new command runs before it succeeds.
    ModelSubcommandResult select_sequence(ControllerObjectId id,std::string_view name,
        const ModelSubcommandServices& services = {});
    ModelSubcommandResult resume_sequence(ControllerObjectId id,
        const ModelSubcommandServices& services = {});
    ModelSequenceResult run_sequence(ControllerObjectId id,std::uint64_t world_time,
        const ModelActionDispatch& action = {},const ModelSubcommandServices& services = {},
        const ModelActionTryDispatch& try_action = {},const ModelLuaDispatch& lua = {});
    // Dispatch/resume the registered ACTION without replaying its committed
    // command prefix. Shared random state stays in action; broadcasts use this
    // owner's messenger. A suspended invocation retains its original time and
    // requires the same source/service owners and unchanged live ACTION state.
    // Complete any older unrelated ACTION broadcast before starting this call.
    ModelSequenceResult run_action_sequence(ControllerObjectId id,AnimationSourceRuntime& sources,
        std::uint64_t world_time,ActionServices& action,const ModelSubcommandServices& services = {},
        const ModelLuaDispatch& lua = {});
    std::optional<ActionResult> last_action_result(ControllerObjectId id) const;
    // Explicit prepared work, not original player initialization. Entry clears
    // only name; control and attack latch fields survive ordinary selections.
    void prepare_wait_state(ControllerObjectId id,ModelWaitState state);
    ModelWaitResult update_wait(ControllerObjectId id,const PlayerWaitServices& services = {});
    ModelWaitResult resume_wait(ControllerObjectId id,const PlayerWaitServices& services = {});
    bool wait_pending(ControllerObjectId id) const;
    // Sends original ACTION/name/RAM ISCOMEND and consumes boolean[0]. Missing
    // live command work suspends; an empty result is never treated as false.
    std::optional<bool> query_action_complete(ControllerObjectId model);
    ModelSubcommandResult update_subcommands(ControllerObjectId id,float system_frame_delta,
        const ModelSubcommandServices& services = {},const ModelHitboxServices& hitbox = {});
    ModelSubcommandResult resume_subcommands(ControllerObjectId id,
        const ModelSubcommandServices& services = {},const ModelHitboxServices& hitbox = {});

    // Registration/explicit sending access. Do not retain this mutable reference
    // across a suspended top-level send; the original shared buffer must survive
    // until that same send resumes. Access while suspended rejects.
    NativeMessenger& messenger();
    // Registration leaves the shared buffer and cached recipient sets intact;
    // it can supply a missing recipient while a prepared service is suspended.
    void register_recipient(ControllerObjectId id,std::string category,
        std::optional<std::string> name,std::int32_t index);
    // Unowned registered RAMs require explicit real managers and activity.
    void set_external_services(MessengerServices services);
    MessengerResult resume(MessengerDispatch& dispatch);
    // Explicit original context time and one manager invocation. This does not
    // choose the original outer manager order or advance any gameplay clock.
    MessengerDeferredUpdate begin_deferred_update(std::uint64_t context_time);
    MessengerResult resume(MessengerDeferredUpdate& update);
    // One retained send per bound ACTION RAM. Retrying a different event while
    // delivery is suspended rejects before starting another send.
    MessengerResult broadcast(ControllerObjectId model,const ActionBroadcast& event);
    ActionServices action_services(ControllerObjectId model);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
