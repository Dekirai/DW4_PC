#pragma once

#include "dw4/action_event_tables.hpp"
#include "dw4/messenger.hpp"
#include "dw4/player_runtime_binding.hpp"
#include "dw4/pad.hpp"

namespace dw4 {
struct PlayerWaitServices {
    // Four already processed original pad slots. Null preserves the missing
    // input service boundary; disconnected slots carry the original zero input.
    const OriginalPads* pads{};
    std::function<bool()> resume_commands;
    // Full outer sequence dispatch, including required player entry callbacks.
    // Selecting/running a prepared sequence alone does not implement this hook.
    std::function<bool(std::string_view)> dispatch_sequence;
};
// Explicitly prepared model work and loaded event-consumer RAMs. This owns
// ordinary sound/effect queues and synchronous messenger continuations, not
// original player creation, a frame scheduler, audio playback or effect actors.
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
    // Retain this exact live ACTION owner under its actual qualified RAM.
    // One runtime cannot belong to multiple RAMs or repeated source loads.
    ControllerObjectId register_action(ControllerObjectId model,std::shared_ptr<ActionRuntime> action);
    // Register every RAM occurrence, including aliases, in the supplied order.
    // active is caller-prepared RAM state, not inferred from a loaded export.
    void register_consumer(ActionEventTableProgram program,bool active);
    void set_active(ControllerObjectId id,bool active);
    const PlayerRuntimeBinding& binding(ControllerObjectId id) const;
    const ModelSequenceRuntime& sequence(ControllerObjectId id) const;
    const ModelSubcommandQueue& subcommands(ControllerObjectId id) const;
    void set_label(ControllerObjectId id,std::optional<std::string> sequence,std::string label);

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
        const ModelActionTryDispatch& try_action = {});
    // Dispatch/resume the registered ACTION without replaying its committed
    // command prefix. Shared random state stays in action; broadcasts use this
    // owner's messenger. A suspended invocation retains its original time and
    // requires the same source/service owners and unchanged live ACTION state.
    // Complete any older unrelated ACTION broadcast before starting this call.
    ModelSequenceResult run_action_sequence(ControllerObjectId id,AnimationSourceRuntime& sources,
        std::uint64_t world_time,ActionServices& action,const ModelSubcommandServices& services = {});
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
        const ModelSubcommandServices& services = {});
    ModelSubcommandResult resume_subcommands(ControllerObjectId id,
        const ModelSubcommandServices& services = {});

    // Registration/explicit sending access. Do not retain this mutable reference
    // across a suspended top-level send; the original shared buffer must survive
    // until that same send resumes. Access while suspended rejects.
    NativeMessenger& messenger();
    // Unowned registered RAMs require explicit real managers and activity.
    void set_external_services(MessengerServices services);
    MessengerResult resume(MessengerDispatch& dispatch);
    // One retained send per bound ACTION RAM. Retrying a different event while
    // delivery is suspended rejects before starting another send.
    MessengerResult broadcast(ControllerObjectId model,const ActionBroadcast& event);
    ActionServices action_services(ControllerObjectId model);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
