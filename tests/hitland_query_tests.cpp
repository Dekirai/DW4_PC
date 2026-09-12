#include "dw4/hitland_query.hpp"
#include "dw4/hitland_traversal.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
int checks{};
void require(bool condition) {++checks;if(!condition)throw std::runtime_error("HITLAND query check failed.");}
template<class F> void rejects(F action) {
    bool caught=false;try {action();}catch(const HitLandQueryError&) {caught=true;}require(caught);
}
std::shared_ptr<AnimationMatrix> matrix() {
    auto result=std::make_shared<AnimationMatrix>();
    (*result)[0]=(*result)[5]=(*result)[10]=(*result)[15]=1;return result;
}
void reset_and_reuse() {
    HitLandQuery query;require(query.current_name && query.current_name->empty());
    require(HitLandCandidate{}.name && HitLandCandidate{}.name->empty());
    query.maximum={9,8,7};query.minimum={1,2,3};
    query.current_name="floor";query.current_index=4;query.flags=0x1fff;
    auto first=query.append_segment({1,2,3},{4,6,3});
    require(first->origin==std::array<float,3>{1,2,3});
    require(first->direction==std::array<float,3>{0.6f,0.8f,0});
    require(first->length==5 && query.segment_count==1 && query.segments->count==1);
    auto storage=query.segments->backing;auto list=query.segments;
    HitLandVisitorPayload input;input.matrix=matrix();input.normal={0,1,0};input.position={3,5,9};
    auto result=query.append_candidate(input);auto results=query.candidates->backing;
    require(result->name=="floor" && result->child_index==4 && query.candidate_count==1);
    query.reset();
    require(query.flags==0 && query.segment_count==0 && query.candidate_count==0);
    require(query.maximum==std::array<float,3>{9,8,7} && query.minimum==std::array<float,3>{1,2,3});
    require(query.current_name=="floor" && query.current_index==4);
    require(query.segments==list && query.segments->backing==storage && query.candidates->backing==results);
    require(query.segments->count==1 && query.candidates->count==1);
    auto reused=query.append_segment({7,8,9},{7,8,9});
    require(reused==first && first->origin==std::array<float,3>{7,8,9});
    require(first->direction==std::array<float,3>{0,0,0} && first->length==0);
    query.current_name.reset();query.current_index=-7;input.normal={1,2,3};
    require(query.append_candidate(input)==result && !result->name && result->child_index==-7);
    query.reset();query.segments.reset();query.candidates.reset();query.reset();
    require(!query.segments && !query.candidates);
}
void segment_boundaries_and_aliases() {
    HitLandQuery query;
    const float tiny=std::numeric_limits<float>::denorm_min();
    auto value=query.append_segment({-0.0f,tiny,0},{-0.0f,tiny,0});
    require(std::signbit(value->origin[0]) && value->origin[1]==tiny && value->length==0);
    query.reset();const float threshold=std::bit_cast<float>(std::uint32_t{0x350637bd});
    value=query.append_segment({0,0,0},{threshold,0,0});
    require(value->length==threshold && value->direction[0]==threshold);
    query.reset();const float above=std::nextafter(threshold,1.0f);
    value=query.append_segment({0,0,0},{above,0,0});
    require(value->length==above && value->direction[0]>0.99f);
    query.reset();value->origin={1,2,3};value->direction={4,6,3};
    query.append_segment(value->origin,value->direction);
    require(value->origin==std::array<float,3>{1,2,3} && value->length==5);
    require(value->direction==std::array<float,3>{0.6f,0.8f,0});
    query.reset();const auto old_direction=value->direction;
    rejects([&] {query.append_segment({std::numeric_limits<float>::quiet_NaN(),4,5},{1,2,3});});
    require(std::isnan(value->origin[0]) && value->origin[1]==2 && value->direction==old_direction && query.segment_count==0);
    HitLandQuery fresh;
    rejects([&] {fresh.append_segment({0,0,0},{std::numeric_limits<float>::infinity(),0,0});});
    require(fresh.segments->count==1 && fresh.segment_count==0 && fresh.segments->capacity==4);
    query.segments->capacity=-100;query.segment_count=0;
    require(query.append_segment({0,0,0},{1,0,0})==value); // Reuse ignores capacity.
}
void candidate_payload_and_order() {
    HitLandQuery query;query.current_name="wall";query.current_index=23;
    HitLandVisitorPayload input;input.kind=4;input.matrix=matrix();input.segment=std::make_shared<HitLandSegment>();
    input.scratch=std::make_shared<HitLandTraversalScratch>();
    input.distance=-0.0f;input.position={1,2,3};input.local_position={4,5,6};input.normal={1,2,3};
    input.attribute_index=-1;input.group_index=31;input.triangle_index=32;
    (*input.matrix)[0]=2;(*input.matrix)[5]=3;(*input.matrix)[10]=4;
    (*input.matrix)[12]=std::numeric_limits<float>::quiet_NaN(); // Not consumed by rotation.
    auto candidate=query.append_candidate(input);
    require(input.normal==std::array<float,3>{2,6,12});
    require(candidate->payload.normal==input.normal && candidate->payload.position==input.position);
    require(candidate->payload.local_position==input.local_position && std::signbit(candidate->payload.distance));
    require(candidate->payload.matrix==input.matrix && candidate->payload.segment==input.segment && candidate->payload.scratch==input.scratch);
    require(candidate->payload.kind==4 && candidate->payload.attribute_index==-1 && candidate->payload.group_index==31 && candidate->payload.triangle_index==32);
    query.reset();query.current_name=std::string("bad\0name",8);input.normal={1,2,3};input.position={9,8,7};
    rejects([&] {query.append_candidate(input);});
    require(candidate->payload.position==input.position && candidate->payload.normal==std::array<float,3>{2,6,12});
    require(candidate->name=="wall" && candidate->child_index==23 && query.candidate_count==0);
    query.current_name="new";query.current_index=45;
    require(query.append_candidate(input)==candidate && candidate->name=="new" && candidate->child_index==45);
    require(candidate->payload.normal==std::array<float,3>{4,18,48}); // Input was mutated by failed name copy.
    HitLandQuery missing;HitLandVisitorPayload null_matrix;
    rejects([&] {missing.append_candidate(null_matrix);});
    require(missing.candidates->count==1 && missing.candidate_count==0);
    missing.candidate_count=-1;null_matrix.matrix=matrix();null_matrix.normal={3,4,5};
    (*null_matrix.matrix)[0]=2;
    rejects([&] {missing.append_candidate(null_matrix);});
    require(null_matrix.normal==std::array<float,3>{6,4,5} && missing.candidate_count==-1);
    query.candidate_count=0;query.candidates->count=1;(*query.candidates->backing)[0].reset();
    input.normal={1,1,1};rejects([&] {query.append_candidate(input);});
    require(input.normal==std::array<float,3>{2,3,4});
}
void physical_lists() {
    auto list=std::make_shared<ModelSceneList>();
    auto first=std::make_shared<HitLandSegment>();
    hitland_append_object(list,first);auto old=list->backing;
    require(list->count==1 && list->capacity==4 && hitland_physical_object(list,0)==first);
    rejects([&] {hitland_physical_object(list,1);});
    for(int i=0;i<4;++i)hitland_append_object(list,std::make_shared<HitLandSegment>());
    require(list->count==5 && list->capacity==8 && list->backing!=old && (*old)[0]==first);
    auto alias=std::make_shared<ModelSceneList>();alias->backing=list->backing;alias->count=-100;alias->capacity=-9;
    require(hitland_physical_object(alias,0)==first);
    auto storage=list->backing;hitland_clear_objects(list);
    require(list->count==0 && list->capacity==8 && list->backing==storage);
    require(!hitland_physical_object(alias,0) && !hitland_physical_object(alias,4));
    hitland_append_object(list,{});require(list->count==1 && !hitland_physical_object(list,0));
    list->count=-1;rejects([&] {hitland_clear_objects(list);});require(list->count==0);
    list->count=-2;list->capacity=10;rejects([&] {hitland_append_object(list,{});});require(list->count==-1);
    list->count=0;list->capacity=1;list->backing.reset();
    rejects([&] {hitland_append_object(list,first);});require(list->count==1);
    auto damaged=std::make_shared<ModelSceneList>();damaged->count=4;damaged->capacity=4;
    damaged->backing=std::make_shared<ModelSceneList::Storage>(2);auto before=damaged->backing;
    rejects([&] {hitland_append_object(damaged,first);});
    require(damaged->backing!=before && damaged->count==4 && damaged->capacity==4);
    auto spare=std::make_shared<ModelSceneList>();spare->capacity=1;
    spare->backing=std::make_shared<ModelSceneList::Storage>(1,first);
    rejects([&] {hitland_append_object(spare,{});});require(spare->count==1 && (*spare->backing)[0]==first);
    auto unknown=std::make_shared<ModelSceneList>();unknown->count=1;unknown->capacity=1;
    unknown->backing=std::make_shared<ModelSceneList::Storage>(1,std::make_shared<ModelSceneData>());
    rejects([&] {hitland_clear_objects(unknown);});require(unknown->count==1 && (*unknown->backing)[0]);
}
}
int main() {
    try {reset_and_reuse();segment_boundaries_and_aliases();candidate_payload_and_order();physical_lists();
        std::cout<<"HITLAND query checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"After "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
