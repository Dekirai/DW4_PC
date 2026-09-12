#include "dw4/action_event_runtime.hpp"
#include "dw4/action_queries.hpp"
#include "dw4/model_status.hpp"

#include <limits>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace dw4 {
namespace {
using Key=ControllerObjectId;
Key key(ControllerObjectId id) { return id; }
[[noreturn]] void fail(const char* message) { throw MessengerError(message); }
bool same(const ActionBroadcast& a,const ActionBroadcast& b) {
    return a.source==b.source && a.info_name==b.info_name && a.ram_no==b.ram_no && a.action==b.action;
}
bool same(const ModelActionRequest& a,const ModelActionRequest& b) {
    return a.source==b.source && a.command_reference==b.command_reference &&
        a.info_name==b.info_name && a.ram_no==b.ram_no && a.action==b.action;
}
struct OperationScope {
    std::optional<Key>& current;
    std::optional<Key> previous;
    OperationScope(std::optional<Key>& target,Key value):current(target),previous(target) { current=value; }
    ~OperationScope() { current=previous; }
};
}
struct ActionEventRuntime::Impl {
    ActionEventRuntime& events_runtime;
    explicit Impl(ActionEventRuntime& owner):events_runtime(owner) {}
    struct PendingBroadcast {
        ActionBroadcast event;
        MessengerDispatch dispatch;
    };
    struct PendingHitbox {
        enum class Phase { geometry, debug, select, send, script };
        ModelHitboxCheckRequest request;
        Phase phase = Phase::geometry;
        MessengerBox box;
        std::optional<MessengerDispatch> dispatch;
        std::optional<ModelLuaRequest> script;
        explicit PendingHitbox(ModelHitboxCheckRequest value):request(std::move(value)) {}
    };
    struct Model {
        PlayerRuntimeBinding binding;
        ModelSequenceRuntime sequence;
        ModelSubcommandQueue queue;
        ModelSubcommandContext context;
        std::shared_ptr<std::uint8_t> active;
        std::optional<ModelQueryOwner> query_owner;
        bool jump_cleanup{};
        bool selecting{};
        std::optional<PendingBroadcast> broadcast;
        std::optional<MessengerDispatch> query;
        std::optional<ModelActionRequest> action_request;
        std::optional<ActionResult> action_result;
        std::uint64_t action_time{};
        AnimationSourceRuntime* action_sources{};
        std::shared_ptr<const void> action_state;
        bool action_faulted{};
        std::optional<std::uint64_t> sequence_time;
        AnimationSourceRuntime* sequence_sources{};
        ActionServices* sequence_services{};
        bool running_action_sequence{};
        ModelWaitRuntime wait;
        ModelWaitState* pending_wait_state{};
        bool debug_hitbox{};
        std::optional<PendingHitbox> hitbox;
        ModelHitboxResult hitbox_result;
        std::unique_ptr<ModelCollisionRuntime> collision;
        ModelControlState* collision_model{};
        bool hp_reference_present{};
        std::weak_ptr<ModelScriptWork> script_work;
        bool script_work_prepared{};
        std::weak_ptr<ModelStatusWork> status_work;
        bool status_work_prepared{};
        Model(PlayerRuntimeBinding bound,ModelControlState state,bool enabled,
            std::optional<ModelSubcommandOwner> attachment)
            : binding(std::move(bound)),sequence(binding.sequence_program(),std::move(state),
              binding.model_name(),static_cast<std::int32_t>(binding.instance_index())),
              context{{binding.model_name(),static_cast<std::int32_t>(binding.instance_index())},std::move(attachment)},
              active(std::make_shared<std::uint8_t>(enabled ? std::uint8_t{1} : std::uint8_t{0})) {}
    };
    struct Consumer { std::shared_ptr<const ActionEventTableProgram> program; bool active; };
    struct Action {
        std::shared_ptr<ActionRuntime> runtime;
        MessengerManager manager;
        std::shared_ptr<ActionSceneQueryOwner> scene;
        ControllerObjectId model;
    };
    struct PendingConsumer {
        std::string model;
        std::int32_t ram_no;
        std::vector<ActionEventTableMatch> matches;
        std::size_t cursor{};
        std::optional<MessengerDispatch> dispatch;
    };
    NativeMessenger messenger;
    MessengerServices external;
    ModelCollisionServices collision_services;
    std::map<Key,std::shared_ptr<Model>> models;
    std::map<Key,Consumer> consumers;
    std::map<Key,Action> actions;
    std::map<Key,NativeControllerServices> prepared_controllers;
    std::map<std::uint32_t,std::string> sources;
    std::map<std::pair<std::uint64_t,std::size_t>,PendingConsumer> pending;
    std::optional<std::uint64_t> suspended_send;
    std::optional<Key> suspended_send_wait;
    std::optional<Key> suspended_wait;
    std::optional<Key> suspended_hitbox;
    std::optional<Key> suspended_send_sequence, suspended_send_subcommands;
    std::optional<Key> processing_sequence, processing_subcommands;
    std::optional<Key> processing_collision;
    bool processing_wait{};
    std::size_t delivery_depth{};
    void require_top_level_idle() const {
        require_wait_idle();
        if(suspended_send && delivery_depth==0)
            fail("Resume the suspended synchronous messenger delivery before another top-level operation.");
    }
    void require_wait_idle() const {
        if(suspended_wait && !processing_wait)
            fail("Resume the suspended player wait before another top-level operation.");
        if(suspended_hitbox && processing_subcommands!=suspended_hitbox)
            fail("Resume the suspended model HITBOX operation before another top-level operation.");
    }
    void require_source(std::uint32_t occurrence,const std::string& source) {
        const auto found=sources.find(occurrence);
        if(found!=sources.end()) {
            if(found->second!=source) fail("Loaded source occurrence already belongs to a different source path.");
        } else {
            if(sources.size()>=4096) fail("Prepared event sources exceed 4096 loaded occurrences.");
            sources.emplace(occurrence,source);
        }
    }
    void require_registration_capacity(std::size_t count,std::size_t text) const {
        if(count>65536-messenger.registration_count() || text>8u*1024u*1024u-messenger.registration_text_bytes())
            fail("Prepared event registration exceeds the messenger's aggregate capacity.");
    }
    Model& model(ControllerObjectId id) {
        const auto found=models.find(key(id));
        if(found==models.end()) fail("No prepared model work owns this source-qualified RAM.");
        return *found->second;
    }
    std::size_t owner_count() const noexcept {
        return models.size()+consumers.size()+actions.size()+prepared_controllers.size();
    }
    void check_new(Key id) const {
        if(models.contains(id) || consumers.contains(id) || actions.contains(id) || prepared_controllers.contains(id))
            fail("RAM already has a prepared event owner.");
        if(owner_count()>=4096) fail("Prepared event RAM ownership exceeds 4096 objects.");
    }
    MessengerCallResult model_message(ControllerObjectId id,Model& owner,const MessengerDelivery& delivery) {
        const auto& command=delivery.message.command;
        if(command=="GETINFONAME" || command=="GETINFONAME_RUN") {
            if(command=="GETINFONAME_RUN" && *owner.active==0)return {true,1,{}};
            if(!owner.query_owner)return {false,0,"MODELCTRL name query requires its actual prepared parent-name owner."};
            // Base8030DEFC/DF20 use the captured info's live name, independent
            // of registered routing keys. Null becomes a non-null empty string.
            messenger.buffer().add_string(owner.query_owner->parent_name->value_or(std::string{}));
            return {true,1,{}};
        }
        if(command=="SETLABEL") {
            if(*owner.active==0)return {true,1,{}};
            if(!owner.query_owner)return {false,0,"MODELCTRL SETLABEL requires its actual prepared work-presence field."};
            if(!*owner.query_owner->work_available)return {true,1,{}};
            if(owner.selecting || owner.jump_cleanup || owner.sequence_time || owner.hitbox || processing_subcommands==key(id) ||
                owner.sequence.selection_pending() || owner.sequence.lua_pending() || owner.sequence.lua_faulted())
                fail("MODELCTRL SETLABEL conflicts with this model's in-flight sequence or subcommand work.");
            const auto& strings=messenger.buffer().strings();
            if(strings.size()<2)fail("Active MODELCTRL SETLABEL requires its two nullable string arguments.");
            owner.sequence.set_label(strings[0],strings[1]);
            return {true,1,{}};
        }
        const auto script_work=owner.script_work.lock();
        if(command=="GETHP") {
            // Base8030E49C first gates the actual active byte and captured
            // base-work pointer. Missing native preparation is not a null
            // original pointer. Append to the existing packet in original order.
            if(*owner.active==0 || (owner.query_owner && !*owner.query_owner->work_available)) {
                messenger.buffer().add_int(0);return {true,1,{}};
            }
            if(!script_work)return {false,0,"MODELCTRL GETHP requires its actual prepared HP work owner."};
            if(!script_work->hp)fail("Active MODELCTRL GETHP reached a null current-HP wrapper.");
            messenger.buffer().add_int(script_work->hp->data);
            messenger.buffer().add_game_object(script_work->hp);
            if(script_work->max_hp)messenger.buffer().add_game_object(script_work->max_hp);
            else messenger.buffer().add_object({});
            return {true,1,{}};
        }
        if(owner.script_work_prepared && !script_work && (command=="CHKBULLET" || command=="GETRECNO" ||
           (owner.collision && owner.collision->owns_delivery(delivery))))
            return {false,0,"The target's prepared script work owner is unavailable."};
        if(command=="GETRECNO" && script_work) {
            // Base 8030E55C loads the signed work+10 value directly, with no
            // wrapper dereference, active gate or automatic default on lookup.
            messenger.buffer().add_int(script_work->record);return {true,1,{}};
        }
        const bool collision_retry=owner.collision && owner.collision->owns_delivery(delivery);
        if(collision_retry || command=="CHKBULLET") {
            if(!owner.collision) return {false,0,"CHKBULLET requires this target's authored HIT graph and prepared ONE work."};
            if(!collision_retry && (owner.sequence.selection_pending() || owner.sequence.lua_pending() || owner.sequence.lua_faulted()))
                return {false,0,"Complete the target's sequence operation before its collision delivery."};
            // Original PC virtual+C4 (803AEBB4) declines CHKBULLET: the command
            // is absent from its sixteen-name status dictionary. This adapter
            // already owns the resolved ONE and explicitly prepared base path;
            // it does not construct PC status or infer another controller type.
            OperationScope operation(processing_collision,key(id));
            const auto& sequence=std::as_const(owner.sequence);
            ModelCollisionWork work{sequence.wait_state().control_enabled,script_work ? bool(script_work->hp) : owner.hp_reference_present,
                sequence.selected() ? std::optional<std::string_view>(sequence.current_sequence()) : std::nullopt};
            // A target script may suspend its own sequence cleanup/Lua prefix.
            // Retain the stable member reference obtained before that prefix;
            // public mutable access correctly remains blocked while it is pending.
            if(!collision_retry) owner.collision_model=&owner.sequence.model();
            auto supplied=collision_services;
            if(supplied.hit_script) supplied.hit_script=[&](const ModelTargetHitRequest& request) {
                const auto direction=collision_services.hit_script(request);
                if(direction && (owner.sequence.selection_pending() || owner.sequence.lua_pending() || owner.sequence.lua_faulted()))
                    fail("Complete the target hit script's sequence effects before publishing its result.");
                return direction;
            };
            const auto result=owner.collision->handle(messenger,delivery,*owner.collision_model,work,services(),supplied);
            if(result.complete()) owner.collision_model=nullptr;
            return result.complete() ? MessengerCallResult{true,1,{}} : MessengerCallResult{false,0,result.diagnostic};
        }
        if(!command || (*command!="SETSOUND" && *command!="SETEFFECT"))
            return {false,0,"This MODELCTRL command requires its original outer controller service."};
        if(*owner.active==0) return {true,1,{}};
        if(owner.sequence.selection_pending()) return {false,0,"MODELCTRL sequence cleanup must finish before appending new work."};
        const auto& buffer=messenger.buffer();
        if(*command=="SETSOUND") {
            if(buffer.floats().empty() || buffer.strings().empty()) fail("SETSOUND requires a delay and nullable sound name.");
            owner.queue.append_sound(buffer.floats()[0],buffer.strings()[0]);
        } else {
            if(buffer.floats().empty() || buffer.strings().size()<2 || buffer.vectors().empty() || buffer.booleans().size()<2)
                fail("SETEFFECT requires delay, two nullable strings, offset and two flags.");
            ModelEffectSubcommand effect;
            effect.delay=buffer.floats()[0]; effect.model_name=buffer.strings()[0]; effect.sequence_name=buffer.strings()[1];
            effect.offset=buffer.vectors()[0];
            if(buffer.vectors().size()==2) effect.rotation=buffer.vectors()[1];
            effect.sequence_dead=buffer.booleans()[0]!=0; effect.position_lock=buffer.booleans()[1]!=0;
            owner.queue.append_effect(std::move(effect));
        }
        return {true,1,{}};
    }
    MessengerCallResult consume(const Consumer& owner,const MessengerDelivery& delivery) {
        const auto continuation=std::pair{delivery.dispatch_id,delivery.row_index};
        auto found=pending.find(continuation);
        if(found==pending.end()) {
            if(!delivery.message.name || !delivery.message.command)
                fail("Original ACTION consumer delivery requires non-null model and action keys.");
            if(pending.size()>=256) fail("ACTION consumer continuations exceed 256 entries.");
            PendingConsumer value{*delivery.message.name,delivery.message.index,
                match_action_event_tables(*owner.program,*delivery.message.name,*delivery.message.command),0,{}};
            found=pending.emplace(continuation,std::move(value)).first;
        }
        auto& state=found->second;
        for(;state.cursor<state.matches.size();++state.cursor) {
            if(!state.dispatch) {
                messenger.select("MODELCTRL",state.model,state.ram_no);
                auto& buffer=messenger.buffer();
                const auto& match=state.matches[state.cursor];
                if(const auto* sound=std::get_if<ActionEventSound>(&match.payload)) {
                    buffer.add_string(sound->name); buffer.add_float(sound->delay);
                    state.dispatch.emplace(messenger.begin_send("SETSOUND"));
                } else {
                    const auto& effect=std::get<ModelEffectSubcommand>(match.payload);
                    buffer.set_enabled_only(1);
                    buffer.add_string(effect.model_name); buffer.add_string(effect.sequence_name);
                    buffer.add_float(effect.delay); buffer.add_vector(effect.offset);
                    if(match.has_rotation) buffer.add_vector(effect.rotation);
                    buffer.add_bool(effect.sequence_dead); buffer.add_bool(effect.position_lock);
                    state.dispatch.emplace(messenger.begin_send("SETEFFECT"));
                }
            }
            const auto result=messenger.resume(*state.dispatch,services());
            if(result.stop==MessengerStop::faulted) throw MessengerError(result.diagnostic);
            if(result.stop!=MessengerStop::completed) return {false,0,result.diagnostic};
            state.dispatch.reset();
        }
        pending.erase(found); return {true,1,{}};
    }
    MessengerServices services() {
        return {
            [this](ControllerObjectId id)->std::optional<std::uint8_t> {
                if(const auto found=models.find(key(id));found!=models.end()) return *found->second->active;
                if(const auto found=consumers.find(key(id));found!=consumers.end()) return found->second.active;
                if(const auto found=actions.find(key(id));found!=actions.end()) return found->second.runtime->active();
                if(const auto found=prepared_controllers.find(key(id));found!=prepared_controllers.end()) return found->second.active();
                if(external.active) return external.active(id);
                return std::nullopt;
            },
            [this](ControllerObjectId id)->MessengerManager {
                if(const auto found=models.find(key(id));found!=models.end())
                    return {[this,id,status=std::shared_ptr<ModelStatusInvocation>{},status_result=std::optional<std::uint32_t>{}]
                        (NativeMessenger&,const MessengerDelivery& delivery) mutable {
                        auto& owner=model(id);
                        if(owner.status_work_prepared) {
                            if(!status_result) {
                                if(!status) {
                                    std::shared_ptr<ModelStatusWork> work;
                                    if(*owner.query_owner->work_available) {
                                        work=owner.status_work.lock();
                                        if(!work)return MessengerCallResult{false,0,"The model's prepared status work owner is unavailable."};
                                    }
                                    status=std::make_shared<ModelStatusInvocation>(ModelStatusOwner{std::move(work),owner.query_owner->ram_no},
                                        delivery.message.command);
                                }
                                // Player virtual+C4 runs before the generic switch,
                                // including when RAM activity is zero. Its invocation
                                // belongs to this exact recipient callback.
                                const auto result=status->resume(messenger,events_runtime);
                                if(!result.completed)return result;
                                status_result=result.value;
                                // Preserve the returned value, not a finished work
                                // capture that could keep a self-recipient alive.
                                status.reset();
                            }
                            if((*status_result&0xffu)!=0)return MessengerCallResult{true,*status_result,{}};
                        }
                        return model_message(id,owner,delivery);
                    },{}};
                if(const auto found=consumers.find(key(id));found!=consumers.end())
                    return {[this,id](NativeMessenger&,const MessengerDelivery& delivery) { return consume(consumers.at(key(id)),delivery); },{}};
                if(const auto found=actions.find(key(id));found!=actions.end()) return found->second.manager;
                if(const auto found=prepared_controllers.find(key(id));found!=prepared_controllers.end()) return found->second.manager();
                return external.manager ? external.manager(id) : MessengerManager{};
            }
        };
    }
    ModelWaitServices wait_services(ActionEventRuntime& runtime,ControllerObjectId id,const PlayerWaitServices& supplied) {
        ModelWaitServices result;
        if(supplied.pads) result.held_buttons=[this,id,pads=supplied.pads]()->std::optional<std::uint32_t> {
            const auto slot=model(id).binding.instance_index();
            if(slot>=OriginalPads::slot_count) fail("Prepared player RAM does not identify one of the four original pad slots.");
            return pads->states()[slot].buttons.held;
        };
        result.action_complete=[&runtime,id] { return runtime.query_action_complete(id); };
        result.resume_commands=supplied.resume_commands;
        result.dispatch_sequence=supplied.dispatch_sequence;
        return result;
    }
    ModelWaitResult process_wait(ActionEventRuntime& runtime,ControllerObjectId id,const PlayerWaitServices& supplied,bool retry) {
        if(processing_wait) fail("Recursive prepared player wait processing is unsupported.");
        if(processing_subcommands) fail("Complete the model subcommand callback before processing a player wait.");
        if(suspended_wait && *suspended_wait!=key(id)) fail("Another player's suspended wait must finish first.");
        auto& owner=model(id);
        if(!retry) {
            require_top_level_idle();
            if(owner.sequence_time || owner.broadcast || owner.query)
                fail("Complete prepared sequence execution and delivery before a new player wait pass.");
            owner.pending_wait_state=&owner.sequence.wait_state();
        } else if(!owner.wait.pending() || !owner.pending_wait_state) {
            fail("No prepared player wait is suspended.");
        }
        if(suspended_send && suspended_send_wait!=key(id) && (!owner.query || owner.query->id()!=*suspended_send) &&
           (!owner.broadcast || owner.broadcast->dispatch.id()!=*suspended_send))
            fail("Another synchronous delivery must complete before player wait processing.");
        struct Guard { bool& value; explicit Guard(bool& flag):value(flag) { value=true; } ~Guard() { value=false; } } guard(processing_wait);
        suspended_wait=key(id);
        const auto services=wait_services(runtime,id,supplied);
        const auto result=retry ? owner.wait.resume(*owner.pending_wait_state,services) :
            owner.wait.update(*owner.pending_wait_state,std::as_const(owner.sequence).model().channels,services);
        if(result.complete()) { suspended_wait.reset(); owner.pending_wait_state=nullptr; }
        return result;
    }
    bool check_hitbox(ActionEventRuntime& runtime,ControllerObjectId id,const ModelHitboxCheckRequest& request,
        const ModelHitboxServices& services) {
        auto& owner=model(id);
        if(request.context.source!=owner.context.source)
            fail("HITBOX source does not belong to the prepared model RAM.");
        if(owner.hitbox && owner.hitbox->request.entry_id!=request.entry_id)
            fail("Complete this model's pending HITBOX check before another entry.");
        if(!owner.hitbox) owner.hitbox.emplace(request);
        auto& state=*owner.hitbox;
        const auto needs=[&](ModelHitboxStop stop,std::string diagnostic) {
            owner.hitbox_result={stop,request.entry_id,std::move(diagnostic)}; return false;
        };
        for(;;) {
            switch(state.phase) {
            case PendingHitbox::Phase::geometry:
                state.box=std::make_shared<const CollisionBox>(original_hitbox_box(std::as_const(owner.sequence).model().pose.matrix(),
                    state.request.command.offset,state.request.command.box_size));
                state.phase=owner.debug_hitbox ? PendingHitbox::Phase::debug : PendingHitbox::Phase::select;
                break;
            case PendingHitbox::Phase::debug:
                if(!services.debug_box || !services.debug_box(id,state.request,*state.box))
                    return needs(ModelHitboxStop::debug_required,"Original HITBOX debug display requires its real service.");
                state.phase=PendingHitbox::Phase::select;
                break;
            case PendingHitbox::Phase::select: {
                const auto mode=state.request.bullet_mode;
                const std::string category=mode==0 ? "HITPLAYERBULLET" : mode==1 ? "HITPLAYERTALK" : "HITENEMYBULLET";
                // A prepared source alone does not establish an empty original
                // collision scene. Require a represented recipient category.
                // Registered inactive targets can still produce an empty result.
                if(!messenger.has_category(category))
                    return needs(ModelHitboxStop::collision_registration_required,"Register the original collision target category before HITBOX delivery.");
                require_top_level_idle();
                messenger.select(category,std::nullopt,-1);
                auto& buffer=messenger.buffer();
                buffer.add_int(state.request.command.kind); buffer.add_box(state.box);
                // Original copies the live matrix and attachment after debug,
                // while the temporary box retains its earlier geometry result.
                buffer.add_matrix(std::as_const(owner.sequence).model().pose.matrix());
                const auto source=owner.context.attachment.value_or(owner.context.source);
                buffer.add_string(source.model_name); buffer.add_int(source.ram_no);
                buffer.set_enabled_only(1);
                state.dispatch.emplace(messenger.begin_send("CHKBULLET"));
                state.phase=PendingHitbox::Phase::send;
                break;
            }
            case PendingHitbox::Phase::send: {
                const auto result=runtime.resume(*state.dispatch);
                if(result.stop==MessengerStop::faulted) throw MessengerError(result.diagnostic);
                if(result.stop!=MessengerStop::completed)
                    return needs(ModelHitboxStop::collision_delivery_required,result.diagnostic);
                state.dispatch.reset();
                // 8030690C returns list COUNT, not boolean[0] truth. A false
                // appended result can still require the live source BHit script.
                const auto& prefix=std::as_const(owner.sequence).model().bullet_hit_script;
                if(!messenger.buffer().booleans().empty() && prefix)
                    state.script=ModelLuaRequest{owner.binding.source(),std::nullopt,owner.binding.model_name(),
                        static_cast<std::int32_t>(owner.binding.instance_index()),*prefix,"BHit"};
                state.phase=PendingHitbox::Phase::script;
                break;
            }
            case PendingHitbox::Phase::script:
                if(state.script && (!services.lua || !services.lua(*state.script)))
                    return needs(ModelHitboxStop::lua_required,"Original source BHit requires its Lua and shared-context service.");
                owner.hitbox_result={ModelHitboxStop::complete,request.entry_id,{}};
                owner.hitbox.reset();
                return true;
            }
        }
    }
    ModelSubcommandResult process_subcommands(ActionEventRuntime& runtime,ControllerObjectId id,std::optional<float> frames,
        const ModelSubcommandServices& supplied,const ModelHitboxServices& hitbox_services) {
        if(suspended_wait && !processing_wait) fail("Resume the suspended player wait before model subcommand processing.");
        if(suspended_hitbox && *suspended_hitbox!=key(id)) fail("Another model's suspended HITBOX must finish first.");
        if(processing_subcommands) fail("Nested model subcommand processing is unsupported.");
        if(processing_sequence) fail("Complete sequence execution before processing model subcommands.");
        auto& owner=model(id);
        const bool own_delivery=!frames && suspended_send_subcommands==key(id);
        if(owner.sequence.selection_pending() || owner.sequence.lua_pending() || owner.sequence.lua_faulted() ||
           owner.sequence_time || owner.jump_cleanup ||
           ((owner.broadcast || owner.query) && !own_delivery))
            fail("Model delivery and sequence execution must finish before subcommand processing.");
        if(frames && (owner.queue.pending() || owner.hitbox)) fail("Resume the pending model subcommand pass before updating.");
        if(suspended_send && (frames || suspended_send_subcommands!=key(id)))
            fail("Another synchronous delivery must complete before subcommand processing.");
        OperationScope operation(processing_subcommands,key(id));
        auto services=supplied;
        services.check_hitbox=[&](const ModelHitboxCheckRequest& request) { return check_hitbox(runtime,id,request,hitbox_services); };
        try {
            const auto result=frames ? owner.queue.update(*frames,owner.context,services) : owner.queue.resume(services);
            if(owner.hitbox) suspended_hitbox=key(id);
            else if(suspended_hitbox==key(id)) suspended_hitbox.reset();
            return result;
        } catch(...) {
            if(owner.hitbox) suspended_hitbox=key(id);
            throw;
        }
    }
};
ActionEventRuntime::ActionEventRuntime():impl_(std::make_unique<Impl>(*this)) {}
ActionEventRuntime::~ActionEventRuntime() {
    // A paused self-recipient can retain status -> script work -> the live
    // Model matrix. Break model-owned continuation edges while the model map
    // still owns every target; captured work must stay strong during execution.
    for(auto& entry:impl_->models) {
        entry.second->query.reset();
        entry.second->broadcast.reset();
        entry.second->hitbox.reset();
    }
}
ControllerObjectId ActionEventRuntime::register_model(PlayerRuntimeBinding binding,ModelControlState model,
    bool active,std::optional<ModelSubcommandOwner> attachment) {
    impl_->require_top_level_idle();
    if(binding.source_occurrence()>std::numeric_limits<std::uint32_t>::max() ||
       binding.instance_index()>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        fail("Prepared model identity cannot be represented in the original messenger.");
    const ControllerObjectId id{static_cast<std::uint32_t>(binding.source_occurrence()),binding.model_ram_reference()};
    impl_->check_new(key(id));
    impl_->require_registration_capacity(1,std::string_view("MODELCTRL").size()+binding.model_name().size());
    impl_->require_source(id.file_index,binding.source());
    const auto [found,inserted]=impl_->models.emplace(key(id),
        std::make_shared<Impl::Model>(std::move(binding),std::move(model),active,std::move(attachment)));
    (void)inserted;
    const auto& value=found->second->binding;
    impl_->messenger.register_recipient(id,"MODELCTRL",value.model_name(),static_cast<std::int32_t>(value.instance_index()));
    return id;
}
void ActionEventRuntime::prepare_model_queries(ControllerObjectId id,ModelQueryOwner fields) {
    // Like installing a missing recipient provider, first preparation can
    // resolve a suspended query. It cannot replace any existing live owner.
    auto& owner=impl_->model(id);
    const auto& bound=owner.binding;
    if(owner.query_owner)fail("This MODELCTRL already retains its prepared query fields.");
    if(fields.parent_info.is_native() || fields.parent_info!=ControllerObjectId{id.file_index,bound.model_reference()} ||
        fields.source!=bound.source() || !fields.parent_name || !fields.ram_no || !fields.active || !fields.work_available)
        fail("MODELCTRL query fields must belong to the exact bound source, parent info and live RAM.");
    if(*fields.ram_no<0 || *fields.ram_no>=4 || *fields.ram_no!=static_cast<std::int32_t>(bound.instance_index()))
        fail("Initial prepared PLAYER RAM number must match the bound selected occurrence.");
    if(*fields.parent_name && ((*fields.parent_name)->size()>65535 || (*fields.parent_name)->find('\0')!=std::string::npos))
        fail("Prepared MODELCTRL parent name exceeds the original bounded string profile.");
    for(const auto& [other_id,other]:impl_->models) {
        if(other_id==id || !other->query_owner)continue;
        const auto& previous=*other->query_owner;
        if(previous.ram_no==fields.ram_no || previous.active==fields.active || previous.work_available==fields.work_available)
            fail("Distinct MODELCTRL RAMs cannot share their physical RAM field owners.");
        if((previous.parent_info==fields.parent_info)!=(previous.parent_name==fields.parent_name))
            fail("Prepared parent-name field aliases must agree with their physical info owner.");
    }
    owner.query_owner.emplace(std::move(fields));
    owner.active=owner.query_owner->active;
}
void ActionEventRuntime::prepare_model_status(ControllerObjectId id,std::shared_ptr<ModelStatusWork> work) {
    auto& owner=impl_->model(id);
    if(owner.status_work_prepared)fail("This MODELCTRL already retains its prepared status fields.");
    const auto actual=owner.script_work.lock();
    if(!owner.query_owner || !actual || !work || work->actual_work()!=actual || actual->target()!=id)
        fail("MODELCTRL status fields require the same actual prepared ONE and live query owners.");
    owner.status_work=std::move(work);owner.status_work_prepared=true;
}
void ActionEventRuntime::register_player(ControllerObjectId id) {
    impl_->require_top_level_idle();
    const auto& owner=impl_->model(id);
    if(!owner.query_owner)fail("PLAYER registration requires explicit actual MODELCTRL query field preparation.");
    const auto& fields=*owner.query_owner;
    if(*fields.ram_no<0 || *fields.ram_no>=4)fail("Prepared PLAYER registration requires live RAM zero through three.");
    // Binding construction independently requires KIND_PLAYER. This explicit
    // registration represents8034C39C, not the omitted PC preparation prelude.
    impl_->messenger.register_recipient(id,"PLAYER",fields.parent_name->value_or(std::string{}),*fields.ram_no);
}
ControllerObjectId ActionEventRuntime::register_action(ControllerObjectId id,std::shared_ptr<ActionRuntime> action) {
    impl_->require_top_level_idle();
    const auto& bound=impl_->model(id).binding;
    const ControllerObjectId recipient{id.file_index,bound.action_ram_reference()};
    impl_->check_new(key(recipient));
    for(const auto& [owned,value]:impl_->actions) {
        (void)owned;
        if(value.runtime==action) fail("A live ACTION runtime already belongs to another qualified RAM.");
    }
    auto scene=std::make_shared<ActionSceneQueryOwner>();
    auto manager=make_action_query_manager(bound,action,scene);
    impl_->require_registration_capacity(1,std::string_view("ACTION").size()+bound.model_name().size());
    impl_->actions.emplace(key(recipient),Impl::Action{std::move(action),std::move(manager),std::move(scene),id});
    impl_->messenger.register_recipient(recipient,"ACTION",bound.model_name(),static_cast<std::int32_t>(bound.instance_index()));
    return recipient;
}
void ActionEventRuntime::prepare_action_scene(ControllerObjectId id,ActorSceneAction scene) {
    // This is initial provider preparation, including while its retained
    // delivery awaits the scene. The scene owner validates and rejects replacement.
    const auto found=impl_->actions.find(key(id));
    if(found==impl_->actions.end())fail("No prepared ACTION owns this source-qualified RAM.");
    found->second.scene->prepare(impl_->model(found->second.model).binding,std::move(scene));
}
void ActionEventRuntime::register_consumer(ActionEventTableProgram program,bool active) {
    impl_->require_top_level_idle();
    auto owned=std::make_shared<const ActionEventTableProgram>(std::move(program));
    std::set<Key> new_owners;
    // Repeated references within this program are registration occurrences of
    // one RAM. Conflicting existing owners must be rejected before registration.
    for(const auto ram:owned->ram_owners()) {
        const Key id{owned->source_occurrence(),ram};
        if(impl_->models.contains(id) || impl_->actions.contains(id) || impl_->prepared_controllers.contains(id))
            fail("Consumer RAM conflicts with existing prepared controller work.");
        const auto found=impl_->consumers.find(id);
        if(found!=impl_->consumers.end() && found->second.program->kind()!=owned->kind())
            fail("Changing an owned consumer RAM's manager class is outside the prepared profile.");
        if(found==impl_->consumers.end()) new_owners.insert(id);
    }
    if(new_owners.size()>4096-impl_->owner_count())
        fail("Prepared event RAM ownership exceeds 4096 objects.");
    impl_->require_registration_capacity(owned->ram_owners().size(),
        owned->ram_owners().size()*std::string_view("ACTION_BROADCAST").size());
    impl_->require_source(owned->source_occurrence(),owned->source());
    for(const auto ram:owned->ram_owners()) {
        const ControllerObjectId id{owned->source_occurrence(),ram};
        if(!impl_->consumers.contains(key(id))) {
            impl_->check_new(key(id)); impl_->consumers.emplace(key(id),Impl::Consumer{owned,active});
        } else impl_->consumers.at(key(id))=Impl::Consumer{owned,active};
        impl_->messenger.register_recipient(id,"ACTION_BROADCAST",std::nullopt,-1);
    }
}
void ActionEventRuntime::register_native_controller(ControllerObjectId id,NativeControllerServices services) {
    if(!id.is_native() || !id.valid()) fail("Generated controller work requires a valid native identity.");
    if(!services.active || !services.manager) fail("Generated controller work requires its actual live providers.");
    impl_->check_new(id);
    impl_->prepared_controllers.emplace(id,std::move(services));
}
void ActionEventRuntime::register_native_controller(ControllerObjectId id,NativeControllerServices services,
    std::string category,std::optional<std::string> name,std::int32_t index) {
    impl_->require_registration_capacity(1,category.size()+(name ? name->size() : 0));
    register_native_controller(id,std::move(services));
    try {impl_->messenger.register_recipient(id,std::move(category),std::move(name),index);}
    catch(...) {impl_->prepared_controllers.erase(id);throw;}
}
void ActionEventRuntime::register_controllers(std::span<const PreparedControllerRegistration> controllers,
    std::span<const MessengerRegistration> rows) {
    if(controllers.size()>4096-impl_->owner_count()) fail("Prepared event RAM ownership exceeds 4096 objects.");
    std::map<Key,NativeControllerServices> providers;
    std::map<std::uint32_t,std::string> sources;
    for(const auto& entry:controllers) {
        if(!entry.id.valid() || !entry.services.active || !entry.services.manager)
            fail("Prepared controller registration requires valid identity and live providers.");
        impl_->check_new(entry.id);
        if(!providers.emplace(entry.id,entry.services).second) fail("Prepared controller batch repeats a provider owner.");
        if(entry.id.is_native()) {
            if(entry.source) fail("A generated native RAM cannot claim a serialized source occurrence.");
        } else {
            if(!entry.source || entry.source->empty() || entry.source->size()>65535 ||
               entry.source->find('\0')!=std::string::npos || entry.id.reference_index==std::numeric_limits<std::uint32_t>::max())
                fail("A serialized prepared controller requires its actual source and non-null RAM reference.");
            const auto existing=impl_->sources.find(entry.id.file_index);
            if(existing!=impl_->sources.end()) {
                if(existing->second!=*entry.source) fail("Loaded source occurrence belongs to another source path.");
            } else {
                const auto [source,inserted]=sources.emplace(entry.id.file_index,*entry.source);
                if(!inserted && source->second!=*entry.source) fail("Prepared controller batch mixes source paths in one occurrence.");
            }
        }
    }
    if(sources.size()>4096-impl_->sources.size()) fail("Prepared event sources exceed 4096 loaded occurrences.");
    impl_->messenger.register_recipients(rows);
    // Preallocated map nodes are merged without replacing any executing
    // provider function. All rejecting validation/allocation happened above.
    impl_->sources.merge(sources);impl_->prepared_controllers.merge(providers);
}
void ActionEventRuntime::set_active(ControllerObjectId id,bool active) {
    if(const auto found=impl_->models.find(key(id));found!=impl_->models.end()) *found->second->active=active ? std::uint8_t{1} : std::uint8_t{0};
    else if(const auto consumer=impl_->consumers.find(key(id));consumer!=impl_->consumers.end()) consumer->second.active=active;
    else fail("Cannot change activity of an unowned event RAM.");
}
const PlayerRuntimeBinding& ActionEventRuntime::binding(ControllerObjectId id) const { return impl_->model(id).binding; }
const ModelSequenceRuntime& ActionEventRuntime::sequence(ControllerObjectId id) const { return impl_->model(id).sequence; }
const ModelSubcommandQueue& ActionEventRuntime::subcommands(ControllerObjectId id) const { return impl_->model(id).queue; }
void ActionEventRuntime::set_label(ControllerObjectId id,std::optional<std::string> sequence,std::optional<std::string> label) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.sequence_time || owner.hitbox || impl_->processing_subcommands==key(id))
        fail("Cannot change a label while sequence or subcommand execution owns this model.");
    owner.sequence.set_label(std::move(sequence),std::move(label));
}
void ActionEventRuntime::prepare_model_state(ControllerObjectId id,ModelControlState state) {
    // A live callback may change original work before the later packet/script
    // reads. Outside that callback, a suspended check owns the work until retry.
    const bool in_check=impl_->processing_subcommands==key(id) || impl_->processing_collision==key(id);
    if(!in_check) impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.sequence_time || owner.wait.pending() ||
       (!in_check && (owner.hitbox || owner.broadcast || owner.query)))
        fail("Cannot replace prepared model work during a suspended operation.");
    for(const auto value:state.pose.matrix()) if(!std::isfinite(value)) fail("Prepared model matrix must be finite.");
    for(const auto* script:{&state.bullet_hit_script,&state.wall_hit_script,&state.floor_hit_script})
        if(*script && ((*script)->size()>4096 || (*script)->find('\0')!=std::string::npos))
            fail("Prepared model script names exceed the bounded sequence string profile.");
    if(!std::isfinite(state.invulnerability_frames)) fail("Prepared invulnerability frames must be finite.");
    owner.sequence.model()=std::move(state);
}
void ActionEventRuntime::set_hitbox_debug(ControllerObjectId id,bool enabled) {
    impl_->require_top_level_idle();
    impl_->model(id).debug_hitbox=enabled;
}
ModelHitboxResult ActionEventRuntime::hitbox_result(ControllerObjectId id) const { return impl_->model(id).hitbox_result; }
void ActionEventRuntime::prepare_collision_target(ControllerObjectId id,ModelHitBodyDefinition definition,bool hp_reference_present) {
    auto& owner=impl_->model(id);
    if(owner.collision) fail("This model already owns its prepared collision body.");
    const auto& binding=owner.binding;
    ModelHitBodyCache body(std::move(definition),binding.instance_index());
    const auto& graph=body.definition();
    if(graph.source()!=binding.source() || graph.source_occurrence()!=binding.source_occurrence() ||
       graph.model_reference()!=binding.model_reference() || graph.model_name()!=binding.model_name() ||
       (id.reference_index!=body.ram_reference() && id.reference_index!=body.ram_owner_reference()))
        fail("Prepared collision HIT graph does not belong to this model's actual source-qualified RAM.");
    for(const auto& [other_id,other]:impl_->models) {
        if(other_id.file_index==id.file_index && other->collision &&
           other->collision->body().ram_owner_reference()==body.ram_owner_reference())
            fail("The physical model RAM already owns its mutable collision body through another reference.");
    }
    std::vector<std::string> categories;
    if(graph.hit_player_bullet()!=0) categories.emplace_back("HITPLAYERBULLET");
    if(graph.hit_player_talk()!=0) categories.emplace_back("HITPLAYERTALK");
    if(graph.hit_enemy_bullet()!=0) categories.emplace_back("HITENEMYBULLET");
    std::size_t text{};
    for(const auto& category:categories) text+=category.size()+binding.model_name().size();
    impl_->require_registration_capacity(categories.size(),text);
    auto target=std::make_unique<ModelCollisionRuntime>(std::move(body));
    for(auto& category:categories) impl_->messenger.register_recipient(id,std::move(category),binding.model_name(),
        static_cast<std::int32_t>(binding.instance_index()));
    owner.hp_reference_present=hp_reference_present;
    owner.collision=std::move(target);
}
const ModelCollisionRuntime& ActionEventRuntime::collision_target(ControllerObjectId id) const {
    const auto& target=impl_->model(id).collision;
    if(!target) fail("This model has no prepared collision target.");
    return *target;
}
std::shared_ptr<ModelScriptWork> ActionEventRuntime::prepare_target_script_work(ControllerObjectId id) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(!owner.collision) fail("Validate the target's authored collision ownership before preparing its script work.");
    if(owner.script_work_prepared) fail("This target already has its prepared script work owner.");
    const auto& bound=owner.binding;
    // The scene matrix has its own lifetime. A retained script work owns that
    // same allocation without retaining the whole controller and its callbacks.
    auto matrix=owner.sequence.model().pose.matrix_owner();
    auto work=std::make_shared<ModelScriptWork>(id,bound.source(),bound.model_name(),
        static_cast<std::int32_t>(bound.instance_index()),std::move(matrix));
    owner.script_work=work;owner.script_work_prepared=true;return work;
}
void ActionEventRuntime::set_collision_services(ModelCollisionServices services) {
    if(impl_->processing_collision) fail("Complete the collision callback before replacing its service owner.");
    impl_->collision_services=std::move(services);
}
ModelSubcommandResult ActionEventRuntime::select_sequence(ControllerObjectId id,std::string_view name,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.broadcast || owner.sequence_time || owner.query ||
       owner.hitbox || impl_->processing_subcommands==key(id))
        fail("Resume pending model delivery or sequence cleanup first.");
    owner.sequence.begin_select(name); // Publish the original pre-cleanup fields.
    owner.selecting=true;
    const auto result=owner.queue.cleanup(services);
    if(result.complete()) { owner.sequence.finish_select(); owner.selecting=false; }
    return result;
}
ModelSubcommandResult ActionEventRuntime::resume_sequence(ControllerObjectId id,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(!owner.selecting) fail("No prepared sequence selection is suspended.");
    const auto result=owner.queue.resume(services);
    if(result.complete()) { owner.sequence.finish_select(); owner.selecting=false; }
    return result;
}
ModelSequenceResult ActionEventRuntime::run_sequence(ControllerObjectId id,std::uint64_t time,const ModelActionDispatch& action,
    const ModelSubcommandServices& services,const ModelActionTryDispatch& try_action,const ModelLuaDispatch& lua) {
    if(impl_->processing_sequence) fail("Nested prepared sequence execution is unsupported.");
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(owner.hitbox || impl_->processing_subcommands==key(id))
        fail("Complete this model's subcommand callback before executing its sequence.");
    if(owner.sequence_time && !owner.running_action_sequence)
        fail("Resume the registered ACTION sequence through its owning operation.");
    if(owner.selecting) fail("Sequence cleanup must complete before executing commands.");
    if(impl_->suspended_send && impl_->delivery_depth==0 && impl_->suspended_send_sequence!=key(id) &&
       (!owner.broadcast || owner.broadcast->dispatch.id()!=*impl_->suspended_send))
        fail("Another model's suspended synchronous delivery must finish before sequence execution.");
    OperationScope operation(impl_->processing_sequence,key(id));
    return owner.sequence.run(time,action,[&](std::string_view) {
        const auto result=owner.jump_cleanup ? owner.queue.resume(services) : owner.queue.cleanup(services);
        owner.jump_cleanup=!result.complete();
        return result.complete();
    },try_action,[&](const ModelHitboxCommand& command) {
        if(command.name=="Set") owner.queue.append_hitbox(command.parameters);
        else if(command.name=="AllClear") owner.queue.clear_hitboxes();
        return true;
    },lua);
}
ModelSequenceResult ActionEventRuntime::run_action_sequence(ControllerObjectId id,AnimationSourceRuntime& sources,
    std::uint64_t time,ActionServices& action,const ModelSubcommandServices& services,const ModelLuaDispatch& lua) {
    if(impl_->processing_sequence) fail("Nested prepared ACTION sequence execution is unsupported.");
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(owner.hitbox || impl_->processing_subcommands==key(id))
        fail("Complete this model's subcommand callback before executing its ACTION sequence.");
    if(owner.action_faulted) fail("Prepared ACTION service has an uncertain outcome; automatic replay is forbidden.");
    if(owner.running_action_sequence) fail("Recursive prepared ACTION sequence execution is unsupported.");
    if(!owner.sequence.selected()) fail("Select the prepared model sequence before executing its ACTION commands.");
    if(owner.selecting || owner.query) fail("Complete sequence cleanup and pending queries before prepared ACTION execution.");
    if(impl_->suspended_send && impl_->delivery_depth==0 && impl_->suspended_send_sequence!=key(id) &&
       (!owner.broadcast || owner.broadcast->dispatch.id()!=*impl_->suspended_send))
        fail("Another model's suspended delivery must finish before prepared ACTION execution.");
    const auto found=impl_->actions.find({id.file_index,owner.binding.action_ram_reference()});
    if(found==impl_->actions.end()) fail("Sequence execution requires this model's registered live ACTION runtime.");
    if(!owner.action_request && found->second.runtime->broadcast_pending())
        fail("Complete the ACTION runtime's earlier broadcast before starting a prepared sequence dispatch.");
    (void)make_action_query_manager(owner.binding,found->second.runtime);
    found->second.runtime->combiner().require_sources(sources);
    if(owner.sequence_time && owner.sequence_sources!=&sources)
        fail("Resume prepared sequence execution with the same live animation source owner.");
    if(owner.sequence_time && owner.sequence_services!=&action)
        fail("Resume prepared sequence execution with the same shared ACTION service owner.");
    if(owner.action_request && owner.action_state!=found->second.runtime->state_identity())
        fail("The suspended ACTION runtime changed outside its owning sequence continuation.");
    if(!owner.sequence_time) { owner.sequence_time=time; owner.sequence_sources=&sources; owner.sequence_services=&action; }
    const auto invocation_time=*owner.sequence_time;
    auto delivery=action_services(id).try_broadcast;
    struct BroadcastScope {
        ActionServices& services;
        std::function<bool(const ActionBroadcast&)> previous;
        ~BroadcastScope() { services.try_broadcast=std::move(previous); }
    } broadcast_scope{action,std::move(action.try_broadcast)};
    action.try_broadcast=std::move(delivery);
    struct Guard { bool& value; explicit Guard(bool& flag):value(flag) { value=true; } ~Guard() { value=false; } } guard(owner.running_action_sequence);
    const auto result=run_sequence(id,invocation_time,{},services,[&](const ModelActionRequest& request) {
        if(owner.action_request && !same(*owner.action_request,request))
            fail("Pending ACTION request differs from the retained model command.");
        const bool retry=owner.action_request.has_value();
        if(!retry) {
            owner.action_request=request; owner.action_time=invocation_time; owner.action_sources=&sources;
        }
        if(owner.action_sources!=&sources) fail("Pending ACTION request requires its original animation source owner.");
        try {
            owner.action_result=retry ? found->second.runtime->resume(sources,action,owner.action_time) :
                found->second.runtime->dispatch(sources,action,request,owner.action_time);
            owner.action_state=found->second.runtime->state_identity();
        } catch(...) { owner.action_faulted=true; throw; }
        const auto stop=owner.action_result->stop;
        if(stop!=ActionStop::end && stop!=ActionStop::waiting) return false;
        owner.action_request.reset(); owner.action_sources=nullptr; owner.action_state.reset(); return true;
    },lua);
    if(result.stop==ModelSequenceStop::end || result.stop==ModelSequenceStop::label_boundary || result.stop==ModelSequenceStop::waiting) {
        owner.sequence_time.reset(); owner.sequence_sources=nullptr; owner.sequence_services=nullptr;
    }
    return result;
}
std::optional<ActionResult> ActionEventRuntime::last_action_result(ControllerObjectId id) const {
    return impl_->model(id).action_result;
}
void ActionEventRuntime::prepare_wait_state(ControllerObjectId id,ModelWaitState state) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.wait.pending() || owner.sequence_time || owner.broadcast || owner.query)
        fail("Cannot replace prepared wait work during a suspended operation.");
    if(state.name && (state.name->size()>65535 || state.name->find('\0')!=std::string::npos))
        fail("Prepared wait name exceeds the original bounded string profile.");
    owner.sequence.wait_state()=std::move(state);
}
ModelWaitResult ActionEventRuntime::update_wait(ControllerObjectId id,const PlayerWaitServices& services) {
    return impl_->process_wait(*this,id,services,false);
}
ModelWaitResult ActionEventRuntime::resume_wait(ControllerObjectId id,const PlayerWaitServices& services) {
    return impl_->process_wait(*this,id,services,true);
}
bool ActionEventRuntime::wait_pending(ControllerObjectId id) const { return impl_->model(id).wait.pending(); }
std::optional<bool> ActionEventRuntime::query_action_complete(ControllerObjectId id) {
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(owner.sequence.selection_pending() || owner.action_request)
        fail("Complete sequence cleanup and ACTION dispatch before querying its command work.");
    if(!owner.query) {
        impl_->require_top_level_idle();
        owner.query.emplace(impl_->messenger.begin_send_to("ACTION",owner.binding.model_name(),
            static_cast<std::int32_t>(owner.binding.instance_index()),"ISCOMEND"));
    }
    const auto result=resume(*owner.query);
    if(result.stop==MessengerStop::faulted) throw MessengerError(result.diagnostic);
    if(result.stop!=MessengerStop::completed) return std::nullopt;
    owner.query.reset();
    const auto& values=impl_->messenger.buffer().booleans();
    if(values.empty()) fail("Original ACTION ISCOMEND completed without its required boolean result.");
    return values.front()!=0;
}
ModelSubcommandResult ActionEventRuntime::update_subcommands(ControllerObjectId id,float frames,const ModelSubcommandServices& services,
    const ModelHitboxServices& hitbox) {
    return impl_->process_subcommands(*this,id,frames,services,hitbox);
}
ModelSubcommandResult ActionEventRuntime::resume_subcommands(ControllerObjectId id,const ModelSubcommandServices& services,
    const ModelHitboxServices& hitbox) {
    return impl_->process_subcommands(*this,id,std::nullopt,services,hitbox);
}
NativeMessenger& ActionEventRuntime::messenger() { impl_->require_top_level_idle(); return impl_->messenger; }
void ActionEventRuntime::register_recipient(ControllerObjectId id,std::string category,std::optional<std::string> name,std::int32_t index) {
    impl_->messenger.register_recipient(id,std::move(category),std::move(name),index);
}
void ActionEventRuntime::set_external_services(MessengerServices services) { impl_->external=std::move(services); }
MessengerResult ActionEventRuntime::resume(MessengerDispatch& dispatch) {
    impl_->require_wait_idle();
    const bool outer=impl_->delivery_depth==0;
    if(outer && impl_->suspended_send && *impl_->suspended_send!=dispatch.id())
        fail("Only the suspended top-level messenger delivery can be resumed.");
    struct Depth {
        std::size_t& value;
        explicit Depth(std::size_t& depth):value(depth) { ++value; }
        ~Depth() { --value; }
    } depth(impl_->delivery_depth);
    const auto result=impl_->messenger.resume(dispatch,impl_->services());
    if(outer) {
        if(result.stop==MessengerStop::completed) {
            impl_->suspended_send.reset(); impl_->suspended_send_wait.reset();
            impl_->suspended_send_sequence.reset(); impl_->suspended_send_subcommands.reset();
        } else {
            impl_->suspended_send=dispatch.id();
            impl_->suspended_send_wait=impl_->processing_wait ? impl_->suspended_wait : std::nullopt;
            impl_->suspended_send_sequence=impl_->processing_sequence;
            impl_->suspended_send_subcommands=impl_->processing_subcommands;
        }
    }
    return result;
}
MessengerDeferredUpdate ActionEventRuntime::begin_deferred_update(std::uint64_t context_time) {
    impl_->require_top_level_idle();
    auto update=impl_->messenger.begin_deferred_update(context_time);
    if(!update.completed()) impl_->suspended_send=update.id();
    return update;
}
MessengerResult ActionEventRuntime::resume(MessengerDeferredUpdate& update) {
    impl_->require_wait_idle();
    if(impl_->delivery_depth || (impl_->suspended_send && *impl_->suspended_send!=update.id()))
        fail("Only the owning top-level deferred manager update can be resumed.");
    struct Depth {
        std::size_t& value;
        explicit Depth(std::size_t& depth):value(depth) { ++value; }
        ~Depth() { --value; }
    } depth(impl_->delivery_depth);
    const auto result=impl_->messenger.resume(update,impl_->services());
    if(result.stop==MessengerStop::completed) {
        impl_->suspended_send.reset(); impl_->suspended_send_wait.reset();
        impl_->suspended_send_sequence.reset(); impl_->suspended_send_subcommands.reset();
    } else {
        impl_->suspended_send=update.id(); impl_->suspended_send_wait.reset();
        impl_->suspended_send_sequence.reset(); impl_->suspended_send_subcommands.reset();
    }
    return result;
}
MessengerResult ActionEventRuntime::broadcast(ControllerObjectId id,const ActionBroadcast& event) {
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(event.source!=owner.binding.source() || event.info_name!=owner.binding.model_name() ||
       event.ram_no!=static_cast<std::int32_t>(owner.binding.instance_index()))
        fail("ACTION broadcast does not belong to this prepared model binding.");
    if(owner.sequence.selection_pending()) fail("Sequence cleanup must finish before a new ACTION broadcast.");
    if(owner.broadcast && !same(owner.broadcast->event,event)) fail("Resume the pending ACTION event before starting another.");
    if(!owner.broadcast) {
        impl_->require_top_level_idle();
        owner.broadcast.emplace(Impl::PendingBroadcast{event,
            impl_->messenger.begin_send_to("ACTION_BROADCAST",event.info_name,event.ram_no,event.action)});
    }
    const auto result=resume(owner.broadcast->dispatch);
    if(result.stop==MessengerStop::completed) owner.broadcast.reset();
    return result;
}
ActionServices ActionEventRuntime::action_services(ControllerObjectId id) {
    impl_->model(id);
    ActionServices services;
    services.try_broadcast=[this,id](const ActionBroadcast& event) {
        const auto result=broadcast(id,event);
        if(result.stop==MessengerStop::faulted) throw MessengerError(result.diagnostic);
        return result.stop==MessengerStop::completed;
    };
    return services;
}
} // namespace dw4
