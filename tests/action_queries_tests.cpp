#include "dw4/action_queries.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool condition,const char* reason) { ++checks; if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks; try { operation(); } catch (const MessengerError&) { return; }
    throw std::runtime_error(reason);
}
AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
struct Fixture : pf::RuntimeFixture {
    std::vector<std::uint32_t> definitions;
    std::map<std::uint32_t,std::string> types;
    std::map<std::uint32_t,std::vector<std::uint16_t>> float_slots;
    Fixture() {
        const auto wait=command("beAction2WAITCHECK","FRAME",{3}); scalar(wait,3,std::bit_cast<std::uint32_t>(2.f));
        const auto unknown=command("beAction2WAITCHECK","unknown",{3});
        const auto motion=command("beAction2WAITCHECK","MOTION",{3});
        const auto joint=command("beAction2WAITCHECK","JOINT",{3});
        const auto hide=command("beAction2FADESET","HIDE",{3,4});
        const auto unsupported=command("UnimplementedAction","",{});
        define("",{}); define("empty",{}); define("wait",{wait}); define("unknown-wait",{unknown});
        define("motion-wait",{motion}); define("joint-wait",{joint}); define("hide",{hide});
        define("unsupported",{unsupported}); define("NDSP",{},false);
        scalar(action,9,list(pf::action_data_list,definitions));
    }
    std::uint32_t command(std::string type,std::string name,std::vector<std::uint16_t> floats) {
        const auto ref=add(pf::sequence_type); text(ref,2,name); scalar(ref,3,0); scalar(ref,4,0);
        types[ref]=std::move(type); float_slots[ref]=std::move(floats); return ref;
    }
    void define(const std::string& name,const std::vector<std::uint32_t>& commands,bool present=true) {
        const auto ref=add(pf::sequence_data_type); text(ref,2,name);
        scalar(ref,3,present ? list(pf::named_list,commands) : pf::null_ref);
        types[ref]="beAction2Data"; definitions.push_back(ref);
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        for (const auto& [reference,type]:types) {
            auto& object=doc.objects[doc.references[reference].value_index];
            auto descriptor=doc.metadata.object_types[object.type_index]; descriptor.name=type; descriptor.parent_index=pf::object;
            if (const auto found=float_slots.find(reference);found!=float_slots.end()) for (const auto slot:found->second) {
                const_cast<IgbFieldValue*>(doc.field(object,slot))->type_index=8;
                for (auto& field:descriptor.fields) if (field.slot==slot) field.type_index=8;
            }
            object.type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back(std::move(descriptor));
        }
        return doc;
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0,std::size_t occurrence=3) const {
        return decode_player_runtime_binding(document(),runtime_request(instance),"synthetic/player.igb",occurrence);
    }
    AnimationSourceRuntime library(const PlayerRuntimeBinding& binding) const {
        // Four independent identity tracks; only activation validates them here.
        std::array<pf::Bytes,14> streams; streams[0].resize(7); streams[1]={1,1,1,1};
        streams[9]={64}; streams[10]={100}; streams[13]={0,0,0,0};
        for (const auto component:{3u,10u,17u,24u}) streams[0][component/4]|=std::uint8_t(1u<<(6-2*(component%4)));
        pf::Bytes bytes(80); pf::word(bytes,0,0x002abf84); pf::word(bytes,4,4);
        pf::word(bytes,8,std::bit_cast<std::uint32_t>(1.f)); pf::word(bytes,12,std::bit_cast<std::uint32_t>(1.f)); pf::word(bytes,16,1);
        for (std::size_t i=0;i<streams.size();++i) pf::word(bytes,20+4*i,static_cast<std::uint32_t>(streams[i].size()));
        for (const auto i:{3,8,12,2,7,11,0,1,4,5,6,9,10,13}) pf::append(bytes,streams[i]);
        AnimationClip clip; clip.info.supported=true; clip.info.reference_index=binding.action_program().animations().front().first;
        clip.info.name="waitS"; clip.info.duration_seconds=1; clip.info.duration_ticks=1000000000; clip.info.track_count=4;
        clip.info.source_reference=9000; clip.info.skeleton_reference=skeleton; clip.channels=decode_enbaya(bytes);
        clip.bone_references=bones; clip.constant_translations.resize(4); clip.use_sample_translation.resize(4);
        AnimationSourceRuntime result(binding.source(),{std::move(clip)}); result.initialize_action_offset(); return result;
    }
    std::shared_ptr<ActionRuntime> runtime(const PlayerRuntimeBinding& binding,const AnimationSourceRuntime& sources,
        bool actors=true,std::optional<std::uint32_t> combiner_reference={},bool reverse_actors=false) const {
        AnimationCombiner combiner(sources,std::vector<AnimationMatrix>(4,identity()),decode_animation_hierarchy(document(),skeleton),
            std::vector<AnimationMatrix>(4,identity()),std::vector<AnimationMatrix>(2,identity()),combiner_reference.value_or(binding.combiner_reference()));
        const auto ram_no=static_cast<std::int32_t>(binding.instance_index());
        if (!actors) return std::make_shared<ActionRuntime>(binding.action_program(),sources,ram_no,std::move(combiner));
        auto group=decode_action_actor_group(document(),binding.action_ram_reference(),binding.combiner_reference(),binding.source());
        if (reverse_actors) {
            auto reordered=group.definitions(); std::reverse(reordered.begin(),reordered.end());
            group=ActionActorGroup(group.source(),group.ram_reference(),group.combiner_reference(),std::move(reordered));
        }
        return std::make_shared<ActionRuntime>(binding.action_program(),sources,ram_no,std::move(combiner),std::move(group));
    }
};
ControllerObjectId id(const PlayerRuntimeBinding& binding) {
    return {static_cast<std::uint32_t>(binding.source_occurrence()),binding.action_ram_reference()};
}
MessengerCallResult query(MessengerManager& manager,NativeMessenger& messenger,ControllerObjectId recipient,
    std::string_view command="ISCOMEND") {
    // Fallback receives the originally captured command separately from the live record.
    const MessengerMessage message{"changed-category",std::nullopt,-17,"changed-command"};
    return manager.fallback(messenger,{1,0,recipient,message},command);
}
struct Services : ActionServices {
    unsigned broadcasts{};
    Services() { broadcast=[this](const auto&) { ++broadcasts; }; }
};
void live_query_and_boundaries() {
    Fixture fixture; const auto binding=fixture.binding(); auto sources=fixture.library(binding);
    auto runtime=fixture.runtime(binding,sources); auto manager=make_action_query_manager(binding,runtime);
    NativeMessenger messenger; messenger.select("ACTION",binding.model_name(),0); messenger.buffer().add_bool(0x7f);
    const MessengerDelivery delivery{1,0,id(binding),messenger.selected()};
    check(manager.structured(messenger,delivery).value==0 && messenger.buffer().booleans()==std::vector<std::uint8_t>{0x7f},
        "Original structured hook leaves the buffer untouched and selects fallback");
    check(!runtime->query_command_end() && !runtime->command_end() && !runtime->broadcast_pending(),
        "Fresh work availability differs from a false result and carries no pending event");
    check(!query(manager,messenger,id(binding)).completed && messenger.buffer().booleans().size()==1,
        "Missing selected work suspends without appending a fabricated false result");
    Services services;
    runtime->dispatch(sources,services,"",100);
    check(runtime->current_action().empty() && runtime->query_command_end()==true,"An empty action name is still selected work");
    runtime->dispatch(sources,services,"wait",200);
    check(runtime->command_index()==1 && runtime->query_command_end()==false,"Outstanding wait wins over an end cursor");
    check(query(manager,messenger,id(binding)).completed && messenger.buffer().booleans().back()==0,
        "Captured manager observes the live wait state and original fallback command");
    runtime->update(sources,services,1000000000);
    check(runtime->query_command_end()==true,"Explicit update resolves the frame wait before the next query");
    runtime->dispatch(sources,services,"unknown-wait",1000000100);
    check(runtime->query_command_end()==true,"A yielding unknown wait with state zero can still report command end");
    runtime->dispatch(sources,services,"unsupported",1000000200);
    check(runtime->query_command_end()==false,"An unexecuted command remains before the end cursor");
    runtime->dispatch(sources,services,"hide",1000000300); runtime->freeze(true,1000000400);
    const auto before_matrices=runtime->combiner().cached_matrices(); const auto before_clock=runtime->node_clock();
    const auto before_count=services.broadcasts;
    check(!runtime->visible() && runtime->query_command_end()==true,"Visibility and freeze are not ISCOMEND gates");
    const auto result=query(manager,messenger,id(binding));
    check(result.completed && result.value==1 && messenger.buffer().booleans()==std::vector<std::uint8_t>({0x7f,0,1}),
        "The exact query appends its bool after the existing prefix and returns handled one");
    check(runtime->combiner().cached_matrices()==before_matrices && runtime->node_clock().time_scale==before_clock.time_scale &&
        runtime->node_clock().time_bias==before_clock.time_bias && runtime->node_clock().local_time==before_clock.local_time &&
        services.broadcasts==before_count && runtime->work().frozen && runtime->command_index()==1,
        "Query neither samples, advances clocks, changes work nor broadcasts");
    for (const auto command:{"iscomend","ISCOMEND ","NDSP","ONFREEZE","unimplemented"})
        check(!query(manager,messenger,id(binding),command).completed && messenger.buffer().booleans().size()==3,
            "Other commands remain explicit controller service boundaries");
    runtime->dispatch(sources,services,"NDSP",1000000500);
    check(!runtime->active() && !runtime->query_command_end() && !query(manager,messenger,id(binding)).completed,
        "NDSP removes selected query work while the retained manager remains available");
    ActionServices unavailable;
    check(runtime->dispatch(sources,unavailable,"empty",1000000600).stop==ActionStop::broadcast_required && runtime->broadcast_pending(),
        "The test retains a real pending broadcast service boundary");
    check(query(manager,messenger,id(binding)).completed && messenger.buffer().booleans().back()==1,
        "Query reads reactivated work without delivering its pending broadcast");
    check(runtime->resume(sources,unavailable,1000000700).stop==ActionStop::broadcast_required && runtime->broadcast_pending(),
        "ISCOMEND leaves the pending broadcast for its owning service");
    check(runtime->resume(sources,services,1000000800).stop==ActionStop::end && !runtime->broadcast_pending(),
        "Only completed delivery clears the live pending-broadcast inspection");
}
void identity_and_lifetime() {
    Fixture fixture; const auto binding=fixture.binding(); auto sources=fixture.library(binding);
    auto runtime=fixture.runtime(binding,sources);
    rejects([&]{make_action_query_manager(binding,{});},"Null runtime ownership rejects");
    rejects([&]{make_action_query_manager(binding,fixture.runtime(binding,sources,false));},"Query requires an actual actor RAM owner");
    rejects([&]{make_action_query_manager(binding,fixture.runtime(binding,sources,true,binding.combiner_reference()+1));},
        "Foreign combiner identity rejects even with matching animation data");
    rejects([&]{make_action_query_manager(binding,fixture.runtime(binding,sources,true,{},true));},"Actor occurrence order is part of ownership");
    const auto foreign=decode_player_runtime_binding(fixture.document(),fixture.runtime_request(),"synthetic/other.igb",3);
    auto foreign_sources=fixture.library(foreign);
    rejects([&]{make_action_query_manager(binding,fixture.runtime(foreign,foreign_sources));},"Matching local references in another source do not establish ownership");
    auto renamed=fixture; renamed.text(renamed.model,2,"Other player"); renamed.text(renamed.action,2,"Other player");
    const auto renamed_binding=renamed.binding();
    rejects([&]{make_action_query_manager(binding,renamed.runtime(renamed_binding,sources));},"The actual ACTION info name must match the binding");
    auto other_info=fixture; const auto next_info=other_info.add(pf::action_info); other_info.records[next_info]=other_info.records[other_info.action];
    other_info.action=next_info; other_info.contents(other_info.info_reference,{other_info.model,next_info});
    const auto info_binding=other_info.binding();
    rejects([&]{make_action_query_manager(binding,other_info.runtime(info_binding,sources));},"An identical copied program under another info reference is a different owner");
    const auto second=fixture.binding(1); auto other=fixture.runtime(second,sources);
    rejects([&]{make_action_query_manager(binding,other);},"Another actual RAM cannot answer through this binding");
    if constexpr (sizeof(std::size_t)>sizeof(std::uint32_t)) {
        const auto wide=fixture.binding(0,std::size_t(std::numeric_limits<std::uint32_t>::max())+1);
        rejects([&]{make_action_query_manager(wide,runtime);},"Source occurrence narrowing is checked");
    }
    auto manager=make_action_query_manager(binding,runtime); NativeMessenger messenger; messenger.select("ACTION",{},0);
    rejects([&]{query(manager,messenger,{4,binding.action_ram_reference()});},"Another loaded source occurrence is a different recipient");
    rejects([&]{query(manager,messenger,id(second));},"Delivery cannot substitute another RAM");
    *runtime=*other;
    rejects([&]{query(manager,messenger,id(binding));},"Reassigning the shared runtime cannot silently change its owner");
    check(messenger.buffer().booleans().empty(),"Identity failures occur before result mutation");
    runtime=fixture.runtime(binding,sources); manager=make_action_query_manager(binding,runtime);
    const std::weak_ptr<ActionRuntime> retained=runtime; runtime.reset();
    check(!retained.expired(),"Manager retains the real shared runtime after the caller releases its pointer");
    Services services; retained.lock()->dispatch(sources,services,"empty",1);
    check(query(manager,messenger,id(binding)).completed && messenger.buffer().booleans().back()==1,"Retained live ownership remains queryable");
    manager={}; check(retained.expired(),"Releasing the manager releases its owned runtime");
}
void state_tokens() {
    Fixture fixture; const auto binding=fixture.binding(); auto sources=fixture.library(binding);
    auto runtime=fixture.runtime(binding,sources),other=fixture.runtime(binding,sources);
    const auto initial=runtime->state_identity();
    check(initial && other->state_identity() && initial!=other->state_identity(),"Separately prepared states have distinct retained identities");
    auto copy=*runtime;
    check(copy.state_identity()==initial,"A default copy preserves exact state identity");
    copy.freeze(true,1);
    check(copy.state_identity()!=initial && runtime->state_identity()==initial,"A copied state's mutation does not change the original token");
    *other=*runtime;
    check(other->state_identity()==initial,"Default assignment preserves the exact copied state token");
    *other=copy;
    check(other->state_identity()==copy.state_identity() && other->state_identity()!=initial,"Same-RAM replacement exposes its different state token");
    const auto changed=[&](auto&& operation,const char* reason) {
        const auto before=runtime->state_identity(); operation();
        check(runtime->state_identity() && runtime->state_identity()!=before,reason);
    };
    Services services;
    changed([&]{runtime->freeze(true,1);},"Even inactive freeze is an explicit mutating entry point");
    changed([&]{runtime->dispatch(sources,services,"empty",100);},"Dispatch changes identity before executing its state effects");
    changed([&]{runtime->dispatch(sources,services,"empty",101);},"Repeated same-name dispatch cannot masquerade as unchanged pending state");
    const ModelActionRequest request{binding.source(),7,binding.model_name(),0,"hide"};
    changed([&]{runtime->dispatch(sources,services,request,102);},"The qualified dispatch overload shares token mutation semantics");
    changed([&]{runtime->update(sources,services,103);},"Update refreshes state identity through candidate copies");
    changed([&]{runtime->resume(sources,services,104);},"Resume refreshes identity even when its current work is already complete");
    changed([&]{runtime->freeze(true,105);},"Direct freeze refreshes identity through its copied candidate");
    changed([&]{runtime->dispatch(sources,services,"OFFREEZE",106);},"Native freeze dispatch refreshes the delegated state token");
    changed([&]{runtime->pose(sources,binding.skin_binding().skin(),107);},"Pose sampling refreshes state identity");
    changed([&]{runtime->traverse_actor(sources,0,identity(),identity(),108);},"Actor traversal refreshes state identity");
    const auto before_query=runtime->state_identity();
    auto manager=make_action_query_manager(binding,runtime); NativeMessenger messenger; messenger.select("ACTION",binding.model_name(),0);
    check(query(manager,messenger,id(binding)).completed && runtime->bone_matrix("Bone 1").has_value() &&
        runtime->query_command_end()==true && runtime->state_identity()==before_query,
        "Manager creation, ISCOMEND and cached bone queries retain state identity");
    bool invalid=false;
    try { runtime->dispatch(sources,services,"ISCOMEND",109); } catch (const ActionError&) { invalid=true; }
    check(invalid && runtime->state_identity()==before_query,"Rejected native-query dispatch does not invalidate state before entering mutation");
    auto foreign=fixture.library(binding); invalid=false;
    try { runtime->update(foreign,services,110); } catch (const AnimationError&) { invalid=true; }
    check(invalid && runtime->state_identity()==before_query,"Foreign animation library validation precedes identity mutation");
    Services callback_services;
    callback_services.broadcast=[&](const auto&) {
        const auto during=runtime->state_identity();
        check(during!=before_query,"Service callbacks see the already refreshed token");
        bool reentrant=false;
        try { runtime->freeze(true,111); } catch (const ActionError&) { reentrant=true; }
        check(reentrant && runtime->state_identity()==during,"Reentrancy rejection precedes replacement of the accepted operation's token");
    };
    runtime->dispatch(sources,callback_services,"empty",111);
    auto retained=runtime->state_identity(); const std::weak_ptr<const void> old=retained;
    runtime->resume(sources,services,112);
    check(!old.expired() && runtime->state_identity()!=retained,"Retained previous identities survive mutation and cannot be reused");
    retained.reset(); check(old.expired(),"Unused state identities release after their last explicit owner");
}
void messenger_suspension() {
    Fixture fixture; const auto first=fixture.binding(),second=fixture.binding(1); auto sources=fixture.library(first);
    auto runtime0=fixture.runtime(first,sources),runtime1=fixture.runtime(second,sources);
    auto manager0=make_action_query_manager(first,runtime0),manager1=make_action_query_manager(second,runtime1);
    Services action_services; runtime0->dispatch(sources,action_services,"empty",10);
    NativeMessenger messenger;
    messenger.register_recipient(id(first),"ACTION",first.model_name(),0);
    messenger.register_recipient(id(second),"ACTION",second.model_name(),1);
    messenger.register_recipient(id(first),"ACTION",first.model_name(),0);
    unsigned resolutions{},activity_reads{};
    MessengerServices services;
    services.active=[&](auto)->std::optional<std::uint8_t> { ++activity_reads; return std::uint8_t(0); };
    services.manager=[&](auto recipient) { ++resolutions; return recipient==id(first) ? manager0 : manager1; };
    messenger.select("ACTION",first.model_name(),-1); messenger.buffer().add_bool(0x23);
    auto dispatch=messenger.begin_send("ISCOMEND");
    auto result=messenger.resume(dispatch,services);
    check(result.stop==MessengerStop::service_required && result.recipients_completed==1 && resolutions==2 && activity_reads==0 &&
        messenger.buffer().booleans()==std::vector<std::uint8_t>({0x23,1}),
        "A wildcard query stops at unavailable work after committing the first recipient once");
    runtime1->dispatch(sources,action_services,"wait",20);
    result=messenger.resume(dispatch,services);
    check(result.stop==MessengerStop::completed && resolutions==3 &&
        messenger.buffer().booleans()==std::vector<std::uint8_t>({0x23,1,0,1}),
        "Resume uses the captured live manager then continues duplicate registration order without replay");
}
// Reachable states emitted for the ignored raw-instruction comparison. This
// does not manufacture restored null lists or impossible cursor values.
void oracle_rows() {
    Fixture fixture;
    for (std::uint32_t instance=0;instance<2;++instance) for (unsigned profile=0;profile<14;++profile) for (unsigned prefix=0;prefix<2;++prefix) {
        const auto binding=fixture.binding(instance); auto sources=fixture.library(binding); auto runtime=fixture.runtime(binding,sources);
        Services services; ActionServices unavailable;
        const std::array<const char*,14> actions{"","empty","empty","wait","wait","unknown-wait","unsupported","","empty","empty","hide","hide","joint-wait","empty"};
        if (profile) runtime->dispatch(sources,profile==13 ? unavailable : services,actions[profile],100);
        if (profile==2 || profile==11) runtime->freeze(true,200);
        if (profile==4) runtime->update(sources,services,1000000000);
        if (profile==8 || profile==9) runtime->dispatch(sources,services,"NDSP",300);
        if (profile==9) runtime->dispatch(sources,services,"empty",400);
        auto manager=make_action_query_manager(binding,runtime); NativeMessenger messenger; messenger.select("ACTION",binding.model_name(),static_cast<std::int32_t>(instance));
        if (prefix) { messenger.buffer().add_bool(0x7f); messenger.buffer().add_bool(0x23); }
        const auto value=runtime->query_command_end(); const auto result=query(manager,messenger,id(binding));
        std::size_t count{}; if (value.has_value()) for (const auto& action:runtime->program().actions()) if (action.name==runtime->current_action()) { count=action.commands.size(); break; }
        std::cout<<instance<<' '<<profile<<' '<<prefix<<' '<<runtime->active()<<' '<<value.has_value()<<' '<<runtime->work().wait_state<<' '
            <<runtime->command_index()<<' '<<count<<' '<<result.completed<<' '<<result.value<<' '<<messenger.buffer().booleans().size();
        for (const auto byte:messenger.buffer().booleans()) std::cout<<' '<<unsigned(byte);
        std::cout<<'\n';
    }
}
void original(const std::filesystem::path& root) {
    const auto resources=MountedResources::open(root); const std::string source="_common/p_agumon.igb";
    const auto doc=read_igb_document(resources.read(source)); const auto skin=decode_igb_skinning(doc,5665);
    std::vector<AnimationClip> clips;
    for (const auto& info:list_igb_animations(doc,skin)) clips.push_back(decode_igb_animation(doc,skin,info.reference_index));
    AnimationSourceRuntime sources(source,std::move(clips)); sources.initialize_action_offset();
    Services services;
    for (std::uint32_t i=0;i<4;++i) {
        const auto binding=decode_player_runtime_binding(doc,{1386,546,i,1},source,17);
        auto runtime=std::make_shared<ActionRuntime>(decode_action_actor_runtime(doc,binding.action_program(),sources,static_cast<std::int32_t>(i)));
        auto manager=make_action_query_manager(binding,runtime); NativeMessenger messenger; messenger.select("ACTION",binding.model_name(),static_cast<std::int32_t>(i));
        check(!query(manager,messenger,id(binding)).completed,"Original fresh RAM requires selected work");
        const std::array<const char*,4> waits{"WAITD1","WAITS1","WAITA1","WAITG1"};
        runtime->dispatch(sources,services,waits[i],1000000000);
        const auto expected=runtime->command_end(); runtime->freeze(true,1000000001);
        check(query(manager,messenger,id(binding)).completed && messenger.buffer().booleans().back()==std::uint8_t(expected),
            "Each supplied ACTION RAM answers from its actual live selected work while frozen");
        const auto before=messenger.buffer().booleans(); runtime->dispatch(sources,services,"NDSP",1000000002);
        check(!query(manager,messenger,id(binding)).completed && messenger.buffer().booleans()==before,
            "Original NDSP removes work without appending an unavailable query value");
        runtime->dispatch(sources,services,waits[i],1000000003);
        check(query(manager,messenger,id(binding)).completed,"Original actor RAM reactivation remains visible through the retained manager");
    }
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        if (args.size()==2 && args[1]=="--oracle") { oracle_rows(); return 0; }
        live_query_and_boundaries(); identity_and_lifetime(); state_tokens(); messenger_suspension();
        if (args.size()==2) original(args[1]);
        else if (args.size()!=1) throw std::runtime_error("Usage: dw4_action_queries_tests [EXTRACTED_ROOT]");
        std::cout<<checks<<" action query checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Action query failure after "<<checks<<" checks: "<<error.what()<<'\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
#else
int main(int argc,char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}
