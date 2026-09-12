#include "dw4/action_dispatch.hpp"
#include "dw4/model_scene.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& action,const char* reason) {
    ++checks;try {action();}catch(const MessengerError&){return;}throw std::runtime_error(reason);
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Fixture : pf::RuntimeFixture {
    std::map<std::uint32_t,std::string> types;
    std::map<std::uint32_t,std::vector<std::uint16_t>> floats;
    Fixture() {
        std::vector<std::uint32_t> definitions;
        const auto define=[&](std::string name,std::vector<std::uint32_t> commands) {
            const auto ref=add(pf::sequence_data_type);text(ref,2,name);scalar(ref,3,list(pf::named_list,commands));
            types[ref]="beAction2Data";definitions.push_back(ref);
        };
        const auto wait=add(pf::sequence_type);text(wait,2,"FRAME");scalar(wait,3,std::bit_cast<std::uint32_t>(2.f));
        types[wait]="beAction2WAITCHECK";floats[wait]={3};
        const auto random=add(pf::sequence_type);text(random,2,"RANDOM");scalar(random,3,std::bit_cast<std::uint32_t>(4.f));
        types[random]="beAction2WAITCHECK";floats[random]={3};
        const auto unknown=add(pf::sequence_type);text(unknown,2,"unknown");types[unknown]="UnsupportedAction";
        define("empty",{});define("wait",{wait});define("random",{random});define("unsupported",{unknown});
        define("ISRUN",{}); // Native dictionary priority must still win.
        scalar(action,9,list(pf::action_data_list,definitions));
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        for(const auto& [ref,type]:types) {
            auto& object=doc.objects[doc.references[ref].value_index];
            auto descriptor=doc.metadata.object_types[object.type_index];descriptor.name=type;descriptor.parent_index=pf::object;
            if(const auto found=floats.find(ref);found!=floats.end())for(const auto slot:found->second)
                const_cast<IgbFieldValue*>(doc.field(object,slot))->type_index=8;
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(descriptor));
        }
        return doc;
    }
    PlayerRuntimeBinding binding() const {
        return decode_player_runtime_binding(document(),runtime_request(),"synthetic/action.igb",21);
    }
    std::shared_ptr<AnimationSourceRuntime> library(const PlayerRuntimeBinding& binding) const {
        std::array<pf::Bytes,14> streams;streams[0].resize(7);streams[1]={1,1,1,1};
        streams[9]={64};streams[10]={100};streams[13]={0,0,0,0};
        for(const auto component:{3u,10u,17u,24u})streams[0][component/4]|=std::uint8_t(1u<<(6-2*(component%4)));
        pf::Bytes bytes(80);pf::word(bytes,0,0x002abf84);pf::word(bytes,4,4);
        pf::word(bytes,8,std::bit_cast<std::uint32_t>(1.f));pf::word(bytes,12,std::bit_cast<std::uint32_t>(1.f));pf::word(bytes,16,1);
        for(std::size_t i=0;i<streams.size();++i)pf::word(bytes,20+4*i,static_cast<std::uint32_t>(streams[i].size()));
        for(const auto i:{3,8,12,2,7,11,0,1,4,5,6,9,10,13})pf::append(bytes,streams[i]);
        AnimationClip clip;clip.info.supported=true;clip.info.reference_index=binding.action_program().animations().front().first;
        clip.info.name="waitS";clip.info.duration_seconds=1;clip.info.duration_ticks=1000000000;clip.info.track_count=4;
        clip.info.source_reference=9000;clip.info.skeleton_reference=skeleton;clip.channels=decode_enbaya(bytes);
        clip.bone_references=bones;clip.constant_translations.resize(4);clip.use_sample_translation.resize(4);
        auto result=std::make_shared<AnimationSourceRuntime>(binding.source(),std::vector<AnimationClip>{std::move(clip)});
        result->initialize_action_offset();return result;
    }
    std::shared_ptr<ActionRuntime> runtime(const PlayerRuntimeBinding& binding,const AnimationSourceRuntime& sources) const {
        AnimationCombiner combiner(sources,std::vector<AnimationMatrix>(4,identity()),decode_animation_hierarchy(document(),skeleton),
            std::vector<AnimationMatrix>(4,identity()),std::vector<AnimationMatrix>(2,identity()),binding.combiner_reference());
        auto actors=decode_action_actor_group(document(),binding.action_ram_reference(),binding.combiner_reference(),binding.source());
        return std::make_shared<ActionRuntime>(binding.action_program(),sources,0,std::move(combiner),std::move(actors));
    }
};
struct Endpoint {
    Fixture fixture;
    PlayerRuntimeBinding binding=fixture.binding();
    std::shared_ptr<AnimationSourceRuntime> sources=fixture.library(binding);
    std::shared_ptr<ActionRuntime> runtime=fixture.runtime(binding,*sources);
    std::shared_ptr<ActionServices> services=std::make_shared<ActionServices>();
    std::shared_ptr<ModelSceneManager> parent=std::make_shared<ModelSceneManager>();
    MessengerManager manager=make_action_dispatch_manager(binding,runtime,sources,services,parent);
    NativeMessenger messenger;
    ControllerObjectId recipient{21,binding.action_ram_reference()};
    Endpoint() {messenger.select("ACTION",binding.model_name(),0);}
    MessengerCallResult call(std::string_view command,std::uint64_t dispatch=1,std::size_t row=0) {
        const MessengerMessage message{"ACTION",binding.model_name(),0,"unrelated-live-cache-command"};
        return manager.fallback(messenger,{dispatch,row,recipient,message},command);
    }
    void clock(std::uint64_t value) {parent->context=std::make_shared<ModelSceneContext>();parent->context->world_time=value;}
};
void query_and_execution() {
    Endpoint e;unsigned broadcasts{};e.services->broadcast=[&](const auto&){++broadcasts;};
    check(!e.call("ISCOMEND").completed && e.call("GETAPPEARANCELIST").completed && !e.runtime->active(),
        "Query behavior must remain available before context preparation without action activation.");
    check(!e.call("ISRUN").completed && !e.call("unknown").completed && !e.runtime->active(),
        "Native query priority and missing named commands must remain explicit boundaries.");
    check(!e.call("empty").completed && !e.runtime->active(),"Missing actual context must suspend before accepting named action execution.");
    e.clock(123456);check(e.call("empty").completed && e.runtime->active() && e.runtime->command_end() && broadcasts==1,
        "The retained ACTION delivery must execute its real definition after context preparation.");
    e.messenger.buffer().add_int(99);
    check(e.call("ISCOMEND",2).completed && e.messenger.buffer().booleans()==std::vector<std::uint8_t>{1} &&
        e.messenger.buffer().integers()==std::vector<std::int32_t>{99},"Live query must append its result without erasing packet prefixes.");
    e.parent->context.reset();check(e.call("NDSP",3).completed && !e.runtime->active(),"NDSP must execute without consuming a context clock.");
    check(e.call("ONFREEZE",4).completed,"Inactive freeze must preserve its original clock-free early exit.");
}
void broadcast_continuation_and_clock() {
    Endpoint e;e.clock(1000000000);unsigned deliveries{};
    e.services->try_broadcast=[&](const auto&){++deliveries;return deliveries==3;};
    check(!e.call("wait").completed && e.runtime->command_index()==1 && e.runtime->broadcast_pending(),
        "Actual WAIT command and cursor must publish before a suspended broadcast.");
    const auto timer=e.runtime->timers().at("WAIT");const auto state=e.runtime->state_identity();
    e.clock(9000000000);
    check(!e.call("wait").completed && e.runtime->command_index()==1 && e.runtime->timers().at("WAIT")==timer &&
        e.runtime->state_identity()!=state,"Broadcast retry must retain accepted command/time instead of restarting WAIT.");
    check(e.call("ISCOMEND",77).completed && e.messenger.buffer().booleans().back()==0,
        "Queries may observe selected work while its original broadcast is pending.");
    check(e.call("wait").completed && deliveries==3 && !e.runtime->broadcast_pending() && e.runtime->work().wait_state==1,
        "Completing broadcast returns the real waiting action, not a restarted command list.");
}
void earlier_broadcast_before_new_acceptance() {
    Endpoint e;e.clock(700);ActionServices missing;
    check(e.runtime->dispatch(*e.sources,missing,"empty",100).stop==ActionStop::broadcast_required,
        "Fixture must begin with a real older undelivered ACTION event.");
    std::vector<std::string> events;bool allow{};
    e.services->try_broadcast=[&](const auto& event){events.push_back(event.action);return allow;};
    check(!e.call("wait").completed && e.runtime->current_action()=="empty",
        "A new request must not become accepted while its earlier broadcast is suspended.");
    allow=true;e.clock(123456789);
    check(e.call("wait").completed && e.runtime->current_action()=="wait" &&
        events==std::vector<std::string>{"empty","empty","wait"},
        "Resume must drain the earlier event then dispatch and broadcast the newly accepted action.");
    check(e.runtime->timers().at("WAIT").saved_world_time==123456789,
        "New action timestamp must come from current context after the earlier broadcast completes.");
    Endpoint freeze;freeze.clock(900);
    check(freeze.runtime->dispatch(*freeze.sources,missing,"empty",100).stop==ActionStop::broadcast_required,
        "Freeze fixture must begin with an earlier pending event.");
    unsigned delivered{};freeze.services->try_broadcast=[&](const auto&){++delivered;return true;};
    check(freeze.call("ONFREEZE").completed && freeze.runtime->work().frozen && freeze.runtime->broadcast_pending() && delivered==0,
        "Native freeze must precede ordinary broadcast draining and leave the earlier event pending.");
}
void random_and_unsupported_boundaries() {
    Endpoint e;e.clock(33);e.services->broadcast=[](const auto&){};
    check(!e.call("random").completed && e.runtime->command_index()==0,"Missing random service must retain the reached actual command.");
    e.services->random_word=[] {return 0u;};
    check(e.call("random").completed && e.runtime->command_index()==1,"A prepared random service must resume the same command once.");
    check(!e.call("unsupported",2).completed && e.runtime->current_action()=="unsupported" && e.runtime->command_index()==0,
        "Unsupported original action commands must not be reported as successful playback.");
    check(!e.call("unsupported",2).completed && e.runtime->command_index()==0,
        "Unsupported command retries must preserve the exact stopped cursor.");
    rejects([&]{e.call("empty",3);},"A different action cannot steal an existing retained delivery.");
    rejects([&]{e.call("unsupported",2);},"An endpoint with an uncertain reentry attempt must remain faulted.");
}
void recursive_callbacks_and_owner_changes() {
    Endpoint e;e.clock(17);bool queried{};
    e.services->try_broadcast=[&](const auto&){queried=e.call("ISCOMEND",99).completed;return true;};
    check(e.call("empty").completed && queried,"Read-only ACTION queries must remain legal inside synchronous broadcasts.");
    Endpoint recursive;recursive.clock(18);
    recursive.services->try_broadcast=[&](const auto&){try {recursive.call("wait",2);}catch(const MessengerError&){}return true;};
    rejects([&]{recursive.call("empty");},"Catching an inner recursive action failure must still poison outer completion.");
    check(recursive.runtime->active() && recursive.runtime->current_action()=="empty",
        "A recursive service failure must retain already published activation/selection prefixes.");
    rejects([&]{recursive.call("empty");},"Faulted endpoint cannot replay a partially completed action.");
    Endpoint changed;changed.clock(19);changed.services->try_broadcast=[](const auto&){return false;};
    check(!changed.call("empty").completed,"Fixture must retain an actual pending delivery.");
    changed.runtime->freeze(true,20);
    rejects([&]{changed.call("empty");},"External runtime mutation must not silently redirect a suspended delivery.");
}
void retained_services_and_weak_manager() {
    Endpoint e;std::weak_ptr<AnimationSourceRuntime> sources=e.sources;std::weak_ptr<ActionServices> services=e.services;
    std::weak_ptr<ModelSceneManager> parent=e.parent;
    e.sources.reset();e.services.reset();e.parent.reset();
    check(!sources.expired() && !services.expired() && parent.expired(),
        "Adapter must retain source/service owners without a strong manager cycle.");
    check(!e.call("empty").completed,"Expired manager context must remain an explicit execution boundary.");
    e.manager={};check(sources.expired() && services.expired(),"Releasing the adapter must release its retained source/service allocations.");
}
void native_messenger_rows() {
    Endpoint e;e.clock(888);unsigned deliveries{},resolutions{};
    e.messenger.reset();
    e.services->try_broadcast=[&](const auto&){return ++deliveries>1;};
    e.messenger.register_recipient(e.recipient,"ACTION",e.binding.model_name(),0);
    e.messenger.register_recipient(e.recipient,"ACTION",e.binding.model_name(),0);
    e.messenger.select("ACTION",e.binding.model_name(),0);
    MessengerServices services;
    services.active=[](ControllerObjectId){return std::optional<std::uint8_t>{std::uint8_t{1}};};
    services.manager=[&](ControllerObjectId){++resolutions;return e.manager;};
    auto call=e.messenger.begin_send("wait");
    auto result=e.messenger.resume(call,services);
    check(result.stop==MessengerStop::service_required && result.recipients_completed==0 && deliveries==1 && resolutions==1,
        "Native messenger must retain the ACTION callback at its incomplete first row.");
    result=e.messenger.resume(call,services);
    check(result.stop==MessengerStop::completed && result.recipients_completed==2 && deliveries==2 && resolutions==2,
        "Native resume must complete retained row and alias occurrence without redelivering the action event.");
    check(e.runtime->command_index()==1 && e.runtime->work().wait_state==1,
        "Duplicate registration rows must preserve the actual selected action identity and waiting cursor.");
}
}
int main() {
    try {
        query_and_execution();broadcast_continuation_and_clock();earlier_broadcast_before_new_acceptance();
        random_and_unsupported_boundaries();recursive_callbacks_and_owner_changes();retained_services_and_weak_manager();native_messenger_rows();
        std::cout<<"ACTION dispatch checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"ACTION dispatch after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
