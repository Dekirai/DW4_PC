#include "dw4/action_event_runtime.hpp"

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
        Model(PlayerRuntimeBinding bound,ModelControlState state,bool enabled,
            std::optional<ModelSubcommandOwner> attachment)
            : binding(std::move(bound)),sequence(binding.sequence_program(),std::move(state),
              binding.model_name(),static_cast<std::int32_t>(binding.instance_index())),
              context{{binding.model_name(),static_cast<std::int32_t>(binding.instance_index())},std::move(attachment)},
              active(enabled) {}
    };
    struct Consumer { std::shared_ptr<const ActionEventTableProgram> program; bool active; };
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
    std::map<std::uint32_t,std::string> sources;
    std::map<std::pair<std::uint64_t,std::size_t>,PendingConsumer> pending;
    std::optional<std::uint64_t> suspended_send;
    std::size_t delivery_depth{};
    void require_top_level_idle() const {
        if(suspended_send && delivery_depth==0)
            fail("Resume the suspended synchronous messenger delivery before another top-level operation.");
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
        if(models.contains(id) || consumers.contains(id)) fail("RAM already has a prepared event owner.");
        if(models.size()+consumers.size()>=4096) fail("Prepared event RAM ownership exceeds 4096 objects.");
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
                if(external.active) return external.active(id);
                return std::nullopt;
            },
            [this](ControllerObjectId id)->MessengerManager {
                if(const auto found=models.find(key(id));found!=models.end())
                    return {[this,id](NativeMessenger&,const MessengerDelivery& delivery) { return model_message(model(id),delivery); },{}};
                if(const auto found=consumers.find(key(id));found!=consumers.end())
                    return {[this,id](NativeMessenger&,const MessengerDelivery& delivery) { return consume(consumers.at(key(id)),delivery); },{}};
                return external.manager ? external.manager(id) : MessengerManager{};
            }
        };
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
void ActionEventRuntime::register_consumer(ActionEventTableProgram program,bool active) {
    impl_->require_top_level_idle();
    auto owned=std::make_shared<const ActionEventTableProgram>(std::move(program));
    std::set<Key> new_owners;
    // Repeated references within this program are registration occurrences of
    // one RAM. Conflicting existing owners must be rejected before registration.
    for(const auto ram:owned->ram_owners()) {
        const Key id{owned->source_occurrence(),ram};
        if(impl_->models.contains(id)) fail("Consumer RAM conflicts with prepared MODELCTRL work.");
        const auto found=impl_->consumers.find(id);
        if(found!=impl_->consumers.end() && found->second.program->kind()!=owned->kind())
            fail("Changing an owned consumer RAM's manager class is outside the prepared profile.");
        if(found==impl_->consumers.end()) new_owners.insert(id);
    }
    if(new_owners.size()>4096-impl_->models.size()-impl_->consumers.size())
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
    if(owner.selecting || owner.jump_cleanup) fail("Cannot change a label while sequence cleanup is suspended.");
    owner.sequence.set_label(std::move(sequence),std::move(label));
}
ModelSubcommandResult ActionEventRuntime::select_sequence(ControllerObjectId id,std::string_view name,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.selecting || owner.jump_cleanup || owner.broadcast) fail("Resume pending model delivery or sequence cleanup first.");
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
    const ModelSubcommandServices& services) {
    auto& owner=impl_->model(id);
    if(owner.selecting) fail("Sequence cleanup must complete before executing commands.");
    if(impl_->suspended_send && impl_->delivery_depth==0 &&
       (!owner.broadcast || owner.broadcast->dispatch.id()!=*impl_->suspended_send))
        fail("Another model's suspended synchronous delivery must finish before sequence execution.");
    return owner.sequence.run(time,action,[&](std::string_view) {
        const auto result=owner.jump_cleanup ? owner.queue.resume(services) : owner.queue.cleanup(services);
        owner.jump_cleanup=!result.complete();
        return result.complete();
    });
}
ModelSubcommandResult ActionEventRuntime::update_subcommands(ControllerObjectId id,float frames,const ModelSubcommandServices& services) {
    impl_->require_top_level_idle();
    auto& owner=impl_->model(id);
    if(owner.sequence.selection_pending() || owner.broadcast) fail("Model delivery and sequence cleanup must finish before frame processing.");
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
        if(result.stop==MessengerStop::completed) impl_->suspended_send.reset();
        else impl_->suspended_send=dispatch.id();
    }
    return result;
}
MessengerResult ActionEventRuntime::broadcast(ControllerObjectId id,const ActionBroadcast& event) {
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
