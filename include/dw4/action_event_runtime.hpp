#pragma once

#include "dw4/action_event_tables.hpp"
#include "dw4/messenger.hpp"
#include "dw4/player_runtime_binding.hpp"

namespace dw4 {
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
    // required. No new command runs until the complete cleanup pass succeeds.
    ModelSubcommandResult select_sequence(ControllerObjectId id,std::string_view name,
        const ModelSubcommandServices& services = {});
    ModelSubcommandResult resume_sequence(ControllerObjectId id,
        const ModelSubcommandServices& services = {});
    ModelSequenceResult run_sequence(ControllerObjectId id,std::uint64_t world_time,
        const ModelActionDispatch& action = {},const ModelSubcommandServices& services = {});
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
