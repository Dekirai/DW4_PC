#include "dw4/hitland_wall.hpp"
#include "dw4/hitland_floor.hpp"
#include "dw4/hitland_wall_math.hpp"
#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/actor_geometry.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_hit_land_handler.hpp"
#include "dw4/assets.hpp"
#include "hitland_scene_fixture.hpp"

#include <bit>
#include <algorithm>
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
    result->infos=objects(info ? std::vector<std::shared_ptr<ModelSceneObject>>{info}:std::vector<std::shared_ptr<ModelSceneObject>>{});
    result->query=std::make_shared<HitLandQuery>();result->visitor=std::make_shared<HitLandIntersectionVisitor>();
    result->scratch=std::make_shared<HitLandTraversalScratch>();result->scratch->segments=objects();
    result->scratch->indices=std::make_shared<HitLandIndexList>();return result;
}
std::shared_ptr<HitLandCandidate> hit(float distance,std::int32_t index,std::optional<std::string> name,
    std::array<float,3> normal={0,0,1}) {
    auto result=std::make_shared<HitLandCandidate>();result->payload.distance=distance;result->payload.normal=normal;
    result->child_index=index;result->name=std::move(name);return result;
}
void candidates(const std::shared_ptr<HitLandQuery>& query,std::vector<std::shared_ptr<ModelSceneObject>> values) {
    query->candidate_count=static_cast<std::int32_t>(values.size());query->candidates=objects(std::move(values));
}
void arguments(NativeMessenger& messenger,std::int32_t previous=0,float width=100,
    std::array<float,3> position={0,9,0},std::array<float,3> direction={1,0,0},float down=1,std::int32_t flags=0x2003) {
    messenger.select("HITLANDMODEL",{},-1);auto& packet=messenger.buffer();
    packet.add_vector(position);packet.add_vector(direction);packet.add_float(width);packet.add_float(77);packet.add_float(down);
    packet.add_int(flags);packet.add_int(previous);
}
struct Fixture {
    hitland_scene_fixture::Fixture source;
    std::shared_ptr<ActorSceneResource> scene=source.scene();
    std::shared_ptr<ModelHitLandGeometryInfo> info=scene->models().hit_land_geometry_info(source.info);
    std::shared_ptr<ActorSceneNode> node=scene->mutable_node(source.source.owner);
    std::shared_ptr<HitLandTraversalManager> manager;
    Fixture() {
        info->kind=2;info->name="synthetic wall";info->check_switch->child_list_owner()->replace({node});
        *info->check_switch->switch_mask_owner()->backing={1};
        auto box=std::make_shared<CollisionBox>();box->minimum={-1000,-1000,-1000};box->maximum={1000,1000,1000};node->set_box_owner(box);
        manager=manager_for(info);register_hitland_node_type(manager->visitor,node);
    }
    void exact_rotation() {
        // Explicit prepared matrix isolates phase geometry from the independently
        // tested original Euler default: +X becomes exactly -Z.
        manager->wall_rotation={0,0,-1,0,0,1,0,0,1,0,0,0,0,0,0,1};
    }
};
void check_segment(const std::shared_ptr<HitLandQuery>& query,std::int32_t index,
    std::array<float,3> start,std::array<float,3> end) {
    const auto segment=std::dynamic_pointer_cast<HitLandSegment>(hitland_physical_object(query->segments,index));
    check(segment && segment->origin==start,"Wall pass changed its original segment origin or order.");
    for(unsigned axis=0;axis<3;++axis)
        check(std::abs(std::fma(segment->length,segment->direction[axis],segment->origin[axis])-end[axis])<0.0001f,
            "Wall pass segment endpoint differs after original direction/length storage.");
}
void original_instruction_math_fixtures() {
    const auto bits=[](const std::array<float,3>& value) {
        return std::array<std::uint32_t,3>{std::bit_cast<std::uint32_t>(value[0]),
            std::bit_cast<std::uint32_t>(value[1]),std::bit_cast<std::uint32_t>(value[2])};
    };
    // Independently obtained by executing original instructions on authored
    // mathematical inputs; no game instruction bytes or assets are embedded.
    const auto rotation=original_hitland_wall_rotation();
    constexpr std::array<std::uint32_t,16> rotation_words{0xb33bbd2e,0x80000000,0xbf800000,0,
        0,0x3f800000,0x80000000,0,0x3f800000,0,0xb33bbd2e,0,0,0,0,0x3f800000};
    for(std::size_t i=0;i<rotation.size();++i)
        check(std::bit_cast<std::uint32_t>(rotation[i])==rotation_words[i],"Original reset Euler matrix changed a word or signed zero.");
    const AnimationMatrix general{1,0.25f,-0.5f,0,-2,0.75f,1.5f,0,0.5f,-1,2,0,100,200,300,1};
    check(bits(original_hitland_wall_side({0.3f,-0.7f,0.11f},general))==
        std::array<std::uint32_t,3>{0x3f57504d,0xbe89687d,0xbef076d9},"Oblique side transform/normalization differs from original instructions.");
    const auto scale=std::bit_cast<float>(std::uint32_t{0x3f800001});
    const std::array<float,3> direction{std::bit_cast<float>(std::uint32_t{0x3f7ffffe}),0,0};
    check(bits(original_hitland_wall_offset({-1,0,0},scale,direction))==std::array<std::uint32_t,3>{},
        "Split offset lost its separately rounded product.");
    check(bits(original_hitland_wall_fused_offset({-1,0,0},scale,direction))==
        std::array<std::uint32_t,3>{0xa8800000,0,0},"Lateral endpoint offset lost original fused arithmetic.");
    check(bits(original_hitland_wall_longitudinal({1,2,3},4,1.25f,{0.3f,-0.7f,0.11f}))==
        std::array<std::uint32_t,3>{0x3e333331,0x407b3333,0x402ca3d7},"Oblique longitudinal correction differs from original instructions.");
    check(bits(original_hitland_wall_lateral({1,2,3},2,0.25f,{1,2,3}))==
        std::array<std::uint32_t,3>{0x40300000,0x40b00000,0x41040000},"Lateral correction normalized the original raw normal.");
    check(bits(original_hitland_wall_lateral({0,0,0},2,0.25f,{1,2,3}))==
        std::array<std::uint32_t,3>{0x3fdfffff,0x405fffff,0x40a7ffff},"Lateral correction collapsed the rounded acos/cos factor to one.");
    const auto tiny=std::bit_cast<float>(std::uint32_t{1});
    check(bits(original_hitland_wall_fused_offset({std::numeric_limits<float>::min(),0,0},-tiny,{tiny,0,0}))==
        std::array<std::uint32_t,3>{},"NI fused offset lost pre-round tininess at the normal boundary.");
    float equal=-0.f;original_hitland_wall_extremum(equal,0.f,0);
    check(std::signbit(equal),"Equal extrema replaced the original signed zero.");
    original_hitland_wall_extremum(equal,std::numeric_limits<float>::quiet_NaN(),1);
    check(std::signbit(equal),"Unordered extrema changed the destination.");
    original_hitland_wall_extremum(equal,1,2);
    check(std::signbit(equal),"Noncanonical extrema mode performed a store.");
}
void argument_gates_and_failures() {
    for(unsigned gate=0;gate<4;++gate) {
        NativeMessenger messenger;messenger.select("HITLANDMODEL",{},-1);
        if(gate==0){arguments(messenger);messenger.buffer().add_bool(5);}
        if(gate>=2)messenger.buffer().add_vector({1,2,3});
        if(gate>=3)messenger.buffer().add_float(4);
        messenger.buffer().add_string("kept");
        HitLandWallInvocation call({});const auto result=call.resume(messenger);
        check(result.completed && result.value==1 && !call.faulted() && messenger.buffer().strings()[0]=="kept",
            "CHECKWALL early count gate touched an unavailable manager or rewrote unrelated packet state.");
    }
    for(unsigned missing=0;missing<4;++missing) {
        NativeMessenger messenger;messenger.select("HITLANDMODEL",{},-1);auto& packet=messenger.buffer();
        packet.add_vector({0,9,0});if(missing!=0)packet.add_vector({1,0,0});
        packet.add_float(100);if(missing!=1)packet.add_float(77);if(missing!=1 && missing!=2)packet.add_float(1);
        packet.add_int(3);if(missing!=3)packet.add_int(0);
        auto manager=manager_for();manager->query->flags=91;HitLandWallInvocation call(manager);
        rejects([&]{call.resume(messenger);},"CHECKWALL short nonempty typed list did not fault at its required indexed read.");
        check(call.faulted() && manager->query->flags==91 && packet.integers().front()==3,
            "Short CHECKWALL arguments failed after integer clearing or query reset.");
    }
    NativeMessenger messenger;arguments(messenger,0,100,{0,9,0},{0,0,0});auto manager=manager_for();manager->query->flags=91;
    HitLandWallInvocation zero(manager);rejects([&]{zero.resume(messenger);},"Zero wall direction manufactured a substitute side direction.");
    check(zero.faulted() && messenger.buffer().integers().empty() && messenger.buffer().vectors().size()==2 && manager->query->flags==91,
        "Direction failure did not preserve integer clearing before the first query reset.");
}
void three_pass_geometry_and_ties() {
    Fixture fixture;fixture.exact_rotation();NativeMessenger messenger;arguments(messenger);
    messenger.buffer().add_string("existing");messenger.buffer().add_int64(123456789);messenger.buffer().set_enabled_only(255);
    auto services=std::make_shared<HitLandIntersectionServices>();unsigned visits{};
    // Scripted visitor results establish orchestration and ownership only. The
    // authored mapped vertical-triangle integration below uses no visitor seam.
    services->node_dispatch=[&](const auto& visitor,const auto&) {
        const auto query=visitor->query;++visits;
        check(query->flags==0x2002 && query->minimum[1]==9 && query->maximum[1]==9,"Wall pass lost captured flags or used ray height for bounds Y.");
        if(visits<=2) {
            const float side=visits==1 ? 142.f:71.f;
            check(query->segment_count==3 && query->minimum==std::array<float,3>{-142,9,-side} &&
                query->maximum==std::array<float,3>{142,9,side},"Wall wide/narrow pass bounds differ from the original endpoints.");
            check_segment(query,0,{-142,10,0},{142,10,0});
            check_segment(query,1,{-142,10,-side},{142,10,-side});
            check_segment(query,2,{-142,10,side},{142,10,side});
            candidates(query,visits==1 ? std::vector<std::shared_ptr<ModelSceneObject>>{hit(999,80,"preflight")}:
                std::vector<std::shared_ptr<ModelSceneObject>>{hit(200,9,"longitudinal"),hit(200,10,"wrong tie"),hit(250,11,"farther")});
        }else {
            check(visits==3 && query->segment_count==2 && query->minimum==std::array<float,3>{22.5f,9,-142} &&
                query->maximum==std::array<float,3>{22.5f,9,142},"Lateral pass omitted the earlier longitudinal correction or original forward offset.");
            check_segment(query,0,{22.5f,10,0},{22.5f,10,-142});check_segment(query,1,{22.5f,10,0},{22.5f,10,142});
            check(messenger.buffer().integers()==std::vector<std::int32_t>{9} && messenger.buffer().strings().back()=="longitudinal",
                "Longitudinal metadata was not published before lateral traversal.");
            candidates(query,{hit(100,13,"lateral"),hit(100,14,"wrong tie")});
            query->candidates->count=0;query->candidates->capacity=-1; // Physical results remain valid.
        }
        return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation call(fixture.manager);check(call.resume(messenger,services).completed && visits==3,"CHECKWALL did not execute all three retained passes.");
    check(messenger.buffer().vectors()==std::vector<std::array<float,3>>{{-84,9,42}} && messenger.buffer().booleans()==std::vector<std::uint8_t>{1} &&
        messenger.buffer().integers()==std::vector<std::int32_t>({9,13}) &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>({"existing","longitudinal","lateral"}),
        "Wall corrections or ordered first-tie metadata differ from the recovered three-pass body.");
    check(messenger.buffer().floats()==std::vector<float>({100,77,1}) && messenger.buffer().integers64()==std::vector<std::int64_t>{123456789} &&
        messenger.buffer().enabled_only()==255,"Wall response discarded unused upper range, other typed payloads or enabled-only gate.");
    messenger.buffer().add_int(88);check(call.resume(messenger,services).completed && visits==3 && messenger.buffer().integers().back()==88,
        "Completed wall invocation replayed a traversal or result publication.");
}
void admitted_pass_combinations() {
    for(const bool longitudinal:{false,true})for(const bool lateral:{false,true}) {
        Fixture fixture;fixture.exact_rotation();NativeMessenger messenger;arguments(messenger,-1);
        unsigned visits{};auto services=std::make_shared<HitLandIntersectionServices>();
        services->node_dispatch=[&](const auto& visitor,const auto&) {
            ++visits;const auto query=visitor->query;
            check(query->segment_count==(visits==1 ? 3:2),"Nonzero previous-hit flag did not skip the wide preflight.");
            if(visits==1 && longitudinal)candidates(query,{hit(200,4,{},{std::numeric_limits<float>::quiet_NaN(),0,0})});
            else if(visits==2 && lateral)candidates(query,{hit(100,5,"side")});
            else {query->candidate_count=-1;query->candidates.reset();}
            return MessengerCallResult{true,0,{}};
        };
        HitLandWallInvocation call(fixture.manager);check(call.resume(messenger,services).completed && visits==2,
            "Admitted CHECKWALL omitted lateral traversal after a longitudinal miss or read unused candidate backing.");
        check(messenger.buffer().vectors()==std::vector<std::array<float,3>>{{longitudinal ? -84.f:0.f,9,lateral ? 42.f:0.f}} &&
            messenger.buffer().booleans()==std::vector<std::uint8_t>{std::uint8_t(longitudinal || lateral)},
            "Longitudinal/lateral hit combination produced the wrong correction or hit flag.");
        if(longitudinal)check(!messenger.buffer().strings().front(),"Longitudinal null metadata was normalized or unused normal consumed.");
    }
    Fixture early;early.exact_rotation();NativeMessenger messenger;arguments(messenger);unsigned visits{};
    auto services=std::make_shared<HitLandIntersectionServices>();services->node_dispatch=[&](const auto& visitor,const auto&) {
        ++visits;visitor->query->candidate_count=-1;visitor->query->candidates.reset();return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation call(early.manager);check(call.resume(messenger,services).completed && visits==1 &&
        messenger.buffer().vectors()==std::vector<std::array<float,3>>{{0,9,0}} && messenger.buffer().booleans()==std::vector<std::uint8_t>{0} &&
        messenger.buffer().integers().empty(),"Empty wide preflight failed to return immediately with unchanged position.");
}
void retained_pass_queries_and_packet() {
    Fixture fixture;fixture.exact_rotation();NativeMessenger messenger;arguments(messenger,1);
    const auto first=fixture.manager->query;auto second=std::make_shared<HitLandQuery>();auto third=std::make_shared<HitLandQuery>();third->flags=77;
    const auto old_packet=messenger.buffer_owner();unsigned visits{};auto services=std::make_shared<HitLandIntersectionServices>();
    services->node_dispatch=[&](const auto& visitor,const auto&) {
        ++visits;
        if(visits<=2) {
            check(visitor->query==first && first->segment_count==3,"Longitudinal retry adopted the replacement manager query or repeated appends.");
            if(visits==1){candidates(first,{hit(200,6,"first")});return MessengerCallResult{false,0,"Retained longitudinal pass"};}
        }else {
            check(visitor->query==second && second->segment_count==2,"Lateral pass did not capture the then-current manager query across retry.");
            if(visits==3)return MessengerCallResult{false,0,"Retained lateral pass"};
        }
        return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation call(fixture.manager);check(!call.resume(messenger,services).completed && visits==1,"Longitudinal visitor failed to suspend.");
    fixture.manager->query=second;messenger.push_buffer();messenger.buffer().add_vector({99,99,99});messenger.buffer().add_float(99);
    messenger.buffer().add_int(99);messenger.buffer().add_string("new packet");
    check(!call.resume(messenger,services).completed && visits==3 && messenger.buffer().integers()==std::vector<std::int32_t>({99,6}),
        "Longitudinal completion did not append metadata to the live packet before lateral suspension.");
    fixture.manager->query=third;
    check(call.resume(messenger,services).completed && visits==4 && first->segment_count==3 && second->segment_count==2 && third->flags==77 &&
        messenger.buffer().vectors()==std::vector<std::array<float,3>>{{-84,9,0}} && messenger.buffer().floats()==std::vector<float>{99} &&
        old_packet->vectors()==std::vector<std::array<float,3>>({{0,9,0},{1,0,0}}),
        "Wall retry lost captured input/endpoints, modified a replacement query or published to an obsolete packet.");
    messenger.pop_buffer();NativeMessenger wrong;wrong.select("HITLANDMODEL",{},-1);
    rejects([&]{call.resume(wrong,services);},"Wall invocation accepted a different messenger owner.");
    check(call.faulted(),"Wrong wall messenger did not fault the invocation.");
}
void failure_prefixes() {
    Fixture fixture;fixture.exact_rotation();NativeMessenger messenger;arguments(messenger,1);messenger.buffer().add_string("old");
    unsigned visits{};auto services=std::make_shared<HitLandIntersectionServices>();
    services->node_dispatch=[&](const auto& visitor,const auto&) {
        ++visits;candidates(visitor->query,visits==1 ? std::vector<std::shared_ptr<ModelSceneObject>>{hit(200,9,"committed")}:
            std::vector<std::shared_ptr<ModelSceneObject>>{hit(100,10,"unreached",{0,0,0})});return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation call(fixture.manager);rejects([&]{call.resume(messenger,services);},"Degenerate lateral normal was silently substituted.");
    check(call.faulted() && visits==2 && messenger.buffer().integers()==std::vector<std::int32_t>{9} &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>({"old","committed"}) && messenger.buffer().vectors().size()==2 &&
        messenger.buffer().booleans().empty(),"Lateral failure erased committed longitudinal metadata or published unfinished position.");
    rejects([&]{call.resume(messenger,services);},"Failed wall invocation replayed already committed metadata.");
    Fixture named;named.exact_rotation();NativeMessenger bad;arguments(bad,1);
    auto invalid_name=std::make_shared<HitLandIntersectionServices>();invalid_name->node_dispatch=[](const auto& visitor,const auto&) {
        candidates(visitor->query,{hit(200,17,std::string("bad\0name",8))});return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation name(named.manager);rejects([&]{name.resume(bad,invalid_name);},"Invalid wall metadata name was accepted.");
    check(name.faulted() && bad.buffer().integers()==std::vector<std::int32_t>{17} && bad.buffer().strings().empty() &&
        bad.buffer().vectors().size()==2,"Wall metadata validation did not preserve its preceding child-index append.");
    Fixture recursion;NativeMessenger nested;arguments(nested);HitLandWallInvocation recursive(recursion.manager);
    auto reenter=std::make_shared<HitLandIntersectionServices>();reenter->node_dispatch=[&](const auto&,const auto&) {
        try {recursive.resume(nested,reenter);}catch(const std::exception&) {}return MessengerCallResult{true,0,{}};
    };
    rejects([&]{recursive.resume(nested,reenter);},"Caught recursive wall entry allowed result publication.");
    check(recursive.faulted() && nested.buffer().booleans().empty(),"Wall recursion poison was lost at callback return.");
    Fixture capacity;NativeMessenger full;arguments(full);auto fill=std::make_shared<HitLandIntersectionServices>();
    fill->node_dispatch=[&](const auto&,const auto&) {
        full.push_buffer();for(std::int32_t index=0;index<32767;++index)full.buffer().add_int(index);
        return MessengerCallResult{true,0,{}};
    };
    HitLandWallInvocation overflow(capacity.manager);rejects([&]{overflow.resume(full,fill);},"Wall final hit boolean ignored packet capacity.");
    check(overflow.faulted() && full.buffer().vectors()==std::vector<std::array<float,3>>{{0,9,0}} &&
        full.buffer().booleans().empty() && full.buffer().integers().size()==32767,
        "Wall final publication did not append position before the failing boolean.");
}
void receiver_gates() {
    for(const auto command:{"CHECKWALL","CHECKWALLFULL"})for(const bool short_request:{false,true}) {
        auto manager=manager_for();manager->wall_enabled=0;manager->query.reset();manager->infos.reset();
        auto receiver=make_hitland_query_receiver(manager);NativeMessenger messenger;
        if(short_request) {
            messenger.select("HITLANDMODEL",{},-1);messenger.buffer().add_vector({1,2,3});
            messenger.buffer().add_float(1);messenger.buffer().add_int(3);
        }else arguments(messenger);
        messenger.buffer().add_bool(99);
        MessengerMessage message{"HITLANDMODEL",{},0,command};MessengerDelivery delivery{1,0,ControllerObjectId::allocate_native(),message};
        const auto result=receiver.structured(messenger,delivery);
        check(result.completed && result.value==1 && messenger.buffer().booleans()==std::vector<std::uint8_t>{0} &&
            messenger.buffer().vectors().size()==(short_request ? 1u:2u) && messenger.buffer().integers().size()==(short_request ? 1u:2u),
            "Disabled wall dispatcher entered geometry or cleared unrelated request data.");
    }
    auto manager=manager_for();manager->wall_enabled=255;auto receiver=make_hitland_query_receiver(manager);
    NativeMessenger messenger;arguments(messenger);MessengerMessage full{"HITLANDMODEL",{},0,"CHECKWALLFULL"};
    MessengerDelivery delivery{1,0,ControllerObjectId::allocate_native(),full};
    check(!receiver.structured(messenger,delivery).completed && messenger.buffer().booleans().empty(),
        "Enabled CHECKWALLFULL was falsely implemented by CHECKWALL.");
    manager=manager_for();manager->wall_enabled=0;receiver=make_hitland_query_receiver(manager);
    NativeMessenger floor;arguments(floor);MessengerMessage land{"HITLANDMODEL",{},0,"CHECKLAND"};
    MessengerDelivery floor_delivery{2,0,ControllerObjectId::allocate_native(),land};
    check(receiver.structured(floor,floor_delivery).completed && floor.buffer().booleans()==std::vector<std::uint8_t>({0,0,0}) &&
        floor.buffer().vectors()==std::vector<std::array<float,3>>{{0,-1,0}},"Wall-enabled gate incorrectly disabled CHECKLAND.");
    const auto original=original_hitland_wall_rotation();auto defaults=manager_for();
    check(defaults->wall_enabled==1 && defaults->wall_rotation==original && original[0]!=0.f,
        "Fresh wall manager replaced its original tiny Euler cosine with an ideal axis swap.");
}
void registered_wall(const std::shared_ptr<HitLandTraversalManager>& manager,const std::shared_ptr<ActorSceneResource>& scene,
    std::uint32_t node_reference,std::array<float,3> position,std::array<float,3> direction,float width,float down,std::array<float,3> expected) {
    ActionEventRuntime events;auto owner=std::make_shared<ModelSceneManager>();auto ram=std::make_shared<ModelHitLandRam>();
    ram->manager=owner;ram->node=scene->mutable_node(node_reference);ram->half_width=width;ram->up_range=77;ram->down_range=down;ram->check_flags=2;
    for(unsigned axis=0;axis<3;++axis)ram->previous_position[axis]=position[axis]-direction[axis];
    const auto matrix=ram->node->matrix_owner();for(unsigned axis=0;axis<3;++axis)(*matrix)[12+axis]=position[axis];
    unsigned resolutions{};events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return 1;},[&]{++resolutions;return make_hitland_query_receiver(manager);}},"HITLANDMODEL",{},0);
    events.messenger().select("HITLANDDELIVER",{},-1);ModelHitLandInvocation call(ram,"HITWALL",owner);
    const auto result=call.resume(events.messenger(),events);if(!result.completed)throw std::runtime_error(result.diagnostic);
    check(result.value==1 && resolutions==1 && ram->position==std::array<float,3>{1,0,0} && ram->previous_position==expected &&
        (*matrix)[12]==expected[0] && (*matrix)[13]==expected[1] && (*matrix)[14]==expected[2],
        "Registered original HITWALL did not apply the native query response to its shared node.");
    check(events.messenger().buffer().booleans()==std::vector<std::uint8_t>({1,1,0}),"Outer HITWALL failed its exact-one movement response.");
}
void native_vertical_triangle() {
    Fixture fixture; // Real original default rotation; no exact-axis override.
    auto data=std::make_shared<ActorGeometryBuffer>();data->bytes=player_runtime_fixture::floats({0.5f,-100,-100,0.5f,-100,300,0.5f,300,-100});
    auto attribute=std::make_shared<ActorGeometryAttribute>();attribute->primitive_mode=3;attribute->primitive_count=1;
    attribute->vertices=std::make_shared<ActorVertexArray>();unsigned maps{},unmaps{};
    attribute->vertices->map_service=[&](auto&,ActorVertexMapping& mapping,auto) {++maps;mapping.position={data,0};mapping.stride=12;};
    attribute->vertices->unmap_service=[&](auto&,auto&,auto) {++unmaps;};fixture.node->set_attribute_list_owner(objects({attribute}));
    auto box=std::make_shared<CollisionBox>();box->minimum={0.5f,-100,-100};box->maximum={0.5f,300,300};fixture.node->set_box_owner(box);
    NativeMessenger messenger;arguments(messenger,0,1,{0,9,0},{1,0,0},1,2);HitLandWallInvocation query(fixture.manager);
    const auto result=query.resume(messenger);if(!result.completed)throw std::runtime_error(result.diagnostic);
    const auto corrected=messenger.buffer().vectors().at(0);
    check(messenger.buffer().booleans()==std::vector<std::uint8_t>{1} && std::abs(corrected[0]+0.92f)<0.00001f && corrected[1]==9 && corrected[2]==0 &&
        messenger.buffer().integers()==std::vector<std::int32_t>{0} && messenger.buffer().strings()==std::vector<std::optional<std::string>>{"synthetic wall"} &&
        maps>0 && maps==unmaps && fixture.manager->visitor->node_path->count==0,
        "Real vertical triangle failed longitudinal wall correction, selected metadata or balanced native traversal.");
    const auto previous_maps=maps;registered_wall(fixture.manager,fixture.scene,fixture.source.source.nodes[0],{0,9,0},{1,0,0},1,1,corrected);
    check(maps>previous_maps && maps==unmaps,"Registered HITWALL bypassed mapped geometry or left a live mapping.");
}
void original(const std::filesystem::path& root) {
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),"ndmw.afs[179]",179);
    auto manager=manager_for();std::vector<std::shared_ptr<const ActorSceneNode>> nodes;
    for(const auto reference:{192u,1953u}) {
        auto info=scene->models().hit_land_geometry_info(reference);manager->infos->append(info);
        for(const auto& node:info->check_switch->children())if(node) {nodes.push_back(node);register_hitland_node_type(manager->visitor,node);}
    }
    unsigned queries{},hits{};std::array<float,3> accepted_position{},accepted_direction{},accepted_result{};float accepted_width{};
    for(const auto& node:nodes) {
        const auto box=node->box_owner();if(!box)continue;
        std::array<float,3> position{};for(unsigned axis=0;axis<3;++axis)position[axis]=(box->minimum[axis]+box->maximum[axis])*0.5f;
        const float dx=box->maximum[0]-box->minimum[0],dz=box->maximum[2]-box->minimum[2];
        const std::array<float,3> direction=dx<=dz ? std::array<float,3>{1,0,0}:std::array<float,3>{0,0,1};
        const float width=std::max(2.f,std::min(dx,dz)*0.75f+1.f);
        NativeMessenger messenger;arguments(messenger,0,width,position,direction,0,2);HitLandWallInvocation query(manager);
        const auto result=query.resume(messenger);if(!result.completed)throw std::runtime_error(result.diagnostic);++queries;
        check(messenger.buffer().vectors().size()==1 && messenger.buffer().booleans().size()==1 && manager->visitor->node_path->count==0,
            "Imported CHECKWALL did not complete its native traversal and response.");
        if(messenger.buffer().booleans()[0]) {
            ++hits;accepted_position=position;accepted_direction=direction;accepted_width=width;accepted_result=messenger.buffer().vectors()[0];
            check(!messenger.buffer().integers().empty() && messenger.buffer().integers().size()==messenger.buffer().strings().size(),
                "Imported wall correction lacks selected child/name pairs.");
        }
    }
    check(queries==15 && hits==14,"Original stage179 wall-center query responses changed.");
    player_runtime_fixture::RuntimeFixture source;auto recipient=std::make_shared<ActorSceneResource>(source.document(),"synthetic/wall-recipient",902);
    registered_wall(manager,recipient,source.nodes[0],accepted_position,accepted_direction,accepted_width,0,accepted_result);
    std::cout<<"Original stage179: "<<queries<<" CHECKWALL queries, "<<hits<<" wall responses; registered HITWALL applied one response.\n";
}
}
int main(int argc,char** argv) {
    try {
        original_instruction_math_fixtures();argument_gates_and_failures();three_pass_geometry_and_ties();admitted_pass_combinations();retained_pass_queries_and_packet();
        failure_prefixes();receiver_gates();native_vertical_triangle();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" CHECKWALL checks passed (synthetic "<<synthetic<<").\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"CHECKWALL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
