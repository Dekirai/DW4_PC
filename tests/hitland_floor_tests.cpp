#include "dw4/hitland_floor.hpp"
#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/actor_geometry.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_hit_land_handler.hpp"
#include "dw4/assets.hpp"
#include "hitland_scene_fixture.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<ModelSceneList> objects(std::vector<std::shared_ptr<ModelSceneObject>> values={}) {
    auto result=std::make_shared<ModelSceneList>("igObjectList");
    result->count=result->capacity=static_cast<std::int32_t>(values.size());
    result->backing=std::make_shared<ModelSceneList::Storage>(std::move(values));return result;
}
std::shared_ptr<HitLandTraversalManager> manager_for(std::shared_ptr<ModelHitLandGeometryInfo> info={}) {
    auto result=std::make_shared<HitLandTraversalManager>();
    result->infos=objects(info ? std::vector<std::shared_ptr<ModelSceneObject>>{info}:
        std::vector<std::shared_ptr<ModelSceneObject>>{});
    result->query=std::make_shared<HitLandQuery>();result->visitor=std::make_shared<HitLandIntersectionVisitor>();
    result->scratch=std::make_shared<HitLandTraversalScratch>();result->scratch->segments=objects();
    result->scratch->indices=std::make_shared<HitLandIndexList>();return result;
}
std::shared_ptr<HitLandCandidate> hit(float y,std::int32_t index=0,std::optional<std::string> name="floor") {
    auto result=std::make_shared<HitLandCandidate>();result->payload.position={0,y,0};
    result->child_index=index;result->name=std::move(name);return result;
}
void candidates(HitLandQuery& query,std::initializer_list<float> heights) {
    std::vector<std::shared_ptr<ModelSceneObject>> values;
    for(const auto y:heights)values.push_back(hit(y,static_cast<std::int32_t>(values.size())));
    query.candidates=objects(std::move(values));query.candidate_count=static_cast<std::int32_t>(heights.size());
}
void arguments(NativeMessenger& messenger,std::array<float,3> position={0,9,0},float width=1,float up=2,float down=2) {
    messenger.select("HITLANDMODEL",{},-1);
    auto& packet=messenger.buffer();packet.add_vector(position);packet.add_float(width);
    packet.add_float(up);packet.add_float(down);
}
struct Fixture {
    hitland_scene_fixture::Fixture source;
    std::shared_ptr<ActorSceneResource> scene=source.scene();
    std::shared_ptr<ModelHitLandGeometryInfo> info=scene->models().hit_land_geometry_info(source.info);
    std::shared_ptr<ActorSceneNode> node=scene->mutable_node(source.source.owner);
    std::shared_ptr<HitLandTraversalManager> manager;
    std::shared_ptr<ActorGeometryAttribute> attribute=std::make_shared<ActorGeometryAttribute>();
    std::shared_ptr<ActorGeometryBuffer> data=std::make_shared<ActorGeometryBuffer>();
    unsigned maps{},unmaps{};
    Fixture() {
        info->kind=1;info->name="synthetic floor";
        info->check_switch->child_list_owner()->replace({node});
        *info->check_switch->switch_mask_owner()->backing={1};
        auto box=std::make_shared<CollisionBox>();box->minimum={-16,8,-16};box->maximum={32,8,32};node->set_box_owner(box);
        // One upward-facing authored triangle; all four +/-1 corner rays cross it.
        data->bytes=player_runtime_fixture::floats({-16,8,-16,-16,8,32,32,8,-16});
        attribute->primitive_mode=3;attribute->primitive_count=1;attribute->vertices=std::make_shared<ActorVertexArray>();
        attribute->vertices->map_service=[this](auto&,ActorVertexMapping& mapping,auto) {
            ++maps;mapping.position={data,0};mapping.stride=12;
        };
        attribute->vertices->unmap_service=[this](auto&,auto&,auto) {++unmaps;};
        node->set_attribute_list_owner(objects({attribute}));manager=manager_for(info);
        register_hitland_node_type(manager->visitor,node);
    }
};
void selection_passes() {
    HitLandQuery query;
    candidates(query,{7,9,8});auto result=original_hitland_floor_selection(query,8,3,3);
    check(result.found && result.index==1 && result.height==9 && result.in_range && result.adjusted,
        "Window pass did not choose its greatest eligible rounded delta.");
    candidates(query,{1,2});result=original_hitland_floor_selection(query,1e8f,1e9f,1e9f);
    check(result.index==0 && result.height==1 && result.in_range && !result.adjusted,
        "Window pass compared raw heights instead of retaining an equal rounded delta.");
    result=original_hitland_floor_selection(query,1e8f,0,0);
    check(result.index==0 && result.height==1 && !result.in_range && !result.adjusted,
        "Below pass failed its rounded-distance tie rule.");
    candidates(query,{2,7,6});result=original_hitland_floor_selection(query,9,0,0);
    check(result.index==1 && result.height==7 && !result.in_range && !result.adjusted,
        "Below pass did not select the closest strictly lower height.");
    candidates(query,{20,10,10,12});result=original_hitland_floor_selection(query,0,0,0);
    check(result.index==2 && result.height==10 && !result.in_range && result.adjusted,
        "Minimum pass did not choose its later equal minimum.");
    candidates(query,{9});result=original_hitland_floor_selection(query,9,0,0);
    check(result.index==0 && result.adjusted && !result.in_range,"Exact-height equality entered a strict range window.");
    candidates(query,{8});result=original_hitland_floor_selection(query,9,1,20);
    check(!result.in_range && !result.adjusted,"Exact upper-range boundary was treated as inclusive.");
    candidates(query,{10});result=original_hitland_floor_selection(query,9,20,1);
    check(!result.in_range && result.adjusted,"Exact lower-range boundary was treated as inclusive.");
    const auto tiny=std::bit_cast<float>(std::uint32_t{1});
    candidates(query,{-tiny});result=original_hitland_floor_selection(query,0,0,0);
    check(result.adjusted && result.height==-tiny,"Adjustment compared raw coordinates instead of the flushed single subtraction.");
    candidates(query,{0.f,-0.f});result=original_hitland_floor_selection(query,0,0,0);
    check(result.index==1 && std::bit_cast<std::uint32_t>(result.height)==0x80000000u,
        "Minimum tie lost the later candidate's negative-zero height.");
}
void physical_selection_and_unused_values() {
    HitLandQuery query;query.candidates.reset();const auto nan=std::numeric_limits<float>::quiet_NaN();
    for(const auto count:{0,-1,std::numeric_limits<std::int32_t>::min()}) {
        query.candidate_count=count;const auto result=original_hitland_floor_selection(query,nan,nan,nan);
        check(!result.found && !result.in_range && !result.adjusted && result.height==-1,
            "Nonpositive count consumed unused candidate storage or arithmetic.");
    }
    candidates(query,{8,10});query.candidates->count=0;query.candidates->capacity=-1;
    check(original_hitland_floor_selection(query,9,4,4).index==1,"Reduction used list size/capacity instead of physical candidate count.");
    query.candidate_count=1;(*query.candidates->backing)[1].reset();
    check(original_hitland_floor_selection(query,9,4,4).height==8,"Reduction validated an unreached spare candidate.");
    (*query.candidates->backing)[0].reset();
    rejects([&]{original_hitland_floor_selection(query,9,4,4);},"Reduction accepted a reached null candidate.");
    (*query.candidates->backing)[0]=std::make_shared<HitLandSegment>();
    rejects([&]{original_hitland_floor_selection(query,9,4,4);},"Reduction accepted an unsupported candidate layout.");
    query.candidates->backing.reset();
    rejects([&]{original_hitland_floor_selection(query,9,4,4);},"Reduction accepted missing physical backing.");
    candidates(query,{nan});rejects([&]{original_hitland_floor_selection(query,9,4,4);},"Used nonfinite height passed the documented finite profile.");
}
void guards_and_construction() {
    for(unsigned mode=0;mode<3;++mode) {
        NativeMessenger messenger;messenger.select("HITLANDMODEL",{},-1);
        if(mode==0){arguments(messenger);messenger.buffer().add_bool(7);}
        if(mode==1)messenger.buffer().add_float(1);
        if(mode==2)messenger.buffer().add_vector({1,2,3});
        messenger.buffer().add_string("untouched");const auto old=messenger.buffer_owner();
        HitLandFloorInvocation call({});const auto result=call.resume(messenger);
        check(result.completed && result.value==1 && call.completed() && !call.faulted() && messenger.buffer_owner()==old &&
            messenger.buffer().strings()==std::vector<std::optional<std::string>>{"untouched"},
            "Argument guard consumed a null manager or rewrote the packet.");
    }
    for(unsigned count=1;count<3;++count) {
        NativeMessenger messenger;messenger.select("HITLANDMODEL",{},-1);messenger.buffer().add_vector({1,2,3});
        for(unsigned i=0;i<count;++i)messenger.buffer().add_float(float(i));
        const auto manager=manager_for();manager->query->flags=123;manager->query->candidate_count=19;
        HitLandFloorInvocation call(manager);
        rejects([&]{call.resume(messenger);},"Short nonempty float list silently skipped required physical reads.");
        check(call.faulted() && manager->query->flags==123 && manager->query->candidate_count==19 &&
            messenger.buffer().vectors()==std::vector<std::array<float,3>>{{1,2,3}},"Malformed argument fault occurred after query reset/publication.");
    }
    for(const auto width:{2.f,-2.f}) {
        NativeMessenger messenger;arguments(messenger,{3,9,4},width);
        const auto manager=manager_for();manager->infos->backing.reset();
        HitLandFloorInvocation call(manager);check(call.resume(messenger).completed,"Empty info traversal did not finish.");
        const auto query=manager->query;
        check(query->flags==1 && query->segment_count==4 && query->maximum==std::array<float,3>{3+width,1000,4+width} &&
            query->minimum==std::array<float,3>{3-width,-100,4-width},"CHECKLAND normalized negative width, changed ray bounds or copied integer flags.");
        const std::array<std::array<float,3>,4> origins{{{3+width,1000,4+width},{3-width,1000,4+width},
            {3+width,1000,4-width},{3-width,1000,4-width}}};
        for(std::int32_t i=0;i<4;++i) {
            const auto segment=std::dynamic_pointer_cast<HitLandSegment>(hitland_physical_object(query->segments,i));
            check(segment && segment->origin==origins[std::size_t(i)] && segment->direction==std::array<float,3>{0,-1,0} && segment->length==1100,
                "CHECKLAND changed corner order or absolute vertical segment endpoints.");
        }
        check(messenger.buffer().booleans()==std::vector<std::uint8_t>({0,0,0}) &&
            messenger.buffer().vectors()==std::vector<std::array<float,3>>{{3,-1,4}},"No-hit response did not publish original sentinel/flags.");
    }
}
void packet_preservation() {
    for(const bool successful:{false,true}) {
        Fixture fixture;NativeMessenger messenger;arguments(messenger);
        auto& packet=messenger.buffer();packet.add_int(0x1234);packet.add_int(-7);packet.add_int64(0x123456789ll);
        packet.add_string("previous");packet.add_string({});packet.add_vector({8,9,10});
        packet.add_object({});packet.add_matrix({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});packet.set_enabled_only(255);
        if(!successful)fixture.manager->infos->count=0;
        HitLandFloorInvocation call(fixture.manager);check(call.resume(messenger).completed,"Actual floor query did not finish.");
        check(packet.floats()==std::vector<float>({1,2,2}) && packet.integers64()==std::vector<std::int64_t>{0x123456789ll} &&
            packet.objects().size()==1 && packet.matrices().size()==1 && packet.enabled_only()==255,
            "Floor publication cleared unrelated typed lists or enabled-only gate.");
        check(packet.integers()==(successful ? std::vector<std::int32_t>{0}:std::vector<std::int32_t>{0x1234,-7}),
            "Floor metadata integer replacement differs between hit and no-hit paths.");
        check(packet.strings().size()==(successful ? 3u:2u) && packet.strings()[0]=="previous" && !packet.strings()[1],
            "Floor result cleared or reordered existing nullable strings.");
        check(packet.booleans()==(successful ? std::vector<std::uint8_t>{1,0,1}:std::vector<std::uint8_t>{0,0,0}) &&
            packet.vectors()==std::vector<std::array<float,3>>{{0,successful ? 8.f:-1.f,0}},
            "Floor result failed to clear old vectors or publish exact booleans.");
        if(successful)check(fixture.manager->query->candidate_count==4 && fixture.maps>0 && fixture.maps==fixture.unmaps &&
            packet.strings().back()=="synthetic floor","Real four-ray triangle traversal failed candidate/metadata publication.");
    }
    NativeMessenger messenger;const auto tiny=std::bit_cast<float>(std::uint32_t{1});arguments(messenger,{tiny,9,-0.f});
    HitLandFloorInvocation call(manager_for());check(call.resume(messenger).completed,"Tiny-coordinate no-hit response failed.");
    check(std::bit_cast<std::uint32_t>(messenger.buffer().vectors()[0][0])==1 &&
        std::bit_cast<std::uint32_t>(messenger.buffer().vectors()[0][2])==0x80000000u,"Result scalar X/Z copies flushed or normalized original words.");
}
void retained_query_and_current_packet() {
    Fixture fixture;NativeMessenger messenger;arguments(messenger,{0,9,0});
    auto services=std::make_shared<HitLandIntersectionServices>();const auto captured=fixture.manager->query;
    const auto old_packet=messenger.buffer_owner();unsigned visits{};
    // Explicit visitor seam tests suspension/owner contracts independently of
    // the real mapped-triangle integration above and below.
    services->node_dispatch=[&](const auto& visitor,const auto&) {
        ++visits;check(visitor->query==captured,"Retained traversal adopted a later manager query.");
        if(visits==1) {candidates(*captured,{8});return MessengerCallResult{false,0,"Retained floor visitor"};}
        return MessengerCallResult{true,0,{}};
    };
    HitLandFloorInvocation call(fixture.manager);
    check(!call.resume(messenger,services).completed && visits==1 && captured->segment_count==4 &&
        messenger.buffer().booleans().empty(),"Pending floor traversal published a response or lost four-ray prefix.");
    const auto segments=captured->segments->backing;
    fixture.manager->query=std::make_shared<HitLandQuery>();fixture.manager->query->flags=77;
    messenger.push_buffer();messenger.buffer().add_vector({90,90,90});messenger.buffer().add_float(90);
    messenger.buffer().add_bool(88);messenger.buffer().add_int(99);messenger.buffer().add_string("new packet");
    check(call.resume(messenger,services).completed && visits==2 && captured->segment_count==4 &&
        captured->segments->backing==segments && fixture.manager->query->flags==77,
        "Floor retry repeated ray preparation or adopted the replacement query.");
    check(messenger.buffer().vectors()==std::vector<std::array<float,3>>{{0,8,0}} &&
        messenger.buffer().floats()==std::vector<float>{90} && messenger.buffer().strings()[0]=="new packet" &&
        messenger.buffer().booleans()==std::vector<std::uint8_t>({1,0,1}),
        "Floor completion did not use captured input with the current output packet.");
    check(old_packet->vectors()==std::vector<std::array<float,3>>{{0,9,0}} && old_packet->booleans().empty(),
        "Floor completion published through a stale captured packet owner.");
    messenger.buffer().add_int(123);check(call.resume(messenger,services).completed && visits==2 &&
        messenger.buffer().integers()==std::vector<std::int32_t>({0,123}),"Completed invocation replayed metadata publication.");
    messenger.pop_buffer();NativeMessenger wrong;wrong.select("HITLANDMODEL",{},-1);
    rejects([&]{call.resume(wrong,services);},"Floor invocation accepted a different messenger owner.");
    check(call.faulted(),"Wrong messenger did not poison the floor invocation.");
}
void publication_failure_and_recursion() {
    Fixture fixture;NativeMessenger messenger;arguments(messenger);messenger.buffer().add_int(90);messenger.buffer().add_string("kept");
    auto services=std::make_shared<HitLandIntersectionServices>();
    services->node_dispatch=[&](const auto& visitor,const auto&) {
        visitor->query->candidates=objects({hit(8,42,std::string("bad\0name",8))});visitor->query->candidate_count=1;
        return MessengerCallResult{true,0,{}};
    };
    HitLandFloorInvocation invalid(fixture.manager);
    rejects([&]{invalid.resume(messenger,services);},"Invalid selected metadata name was silently accepted.");
    check(invalid.faulted() && messenger.buffer().integers()==std::vector<std::int32_t>{42} &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>{"kept"} &&
        messenger.buffer().booleans().empty() && messenger.buffer().vectors()==std::vector<std::array<float,3>>{{0,9,0}},
        "Metadata failure did not preserve integer clear/append before string rejection and response publication.");
    rejects([&]{invalid.resume(messenger,services);},"Faulted floor invocation restarted after metadata failure.");
    Fixture recursive;NativeMessenger nested;arguments(nested);HitLandFloorInvocation call(recursive.manager);
    auto recursion=std::make_shared<HitLandIntersectionServices>();
    recursion->node_dispatch=[&](const auto&,const auto&) {
        try {call.resume(nested,recursion);}catch(const std::exception&) {}
        return MessengerCallResult{true,0,{}};
    };
    rejects([&]{call.resume(nested,recursion);},"Caught recursive floor entry allowed later response publication.");
    check(call.faulted() && nested.buffer().booleans().empty(),"Recursive fault was lost at the visitor return boundary.");
}
void publication_capacity_and_nullable_name() {
    NativeMessenger full;arguments(full);
    constexpr std::int32_t integer_count=32768-4; // Three floats and one input vector already occupy four entries.
    for(std::int32_t index=0;index<integer_count;++index)full.buffer().add_int(index);
    const auto original_integers=full.buffer().integers();
    HitLandFloorInvocation exhausted(manager_for());
    rejects([&]{exhausted.resume(full);},"Full no-hit packet completed instead of faulting on the second boolean append.");
    check(exhausted.faulted() && full.buffer().integers()==original_integers &&
        full.buffer().floats()==std::vector<float>({1,2,2}) && full.buffer().vectors().empty() &&
        full.buffer().booleans()==std::vector<std::uint8_t>{0},
        "Capacity failure did not retain vector clearing and the first boolean while preserving integer/float lists.");
    rejects([&]{exhausted.resume(full);},"Capacity-faulted floor invocation retried its publication prefix.");
    check(full.buffer().integers()==original_integers && full.buffer().floats()==std::vector<float>({1,2,2}) &&
        full.buffer().vectors().empty() && full.buffer().booleans()==std::vector<std::uint8_t>{0},
        "Faulted floor retry changed the partially published full packet.");

    Fixture fixture;NativeMessenger messenger;arguments(messenger);
    messenger.buffer().add_string("existing");messenger.buffer().add_int(90);
    auto services=std::make_shared<HitLandIntersectionServices>();
    services->node_dispatch=[](const auto& visitor,const auto&) {
        visitor->query->candidates=objects({hit(8,17,{})});visitor->query->candidate_count=1;
        return MessengerCallResult{true,0,{}};
    };
    HitLandFloorInvocation nullable(fixture.manager);
    check(nullable.resume(messenger,services).completed && !nullable.faulted() &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>({"existing",std::nullopt}) &&
        messenger.buffer().integers()==std::vector<std::int32_t>{17} &&
        messenger.buffer().booleans()==std::vector<std::uint8_t>({1,0,1}) &&
        messenger.buffer().vectors()==std::vector<std::array<float,3>>{{0,8,0}},
        "Successful floor publication did not append the selected null name as a null string.");
}
void registered_floor_recipient(const std::shared_ptr<HitLandTraversalManager>& manager,
    const std::shared_ptr<ActorSceneResource>& scene,std::uint32_t node_reference,std::array<float,3> position,float expected_y,float width=1) {
    ActionEventRuntime events;auto owner=std::make_shared<ModelSceneManager>();auto ram=std::make_shared<ModelHitLandRam>();
    ram->manager=owner;ram->node=scene->mutable_node(node_reference);ram->half_width=width;
    ram->up_range=2;ram->down_range=2;ram->check_flags=0x1000;
    auto matrix=ram->node->matrix_owner();for(unsigned i=0;i<3;++i)(*matrix)[12+i]=position[i];
    unsigned resolutions{};
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return 1;},[&]{++resolutions;return make_hitland_floor_receiver(manager);}},"HITLANDMODEL",{},0);
    events.messenger().select("HITLANDDELIVER",{},-1);
    ModelHitLandInvocation call(ram,"HITFLOOR",owner);
    const auto result=call.resume(events.messenger(),events);
    check(result.completed && result.value==1 && resolutions==1,"Registered native CHECKLAND recipient failed nested HITFLOOR delivery.");
    const std::array<float,3> expected{position[0],expected_y,position[2]};
    check(ram->land_position==expected && ram->previous_position==expected && ram->land_position_ok==1 &&
        (*matrix)[12]==expected[0] && (*matrix)[13]==expected[1] && (*matrix)[14]==expected[2],
        "Actual HITFLOOR did not apply the native geometry response to the shared player node.");
    check(events.messenger().buffer().booleans()==std::vector<std::uint8_t>({1,0}) &&
        events.messenger().buffer().vectors()==std::vector<std::array<float,3>>{expected} && manager->query->flags==1,
        "Nested floor receiver lost the original outer response or consumed ignored input flags.");
    check(call.resume(events.messenger(),events).completed && resolutions==1,"Completed outer recipient resent its floor query.");
}
void native_registered_geometry() {
    Fixture fixture;registered_floor_recipient(fixture.manager,fixture.scene,fixture.source.source.nodes[0],{0,9,0},8);
    check(fixture.manager->query->candidate_count==4 && fixture.maps>0 && fixture.maps==fixture.unmaps &&
        fixture.manager->visitor->node_path->count==0,"Registered floor recipient bypassed actual mapped geometry or leaked visitor state.");
    const auto candidate=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(fixture.manager->query->candidates,0));
    check(candidate && candidate->payload.segment==hitland_physical_object(fixture.manager->query->segments,0) &&
        candidate->payload.scratch==fixture.manager->scratch && candidate->payload.position[1]==8 && candidate->payload.normal[1]==1,
        "Registered floor response was not backed by the original triangle candidate owners.");
    // Original CHECKLAND ignores enabled wall state; unrecovered wall requests
    // must still suspend explicitly rather than report a fabricated clear path.
    NativeMessenger messenger;arguments(messenger);const auto before=messenger.buffer_owner();
    auto receiver=make_hitland_floor_receiver({});MessengerMessage wall{"HITLANDMODEL",{},0,"CHECKWALL"};
    MessengerDelivery delivery{1,0,ControllerObjectId::allocate_native(),wall};
    check(!receiver.structured(messenger,delivery).completed && messenger.buffer_owner()==before &&
        messenger.buffer().booleans().empty(),"Floor receiver fabricated completion for an unrecovered wall command.");
}
void original(const std::filesystem::path& root) {
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),
        "ndmw.afs[179]",179);
    auto info=scene->models().hit_land_geometry_info(1768);auto manager=manager_for(info);
    for(const auto& node:info->check_switch->children())if(node)register_hitland_node_type(manager->visitor,node);
    unsigned queries{},hits{};std::optional<std::array<float,3>> accepted;
    for(const auto& node:info->check_switch->children()) {
        if(!node)continue;const auto box=node->box_owner();if(!box)continue;
        const float x=(box->minimum[0]+box->maximum[0])*0.5f,z=(box->minimum[2]+box->maximum[2])*0.5f;
        NativeMessenger messenger;arguments(messenger,{x,box->maximum[1]+10,z},0,2000,2000);
        HitLandFloorInvocation query(manager);const auto result=query.resume(messenger);
        if(!result.completed)throw std::runtime_error(result.diagnostic);++queries;
        check(manager->query->segment_count==4 && messenger.buffer().booleans().size()==3 && messenger.buffer().vectors().size()==1 &&
            manager->visitor->node_path->count==0,"Imported floor query did not complete actual four-ray traversal and response.");
        if(messenger.buffer().booleans()[2]) {
            ++hits;const auto response=messenger.buffer().vectors()[0];
            check(manager->query->candidate_count>0 && response[0]==x && response[2]==z &&
                messenger.buffer().strings().size()==1 && messenger.buffer().integers().size()==1,
                "Imported CHECKLAND response lacks actual candidate metadata.");
            if(response[1]>=1)accepted=response;
        }
    }
    check(queries==12 && hits==7 && accepted.has_value(),"Original stage179 floor responses changed for the fixed CHECKLAND ray range.");
    // The recipient node is explicit prepared synthetic state; floor geometry
    // and complete query/response execution come from the supplied original map.
    player_runtime_fixture::RuntimeFixture source;
    auto recipient_scene=std::make_shared<ActorSceneResource>(source.document(),"synthetic/floor-recipient",901);
    auto position=*accepted;position[1]+=1;
    registered_floor_recipient(manager,recipient_scene,source.nodes[0],position,(*accepted)[1],0);
    std::cout<<"Original stage179: "<<queries<<" CHECKLAND queries, "<<hits<<" real floor responses; registered HITFLOOR applied one response.\n";
}
}
int main(int argc,char** argv) {
    try {
        selection_passes();physical_selection_and_unused_values();guards_and_construction();packet_preservation();
        retained_query_and_current_packet();publication_failure_and_recursion();publication_capacity_and_nullable_name();native_registered_geometry();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" CHECKLAND floor checks passed (synthetic "<<synthetic<<").\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"CHECKLAND floor after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
