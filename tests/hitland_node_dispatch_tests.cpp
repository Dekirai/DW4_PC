#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/hitland_geometry.hpp"
#include "dw4/hitland_query.hpp"
#include "hitland_scene_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
    ++checks;try {action();}catch(const std::exception&) {return;}
    throw std::runtime_error("Expected reached HITLAND dispatch failure.");
}
struct Fixture {
    hitland_scene_fixture::Fixture source;
    std::shared_ptr<ActorSceneResource> scene;
    std::shared_ptr<ActorSceneNode> group,transform,geometry,base,second;
    std::shared_ptr<HitLandIntersectionVisitor> visitor=std::make_shared<HitLandIntersectionVisitor>();
    Fixture() {
        const auto base_ref=source.source.add(player_runtime_fixture::node);
        const auto second_ref=source.source.add(player_runtime_fixture::node);
        auto document=source.document();
        const auto flags=*document.field(document.object(source.source.owner),5);
        for(const auto ref:{base_ref,second_ref})document.objects.at(document.references.at(ref).value_index).fields.push_back(flags);
        document.metadata.object_types.at(player_runtime_fixture::node).slots=6;
        scene=std::make_shared<ActorSceneResource>(std::move(document),"synthetic/hitland-dispatch",1);
        group=scene->mutable_node(source.outer);transform=scene->mutable_node(source.dynamic);
        geometry=scene->mutable_node(source.source.owner);base=scene->mutable_node(base_ref);second=scene->mutable_node(second_ref);
        group->child_list_owner()->replace({});transform->child_list_owner()->replace({base});
        visitor->dispatch_table=std::make_shared<HitLandDispatchTable>();
        for(const auto& node:{group,transform,geometry,base,second})register_hitland_node_type(visitor,node);
        reset_hitland_intersection(visitor);
    }
    void target(const std::shared_ptr<ActorSceneNode>& node,std::uint32_t value) {
        (*visitor->dispatch_table->backing)[*node->runtime_dispatch_index()]=value;
    }
};
void dispatch_and_filter_gates() {
    Fixture f;
    check(f.geometry->runtime_dispatch_index()!=f.scene->document().object(f.source.source.owner).type_index,
        "Native registration was replaced by the serialized metadata index.");
    const auto index=*f.base->runtime_dispatch_index();
    f.visitor->dispatch_table->count=-9;f.visitor->dispatch_table->capacity=-10;
    HitLandNodeDispatchInvocation base(f.visitor,f.base);
    check(base.resume().completed,"Physical dispatch incorrectly validated logical table count/capacity.");
    check((*f.visitor->dispatch_table->backing)[index]==static_cast<std::uint32_t>(HitLandNodeHandler::empty),"Wrong inherited base handler.");
    f.base->set_runtime_dispatch_index({});HitLandNodeDispatchInvocation absent(f.visitor,f.base);
    check(!absent.resume().completed && !absent.faulted(),"Unregistered runtime index was guessed.");
    f.base->set_runtime_dispatch_index(index);check(absent.resume().completed,"Reached runtime registration did not resume.");
    f.visitor->filter_enabled=255;f.visitor->filter=[](const auto&,const auto&) {return MessengerCallResult{true,3,{}};};
    HitLandNodeDispatchInvocation ignored(f.visitor,{});
    check(ignored.resume().completed,"Outer filter skip consumed an unused null node.");
    f.visitor->filter={};HitLandNodeDispatchInvocation missing_filter(f.visitor,{});
    rejects([&] {missing_filter.resume();});check(missing_filter.faulted(),"Null reached filter did not poison dispatch.");
    unsigned calls{};
    f.visitor->filter=[&](const auto&,const auto&) {++calls;return MessengerCallResult{true,2,{}};};
    HitLandNodeDispatchInvocation exact_base(f.visitor,f.base);
    check(exact_base.resume().completed && calls==1,"Filter2 on exact igNode attempted generic children.");
    f.visitor->filter=[&](const auto&,const auto&) {return MessengerCallResult{true,++calls%2 ? 0u:3u,{}};};calls=0;
    HitLandNodeDispatchInvocation geometry(f.visitor,f.geometry);
    check(geometry.resume().completed && calls==2 && f.visitor->node_path->count==0,
        "Inner geometry filter did not accept non1/non2 after the outer zero.");
    f.visitor->filter_enabled=0;f.target(f.base,0);HitLandNodeDispatchInvocation null_target(f.visitor,f.base);
    rejects([&] {null_target.resume();});
    f.visitor->dispatch_table->backing->at(index).reset();HitLandNodeDispatchInvocation unknown_target(f.visitor,f.base);
    rejects([&] {unknown_target.resume();});
}
void retained_filter_and_generic_children() {
    Fixture f;auto services=std::make_shared<HitLandNodeDispatchServices>();
    f.group->child_list_owner()->replace({f.base,f.second});
    f.target(f.base,0x12345678);unsigned parent_filters{},child_calls{};bool ready{};
    f.visitor->filter_enabled=1;
    f.visitor->filter=[&](const auto& visitor,const auto& node) {
        if(node==f.group) {++parent_filters;check(visitor->node_path->count==0,"Filter2 added a group path prefix.");return MessengerCallResult{true,2,{}};}
        return MessengerCallResult{true,0,{}};
    };
    services->other_handler=[&](auto target,const auto& visitor,const auto& node) {
        check(target==0x12345678 && visitor==f.visitor,"Custom table target lost its identity.");
        ++child_calls;
        if(node==f.base && !ready) {f.group->set_child_list_owner(std::make_shared<ActorSceneNodeList>());return MessengerCallResult{false,0,"Pending actual child target."};}
        return MessengerCallResult{true,node==f.second ? 2u:0u,{}};
    };
    HitLandNodeDispatchInvocation call(f.visitor,f.group);
    check(!call.resume(services).completed && child_calls==1 && parent_filters==1,"Generic child did not retain its pause.");
    ready=true;const auto result=call.resume(services);
    check(result.completed && result.value==2 && child_calls==3 && parent_filters==1,
        "Generic traversal replaced captured backing/count or replayed the parent filter.");
    check(f.visitor->node_path->count==0,"Filter2 generic traversal changed the path.");
    Fixture paused;unsigned selected{};bool filter_ready{};
    paused.visitor->filter_enabled=1;
    paused.visitor->filter=[&](const auto&,const auto&) {++selected;return MessengerCallResult{filter_ready,1,"Filter pending."};};
    HitLandNodeDispatchInvocation filter(paused.visitor,{});
    check(!filter.resume().completed,"Pending filter did not suspend before node reads.");
    paused.visitor->filter=[](const auto&,const auto&) {throw std::runtime_error("Replaced filter must not run.");return MessengerCallResult{};};
    paused.visitor->filter_enabled=0;filter_ready=true;
    check(filter.resume().completed && selected==2,"Pending invocation did not retain its entered filter target.");
}
void group_and_geometry_prefixes() {
    Fixture f;f.visitor->node_path.reset();HitLandNodeDispatchInvocation empty(f.visitor,f.group);
    check(empty.resume().completed,"Empty group inspected the unused path or producer.");
    f.visitor->node_path=std::make_shared<HitLandNodePath>();f.group->child_list_owner()->replace({f.base});
    f.group->set_box_owner(std::make_shared<CollisionBox>());f.visitor->flags=1;
    auto services=std::make_shared<HitLandNodeDispatchServices>();services->geometry=std::make_shared<HitLandGeometryServices>();
    bool ready{};unsigned tests{};
    services->geometry->volume_test=[&](const auto&,const auto&,const auto&) {++tests;return MessengerCallResult{ready,2,"Actual volume pending."};};
    HitLandNodeDispatchInvocation group(f.visitor,f.group);
    check(!group.resume(services).completed && f.visitor->node_path->count==1,"Producer pause lost the actual pushed path.");
    auto old=f.visitor->node_path;auto replacement=std::make_shared<HitLandNodePath>();
    hitland_push_path(replacement,f.base);hitland_push_path(replacement,f.second);f.visitor->node_path=replacement;ready=true;
    const auto result=group.resume(services);
    check(result.completed && result.value==2 && tests==2,"Group did not propagate captured producer2.");
    check(old->count==1 && replacement->count==1 && !*replacement->backing->at(1),"Group pop used a captured obsolete path.");
    Fixture bound;bound.geometry->set_box_owner({});auto service=std::make_shared<HitLandNodeDispatchServices>();
    HitLandNodeDispatchInvocation geometry(bound.visitor,bound.geometry);
    check(!geometry.resume(service).completed && bound.visitor->node_path->count==0,"Missing geometry bound was silently skipped.");
    unsigned builds{};bool bound_ready{};
    service->geometry_bound=[&](const auto&,const auto& node,auto& captured) {
        ++builds;if(!bound_ready)return MessengerCallResult{false,0,"Bound mapping pending."};
        auto actual=std::make_shared<CollisionBox>();actual->minimum={-1,-2,-3};actual->maximum={4,5,6};
        std::const_pointer_cast<ActorSceneNode>(node)->set_box_owner(actual);captured=actual;return MessengerCallResult{true,0,{}};
    };
    check(!geometry.resume(service).completed && builds==1,"Bound service did not retain its reached pause.");
    bound_ready=true;check(geometry.resume(service).completed && builds==2 && bound.geometry->box_owner()->maximum[1]==5 &&
        bound.visitor->node_path->count==0,"Geometry bound publication/path/producer/pop did not complete.");
}
void time_update_and_stack() {
    Fixture f;auto services=std::make_shared<HitLandNodeDispatchServices>();
    f.visitor->bound_time=255;f.visitor->time_high=0x12345678;f.visitor->time_low=0x9abcdef0;
    HitLandNodeDispatchInvocation call(f.visitor,f.transform);
    check(!call.resume(services).completed && f.visitor->node_path->count==0,"Missing transform update manufactured a no-op.");
    unsigned updates{},child_filters{};bool ready{};
    services->global_clock=[]()->std::optional<std::uint64_t> {throw std::runtime_error("Bound time read unused clock.");};
    services->transform_update=[&](const auto& node,std::uint64_t time) {
        ++updates;check(node==f.transform && time==0x123456789abcdef0ull,"Transform update recaptured changed time.");
        (*std::const_pointer_cast<ActorSceneNode>(node)->matrix_owner())[12]=9;
        return MessengerCallResult{ready,99,"Transform update pending."};
    };
    check(!call.resume(services).completed && updates==1 && f.visitor->matrix_stack->count==1,"Pending update pushed a premature matrix/path.");
    f.visitor->time_high=7;f.visitor->time_low=8;ready=true;
    f.visitor->filter_enabled=1;f.visitor->filter=[&](const auto& visitor,const auto& node) {
        check(node==f.base,"Entered transform filters unexpectedly replayed after update.");++child_filters;
        check(visitor->matrix_stack->count==2 && hitland_physical_matrix(visitor->matrix_stack,1)->at(12)==9 &&
            visitor->node_path->count==1,"Child did not observe the pushed composed transform.");
        return MessengerCallResult{true,0,{}};
    };
    check(call.resume(services).completed && updates==2 && child_filters==1,"Transform continuation did not finish exactly once.");
    check(f.visitor->matrix_stack->count==1 && f.visitor->node_path->count==0,"Transform did not pop current stack and path.");
    Fixture clock;auto dynamic=std::make_shared<HitLandNodeDispatchServices>();bool clock_ready{};unsigned reads{};
    HitLandNodeDispatchInvocation unbound(clock.visitor,clock.transform);
    check(!unbound.resume(dynamic).completed,"Unbound visitor invented a host clock.");
    dynamic->global_clock=[&]()->std::optional<std::uint64_t> {++reads;if(clock_ready)return 1234;return {};};
    check(!unbound.resume(dynamic).completed && reads==1,"Actual clock did not retain its pause.");
    clock.visitor->bound_time=1;clock.visitor->time_low=555;clock_ready=true;
    dynamic->transform_update=[&](const auto&,auto time) {check(time==1234,"Clock pause restarted the bound-time gate.");return MessengerCallResult{true,0,{}};};
    check(unbound.resume(dynamic).completed && reads==2,"Actual clock continuation did not finish.");
}
void helper_prefixes_and_faults() {
    Fixture f;auto path=std::make_shared<HitLandNodePath>();path->capacity=4;
    rejects([&] {hitland_push_path(path,f.base);});check(path->count==1,"Path append lost count-before-store prefix.");
    path->count=0;rejects([&] {hitland_pop_path(path);});check(path->count==0,"Invalid path pop decremented count before pointer read.");
    auto stack=std::make_shared<HitLandMatrixList>();stack->capacity=4;stack->count=1;
    rejects([&] {hitland_pop_transform(stack);});check(stack->count==0,"Matrix pop lost decrement-before-copy prefix.");
    stack=std::make_shared<HitLandMatrixList>();AnimationMatrix matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    matrix[12]=std::numeric_limits<float>::denorm_min();matrix[13]=-std::numeric_limits<float>::denorm_min();
    hitland_push_transform(stack,matrix);auto initial=hitland_physical_matrix(stack,0);
    check((*initial)[12]==0 && std::signbit((*initial)[13]),"PSQ first push did not preserve signed subnormal flush.");
    matrix[0]=std::numeric_limits<float>::quiet_NaN();
    rejects([&] {hitland_push_transform(stack,matrix);});
    check(stack->count==2 && hitland_physical_matrix(stack,1)->at(0)==1,"Failed composition discarded its appended matrix copy.");
    auto visitor=f.visitor;visitor->filter_enabled=1;HitLandNodeDispatchInvocation recursive(visitor,f.base);
    visitor->filter=[&](const auto&,const auto&) {try {recursive.resume();}catch(const HitLandQueryError&) {}return MessengerCallResult{true,0,{}};};
    rejects([&] {recursive.resume();});check(recursive.faulted(),"Caught recursive filter continued native dispatch.");
}
}
int main() {
    try {dispatch_and_filter_gates();retained_filter_and_generic_children();group_and_geometry_prefixes();
        time_update_and_stack();helper_prefixes_and_faults();std::cout<<"HITLAND node dispatch checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"After "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
