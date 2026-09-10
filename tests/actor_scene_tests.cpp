#include "dw4/actor_scene.hpp"
#include "dw4/resources.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <iostream>
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
    for(std::uint32_t i=0;i<4;++i) {
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
        ordered_owners();node_graph();malformed();cached_depth_budget();aggregate_budgets();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Actor scene checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Actor scene after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
