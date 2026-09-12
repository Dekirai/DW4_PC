#include "dw4/hitland_geometry.hpp"
#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/hitland_traversal.hpp"
#include "dw4/actor_geometry.hpp"
#include "dw4/assets.hpp"
#include "hitland_scene_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid reached HITLAND geometry completed.");
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::shared_ptr<ModelSceneList> objects(std::vector<std::shared_ptr<ModelSceneObject>> values={}) {
    auto list=std::make_shared<ModelSceneList>("igObjectList");
    list->count=list->capacity=static_cast<std::int32_t>(values.size());
    list->backing=std::make_shared<ModelSceneList::Storage>(std::move(values));return list;
}
struct Fixture {
    hitland_scene_fixture::Fixture source;
    std::shared_ptr<ActorSceneResource> scene=source.scene();
    std::shared_ptr<ActorSceneNode> node=scene->mutable_node(source.source.owner);
    std::shared_ptr<HitLandIntersectionVisitor> visitor=std::make_shared<HitLandIntersectionVisitor>();
    std::shared_ptr<HitLandQuery> query=std::make_shared<HitLandQuery>();
    std::shared_ptr<ActorGeometryAttribute> attribute=std::make_shared<ActorGeometryAttribute>();
    std::shared_ptr<ActorGeometryBuffer> data=std::make_shared<ActorGeometryBuffer>();
    unsigned maps{},unmaps{};
    Fixture() {
        data->bytes=player_runtime_fixture::floats({0,0,0,0,0,2,2,0,0});
        attribute->primitive_mode=3;attribute->primitive_count=1;
        attribute->vertices=std::make_shared<ActorVertexArray>();
        attribute->vertices->map_service=[this](auto&,ActorVertexMapping& mapping,auto) {
            ++maps;mapping.position={data,0};mapping.stride=12;
        };
        attribute->vertices->unmap_service=[this](auto&,auto&,auto) {++unmaps;};
        node->set_attribute_list_owner(objects({attribute}));
        reset_hitland_intersection(visitor);visitor->flags=4;visitor->query=query;
        visitor->query_candidate_callback=true;
        query->append_segment({0.5f,2,0.5f},{0.5f,-2,0.5f});
        query->current_name="floor";query->current_index=7;
        visitor->scratch=std::make_shared<HitLandTraversalScratch>();
        visitor->scratch->segments=objects({hitland_physical_object(query->segments,0)});
        visitor->scratch->indices=std::make_shared<HitLandIndexList>();
        hitland_append_zero_index(visitor->scratch->indices);
    }
};
void numeric_helpers() {
    auto matrix=identity();matrix[12]=3;matrix[13]=-2;matrix[14]=5;
    std::array<float,3> point{2,4,-1};original_hitland_point(point,point,matrix);
    check(point==std::array<float,3>{5,2,4},"Original point transform lost source/output alias or translation.");
    matrix[15]=2;original_hitland_point(point,std::array<float,3>{2,4,-1},matrix);
    check(point==std::array<float,3>{2.5f,1,2},"Original point transform omitted homogeneous W division.");
    matrix=identity();matrix[15]=-2;original_hitland_point(point,std::array<float,3>{2,4,-1},matrix);
    check(point==std::array<float,3>{-1,-2,0.5f},"Negative homogeneous W lost its sign.");
    check(original_hitland_distance({1,2,3},{4,6,3})==5,"Candidate distance differs from original world-point distance.");
    HitLandSegment line;line.origin={1,2,3};line.direction={0,1,0};line.length=2;
    matrix=identity();matrix[5]=3;matrix[12]=4;original_hitland_transform_segment(line,matrix);
    check(line.origin==std::array<float,3>{5,6,3} && line.direction==std::array<float,3>{0,1,0} && line.length==6,
        "Segment transform failed to transform both endpoints and renormalize.");
    line.length=0;matrix[12]=7;rejects([&]{original_hitland_transform_segment(line,matrix);});
    check(line.origin[0]==12 && line.direction==std::array<float,3>{0,0,0} && line.length==0,
        "Zero transformed length must fail after endpoint/direction stores.");
}
void static_candidate() {
    Fixture f;const auto matrix=hitland_physical_matrix(f.visitor->matrix_stack,0);
    HitLandGeometryInvocation call(f.visitor,f.node);
    check(call.resume().completed && call.completed() && !call.faulted(),"Static original geometry producer did not complete.");
    check(f.query->candidate_count==1 && f.maps==2 && f.unmaps==2,"Producer did not remap once to exhaust its cursor after one hit.");
    const auto candidate=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(f.query->candidates,0));
    check(candidate && candidate->name=="floor" && candidate->child_index==7 && candidate->payload.kind==4 &&
        candidate->payload.attribute_index==0 && candidate->payload.group_index==0 && candidate->payload.triangle_index==0,
        "Original candidate publication lost name, child or primitive cursor.");
    check(candidate->payload.position==std::array<float,3>{0.5f,0,0.5f} && candidate->payload.local_position==candidate->payload.position &&
        candidate->payload.normal==std::array<float,3>{0,1,0} && candidate->payload.distance==2,
        "Original candidate has incorrect local/world position, normal or distance.");
    check(candidate->payload.matrix==matrix && candidate->payload.node_path==f.visitor->node_path &&
        candidate->payload.segment==hitland_physical_object(f.query->segments,0) && candidate->payload.scratch==f.visitor->scratch,
        "Candidate publication detached canonical pointer owners.");
    (*matrix)[12]=25;check((*candidate->payload.matrix)[12]==25,"Candidate matrix must retain its physical stack element alias.");
    check(call.resume().completed && f.maps==2,"Completed producer replayed its map prefix.");
}
void reached_gates() {
    Fixture f;f.node->set_box_owner({});f.visitor->matrix_stack.reset();f.visitor->scratch.reset();
    HitLandGeometryInvocation empty(f.visitor,f.node);
    check(empty.resume().completed && f.maps==0,"Null bound consumed unused matrix/scratch state.");
    Fixture no_flags;no_flags.visitor->flags=0;no_flags.visitor->scratch.reset();
    HitLandGeometryInvocation skipped(no_flags.visitor,no_flags.node);
    check(skipped.resume().completed && no_flags.maps==0,"Flags-zero path read segment/attribute data.");
    Fixture unused;unused.node->set_attribute_list_owner(objects());
    unused.visitor->scratch->segments=objects({{}});
    HitLandGeometryInvocation no_attributes(unused.visitor,unused.node);
    check(no_attributes.resume().completed,"Empty attribute loop dereferenced its unused null segment.");
    Fixture no_rays;no_rays.visitor->matrix_stack->count=0;no_rays.visitor->matrix_stack->backing.reset();
    no_rays.visitor->scratch->segments->count=0;
    HitLandGeometryInvocation unused_matrix(no_rays.visitor,no_rays.node);
    check(unused_matrix.resume().completed,"Empty segment loop dereferenced its unused captured matrix address.");
    Fixture invalid_matrix;invalid_matrix.visitor->matrix_stack->count=0;invalid_matrix.visitor->matrix_stack->backing.reset();
    HitLandGeometryInvocation late_matrix(invalid_matrix.visitor,invalid_matrix.node);rejects([&]{late_matrix.resume();});
    check(invalid_matrix.maps==1 && invalid_matrix.unmaps==1 && invalid_matrix.query->candidate_count==0,
        "Invalid captured matrix failed before the reached mapped triangle and unmap effects.");
    Fixture broken;broken.visitor->scratch->indices.reset();
    HitLandGeometryInvocation missing(broken.visitor,broken.node);rejects([&]{missing.resume();});
    check(missing.faulted() && broken.maps==0,"Missing side index failed after mapping unrelated vertices.");
}
void retained_callback_and_live_backing() {
    Fixture f;auto list=f.node->attribute_list_owner();list->backing->push_back(f.attribute);list->count=2;
    const auto segments=f.visitor->scratch->segments;
    auto replacement=std::make_shared<HitLandSegment>();replacement->origin={0.5f,6,0.5f};
    replacement->direction={0,-1,0};replacement->length=12;
    f.visitor->query_candidate_callback=false;
    HitLandGeometryInvocation call(f.visitor,f.node);auto placeholder=std::make_shared<HitLandGeometryServices>();
    check(!call.resume(placeholder).completed && f.maps==1 && f.unmaps==1,"Missing callback did not retain the completed triangle/map prefix.");
    auto services=std::make_shared<HitLandGeometryServices>();unsigned callbacks{};
    services->candidate=[&](const auto& visitor,const auto&,HitLandVisitorPayload& payload) {
        ++callbacks;
        if(callbacks==1) {
            list->count=0;f.node->set_attribute_list_owner(objects());
            (*segments->backing)[0]=replacement;
            visitor->scratch=std::make_shared<HitLandTraversalScratch>();
            return MessengerCallResult{false,0,"Retained actual callback test."};
        }
        check(payload.attribute_index==(callbacks==2 ? 0:1),"Retained producer lost captured attribute count or current index.");
        f.query->append_candidate(payload);return MessengerCallResult{true,0,{}};
    };
    check(!call.resume(services).completed && f.maps==1,"Suspended callback remapped or repeated candidate construction.");
    check(call.resume(services).completed && callbacks==3 && f.query->candidate_count==2 && f.maps==4,
        "Producer did not resume captured callback then consume the retained attribute-list owner.");
    const auto first=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(f.query->candidates,0));
    const auto second=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(f.query->candidates,1));
    check(first->payload.scratch!=second->payload.scratch && second->payload.scratch==f.visitor->scratch,
        "Callback payload scratch owner must reload for each new candidate.");
    check(first->payload.segment!=second->payload.segment && second->payload.segment==replacement &&
        first->payload.distance==2 && second->payload.distance==6,
        "New payload did not reload the original segment backing for identity and distance.");
    Fixture stop;stop.visitor->query_candidate_callback=false;auto terminate=std::make_shared<HitLandGeometryServices>();
    terminate->candidate=[](const auto&,const auto&,auto&){return MessengerCallResult{true,2,{}};};
    HitLandGeometryInvocation early(stop.visitor,stop.node);
    check(early.resume(terminate).value==2 && early.completed() && stop.maps==1,"Nonzero callback return did not stop the primitive loop.");
}
void caught_recursion() {
    Fixture f;HitLandGeometryInvocation call(f.visitor,f.node);
    f.attribute->vertices->map_service=[&](auto&,auto& mapping,auto) {
        ++f.maps;try{call.resume();}catch(const std::exception&){}
        mapping.position={f.data,0};mapping.stride=12;
    };
    rejects([&]{call.resume();});
    check(call.faulted() && f.maps==1 && f.unmaps==0 && f.query->candidate_count==0,
        "Caught recursive map failure did not stop at the service return before further native effects.");
    rejects([&]{call.resume();});
}
void transform_boundary() {
    Fixture f;f.visitor->matrix_stack->count=2;
    (*f.visitor->matrix_stack->backing)[1]=identity();
    HitLandGeometryInvocation call(f.visitor,f.node);
    check(!call.resume().completed && f.maps==0,"Transformed geometry silently bypassed original bound scratch dispatch.");
    auto services=std::make_shared<HitLandGeometryServices>();unsigned bound_calls{};
    services->transform_bound=[&](const auto&,const auto& source,auto& result) {
        ++bound_calls;result=source;return MessengerCallResult{true,0,{}};
    };
    check(!call.resume(services).completed && bound_calls==1 && f.maps==0,"Missing inverse lost its completed transformed-bound prefix.");
    check(!call.resume(services).completed && bound_calls==1,"Paused inverse replayed bound cloning.");
    services->inverse_matrix=[](const auto&,auto& output) {output=identity();return MessengerCallResult{true,0,{}};};
    f.visitor->transformed_segment=std::make_shared<HitLandSegment>();
    check(call.resume(services).completed && f.query->candidate_count==1 && bound_calls==1,
        "Explicit prepared transform operations did not resume the actual segment/primitive producer.");
}
void original(const std::filesystem::path& root) {
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),
        "ndmw.afs[179]",179);
    auto info=scene->models().hit_land_geometry_info(1768);
    unsigned candidates{},nodes{};std::array<float,3> accepted_start{},accepted_end{};
    for(const auto& node:info->check_switch->children()) {
        if(!node)continue;
        const auto box=node->box_owner();if(!box)continue;
        auto query=std::make_shared<HitLandQuery>();
        const auto x=(box->minimum[0]+box->maximum[0])*0.5f,z=(box->minimum[2]+box->maximum[2])*0.5f;
        query->append_segment({x,box->maximum[1]+10,z},{x,box->minimum[1]-10,z});
        auto visitor=std::make_shared<HitLandIntersectionVisitor>();reset_hitland_intersection(visitor);
        visitor->flags=4;visitor->query=query;visitor->query_candidate_callback=true;
        visitor->scratch=std::make_shared<HitLandTraversalScratch>();visitor->scratch->segments=query->segments;
        visitor->scratch->indices=std::make_shared<HitLandIndexList>();hitland_append_zero_index(visitor->scratch->indices);
        register_hitland_node_type(visitor,node);
        HitLandIntersectionInvocation call(visitor,node);
        const auto result=call.resume();
        if(!result.completed)throw std::runtime_error(result.diagnostic);
        check(visitor->node_path->count==0,"Original geometry handler failed to restore its node path.");
        ++nodes;candidates+=static_cast<unsigned>(query->candidate_count);
        if(query->candidate_count) {accepted_start={x,box->maximum[1]+10,z};accepted_end={x,box->minimum[1]-10,z};}
        for(std::int32_t i=0;i<query->candidate_count;++i) {
            const auto candidate=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(query->candidates,i));
            check(candidate && candidate->payload.segment==hitland_physical_object(query->segments,0) &&
                candidate->payload.kind==4 && candidate->payload.position[0]==x && candidate->payload.position[2]==z,
                "Original floor candidate lost canonical segment or vertical ray position.");
        }
    }
    check(nodes==12 && candidates>0,"Original twelve floor geometries did not produce any native triangle candidates.");
    auto manager=std::make_shared<HitLandTraversalManager>();manager->infos=objects({info});
    manager->visitor=std::make_shared<HitLandIntersectionVisitor>();
    manager->scratch=std::make_shared<HitLandTraversalScratch>();manager->scratch->segments=objects();
    manager->scratch->indices=std::make_shared<HitLandIndexList>();
    manager->query=std::make_shared<HitLandQuery>();manager->query->flags=1;
    manager->query->minimum={-10000,-10000,-10000};manager->query->maximum={10000,10000,10000};
    manager->query->append_segment(accepted_start,accepted_end);
    for(const auto& node:info->check_switch->children())if(node)register_hitland_node_type(manager->visitor,node);
    HitLandTraversalInvocation traversal(manager,manager->query);
    const auto result=traversal.resume();if(!result.completed)throw std::runtime_error(result.diagnostic);
    check(manager->query->candidate_count>0 && manager->visitor->node_path->count==0,
        "Original common traversal, typed dispatch and geometry producer did not return actual floor candidates.");
    const auto first=std::dynamic_pointer_cast<HitLandCandidate>(hitland_physical_object(manager->query->candidates,0));
    check(first && first->name==std::optional<std::string>{info->name.value_or(std::string{})} && first->payload.scratch==manager->scratch &&
        first->payload.segment==hitland_physical_object(manager->query->segments,0),
        "Original common traversal lost query name, segment or scratch in the produced result.");
    std::cout<<"Original stage179: "<<nodes<<" floor geometry queries, "<<candidates<<" triangle candidates.\n";
}
}
int main(int argc,char** argv) {
    try {
        numeric_helpers();static_candidate();reached_gates();retained_callback_and_live_backing();caught_recursion();transform_boundary();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" HITLAND geometry checks passed (synthetic "<<synthetic<<").\n";return 0;
    }catch(const std::exception& error){std::cerr<<"HITLAND geometry after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
