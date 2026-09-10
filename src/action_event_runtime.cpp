#include "dw4/action_event_runtime.hpp"
#include "dw4/action_queries.hpp"

#include <limits>
#include <map>
#include <set>
#include <utility>

namespace dw4 {
namespace {
using Key=std::pair<std::uint32_t,std::uint32_t>;
Key key(ControllerObjectId id) { return {id.file_index,id.reference_index}; }
[[noreturn]] void fail(const char* message) { throw MessengerError(message); }
bool same(const ActionBroadcast& a,const ActionBroadcast& b) {
    return a.source==b.source && a.info_name==b.info_name && a.ram_no==b.ram_no && a.action==b.action;
}
bool same(const ModelActionRequest& a,const ModelActionRequest& b) {
    return a.source==b.source && a.command_reference==b.command_reference &&
        a.info_name==b.info_name && a.ram_no==b.ram_no && a.action==b.action;
}
}
struct ActionEventRuntime::Impl {
    struct PendingBroadcast {
        ActionBroadcast event;
        MessengerDispatch dispatch;
    };
    struct Model {
        PlayerRuntimeBinding binding;
        ModelSequenceRuntime sequence;
        ModelSubcommandQueue queue;
        ModelSubcommandContext context;
        bool active;
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
        Model(PlayerRuntimeBinding bound,ModelControlState state,bool enabled,
            std::optional<ModelSubcommandOwner> attachment)
            : binding(std::move(bound)),sequence(binding.sequence_program(),std::move(state),
              binding.model_name(),static_cast<std::int32_t>(binding.instance_index())),
              context{{binding.model_name(),static_cast<std::int32_t>(binding.instance_index())},std::move(attachment)},
              active(enabled) {}
    };
    struct Consumer { std::shared_ptr<const ActionEventTableProgram> program; bool active; };
    struct Action { std::shared_ptr<ActionRuntime> runtime; MessengerManager manager; };
    struct PendingConsumer {
        std::string model;
        std::int32_t ram_no;
        std::vector<ActionEventTableMatch> matches;
        std::size_t cursor{};
        std::optional<MessengerDispatch> dispatch;
    };
    NativeMessenger messenger;
    MessengerServices external;
    std::map<Key,Model> models;
    std::map<Key,Consumer> consumers;
    std::map<Key,Action> actions;
    std::map<std::uint32_t,std::string> sources;
    std::map<std::pair<std::uint64_t,std::size_t>,PendingConsumer> pending;
    std::optional<std::uint64_t> suspended_send;
    std::optional<Key> suspended_send_wait;
    std::optional<Key> suspended_wait;
    bool processing_wait{};
    std::size_t delivery_depth{};
    void require_top_level_idle() const {
        if(suspended_wait && !processing_wait)
            fail("Resume the suspended player wait before another top-level operation.");
        if(suspended_send && delivery_depth==0)
            fail("Resume the suspended synchronous messenger delivery before another top-level operation.");
    }
    void require_wait_idle() const {
        if(suspended_wait && !processing_wait)
            fail("Resume the suspended player wait before another top-level operation.");
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
        return found->second;
    }
    void check_new(Key id) const {
        if(models.contains(id) || consumers.contains(id) || actions.contains(id)) fail("RAM already has a prepared event owner.");
        if(models.size()+consumers.size()+actions.size()>=4096) fail("Prepared event RAM ownership exceeds 4096 objects.");
    }
    MessengerCallResult model_message(Model& owner,const MessengerDelivery& delivery) {
        const auto& command=delivery.message.command;
        if(!command || (*command!="SETSOUND" && *command!="SETEFFECT"))
            return {false,0,"This MODELCTRL command requires its original outer controller service."};
        if(!owner.active) return {true,1,{}};
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
                if(const auto found=models.find(key(id));found!=models.end()) return found->second.active;
                if(const auto found=consumers.find(key(id));found!=consumers.end()) return found->second.active;
                if(const auto found=actions.find(key(id));found!=actions.end()) return found->second.runtime->active();
                if(external.active) return external.active(id);
                return std::nullopt;
            },
            [this](ControllerObjectId id)->MessengerManager {
                if(const auto found=models.find(key(id));found!=models.end())
                    return {[this,id](NativeMessenger&,const MessengerDelivery& delivery) { return model_message(model(id),delivery); },{}};
                if(const auto found=consumers.find(key(id));found!=consumers.end())
                    return {[this,id](NativeMessenger&,const MessengerDelivery& delivery) { return consume(consumers.at(key(id)),delivery); },{}};
                if(const auto found=actions.find(key(id));found!=actions.end()) return found->second.manager;
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
};
ActionEventRuntime::ActionEventRuntime():impl_(std::make_unique<Impl>()) {}
ActionEventRuntime::~ActionEventRuntime() = default;
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
    const auto [found,inserted]=impl_->models.try_emplace(key(id),std::move(binding),std::move(model),active,std::move(attachment));
    (void)inserted;
    const auto& value=found->second.binding;
    impl_->messenger.register_recipient(id,"MODELCTRL",value.model_name(),static_cast<std::int32_t>(value.instance_index()));
    return id;
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
    auto manager=make_action_query_manager(bound,action);
    impl_->require_registration_capacity(1,std::string_view("ACTION").size()+bound.model_name().size());
    impl_->actions.emplace(key(recipient),Impl::Action{std::move(action),std::move(manager)});
    impl_->messenger.register_recipient(recipient,"ACTION",bound.model_name(),static_cast<std::int32_t>(bound.instance_index()));
    return recipient;
}
void ActionEventRuntime::register_consumer(ActionEventTableProgram program,bool active) {
    impl_->require_top_level_idle();
    auto owned=std::make_shared<const ActionEventTableProgram>(std::move(program));
    std::set<Key> new_owners;
    // Repeated references within this program are registration occurrences of
    // one RAM. Conflicting existing owners must be rejected before registration.
    for(const auto ram:owned->ram_owners()) {
        const Key id{owned->source_occurrence(),ram};
        if(impl_->models.contains(id) || impl_->actions.contains(id)) fail("Consumer RAM conflicts with prepared MODELCTRL/ACTION work.");
        const auto found=impl_->consumers.find(id);
        if(found!=impl_->consumers.end() && found->second.program->kind()!=owned->kind())
            fail("Changing an owned consumer RAM's manager class is outside the prepared profile.");
        if(found==impl_->consumers.end()) new_owners.insert(id);
    }
    if(new_owners.size()>4096-impl_->models.size()-impl_->consumers.size()-impl_->actions.size())
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
void ActionEventRuntime::set_active(ControllerObjectId id,bool active) {
    if(const auto found=impl_->models.find(key(id));found!=impl_->models.end()) found->second.active=active;
    else if(const auto consumer=impl_->consumers.find(key(id));consumer!=impl_->consumers.end()) consumer->second.active=active;
    else fail("Cannot change activity of an unowned event RAM.");
}
const PlayerRuntimeBinding& ActionEventRuntime::binding(ControllerObjectId id) const { return impl_->model(id).binding; }
const ModelSequenceRuntime& ActionEventRuntime::sequence(ControllerObjectId id) const { return impl_->model(id).sequence; }
const ModelSubcommandQueue& ActionEventRuntime::subcommands(ControllerObjectId id) const { return impl_->model(id).queue; }
void ActionEventRuntime::set_label(ControllerObjectId id,std::optional<std::string> sequence,std::string label) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.sequence_time) fail("Cannot change a label while sequence execution is suspended.");
    owner.sequence.set_label(std::move(sequence),std::move(label));
}
ModelSubcommandResult ActionEventRuntime::select_sequence(ControllerObjectId id,std::string_view name,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.broadcast || owner.sequence_time || owner.query)
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
    const ModelSubcommandServices& services,const ModelActionTryDispatch& try_action) {
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(owner.sequence_time && !owner.running_action_sequence)
        fail("Resume the registered ACTION sequence through its owning operation.");
    if(owner.selecting) fail("Sequence cleanup must complete before executing commands.");
    if(impl_->suspended_send && impl_->delivery_depth==0 &&
       (!owner.broadcast || owner.broadcast->dispatch.id()!=*impl_->suspended_send))
        fail("Another model's suspended synchronous delivery must finish before sequence execution.");
    return owner.sequence.run(time,action,[&](std::string_view) {
        const auto result=owner.jump_cleanup ? owner.queue.resume(services) : owner.queue.cleanup(services);
        owner.jump_cleanup=!result.complete();
        return result.complete();
    },try_action);
}
ModelSequenceResult ActionEventRuntime::run_action_sequence(ControllerObjectId id,AnimationSourceRuntime& sources,
    std::uint64_t time,ActionServices& action,const ModelSubcommandServices& services) {
    impl_->require_wait_idle();
    auto& owner=impl_->model(id);
    if(owner.action_faulted) fail("Prepared ACTION service has an uncertain outcome; automatic replay is forbidden.");
    if(owner.running_action_sequence) fail("Recursive prepared ACTION sequence execution is unsupported.");
    if(!owner.sequence.selected()) fail("Select the prepared model sequence before executing its ACTION commands.");
    if(owner.selecting || owner.query) fail("Complete sequence cleanup and pending queries before prepared ACTION execution.");
    if(impl_->suspended_send && impl_->delivery_depth==0 &&
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
    });
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
ModelSubcommandResult ActionEventRuntime::update_subcommands(ControllerObjectId id,float frames,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.sequence.selection_pending() || owner.broadcast || owner.sequence_time || owner.query)
        fail("Model delivery and sequence execution must finish before frame processing.");
    return owner.queue.update(frames,owner.context,services);
}
ModelSubcommandResult ActionEventRuntime::resume_subcommands(ControllerObjectId id,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup) fail("Resume the owning sequence operation to complete cleanup.");
    return owner.queue.resume(services);
}
NativeMessenger& ActionEventRuntime::messenger() { impl_->require_top_level_idle(); return impl_->messenger; }
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
        } else {
            impl_->suspended_send=dispatch.id();
            impl_->suspended_send_wait=impl_->processing_wait ? impl_->suspended_wait : std::nullopt;
        }
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
