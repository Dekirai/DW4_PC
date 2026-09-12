#include "dw4/hitland_intersection.hpp"
#include "dw4/hitland_query.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& call) {
    ++checks;try {call();}catch(const HitLandQueryError&) {return;}
    throw std::runtime_error("Invalid HITLAND intersection was accepted.");
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
void reset_and_backing() {
    auto visitor=std::make_shared<HitLandIntersectionVisitor>();
    visitor->flags=123;visitor->query_candidate_callback=true;
    reset_hitland_intersection(visitor);
    const auto stack=visitor->matrix_stack;
    check(stack->count==1 && stack->capacity==4 && stack->backing->size()==4,"Fresh reset did not allocate original minimum capacity");
    check(*hitland_physical_matrix(stack,0)==identity(),"Reset did not write the original identity matrix");
    check(!(*stack->backing)[1] && visitor->flags==123 && visitor->query_candidate_callback,
        "Reset invented spare matrices or modified unrelated visitor state");
    auto captured=hitland_physical_matrix(stack,0);(*captured)[12]=99;
    const auto allocation=stack->backing;reset_hitland_intersection(visitor);
    check(stack->backing==allocation && *captured==identity(),"Reset lost aliases into an existing physical matrix slot");
    stack->count=0;stack->capacity=-1;
    rejects([&] {reset_hitland_intersection(visitor);});
    check(stack->backing!=allocation && stack->backing->size()==4 && stack->capacity==-1 && stack->count==0,
        "Invalid old capacity rolled back already-published replacement backing");
    check(*captured==identity(),"Replacement invalidated a retained pointer into the old matrix allocation");
    stack->backing.reset();reset_hitland_intersection(visitor);
    check(stack->count==1 && stack->capacity==4,"Null old backing incorrectly consumed its negative old capacity");
    visitor->node_path.reset();(*hitland_physical_matrix(stack,0))[12]=7;
    rejects([&] {reset_hitland_intersection(visitor);});
    check(stack->count==1 && *hitland_physical_matrix(stack,0)==identity(),"Late null node path rolled back the completed identity prefix");
    visitor->node_path=std::make_shared<HitLandNodePath>();visitor->node_path->count=-3;
    reset_hitland_intersection(visitor);
    check(visitor->node_path->count==0,"Negative node-path count did not skip signed release/zero loops");
    const auto path=visitor->node_path;
    path->count=2;path->capacity=0;
    path->backing=std::make_shared<std::vector<std::optional<std::shared_ptr<const ActorSceneNode>>>>();
    path->backing->push_back(std::shared_ptr<const ActorSceneNode>{});path->backing->push_back(std::nullopt);
    rejects([&] {reset_hitland_intersection(visitor);});
    check(path->count==2 && (*path->backing)[0] && !*(*path->backing)[0],
        "A late unspecified path pointer lost its count-last failure prefix");
    (*path->backing)[1]=std::shared_ptr<const ActorSceneNode>{};
    auto alias=std::make_shared<HitLandNodePath>();alias->count=2;alias->backing=path->backing;
    reset_hitland_intersection(visitor);
    check(path->count==0 && alias->count==2 && !*(*alias->backing)[1],"Path clear changed an alias list's count or discarded backing identity");
    stack->count=0;stack->capacity=4;stack->backing.reset();
    rejects([&] {reset_hitland_intersection(visitor);});
    check(stack->count==1,"Missing identity destination lost its prior count publication");
    rejects([&] {hitland_physical_matrix(stack,0);});
    rejects([] {reset_hitland_intersection({});});
    visitor->matrix_stack.reset();rejects([&] {reset_hitland_intersection(visitor);});
}
void retained_dispatch() {
    auto visitor=std::make_shared<HitLandIntersectionVisitor>();
    HitLandIntersectionInvocation call(visitor,{});
    HitLandIntersectionInvocation no_node(visitor,{});
    rejects([&] {no_node.resume();});
    check(no_node.faulted(),"Default native dispatch did not reach its null-node fault");
    auto actual=std::make_shared<HitLandIntersectionServices>();unsigned calls{};bool ready{};
    actual->node_dispatch=[&](const auto& selected,const auto& node) {
        ++calls;check(selected==visitor && !node,"Retained +64 substituted visitor or nullable node identity");
        return MessengerCallResult{ready,2,"Controlled dispatch remains pending."};
    };
    check(!call.resume(actual).completed && calls==1,"Explicit actual dispatch did not retain its operation");
    ready=true;check(call.resume(actual).completed && calls==2 && call.completed(),"Real node dispatch did not resume its retained operation");
    check(call.resume().completed && calls==2,"Completed +64 replayed its node dispatch");
    HitLandIntersectionInvocation changed(visitor,{});ready=false;changed.resume(actual);
    rejects([&] {changed.resume(std::make_shared<HitLandIntersectionServices>());});
    check(changed.faulted(),"Changing a started provider did not invalidate continuation");
    HitLandIntersectionInvocation recursive(visitor,{});auto recursion=std::make_shared<HitLandIntersectionServices>();
    recursion->node_dispatch=[&](const auto&,const auto&) {
        try {recursive.resume(recursion);}catch(const HitLandQueryError&) {}
        return MessengerCallResult{true,0,{}};
    };
    rejects([&] {recursive.resume(recursion);});check(recursive.faulted(),"Caught recursion resumed an uncertain visitor prefix");
    HitLandIntersectionInvocation missing({},{});rejects([&] {missing.resume();});
}
void triangles() {
    using V=std::array<float,3>;
    HitLandSegment segment;segment.origin={0,1,0};segment.direction={0,-1,0};segment.length=2;
    const V a{-1,0,-1},b{0,0,1},c{1,0,-1};V point{77,88,99},normal{11,22,33};
    check(original_hitland_triangle(segment,a,b,c,point,normal,0)==1 && point==V{0,0,0} && normal==V{0,1,0},
        "Original front-facing triangle did not produce its segment distance, point and normal");
    point={77,88,99};
    check(original_hitland_triangle(segment,a,b,c,point,normal,1)==-2 && point==V{77,88,99} && normal==V{0,1,0},
        "Back-face rejection did not preserve the early normal and untouched point");
    check(original_hitland_triangle(segment,a,b,c,point,normal,2)==1,"Side2 did not bypass face selection");
    segment.origin={5,1,5};
    check(original_hitland_triangle(segment,a,b,c,point,normal,0)==-4 && point==V{5,0,5},
        "Outside-triangle rejection discarded the already-published plane intersection");
    segment.origin={0,3,0};point={77,88,99};
    check(original_hitland_triangle(segment,a,b,c,point,normal,0)==-3 && point==V{77,88,99},
        "Segment-range rejection wrote a point before its range gate");
    segment.origin={0,1,0};segment.length=1;
    check(original_hitland_triangle(segment,a,b,c,point,normal,0)==1,"Exact endpoint distance was incorrectly excluded");
    segment.direction={1,0,0};point={77,88,99};
    check(original_hitland_triangle(segment,a,b,c,point,normal,0)==-2 && point==V{77,88,99},"Parallel face did not reject before point publication");
    rejects([&] {original_hitland_triangle(segment,a,b,c,point,normal,2);});
    check(point==V{77,88,99} && normal==V{0,1,0},"Side2 numeric failure discarded its published normal");
    segment.direction={0,-1,0};
    rejects([&] {original_hitland_triangle(segment,a,a,a,point,normal,0);});
    check(normal==V{} && point==V{77,88,99},"Degenerate triangle failed before publishing its raw zero normal");
    normal={11,22,33};segment.origin={0,1,0};point={77,88,99};
    original_hitland_triangle(segment,a,b,c,normal,normal,0);
    check(normal==V{0,0,0},"Aliased normal/point output did not retain the original in-place writes");
}
}
int main() {
    try {reset_and_backing();retained_dispatch();triangles();std::cout<<"HITLAND intersection checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error) {std::cerr<<"HITLAND intersection after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
