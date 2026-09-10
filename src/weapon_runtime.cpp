#include "dw4/weapon_runtime.hpp"
#include "dw4/action_event_runtime.hpp"

#include <map>
#include <optional>
#include <string_view>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw WeaponRuntimeError(reason);}
std::size_t ordinal(std::int32_t index) {
    if(index<0 || index>=4) fail("Weapon tracking requires an original player ordinal in [0,3].");
    return static_cast<std::size_t>(index);
}
struct Generation {
    enum class Phase { player,appearance,labels,nodes,complete };
    NativeMessenger* messenger{};
    std::uint64_t dispatch_id{};
    std::size_t delivery_row{};
    std::shared_ptr<const WeaponProgram> program;
    std::size_t info_index{},row{};
    std::optional<std::string> player_name;
    std::shared_ptr<ActorSceneAppearanceList> appearances;
    std::shared_ptr<const ActorSceneSkeleton> skeleton;
    std::optional<MessengerDispatch> dispatch;
    Phase phase=Phase::player;
    bool started{},executing{},fault{};
};
}
struct WeaponRuntime::Impl {
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    ActionEventRuntime* events{};
    std::vector<WeaponProgram> sources;
    std::map<ControllerObjectId,std::shared_ptr<WeaponRamState>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::array<std::vector<WeaponAttachment>,4> tracking;
    bool fault{};
    std::shared_ptr<WeaponRamState> find(ControllerObjectId id) const {
        const auto alias=aliases.find(id);
        if(alias==aliases.end()) fail("Weapon RAM does not belong to this manager.");
        return rams.at(alias->second);
    }
    void remove(std::int32_t index) {
        auto& pairs=tracking[ordinal(index)];
        for(const auto& pair:pairs)
            for(const auto& appearance:pair.appearances->values())
                appearance->bone_selectors()->remove(pair.selector);
        pairs.clear();
    }
    MessengerCallResult resume(Generation& call,WeaponRamState& ram,NativeMessenger& messenger,
        const MessengerDelivery& delivery,std::string_view command) {
        if(fault || call.fault) fail("An uncertain weapon operation cannot be replayed.");
        if(delivery.recipient!=ram.id) fail("Weapon delivery belongs to another canonical RAM.");
        if(call.executing) fail("The same weapon delivery cannot resume recursively.");
        if(call.started && (call.messenger!=&messenger || call.dispatch_id!=delivery.dispatch_id ||
           call.delivery_row!=delivery.row_index || command!="GENERATE"))
            fail("A retained weapon generation requires its exact delivery and messenger.");
        if(!call.started) {
            if(command=="GETINFONAME") {
                messenger.buffer().add_string(ram.definition().name.value_or(""));return {true,1,{}};
            }
            if(command!="GENERATE" && command!="REMOVE") return {true,0,{}};
            ordinal(ram.index); // A malformed prepared ordinal fails before removal.
        }
        struct Guard {
            bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}
        } guard(call.executing);
        try {
            if(!call.started) {
                // 80318498 captures parent info before direct REMOVE or any send.
                call.program=ram.program;call.info_index=ram.info_index;
                remove(ram.index);
                if(command=="REMOVE") return {true,1,{}};
                call.messenger=&messenger;call.dispatch_id=delivery.dispatch_id;call.delivery_row=delivery.row_index;
                call.started=true;
            }
            const auto& info=call.program->infos().at(call.info_index);
            const auto send=[&]()->std::optional<MessengerCallResult> {
                const auto result=events->resume(*call.dispatch);
                if(result.stop==MessengerStop::faulted) fail("Original weapon nested message delivery faulted.");
                if(result.stop!=MessengerStop::completed) return MessengerCallResult{false,0,result.diagnostic};
                call.dispatch.reset();return {};
            };
            while(call.phase!=Generation::Phase::complete) {
                switch(call.phase) {
                case Generation::Phase::player:
                    if(!call.dispatch)call.dispatch.emplace(messenger.begin_send_to("PLAYER",{},ram.index,"GETINFONAME_RUN"));
                    if(const auto pending=send())return *pending;
                    // 8031850C..10: wrong final cardinality is handled, after REMOVE.
                    if(messenger.buffer().strings().size()!=1) {call.phase=Generation::Phase::complete;break;}
                    call.player_name=messenger.buffer().strings()[0];call.phase=Generation::Phase::appearance;
                    break;
                case Generation::Phase::appearance:
                    if(!call.dispatch)call.dispatch.emplace(messenger.begin_send_to("ACTION",call.player_name,ram.index,"GETAPPEARANCELIST"));
                    if(const auto pending=send())return *pending;
                    {
                        const auto& objects=messenger.buffer().objects();
                        const auto* appearances=objects.empty()?nullptr:std::get_if<std::shared_ptr<ActorSceneAppearanceList>>(&objects[0]);
                        const auto* skeleton=objects.size()<2?nullptr:std::get_if<std::shared_ptr<const ActorSceneSkeleton>>(&objects[1]);
                        if(!appearances || !*appearances || !skeleton || !*skeleton)
                            fail("Weapon generation requires actual appearance-list and skeleton result owners.");
                        if(!(*appearances)->shares_source(**skeleton))
                            fail("Weapon appearance-list and skeleton belong to different loaded sources.");
                        call.appearances=*appearances;call.skeleton=*skeleton;
                    }
                    call.row=0;call.phase=Generation::Phase::labels;
                    break;
                case Generation::Phase::labels:
                    if(call.row==info.sequences.size()) {call.row=0;call.phase=Generation::Phase::nodes;break;}
                    if(!call.dispatch) {
                        const auto& row=info.sequences[call.row];
                        messenger.select("MODELCTRL",call.player_name,ram.index);
                        messenger.buffer().add_string(row.name.value_or(""));messenger.buffer().add_string(row.label);
                        call.dispatch.emplace(messenger.begin_send("SETLABEL"));
                    }
                    if(const auto pending=send())return *pending;
                    ++call.row;break;
                case Generation::Phase::nodes:
                    if(call.row==info.nodes.size()) {call.phase=Generation::Phase::complete;break;}
                    {
                        const auto index=call.skeleton->find_bone(info.bone_names[call.row].name.value_or(""));
                        if(index>=0) {
                            auto& pairs=tracking[ordinal(ram.index)];
                            if(pairs.size()>=4096) fail("Weapon manager tracking exceeds its bounded attachment count.");
                            auto selector=std::make_shared<const ActorBoneSelector>(index,
                                std::vector<std::shared_ptr<const ActorSceneNode>>{info.nodes[call.row].node});
                            // One selector is shared across every ordered appearance occurrence.
                            for(const auto& appearance:call.appearances->values()) appearance->bone_selectors()->append(selector);
                            pairs.push_back({std::move(selector),call.appearances});
                        }
                        ++call.row;
                    }
                    break;
                case Generation::Phase::complete:break;
                }
            }
            return {true,1,{}};
        }catch(...){call.fault=true;fault=true;throw;}
    }
};
WeaponRuntime::WeaponRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()),runtime_(runtime) {impl_->events=&runtime;}
WeaponRuntime::~WeaponRuntime()=default;
ControllerObjectId WeaponRuntime::manager_id() const noexcept {return impl_->manager;}
const std::vector<WeaponProgram>& WeaponRuntime::sources() const noexcept {return impl_->sources;}
const WeaponRamState& WeaponRuntime::ram(ControllerObjectId id) const {return *impl_->find(id);}
const std::vector<WeaponAttachment>& WeaponRuntime::attachments(std::size_t index) const {
    if(index>=4) fail("Weapon tracking requires an original player ordinal in [0,3].");
    return impl_->tracking[index];
}
bool WeaponRuntime::faulted() const noexcept {return impl_->fault;}
void WeaponRuntime::set_active(ControllerObjectId id,std::uint8_t active) {
    if(impl_->fault) fail("An uncertain weapon manager cannot change live RAM state.");
    impl_->find(id)->active=active;
}
void WeaponRuntime::set_index(ControllerObjectId id,std::int32_t index) {
    if(impl_->fault) fail("An uncertain weapon manager cannot change live RAM state.");
    ordinal(index);impl_->find(id)->index=index;
}
void WeaponRuntime::register_source(WeaponProgram program) {
    if(impl_->fault) fail("An uncertain weapon manager cannot register another source.");
    for(const auto& existing:impl_->sources)
        if(existing.source_occurrence()==program.source_occurrence())
            fail("Weapon source occurrence is already registered with this manager.");
    if(impl_->sources.size()>=4096) fail("Weapon loaded source count exceeds its budget.");
    auto sources=impl_->sources;sources.push_back(program);
    auto owner=std::make_shared<const WeaponProgram>(std::move(program));
    std::map<ControllerObjectId,std::shared_ptr<WeaponRamState>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::vector<MessengerRegistration> rows;
    for(std::size_t info_index=0;info_index<owner->infos().size();++info_index) {
        const auto& info=owner->infos()[info_index];
        if(info.rams.size()>4) fail("Prepared weapon registration supports the original four tracking slots.");
        for(std::size_t index=0;index<info.rams.size();++index) {
            const auto& authored=info.rams[index];
            const ControllerObjectId id{owner->source_occurrence(),authored.owner_reference};
            auto found=rams.find(id);
            if(found==rams.end()) found=rams.emplace(id,std::make_shared<WeaponRamState>()).first;
            *found->second={id,impl_->manager,{owner->source_occurrence(),info.owner_reference},
                static_cast<std::int32_t>(index),authored.active,{},owner,info_index};
            aliases.emplace(ControllerObjectId{owner->source_occurrence(),authored.reference},id);aliases.emplace(id,id);
            rows.push_back({id,"PLAYERARMS",info.name.value_or(""),static_cast<std::int32_t>(index)});
        }
    }
    std::vector<PreparedControllerRegistration> providers;providers.reserve(rams.size());
    for(const auto& [id,ram]:rams) {
        providers.push_back({id,{
            [ram]()->std::optional<std::uint8_t> {return ram->active;},
            [impl=impl_,ram] {
                // NativeMessenger retains this manager pair per delivery. Its
                // distinct invocation owner permits nested calls on the same RAM.
                auto invocation=std::make_shared<Generation>();
                return MessengerManager{
                    // Vtable804DFDD0: original +90 is the zero hook802F2238.
                    [](NativeMessenger&,const MessengerDelivery&) {return MessengerCallResult{true,0,{}};},
                    [impl,ram,invocation](NativeMessenger& messenger,const MessengerDelivery& delivery,std::string_view command) {
                        return impl->resume(*invocation,*ram,messenger,delivery,command);
                    }};
            }},owner->source()});
    }
    runtime_.register_controllers(providers,rows);
    impl_->rams.merge(rams);impl_->aliases.merge(aliases);impl_->sources.swap(sources);
}
} // namespace dw4
