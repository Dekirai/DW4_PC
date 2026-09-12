#include "dw4/hitland_traversal.hpp"
#include "dw4/hitland_intersection.hpp"
#include "dw4/hitland_node_dispatch.hpp"
#include "dw4/assets.hpp"
#include "hitland_scene_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F action) {
    ++checks;try{action();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid reached HITLAND traversal operation completed.");
}
std::shared_ptr<ModelSceneList> objects(std::vector<std::shared_ptr<ModelSceneObject>> values={}) {
    auto list=std::make_shared<ModelSceneList>("igObjectList");
    list->count=list->capacity=static_cast<std::int32_t>(values.size());
    list->backing=std::make_shared<ModelSceneList::Storage>(std::move(values));return list;
}
struct Fixture {
    hitland_scene_fixture::Fixture source;
    std::shared_ptr<ActorSceneResource> scene=source.scene();
    std::shared_ptr<ModelHitLandGeometryInfo> info=scene->models().hit_land_geometry_info(source.info);
    std::shared_ptr<HitLandQuery> query=std::make_shared<HitLandQuery>();
    std::shared_ptr<HitLandTraversalManager> manager=std::make_shared<HitLandTraversalManager>();
    std::shared_ptr<HitLandIntersectionServices> services=std::make_shared<HitLandIntersectionServices>();
    explicit Fixture(bool single_child=true) {
        query->segments=objects({std::make_shared<HitLandSegment>()});query->segment_count=1;
        query->minimum={-10,-10,-10};query->maximum={10,10,10};query->flags=2;
        info->kind=2;info->name="first";
        if(single_child)info->check_switch->child_list_owner()->replace({scene->mutable_node(source.source.owner)});
        *info->check_switch->switch_mask_owner()->backing={1};
        manager->infos=objects({info});manager->query=query;
        manager->visitor=std::make_shared<HitLandIntersectionVisitor>();
        manager->scratch=std::make_shared<HitLandTraversalScratch>();
        manager->scratch->segments=objects();manager->scratch->indices=std::make_shared<HitLandIndexList>();
        services->node_dispatch=[](const auto&,const auto&){return MessengerCallResult{true,0,{}};};
    }
};
void index_storage() {
    auto list=std::make_shared<HitLandIndexList>();list->count=-9;list->capacity=4;
    hitland_clear_indices(list);
    check(list->count==0 && !list->backing,"Clearing indices inspected unused backing or allocated with nonnegative capacity.");
    rejects([&]{hitland_append_zero_index(list);});
    check(list->count==1,"Index append must publish count before its missing destination fails.");
    list->capacity=-1;list->count=88;hitland_clear_indices(list);
    check(list->count==0 && list->capacity==4 && list->backing->size()==4,"Negative capacity did not invoke original minimum-four growth.");
    rejects([&]{hitland_physical_index(list,0);});
    hitland_append_zero_index(list);auto old=list->backing;
    (*old)[3]=73;list->count=4;hitland_append_zero_index(list);
    check(list->count==5 && list->capacity==8 && list->backing!=old && (*list->backing)[3]==73 &&
        hitland_physical_index(list,4)==0,"Index growth lost physical spare values, allocation identity or final zero store.");
    check(old->size()==4 && (*old)[3]==73,"Index growth changed a retained old allocation.");
    list->count=-2;rejects([&]{hitland_append_zero_index(list);});
    check(list->count==-1,"Negative append did not retain its published count before the invalid address.");
    auto broken=std::make_shared<HitLandIndexList>();broken->capacity=-1;broken->count=5;
    broken->backing=old;rejects([&]{hitland_clear_indices(broken);});
    check(broken->backing!=old && broken->capacity==-1 && broken->count==5,
        "Malformed growth must retain new allocation publication before its old-capacity copy fails.");
    auto wrapped=std::make_shared<HitLandIndexList>();wrapped->count=std::numeric_limits<std::int32_t>::max();
    rejects([&]{hitland_append_zero_index(wrapped);});
    check(wrapped->count==std::numeric_limits<std::int32_t>::min() && wrapped->capacity==4 && wrapped->backing->size()==4,
        "Signed index overflow must retain original wrapped size and allocation before its invalid physical destination.");
}
void unused_owners_and_preparation() {
    auto query=std::make_shared<HitLandQuery>();query->segment_count=-1;query->segments.reset();
    HitLandTraversalInvocation empty({},query);
    check(empty.resume().completed && !empty.faulted(),"Nonpositive segment count consumed unused manager/list owners.");
    HitLandTraversalInvocation missing({},{});rejects([&]{missing.resume();});check(missing.faulted(),"Missing query did not fault.");
    Fixture f;f.manager->infos->count=0;f.manager->infos->backing.reset();
    f.query->segments->count=0;f.query->segments->capacity=-1;
    f.query->current_name="untouched";f.query->current_index=91;
    const auto segment=(*f.query->segments->backing)[0];
    f.manager->scratch->segments=objects({std::make_shared<HitLandSegment>()});
    auto storage=f.manager->scratch->segments->backing;
    HitLandTraversalInvocation prepared(f.manager,f.query);
    check(prepared.resume().completed,"Empty info list failed after actual visitor preparation.");
    check(f.manager->scratch->segments->backing==storage && (*storage)[0]==segment &&
        f.manager->scratch->segments->count==1 && hitland_physical_index(f.manager->scratch->indices,0)==0,
        "Scratch must read query physical slots independently of list count/capacity and reuse cleared backing.");
    check(f.manager->visitor->query==f.query && f.manager->visitor->scratch==f.manager->scratch &&
        f.manager->visitor->flags==4 && f.manager->visitor->query_candidate_callback &&
        f.manager->visitor->matrix_stack->count==1 && f.manager->visitor->node_path->count==0,
        "Traversal did not publish actual query/scratch, flags, callback and reset stacks.");
    check(f.query->current_name=="untouched" && f.query->current_index==91,"Empty info loop changed unused query metadata.");
    Fixture broken;broken.manager->scratch->segments=objects({std::make_shared<HitLandSegment>()});
    broken.manager->scratch->indices.reset();HitLandTraversalInvocation stopped(broken.manager,broken.query);
    rejects([&]{stopped.resume();});
    check(broken.manager->scratch->segments->count==0 && !(*broken.manager->scratch->segments->backing)[0] &&
        !broken.manager->visitor->query,"Missing index list did not preserve earlier object clear or failed too late.");
}
void kind_gates_and_original_names() {
    const std::array<std::array<std::uint32_t,3>,6> cases{{{2,2,1},{0x802,0x802,0},{0x802,2,1},
        {0x1002,2,0},{0x1002,0x1002,1},{1,2,0}}};
    for(const auto& value:cases) {
        Fixture f;f.query->flags=value[0];f.info->kind=static_cast<std::int32_t>(value[1]);
        f.info->name.reset();f.info->active=0;unsigned visited{};
        f.services->node_dispatch=[&](const auto&,const auto&){++visited;return MessengerCallResult{true,0,{}};};
        HitLandTraversalInvocation call(f.manager,f.query);check(call.resume(f.services).completed && visited==value[2] &&
            f.query->current_name=="","Original kind/upper-flag gates or name-before-filter publication differ.");
    }
    Fixture wrong;auto value=std::make_shared<ModelSceneData>();value->name="wrong physical kind";
    wrong.manager->infos=objects({value});HitLandTraversalInvocation call(wrong.manager,wrong.query);
    rejects([&]{call.resume();});check(wrong.query->current_name==value->name,"Wrong concrete info faulted before its reached name publication.");
    Fixture invalid;invalid.info->kind=0;invalid.info->name=std::string("invalid\0name",12);invalid.query->current_name="old";
    HitLandTraversalInvocation name_failure(invalid.manager,invalid.query);rejects([&]{name_failure.resume();});
    check(invalid.query->current_name=="old" && invalid.manager->visitor->query==invalid.query,
        "Info-name boundary must occur before kind filtering/publication, retaining completed visitor setup.");
}
void captured_mask_bounds_and_live_list() {
    Fixture f;f.query->flags=1;f.info->kind=1;
    auto second=std::make_shared<ModelHitLandGeometryInfo>();second->kind=2;second->name="second";
    auto third=std::make_shared<ModelHitLandGeometryInfo>();third->kind=1;third->name="third";third->check_switch=f.info->check_switch;
    f.manager->infos=objects({f.info,second,third});std::vector<std::string> names;
    f.services->node_dispatch=[&](const auto&,const auto&) {
        names.push_back(*f.query->current_name);
        if(names.size()==1) {
            f.query->flags=2;f.query->minimum={500,500,500};f.query->maximum={600,600,600};
            third->name="replaced";f.manager->infos=objects({{},second,third});f.manager->infos->count=0;
        }
        return MessengerCallResult{true,0,{}};
    };
    HitLandTraversalInvocation call(f.manager,f.query);
    check(call.resume(f.services).completed && names==std::vector<std::string>({"first","replaced"}),
        "Traversal did not retain initial kind mask/bounds/count while reloading current physical info backing.");
}
void retained_visit_and_live_children() {
    Fixture f;const auto selected=f.info->check_switch;const auto node=f.scene->mutable_node(f.source.source.owner);
    selected->child_list_owner()->replace({node,node});*selected->switch_mask_owner()->backing={3};
    const auto old_visitor=f.manager->visitor;unsigned callbacks{};
    f.services->node_dispatch=[&](const auto& visitor,const auto& actual) {
        ++callbacks;check(actual==node,"Retained visit changed its selected physical node.");
        if(callbacks==1)return MessengerCallResult{false,0,"Actual node operation pending"};
        check((callbacks==2 && visitor==old_visitor) || (callbacks==3 && visitor==f.manager->visitor),
            "Pending visit retargeted its visitor, or a new visit did not load current manager visitor.");
        return MessengerCallResult{true,0,{}};
    };
    HitLandTraversalInvocation call(f.manager,f.query);
    check(!call.resume(f.services).completed && callbacks==1,"Pending actual node operation was not retained.");
    f.manager->visitor=std::make_shared<HitLandIntersectionVisitor>();f.manager->query=std::make_shared<HitLandQuery>();
    f.info->check_switch.reset();selected->child_list_owner()->clear();f.query->segment_count=99;
    check(call.resume(f.services).completed && callbacks==3 && old_visitor->query==f.query &&
        f.manager->scratch->segments->count==1 && f.query->current_index==1,
        "Resume replayed preparation, retargeted the passed query/switch or consumed changed logical child count.");
    check(call.resume().completed && callbacks==3,"Completed traversal replayed visits.");
    Fixture late;late.manager->visitor->dispatch_table=std::make_shared<HitLandDispatchTable>();
    HitLandTraversalInvocation unavailable(late.manager,late.query);
    auto blank=std::make_shared<HitLandIntersectionServices>();
    check(!unavailable.resume(blank).completed,"An unregistered native node returned successful geometry traversal.");
    late.manager->visitor->flags=99;
    const auto late_node=late.scene->node(late.source.source.owner);
    register_hitland_node_type(late.manager->visitor,late_node);
    // A prepared empty handler isolates the retained registration boundary from
    // geometry production; it does not stand in for a successful hit query.
    (*late.manager->visitor->dispatch_table->backing)[*late_node->runtime_dispatch_index()]=
        static_cast<std::uint32_t>(HitLandNodeHandler::empty);
    check(unavailable.resume(blank).completed && late.manager->visitor->flags==99,
        "Resuming the entered native dispatcher replayed earlier visitor preparation.");
}
void physical_mask_and_dynamic_copy() {
    Fixture inactive(false);
    *inactive.info->check_switch->switch_mask_owner()->backing={0};
    HitLandTraversalInvocation skipped(inactive.manager,inactive.query);
    check(skipped.resume().completed,"Disabled physical children consumed null/dynamic node fields.");
    Fixture absent;absent.info->check_switch->set_switch_mask_owner({});HitLandTraversalInvocation missing(absent.manager,absent.query);
    rejects([&]{missing.resume();});check(absent.query->current_index==0,"Mask failure changed an unreached index.");
    Fixture f;const auto dynamic=f.scene->mutable_node(f.source.dynamic);f.info->kind=0x402;
    f.info->check_switch->child_list_owner()->replace({dynamic});
    const auto source=dynamic->source_transform_owner()->matrix_owner();const auto destination=dynamic->matrix_owner();
    (*source)[0]=std::bit_cast<float>(1u);(*source)[1]=std::bit_cast<float>(0x80000001u);(*source)[12]=1000;
    unsigned visits{};f.services->node_dispatch=[&](const auto&,const auto&){++visits;return MessengerCallResult{true,0,{}};};
    HitLandTraversalInvocation broad(f.manager,f.query);
    check(broad.resume(f.services).completed && visits==0 && (*destination)[12]==1000 &&
        std::bit_cast<std::uint32_t>((*destination)[0])==0 && std::bit_cast<std::uint32_t>((*destination)[1])==0x80000000u,
        "Dynamic copy did not precede rejected broad phase or missed paired-store subnormal flushing.");
    (*source)[12]=0;(*source)[13]=0;(*source)[14]=0;
    dynamic->child_list_owner()->clear();HitLandTraversalInvocation physical(f.manager,f.query);
    check(physical.resume(f.services).completed && visits==1,"Dynamic bound must read physical nested child zero despite logical empty count.");
    f.scene->mutable_node(f.source.source.owner)->set_box_owner({});(*source)[12]=7;
    HitLandTraversalInvocation malformed(f.manager,f.query);rejects([&]{malformed.resume(f.services);});
    check((*destination)[12]==7 && malformed.faulted(),"Late missing dynamic box rolled back the original matrix prefix.");
    Fixture no_box;no_box.scene->mutable_node(no_box.source.source.owner)->set_box_owner({});
    HitLandTraversalInvocation unbounded(no_box.manager,no_box.query);
    check(unbounded.resume(no_box.services).completed,"Static null bound must still dispatch the selected node.");
}
void recursive_failure() {
    Fixture f;HitLandTraversalInvocation call(f.manager,f.query);unsigned visits{};
    f.services->node_dispatch=[&](const auto&,const auto&) {
        ++visits;try{call.resume(f.services);}catch(const std::exception&){}
        return MessengerCallResult{true,0,{}};
    };
    rejects([&]{call.resume(f.services);});check(call.faulted() && visits==1,"Caught recursion allowed traversal to continue.");
    rejects([&]{call.resume(f.services);});
}
void original(const std::filesystem::path& root) {
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),
        "ndmw.afs[179]",179);
    auto manager=std::make_shared<HitLandTraversalManager>();manager->infos=objects();
    // Explicit prepared registration order, not a claim about manager startup.
    for(auto ref:{1768u,192u,1953u})manager->infos->append(scene->models().hit_land_geometry_info(ref));
    manager->scratch=std::make_shared<HitLandTraversalScratch>();manager->scratch->segments=objects();
    manager->scratch->indices=std::make_shared<HitLandIndexList>();manager->visitor=std::make_shared<HitLandIntersectionVisitor>();
    auto services=std::make_shared<HitLandIntersectionServices>();
    for(const auto kind:{1u,2u}) {
        auto query=std::make_shared<HitLandQuery>();query->segments=objects({std::make_shared<HitLandSegment>()});
        query->segment_count=1;query->flags=kind;query->minimum={-10000,-10000,-10000};query->maximum={10000,10000,10000};
        std::vector<std::shared_ptr<const ActorSceneNode>> visited;
        services->node_dispatch=[&](const auto& visitor,const auto& node) {
            check(visitor->query==query && visitor->scratch==manager->scratch && node==scene->node(node->reference()),
                "Original traversal did not pass canonical node/query/scratch owners.");
            visited.push_back(node);return MessengerCallResult{true,0,{}};
        };
        HitLandTraversalInvocation call(manager,query);
        check(call.resume(services).completed && visited.size()==(kind==1 ? 12u:15u),
            "Original inner-mask traversal differs from the twelve floor/fifteen wall occurrences.");
        check(query->candidate_count==0,"Traversal observer fabricated an intersection result.");
    }
    std::cout<<"Original stage179: 12 floor and 15 wall node visits; typed visitor/triangle production remains required.\n";
}
}
int main(int argc,char** argv) {
    try {
        index_storage();unused_owners_and_preparation();kind_gates_and_original_names();captured_mask_bounds_and_live_list();
        retained_visit_and_live_children();physical_mask_and_dynamic_copy();recursive_failure();
        const auto synthetic=checks;if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" HITLAND traversal checks passed (synthetic "<<synthetic<<").\n";return 0;
    } catch(const std::exception& error){std::cerr<<"HITLAND traversal after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
