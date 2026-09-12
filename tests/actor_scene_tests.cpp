#include "dw4/actor_scene.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <iostream>
#include <bit>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) { ++checks;if(!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();} catch(const ActorSceneError&) {return;}throw std::runtime_error(reason);
}
ActorSceneResource load(const pf::RuntimeFixture& fixture) {return {fixture.document(),"synthetic/player.igb",3};}
void mutable_node_fields() {
    pf::RuntimeFixture fixture;
    const auto box_ref=fixture.add(pf::object);
    const auto box_bytes=fixture.memory(pf::floats({-2,-3,-4,5,6,7}));
    const auto bound_bytes=fixture.memory(pf::words({box_ref,pf::null_ref}));
    auto doc=fixture.document();
    const auto vector_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());
    doc.metadata.field_types.push_back({"igVec3fMetaField",1,0});
    IgbObjectType box_type;box_type.name="igAABox";box_type.parent_index=pf::object;box_type.slots=4;
    const auto box_type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
    doc.metadata.object_types.push_back(std::move(box_type));
    const auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
    const auto offset=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
    object(box_ref).type_index=box_type_index;
    object(box_ref).fields={{2,vector_type,offset(box_bytes),12},{3,vector_type,offset(box_bytes)+12,12}};
    for(const auto ref:fixture.nodes)object(ref).fields.push_back({3,2,offset(bound_bytes),4});
    object(fixture.lods[0]).fields.push_back({3,2,offset(bound_bytes)+4,4});
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[fixture.nodes[0]]);
    auto resource=std::make_shared<ActorSceneResource>(doc,"synthetic/mutable.igb",8);
    auto first=resource->mutable_node(fixture.nodes[0]),second=resource->mutable_node(fixture.nodes[1]);
    check(resource->node(alias)==first && resource->mutable_node(alias)==first,"Mutable and const queries must return the same canonical node.");
    check(resource->document_owner().get()==&first->document(),"Document owner must alias the actual node source document.");
    auto matrix=first->matrix_owner();const auto authored=*matrix;
    check(matrix!=second->matrix_owner() && matrix==resource->mutable_node(alias)->matrix_owner(),"Matrices are per physical node, not per query or equal value.");
    (*matrix)[12]=29;
    check((*resource->node(alias)->matrix_owner())[12]==29 && (*second->matrix_owner())[12]!=29,
        "Live matrix mutation must reach all node views without changing another node.");
    auto placement=first->placement_node();auto replacement=authored;replacement[13]=47;
    check(placement->matrix==matrix,"Placement must use the canonical node matrix allocation.");
    placement->set_matrix(replacement);
    check(*matrix==replacement,"Original concrete setter must publish into the same matrix owner.");
    auto box=first->box_owner();
    check(resource->box(box_ref)==box,"Direct box queries must use the node-bound canonical allocation.");
    check(first->bound_field_available() && second->box_owner()==box && box->minimum==std::array<float,3>{-2,-3,-4} &&
        box->maximum==std::array<float,3>{5,6,7},"Authored AABox aliases or endpoint decoding changed.");
    box->maximum[0]=51;
    check(second->box_owner()->maximum[0]==51,"Aliased scene boxes must share mutation.");
    first->set_box_owner({});
    check(!first->box_owner() && second->box_owner()==box,"Clearing one bound slot must preserve another node's retained box.");
    auto null_bound=resource->mutable_node(fixture.lods[0]);
    check(null_bound->bound_field_available() && !null_bound->box_owner(),"Authored null bound is available and remains null.");
    auto unavailable=resource->mutable_node(fixture.lods[1]);
    check(!unavailable->bound_field_available() && !unavailable->matrix_field_available(),"Unavailable fields must not become invented defaults.");
    rejects([&]{unavailable->box_owner();},"Unavailable bound read must reject.");
    rejects([&]{unavailable->set_box_owner(box);},"Unavailable bound write must reject.");
    rejects([&]{unavailable->matrix_owner();},"Unsupported matrix read must reject.");
    rejects([&]{unavailable->placement_node();},"Unsupported concrete placement setter must reject.");
    const auto old_list=first->child_list_owner();
    auto shared=std::make_shared<ActorSceneNodeList>();shared->replace({unavailable,unavailable});
    first->set_child_list_owner(shared);second->set_child_list_owner(shared);
    const auto captured_storage=shared->storage_owner();
    check(first->children().size()==2 && first->children()[0]==first->children()[1] && second->child_list_owner()==shared,
        "Live child list slot publication must preserve ordered aliases and shared list identity.");
    shared->replace({null_bound});
    check(shared->storage_owner()==captured_storage && captured_storage->values().size()==4 && captured_storage->values()[0]==null_bound && captured_storage->values()[1]==unavailable,
        "Within-capacity list writes must preserve backing aliases and inactive capacity slots.");
    check(std::ranges::equal(first->children(),std::vector<std::shared_ptr<const ActorSceneNode>>{null_bound}) && std::ranges::equal(second->children(),first->children()),
        "Mutating the actual child list must update every attached node.");
    rejects([&]{shared->append(first);},"A shared-list mutation introducing a strong cycle must reject.");
    check(std::ranges::equal(shared->values(),std::vector<std::shared_ptr<const ActorSceneNode>>{null_bound}),"Rejected child mutation must preserve the existing list.");
    rejects([&]{null_bound->set_child_list_owner(shared);},"Attaching a list containing the node itself must reject.");
    rejects([&]{shared->append({});},"A runtime null child must reject.");
    rejects([&]{shared->replace(std::vector<std::shared_ptr<const ActorSceneNode>>(65537,null_bound));},"Runtime child allocation must remain bounded.");
    first->set_child_list_owner({});
    check(!first->child_list_owner() && first->children().empty() && second->child_list_owner()==shared,
        "A null child-list slot must remain distinct from another node's retained list.");
    check(old_list->values().size()==1 && old_list->values()[0]==null_bound,"Replacing a list slot must not rewrite its captured old list.");
    ActorSceneResource other(doc,"synthetic/mutable.igb",8);
    const auto preloaded_box=other.box(box_ref);
    check(other.mutable_node(fixture.nodes[1])->box_owner()==preloaded_box && preloaded_box!=box,
        "Node preparation must reuse an earlier physical box query while separate loads remain independent.");
    const auto document=resource->document_owner();resource.reset();
    check(first->document().objects.size()==document->objects.size() && placement->matrix==matrix && second->box_owner()==box,
        "Captured node, matrix, box and document owners must survive resource release.");
    std::weak_ptr<ActorSceneNode> weak_first=first;
    first.reset();
    check(weak_first.expired(),"Weak child backlinks must not keep a detached node alive.");
    auto bad=doc;
    const auto bad_field=[&](IgbDocument& value,std::uint32_t ref,std::uint16_t slot)->IgbFieldValue& {
        auto& item=value.objects.at(value.references.at(ref).value_index);
        return *const_cast<IgbFieldValue*>(value.field(item,slot));
    };
    bad_field(bad,fixture.nodes[0],8).size=60;
    ActorSceneResource matrix_width(bad,"synthetic/bad.igb",1);
    rejects([&]{matrix_width.node(fixture.nodes[0]);},"Truncated authored node matrix must reject.");
    bad=doc;bad_field(bad,box_ref,2).size=8;
    ActorSceneResource box_width(bad,"synthetic/bad.igb",1);
    rejects([&]{box_width.node(fixture.nodes[0]);},"Truncated authored AABox endpoint must reject.");
    bad=doc;bad_field(bad,fixture.nodes[0],3).type_index=1;
    ActorSceneResource bound_type(bad,"synthetic/bad.igb",1);
    rejects([&]{bound_type.node(fixture.nodes[0]);},"Wrong bound field type must reject before publication.");
}
void serialized_child_list_aliases() {
    pf::RuntimeFixture fixture;
    const auto first=fixture.add(pf::geometry_node),second=fixture.add(pf::geometry_node),leaf=fixture.add(pf::geometry_node);
    const auto list=fixture.list(pf::node_list,{leaf,leaf});
    fixture.scalar(first,7,list);fixture.scalar(second,7,list);fixture.scalar(leaf,7,pf::null_ref);
    auto resource=load(fixture);const auto a=resource.mutable_node(first),b=resource.mutable_node(second),child=resource.mutable_node(leaf);
    check(a->child_list_owner()==b->child_list_owner(),"Two serialized child slots referencing one physical list must retain one mutable owner.");
    check(child->child_field_available() && !child->child_list_owner() && child->children().empty(),
        "Authored null igGroup child lists must remain available and null.");
    a->child_list_owner()->replace({child});
    check(std::ranges::equal(b->children(),std::vector<std::shared_ptr<const ActorSceneNode>>{child}),"Mutation through a serialized list alias must reach its other parent.");
}
void physical_child_backing() {
    pf::RuntimeFixture fixture;
    const auto first=fixture.add(pf::geometry_node),second=fixture.add(pf::geometry_node);
    const auto leaf=fixture.add(pf::geometry_node),alternate=fixture.add(pf::geometry_node);
    fixture.scalar(leaf,7,pf::null_ref);fixture.scalar(alternate,7,pf::null_ref);
    const auto first_list=fixture.list(pf::node_list,{leaf,pf::null_ref,pf::null_ref,pf::null_ref});
    const auto second_list=fixture.add(pf::node_list);fixture.records[second_list]=fixture.records[first_list];
    fixture.scalar(first_list,2,0);fixture.scalar(second_list,2,1);
    fixture.scalar(first,7,first_list);fixture.scalar(second,7,second_list);
    auto resource=load(fixture);const auto a=resource.mutable_node(first)->child_list_owner(),b=resource.mutable_node(second)->child_list_owner();
    const auto node=resource.node(leaf),other=resource.node(alternate);
    check(a!=b && a->storage_owner()==b->storage_owner() && a->count()==0 && b->count()==1 && a->capacity()==4,
        "Distinct authored list objects must keep independent counts and their shared physical capacity allocation.");
    check(a->values().empty() && a->first_backing_node()==node,
        "Original first-backing read must accept logical count zero with an addressable node in slot zero.");
    const auto captured=a->storage_owner();
    check(captured->values().size()==4 && !captured->values()[1] && !captured->values()[3],
        "All authored capacity slots, including nullable inactive entries, must be retained.");
    a->append(other);
    check(a->storage_owner()==captured && b->values()[0]==other && b->count()==1,
        "A within-capacity append must write the actual aliased slot without changing another list's count.");
    a->append(node);a->append(other);a->append(node);a->append(other);
    check(a->capacity()==8 && a->count()==5 && a->storage_owner()!=captured && b->storage_owner()==captured &&
        captured->values().size()==4 && a->storage_owner()->values()[3]==node && !a->storage_owner()->values()[7],
        "Growth must replace only the growing list's backing, copy old capacity and preserve all old aliases.");
    b->replace({node});
    check(captured->values()[0]==node && a->first_backing_node()==other,
        "Old backing mutation after another list grows must remain on the captured physical allocation.");
    const auto grown=a->storage_owner();a->clear();
    check(a->count()==0 && a->capacity()==8 && a->storage_owner()==grown && a->first_backing_node()==other,
        "Logical clear must preserve capacity storage for the original unconditional first-slot read.");
    auto empty=std::make_shared<ActorSceneNodeList>();
    rejects([&]{empty->first_backing_node();},"Missing first backing storage must reject.");
    fixture.scalar(first_list,2,5);auto invalid_count=load(fixture);const auto raw=invalid_count.mutable_node(first)->child_list_owner();
    check(raw->first_backing_node()->reference()==leaf,"Raw first-slot read must not invent a logical count gate.");
    rejects([&]{raw->values();},"An out-of-range logical view must still reject at the logical consumer.");
    fixture.scalar(first_list,2,0);fixture.contents(first_list,{pf::null_ref});fixture.scalar(first_list,2,0);
    auto null_slot=load(fixture);
    check(!null_slot.mutable_node(first)->child_list_owner()->first_backing_node(),"A physical null first slot must remain null for the caller's node check.");
    fixture.contents(first_list,{first});fixture.scalar(first_list,2,0);auto cycle=load(fixture);
    rejects([&]{cycle.node(first);},"Strong cycles through inactive backing slots must reject without relying on logical count.");
}
void mutable_depth_budget() {
    pf::RuntimeFixture fixture;std::vector<std::uint32_t> refs;
    for(unsigned i=0;i<130;++i)refs.push_back(fixture.add(pf::geometry_node));
    for(std::size_t i=0;i<refs.size();++i)fixture.scalar(refs[i],7,fixture.list(pf::node_list,
        i<127 ? std::vector<std::uint32_t>{refs[i+1]} : std::vector<std::uint32_t>{}));
    auto resource=load(fixture);auto root=resource.mutable_node(refs[0]);
    auto leaf=resource.mutable_node(refs[127]),extension=resource.mutable_node(refs[128]);
    leaf->child_list_owner()->append(extension);
    check(resource.node(refs[0])==root,"A mutation ending at the exact 128-edge path limit must remain valid.");
    rejects([&]{extension->child_list_owner()->append(resource.node(refs[129]));},"Runtime mutation must include all ancestor paths in its depth check.");
    check(extension->children().empty(),"Rejected ancestor-depth extension must leave its original list intact.");
    auto cycle=std::make_shared<ActorSceneNodeList>();cycle->append(root);
    rejects([&]{extension->set_child_list_owner(cycle);},"A cycle through a captured ancestor must reject.");
}
void ordered_owners() {
    static_assert(!std::is_default_constructible_v<ActorSceneNode> && !std::is_copy_constructible_v<ActorSceneNode>);
    static_assert(!std::is_default_constructible_v<ActorSceneAppearance> && !std::is_copy_assignable_v<ActorSceneAppearance>);
    static_assert(!std::is_default_constructible_v<ActorSceneAppearanceList> && !std::is_copy_assignable_v<ActorSceneAppearanceList>);
    static_assert(!std::is_copy_constructible_v<ActorSceneSkeleton>);
    pf::RuntimeFixture fixture;
    auto doc=fixture.document();
    const auto refs=doc.object_list(fixture.appearance_lists[0]);
    fixture.contents(fixture.appearance_lists[0],{*refs[2],*refs[0],*refs[1],*refs[0]});
    doc=fixture.document();
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[*refs[0]]);
    auto resource=std::make_shared<ActorSceneResource>(doc,"synthetic/player.igb",3);
    const auto scene=resource->action(fixture.action,fixture.action_rams[0]);
    const auto list=scene.appearances();
    check(list->values().size()==4 && list->values()[0]->reference()==*refs[2] &&
        list->values()[1]==list->values()[3],"Appearance order and repeated physical owners must survive decoding.");
    check(scene.actor_references()==fixture.ordered_actors[0],"Actor order must remain independent from the appearance list.");
    check(resource->appearance(alias)==list->values()[1],"Reference aliases must share the actual appearance owner.");
    check(resource->appearance_list(fixture.appearance_lists[0])==list &&
        resource->action(fixture.action,fixture.action_rams[1]).skeleton()==scene.skeleton(),
        "Repeated queries and different RAMs must preserve list/skeleton ownership.");
    check(scene.skeleton()->find_bone("Bone 2")==2 && scene.skeleton()->find_bone("bone 2")==2 &&
        scene.skeleton()->find_bone("Bone 4")==-1,"Bone lookup must retain the original ASCII uppercase comparison.");
    auto first=std::make_shared<const ActorBoneSelector>(2,std::vector<std::shared_ptr<const ActorSceneNode>>{});
    auto second=std::make_shared<const ActorBoneSelector>(2,std::vector<std::shared_ptr<const ActorSceneNode>>{});
    auto selectors=list->values()[0]->bone_selectors();
    check(selectors==list->values()[1]->bone_selectors(),"Shared serialized selector lists must retain one mutable owner.");
    selectors->append(first);selectors->append(second);selectors->append(first);
    check(selectors->remove(first)==2 && selectors->values()==std::vector<std::shared_ptr<const ActorBoneSelector>>{second},
        "REMOVE must delete all pointer-equal occurrences and preserve different equal-looking selectors.");
    check(selectors->remove(first)==0,"Removing an absent selector must leave other attachments intact.");
    ActorSceneResource other(doc,"synthetic/player.igb",3);
    check(other.appearance(*refs[0])!=resource->appearance(*refs[0]),"Separate loads with equal numeric provenance must own separate objects.");
    resource.reset();
    check(list->source()=="synthetic/player.igb" && scene.skeleton()->find_bone("Bone 3")==3 &&
        selectors->values().front()==second,"Captured scene owners must survive the resource facade.");
    rejects([&]{selectors->append({});},"Null attachment owner must reject.");
    rejects([&]{ActorBoneSelector invalid(0,{nullptr});},"Null selector child must reject.");
    rejects([&]{scene.skeleton()->find_bone(std::string("Bone\0 2",7));},"Embedded-null bone names must reject.");
}
void cached_depth_budget() {
    pf::RuntimeFixture fixture;std::vector<std::uint32_t> nodes;
    for(unsigned i=0;i<130;++i)nodes.push_back(fixture.add(pf::geometry_node));
    for(std::size_t i=0;i<nodes.size();++i)
        fixture.scalar(nodes[i],7,fixture.list(pf::node_list,i+1<nodes.size() ?
            std::vector<std::uint32_t>{nodes[i+1]} : std::vector<std::uint32_t>{}));
    auto cold=load(fixture);rejects([&]{cold.node(nodes[0]);},"Cold node graph must reject more than128 edges.");
    auto warm=load(fixture);const auto subtree=warm.node(nodes[1]);
    check(subtree==warm.node(nodes[1]),"Exact128-edge subtree must remain reusable.");
    rejects([&]{warm.node(nodes[0]);},"Cached subtree height must count toward a new parent's path.");
    auto incremental=load(fixture);
    for(std::size_t i=nodes.size();i>1;--i)incremental.node(nodes[i-1]);
    rejects([&]{incremental.node(nodes[0]);},"Bottom-up cache preparation must not bypass path depth.");
    check(warm.node(nodes[1])==subtree,"Rejected parent must preserve the valid cached subtree owner.");
}
void aggregate_budgets() {
    pf::RuntimeFixture fixture;
    const auto full=fixture.list(pf::appearance_list,std::vector<std::uint32_t>(65535,fixture.appearance));
    std::array<std::uint32_t,4> lists{full};
    for(std::size_t i=1;i<lists.size();++i) {
        lists[i]=fixture.add(pf::appearance_list);fixture.records[lists[i]].values=fixture.records[full].values;
    }
    const auto overflow=fixture.list(pf::appearance_list,{fixture.appearance,fixture.appearance,fixture.appearance});
    auto resource=load(fixture);const auto first=resource.appearance_list(lists[0]);
    for(std::size_t i=1;i<lists.size();++i)resource.appearance_list(lists[i]);
    check(resource.appearance_list(lists[0])==first && first->values().front()==first->values().back(),
        "Shared storage and repeated list entries must retain their identities without recharging a cached list.");
    rejects([&]{resource.appearance_list(overflow);},"Distinct lists sharing one memory graph must share the aggregate entry budget.");
    rejects([&]{resource.skeleton(fixture.skeleton);},"Hierarchy entries must share the same aggregate budget as appearance lists.");
    check(resource.appearance_list(lists[0])==first,"Rejected new owners must preserve already-published list identity.");

    pf::RuntimeFixture names;for(const auto bone:names.bones)names.text(bone,2,std::string(4096,'n'));
    std::vector<std::uint32_t> skeletons{names.skeleton};
    for(unsigned i=1;i<1024;++i) {
        const auto ref=names.add(pf::skeleton_type);names.records[ref].values=names.records[names.skeleton].values;skeletons.push_back(ref);
    }
    const auto root=names.add(pf::geometry_node),empty=names.add(pf::geometry_node);
    names.scalar(empty,7,names.list(pf::node_list,{}));std::vector<std::uint32_t> children;
    for(unsigned i=0;i<4;++i) {
        const auto child=names.add(pf::geometry_node);names.text(child,2,std::string(4096,'x'));
        names.scalar(child,7,names.list(pf::node_list,{}));children.push_back(child);
    }
    names.scalar(root,7,names.list(pf::node_list,children));
    auto text=load(names);const auto retained=text.skeleton(skeletons.front());
    for(std::size_t i=1;i<1023;++i)text.skeleton(skeletons[i]);
    rejects([&]{text.skeleton(skeletons.back());},"Distinct skeleton owners sharing bone storage must not restart the text budget.");
    rejects([&]{text.node(root);},"Node names must share and accumulate the resource's existing copied-text budget.");
    check(text.skeleton(skeletons.front())==retained && text.node(empty)->children().empty(),
        "Failed text preparation must preserve cached owners and release its uncommitted budget.");
}
void node_graph() {
    pf::RuntimeFixture fixture;
    const auto root=fixture.add(pf::geometry_node),child=fixture.add(pf::geometry_node);
    fixture.text(root,2,"Attachment");fixture.text(child,2,"");fixture.scalar(root,5,2);
    fixture.scalar(child,7,fixture.list(pf::node_list,{}));
    fixture.scalar(root,7,fixture.list(pf::node_list,{child,child}));
    auto doc=fixture.document();
    const auto alias=static_cast<std::uint32_t>(doc.references.size());doc.references.push_back(doc.references[child]);
    auto resource=std::make_shared<ActorSceneResource>(doc,"synthetic/nodes.igb",4);
    const auto node=resource->node(root);
    check(node->name()=="Attachment" && node->flags()==2 && node->children().size()==2 &&
        node->children()[0]==node->children()[1] && node->children()[0]==resource->node(alias),
        "Node DAG aliases, flags and ordered duplicate children must be preserved.");
    check(node->children()[0]->name()=="","A non-null empty node name must remain distinct from null.");
    resource.reset();
    check(node->children()[0]->document().object(child).reference_index==child,
        "Retained nodes must retain original geometry/material document access.");
    fixture.scalar(child,7,fixture.list(pf::node_list,{root}));
    auto cyclic=load(fixture);rejects([&]{cyclic.node(root);},"Node cycles must reject.");
    fixture.scalar(child,7,fixture.list(pf::node_list,{fixture.skeleton}));
    auto wrong=load(fixture);rejects([&]{wrong.node(root);},"Non-node child must reject.");
    fixture.scalar(child,7,fixture.list(pf::node_list,{}));
    doc=fixture.document();
    auto& object=doc.objects.at(doc.references[root].value_index);
    const_cast<IgbFieldValue*>(doc.field(object,5))->size=1;
    ActorSceneResource width(doc,"synthetic/nodes.igb",4);rejects([&]{width.node(root);},"Wrong-width flags must reject.");
    doc=fixture.document();auto& repeated=doc.objects.at(doc.references[root].value_index);
    repeated.fields.push_back(repeated.fields.front());
    ActorSceneResource slots(doc,"synthetic/nodes.igb",4);rejects([&]{slots.node(root);},"Repeated field slots must reject.");
    doc=fixture.document();doc.objects.at(doc.references[child].value_index).reference_index=root;
    ActorSceneResource bad_alias(doc,"synthetic/nodes.igb",4);
    rejects([&]{bad_alias.canonical_reference(child);},"Broken physical alias back-reference must reject.");
}
void malformed() {
    pf::RuntimeFixture fixture;
    rejects([&]{ActorSceneResource invalid(fixture.document(),"",3);},"Empty source must reject.");
    rejects([&]{ActorSceneResource invalid(fixture.document(),"fixture",std::numeric_limits<std::uint32_t>::max());},"Reserved occurrence must reject.");
    auto doc=fixture.document();doc.metadata.header.byte_order=IgbByteOrder::big;
    rejects([&]{ActorSceneResource invalid(doc,"fixture",3);},"Unverified byte order must reject.");
    auto resource=load(fixture);
    rejects([&]{resource.action(fixture.action,fixture.model_rams[0]);},"Foreign RAM must reject.");
    rejects([&]{resource.appearance(fixture.actor);},"Wrong appearance type must reject.");
    fixture.contents(fixture.info_reference,{fixture.model});
    auto unexported=load(fixture);rejects([&]{unexported.action(fixture.action,fixture.action_rams[0]);},"Unexported ACTION must reject.");
    fixture.contents(fixture.info_reference,{fixture.model,fixture.action});
    fixture.contents(fixture.database_skeletons,{});
    auto no_skeleton=load(fixture);rejects([&]{no_skeleton.action(fixture.action,fixture.action_rams[0]);},"Missing first skeleton must reject.");
    fixture.contents(fixture.database_skeletons,{fixture.skeleton});
    fixture.scalar(fixture.appearance_lists[0],3,65537);
    auto capacity=load(fixture);rejects([&]{capacity.appearance_list(fixture.appearance_lists[0]);},"Unbounded appearance capacity must reject.");
    fixture.scalar(fixture.appearance_lists[0],3,4);
    auto memory=load(fixture);rejects([&]{memory.appearance_list(fixture.appearance_lists[0]);},"Mismatched list storage must reject.");
    fixture.scalar(fixture.appearance_lists[0],3,3);
    fixture.scalar(fixture.appearance,5,fixture.list(pf::matrix_bone_list,{fixture.owner}));
    auto authored=load(fixture);rejects([&]{authored.appearance(fixture.appearance);},"Unprepared authored selector factory must remain an explicit boundary.");
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    ActorSceneResource resource(read_igb_document(mounted.read("_common/p_agumon.igb")),"_common/p_agumon.igb",17);
    const std::array<std::uint32_t,4> lists{6392,6547,6480,5878};
    const std::array<std::array<std::uint32_t,3>,4> appearances{{{6404,6444,6452},{6476,6511,6519},{6543,6577,6585},{6609,6649,6657}}};
    std::shared_ptr<const ActorSceneSkeleton> skeleton;
    const std::array<std::uint32_t,4> model_nodes{6390,6457,6524,6590};
    std::shared_ptr<CollisionBox> actor_box;
    for(std::uint32_t i=0;i<4;++i) {
        auto model_node=resource.mutable_node(model_nodes[i]);
        check(model_node==resource.node(model_nodes[i]) && model_node->type()=="beModelCtrlNode2" &&
            model_node->bound_field_available() && !model_node->box_owner() && model_node->children().size()==1,
            "Original model roots must use canonical node2 owners with authored null bounds and one LOD child.");
        const auto authored=resource.document().field_bytes(*resource.document().field(resource.document().object(model_nodes[i]),8));
        const auto matrix=model_node->matrix_owner();
        for(std::size_t word=0;word<16;++word) {
            std::uint32_t bits{};for(unsigned byte=0;byte<4;++byte)bits|=std::uint32_t(authored[word*4+byte])<<(8*byte);
            check(std::bit_cast<std::uint32_t>((*matrix)[word])==bits,"Original node transform word changed during canonical field decoding.");
        }
        check(model_node->children()[0]->children().size()==3,"Original LOD must retain three ordered actor owners.");
        for(const auto& actor:model_node->children()[0]->children()) {
            const auto mutable_actor=resource.mutable_node(actor->reference());
            if(!actor_box)actor_box=mutable_actor->box_owner();
            check(actor_box && mutable_actor->box_owner()==actor_box,
                "All twelve original actors must retain the same physical authored AABox allocation.");
        }
        const auto action=resource.action(546,250+i);
        if(!skeleton) skeleton=action.skeleton();
        check(action.skeleton()==skeleton && skeleton->reference()==701 && skeleton->find_bone("RArm04")==15 &&
            skeleton->find_bone("LArm04")==11,"Original shared first skeleton or weapon bone indices differ.");
        check(action.appearances()->reference()==lists[i] && action.appearances()->values().size()==3,
            "Original ACTION appearance list differs.");
        for(std::size_t j=0;j<3;++j)
            check(action.appearances()->values()[j]->reference()==appearances[i][j] &&
                action.appearances()->values()[j]->bone_selectors()->values().empty(),"Original appearance order/initial attachments differ.");
    }
}
}
int main(int argc,char** argv) {
    try {
        ordered_owners();node_graph();mutable_node_fields();serialized_child_list_aliases();physical_child_backing();mutable_depth_budget();malformed();cached_depth_budget();aggregate_budgets();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Actor scene checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Actor scene after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
