#include "dw4/model_scene.hpp"
#include "dw4/assets.hpp"
#include "hitland_scene_fixture.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const std::exception&) {return;}
    throw std::runtime_error("Unsupported geometry-owner operation completed.");
}
using Fixture=hitland_scene_fixture::Fixture;

void canonical_owners() {
    static_assert(!std::is_copy_constructible_v<ModelHitLandGeometryInfo>);
    Fixture f;auto doc=f.document();
    const auto info_alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[f.info]);
    const auto mask_alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[f.mask]);
    const auto memory_alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[f.mask_memory]);
    // Both masks already address the same allocation; the reference table also
    // retains an independently numbered alias of that physical memory.
    check(doc.memory(memory_alias).data()==doc.memory(f.mask_memory).data(),"Synthetic physical memory alias is not shared.");
    auto scene=std::make_shared<ActorSceneResource>(std::move(doc),"synthetic/hitland-geometry",4);
    const auto info=scene->models().hit_land_geometry_info(f.info);
    check(scene->models().hit_land_geometry_info(info_alias)==info && scene->models().object(f.info)==info,
        "Geometry info aliases must use one actual owner.");
    check(info->type()=="beHitLandModelInfo" && info->name=="Synthetic player" && info->active==1 && !info->data &&
        info->rams->count==0 && static_cast<std::uint32_t>(info->kind)==0x80001c03u,"Authored geometry fields were narrowed or fabricated.");
    check(info->switch_graph==scene->mutable_node(f.outer) && info->check_switch==scene->mutable_node(f.inner) &&
        info->switch_graph->child_list_owner()->backing_node(0)==info->check_switch,"Outer/check switch identities diverged.");
    const auto names=info->names;
    check(names->type()=="igNamedObjectList" && names->count==3 && !(*names->backing)[1] &&
        (*names->backing)[0]==(*names->backing)[2],"Named-list nullable repeated occurrences changed.");
    const auto children=info->check_switch->child_list_owner();const auto other=scene->mutable_node(f.other_switch);
    const auto alias_children=other->child_list_owner();
    check(children!=alias_children && children->storage_owner()==alias_children->storage_owner(),"Distinct lists must share physical child backing.");
    check(children->count()==4 && children->backing_node(0)==scene->mutable_node(f.source.owner) &&
        !children->backing_node(1) && children->backing_node(2)==children->backing_node(3),"Physical nullable child order changed.");
    const auto mask=info->check_switch->switch_mask_owner();const auto second=other->switch_mask_owner();
    check(mask==scene->bit_mask(f.mask) && scene->bit_mask(mask_alias)==mask && second!=mask && second->backing==mask->backing,
        "Mask-object and mask-memory aliases must remain independent.");
    check(scene->document().field_reference(scene->document().object(f.other_mask),4)!=f.mask_memory,
        "Mask backing alias must be tested through a different serialized reference number.");
    check(mask->count==-2 && mask->capacity==0 && mask->bit_count==1 && mask->backing_word(1)==0x12345678u,
        "Mask metadata must not validate or truncate a reached physical word.");
    (*mask->backing)[0]=0x100u;
    check(second->backing_word(0)==0x100u && info->switch_graph->switch_mask_owner()==mask,
        "Live mask writes must reach all owners of the same physical allocation.");
    auto replacement=std::make_shared<ActorSceneBitMask>();replacement->backing=std::make_shared<ActorSceneBitMask::Storage>(1,7);
    info->check_switch->set_switch_mask_owner(replacement);
    check(info->check_switch->switch_mask_owner()==replacement && info->switch_graph->switch_mask_owner()==mask,
        "Replacing one switch mask must preserve the captured old owner.");
    const auto node=children->backing_node(2);const auto source=scene->mutable_node(f.source.nodes[1]);
    check(node->type()=="beCopyTransform" && node->source_transform_owner()==source &&
        node->matrix_owner()!=source->matrix_owner(),"Dynamic source linkage must retain distinct canonical matrices.");
    (*source->matrix_owner())[12]=57;
    check((*node->source_transform_owner()->matrix_owner())[12]==57 && (*node->matrix_owner())[12]==123,
        "Loading or changing a source must not perform a speculative matrix copy.");
    auto nested=node->child_list_owner();const auto bound=nested->backing_node(0)->box_owner();
    check(bound==scene->box(f.box) && bound->minimum==std::array<float,3>{-2,-3,-4},"Dynamic nested bound must share the canonical box.");
    nested->clear();check(nested->count()==0 && nested->backing_node(0)==scene->mutable_node(f.source.owner),
        "Physical nested child zero remains addressable after logical clear.");
    children->replace({source,source,node,node});
    check(alias_children->backing_node(0)==source && alias_children->count()==4,"Aliased child backing must observe replacement writes.");
    const auto retained=scene->document_owner();scene.reset();
    check(info->document_owner()==retained && mask->document_owner()==retained && node->source_transform_owner()==source,
        "Captured owners must retain source identity beyond the resource facade.");
}
void reached_boundaries_and_cycles() {
    Fixture f;auto scene=f.scene();const auto info=scene->models().hit_land_geometry_info(f.info);
    auto node=scene->mutable_node(f.dynamic),source=scene->mutable_node(f.source.nodes[1]);
    rejects([&]{node->set_source_transform_owner(node);});
    check(node->source_transform_owner()==source,"Rejected self-link must preserve its original owner.");
    auto list=std::make_shared<ActorSceneNodeList>();list->replace({node});
    rejects([&]{source->set_child_list_owner(list);});
    check(source->child_list_owner()!=list,"Mixed source/child cycle must not publish.");
    node->set_source_transform_owner({});check(!node->source_transform_owner(),"Explicit null source must remain null.");
    node->set_source_transform_owner(source);
    rejects([&]{source->source_transform_owner();});
    rejects([&]{source->switch_mask_owner();});
    info->check_switch->set_switch_mask_owner({});check(!info->check_switch->switch_mask_owner(),"Explicit null mask must remain null.");
    ActorSceneBitMask missing;missing.count=100;missing.capacity=100;missing.bit_count=3200;
    rejects([&]{missing.backing_word(0);});
    const auto children=info->check_switch->child_list_owner();rejects([&]{children->backing_node(4);});
    const auto saved=children->storage_owner();children->clear();
    check(children->backing_node(2)==node && children->storage_owner()==saved,"Count changes must not replace physical child backing.");
    auto malformed=f.document();auto& fields=malformed.objects.at(malformed.references.at(f.children).value_index).fields;
    auto& mask_fields=malformed.object(f.mask).fields;
    const auto negative=*std::find_if(mask_fields.begin(),mask_fields.end(),[](const auto& x){return x.slot==2;});
    for(auto& field:fields)if(field.slot==2)field=negative;
    ActorSceneResource raw(std::move(malformed),"synthetic/negative",4);
    auto loaded=raw.models().hit_land_geometry_info(f.info)->check_switch->child_list_owner();
    check(loaded->count()==-2 && loaded->backing_node(2)->type()=="beCopyTransform",
        "Loading must preserve negative child count without eagerly validating the active prefix.");
    auto cycle=f.document();auto& dynamic=cycle.objects.at(cycle.references.at(f.dynamic).value_index);
    auto& children_object=cycle.objects.at(cycle.references.at(f.children).value_index);
    const auto backing=*cycle.field_reference(children_object,4);
    const auto offset=cycle.memories.at(cycle.references.at(backing).value_index).offset;
    for(auto& field:dynamic.fields)if(field.slot==12)field.offset=offset+8;
    ActorSceneResource cyclic(std::move(cycle),"synthetic/cycle",4);rejects([&]{cyclic.mutable_node(f.dynamic);});
    check(f.scene()->models().hit_land_geometry_info(f.info)!=info,"Separate source loads must retain separate owner identities.");
    auto nullable=f.document();
    const auto null_field=*nullable.field(nullable.object(f.info),5);
    for(const auto ref:{f.info,f.inner,f.dynamic}) {
        auto& owner=nullable.objects.at(nullable.references.at(ref).value_index);
        for(auto& field:owner.fields)if((ref==f.info && (field.slot==7 || field.slot==8 || field.slot==9)) ||
            (ref==f.inner && field.slot==8) || (ref==f.dynamic && field.slot==12))field.offset=null_field.offset;
    }
    ActorSceneResource nulls(std::move(nullable),"synthetic/null-fields",4);
    const auto null_info=nulls.models().hit_land_geometry_info(f.info);
    check(!null_info->switch_graph && !null_info->check_switch && !null_info->names,
        "Loading must preserve explicit null geometry references without constructing default owners.");
    check(!nulls.mutable_node(f.inner)->switch_mask_owner() && !nulls.mutable_node(f.dynamic)->source_transform_owner(),
        "Authored null mask and source link must remain available null fields.");
    rejects([&]{scene->models().hit_land_geometry_info(f.source.model);});
    auto broken=f.document();broken.memories.at(broken.references.at(f.mask_memory).value_index).size=5;
    ActorSceneResource partial(std::move(broken),"synthetic/partial-word",4);rejects([&]{partial.bit_mask(f.mask);});
    std::weak_ptr<ActorSceneNode> weak_source,weak_copy;
    {
        auto local=f.scene();const auto copy=local->mutable_node(f.dynamic);
        weak_copy=copy;weak_source=copy->source_transform_owner();
    }
    check(weak_copy.expired() && weak_source.expired(),"Weak incoming source edges must not keep the node graph alive.");
}
void original(const std::filesystem::path& root) {
    auto archive=assets::AfsArchive::open(root/"files/ndmw.afs");
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(archive.read(179)),"ndmw.afs:179",179);
    for(auto ref:{1768u,192u,1953u}) {
        const auto info=scene->models().hit_land_geometry_info(ref);
        check(info->kind==(ref==1768 ? 1 : 2),"Original category bits changed.");
        check(info->switch_graph->child_list_owner()->backing_node(0)==info->check_switch,"Original outer/check identity changed.");
        const auto count=ref==192 ? 3 : 12;
        check(info->check_switch->child_list_owner()->count()==count && info->names->count==count,
            "Original physical shape/name counts changed.");
        check((info->switch_graph->switch_mask_owner()->backing_word(0)&1u)==0 &&
            (info->check_switch->switch_mask_owner()->backing_word(0)&((1u<<count)-1u))==((1u<<count)-1u),
            "Outer disabled mask must stay distinct from selected collision children.");
        auto child=info->check_switch->child_list_owner()->backing_node(0);
        check(child->type()=="igGeometry" && child==scene->mutable_node(child->reference()) && child->box_owner(),
            "Original selected geometry and bound must be canonical actual node owners.");
    }
    const auto dynamic=scene->models().hit_land_geometry_info(24723);
    check(dynamic->kind==1026 && dynamic->check_switch->child_list_owner()->count()==56,
        "Original dynamic category/count changed.");
    check(dynamic->names->type()=="igStringObjList" && dynamic->names->count==0 && !dynamic->names->backing,
        "Dynamic category must retain its authored empty string-object list.");
    const auto child=dynamic->check_switch->child_list_owner()->backing_node(0);
    check(child->reference()==21503 && child->type()=="beCopyTransform" && child->source_transform_owner()->reference()==24992,
        "Original dynamic linked-node source identity changed.");
    check(child->source_transform_owner()==scene->mutable_node(24992) &&
        child->child_list_owner()->backing_node(0)->box_owner(),"Original dynamic source/bound owners are missing.");
}
}
int main(int argc,char** argv) {
    try {canonical_owners();reached_boundaries_and_cycles();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: model_hit_land_geometry_scene_tests [extracted-root]");
        std::cout<<checks<<" HITLANDMODEL geometry scene checks passed ("<<synthetic<<" synthetic).\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
