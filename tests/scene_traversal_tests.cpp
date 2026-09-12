#include "dw4/scene_traversal.hpp"
#include "dw4/resources.hpp"
#include "dw4/weapon_resource.hpp"
#include "script_fixtures.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace dw4;
using namespace dw4::test;
static_assert(!std::is_copy_assignable_v<SceneAttributeOwner> && !std::is_move_assignable_v<SceneAttributeOwner>);
unsigned checks{};
void check(bool value,const char* text) {++checks;if(!value)throw std::runtime_error(text);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const SceneTraversalError&){return;}
    throw std::runtime_error("Invalid scene operation was accepted.");
}
AnimationMatrix identity() {AnimationMatrix value{};value[0]=value[5]=value[10]=value[15]=1;return value;}
SceneGraphicsScope rigid() {SceneGraphicsScope value;value.inherited_vertex_blend_enabled=false;value.inherited_vertex_blend_list_present=false;value.prepared_transform_clock=0;return value;}
struct Fixture : ScriptFixture {
    struct N {std::uint32_t ref,storage,children,attributes;std::string type;};
    struct A {std::uint32_t ref,storage;std::string type;};
    std::vector<N> nodes;
    std::vector<A> attrs;
    std::uint32_t attribute(std::string type,std::uint32_t value=0,std::uint16_t unit=0) {
        const auto ref=add(object);
        // Geometry now also has a canonical live owner. Keep its authored
        // pointer/scalar fields valid while leaving vertex/index/group owners
        // null: this render-submission fixture never maps triangle data.
        auto data=words({unit,value});
        if(type=="igGeometryAttr1_5")append(data,words({null_ref,null_ref,3,0,0,0,null_ref}));
        attrs.push_back({ref,memory(std::move(data)),std::move(type)});return ref;
    }
    std::uint32_t make_node(std::string type,std::vector<std::uint32_t> children={},
        std::vector<std::uint32_t> attributes={},std::uint32_t flags=0,bool trigger=false,
        AnimationMatrix matrix=identity(),std::uint32_t target=0,std::uint32_t input=null_ref) {
        const auto ref=add(node),child_list=list(object_list,children),attr_list=list(object_list,attributes);
        auto data=words({flags,child_list,attr_list,trigger ? 1u : 0u});
        for(const auto value:matrix)push32(data,std::bit_cast<std::uint32_t>(value));
        push32(data,target);push32(data,input);
        nodes.push_back({ref,memory(std::move(data)),child_list,attr_list,std::move(type)});return ref;
    }
    IgbDocument document() const {
        auto doc=ScriptFixture::document();
        const auto type=[&](std::string name,std::int32_t parent) {
            for(std::size_t i=0;i<doc.metadata.object_types.size();++i)if(doc.metadata.object_types[i].name==name)return static_cast<std::uint32_t>(i);
            IgbObjectType value;value.name=std::move(name);value.parent_index=parent;value.slots=12;
            doc.metadata.object_types.push_back(std::move(value));return static_cast<std::uint32_t>(doc.metadata.object_types.size()-1);
        };
        const auto group=type("igGroup",node),attr_set=type("igAttrSet",static_cast<std::int32_t>(group));
        const auto attr=type("igAttr",object),node_list=type("igNodeList",object_list),attr_list=type("igAttrList",object_list);
        const auto geometry_attr=type("igGeometryAttr",static_cast<std::int32_t>(attr));
        const auto short_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back({"igShortMetaField",1,0});
        const auto obj=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        const auto offset=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
        for(const auto& n:nodes) {
            auto& value=obj(n.ref);value.type_index=type(n.type,n.type=="igGeometry" || n.type=="igOverrideAttrSet" ?
                static_cast<std::int32_t>(attr_set) : static_cast<std::int32_t>(group));const auto at=offset(n.storage);
            value.fields.insert(value.fields.end(),{{5,1,at,4},{7,2,at+4,4}});
            if(n.type=="igTransform")value.fields.insert(value.fields.end(),{{8,7,at+16,64},{9,1,at+80,4},{11,2,at+84,4}});
            else if(n.type=="igAttrSet" || n.type=="igGeometry" || n.type=="igOverrideAttrSet")
                value.fields.insert(value.fields.end(),{{8,2,at+8,4},{9,4,at+12,1}});
            obj(n.children).type_index=node_list;obj(n.attributes).type_index=attr_list;
        }
        for(const auto& a:attrs) {
            auto& value=obj(a.ref);value.type_index=type(a.type,static_cast<std::int32_t>(a.type=="igGeometryAttr1_5" ? geometry_attr : attr));
            const auto at=offset(a.storage);
            if(a.type=="igGeometryAttr1_5")
                value.fields={{2,short_type,at,2},{4,2,at+8,4},{5,2,at+12,4},{6,6,at+16,4},
                    {7,5,at+20,4},{8,5,at+24,4},{10,1,at+28,4},{13,2,at+32,4}};
            else value.fields={{2,short_type,at,2},{4,4,at+4,1}};
        }
        return doc;
    }
    std::shared_ptr<ActorSceneResource> resource() const {return std::make_shared<ActorSceneResource>(document(),"synthetic/scene.igb",4);}
};
struct Execution {
    BoneSelectContext bones;
    SceneTraversalContext scene;
    std::shared_ptr<const ActorBoneSelector> selector;
    std::array<AnimationMatrix,1> matrices{identity()};
    BoneSelectTraversal bone_call;
    std::optional<SceneTraversal> call;
    explicit Execution(std::vector<std::shared_ptr<const ActorSceneNode>> children,SceneGraphicsScope scope=rigid())
        :scene(std::move(scope)),selector(std::make_shared<const ActorBoneSelector>(0,std::move(children))),
         bone_call(bones.begin(selector,std::span<const AnimationMatrix>(matrices))) {}
    BoneSelectResult run(const SceneTraversalServices& services={}) {
        return bones.resume(bone_call,[this,services](BoneSelectContext& context,const auto& owner)->std::optional<std::int32_t> {
            if(!call)call.emplace(scene.begin(context,owner));
            const auto result=scene.resume(*call,services);
            if(result.stop==SceneTraversalStop::faulted)throw SceneTraversalError(result.diagnostic);
            return result.value;
        });
    }
};
void retained_geometry() {
    Fixture f;const auto shade=f.attribute("igShadeModelAttr"),texture=f.attribute("igTextureStateAttr",1);
    const auto geometry_attr=f.attribute("igGeometryAttr1_5");
    const auto geometry=f.make_node("igGeometry",{},{geometry_attr},28,true);
    const auto root=f.make_node("igAttrSet",{geometry,geometry},{shade,texture},2,false);
    auto resource=f.resource();Execution e({resource->node(root)});
    SceneTraversalServices services;unsigned attempts{},consumed{};const SceneSubmissionRequest* saved{};std::vector<SceneSubmissionRequest> draws;
    services.submit_geometry=[&](const auto& request) {
        ++attempts;
        if(!saved) {saved=&request;return false;}
        if(consumed==0)check(saved==&request,"Paused geometry request identity changed.");
        ++consumed;draws.push_back(request);return true;
    };
    check(e.run(services).stop==BoneSelectStop::children_required && e.scene.attribute_depth()==2 && e.scene.node_depth()==2,
        "Trigger-zero parent did not retain both attrs through the child pause.");
    resource.reset();SceneTraversalServices replacement;replacement.submit_geometry=[](const auto&)->bool {throw std::runtime_error("Replaced retained service");};
    // The outer BoneSelectChildren callable also remains retained.
    check(e.run(replacement).stop==BoneSelectStop::completed && consumed==2 && attempts==3,
        "Paused geometry was replayed, skipped, or switched services.");
    check(e.scene.attribute_depth()==0 && e.bones.stack_depth()==0 && !e.scene.pending(),"Completed child scope did not restore both attribute layers.");
    check(draws[0].node==draws[1].node && draws[0].root_to_owner==std::vector<std::uint32_t>{root,geometry} &&
        draws[0].attributes.size()==1 && draws[0].attributes[0]->reference()==geometry_attr &&
        draws[0].effective_attributes.size()==2 && draws[0].effective_attributes[0]->reference()==shade &&
        draws[0].effective_attributes[1]->reference()==texture && draws[0].geometry_flags==28,
        "Ordered attributes, physical DAG owner, exact path or cache flags changed.");
    check(draws[0].attributes[0]==draws[1].attributes[0] && draws[0].node->source()=="synthetic/scene.igb",
        "Repeated geometry lost canonical attribute/document lifetime.");
}
void gates_and_transform() {
    Fixture f;const auto attr=f.attribute("igGeometryAttr1_5");
    const auto skipped=f.make_node("igGeometry",{},{},32,true),off=f.make_node("igGeometry",{},{},0,false);
    const auto leaf=f.make_node("igGeometry",{skipped},{attr},28,true);
    auto local=identity();local[0]=-1;local[1]=std::bit_cast<float>(0x250d3000u);local[4]=std::bit_cast<float>(0xa50d3000u);local[5]=-1;local[12]=7;
    const auto transform=f.make_node("igTransform",{leaf},{},2,false,local);
    const auto root=f.make_node("igGroup",{skipped,off,leaf,transform});auto resource=f.resource();Execution e({resource->node(root)});
    std::vector<SceneSubmissionRequest> draws;SceneTraversalServices service;service.submit_geometry=[&](const auto& r){draws.push_back(r);return true;};
    check(e.run(service).stop==BoneSelectStop::completed && draws.size()==2,"Geometry trigger/invisible gates or child suppression differ.");
    check(draws[0].node==draws[1].node && draws[0].model_view==identity() && draws[1].model_view==original_matrix_multiply(identity(),local) &&
        draws[1].root_to_owner==std::vector<std::uint32_t>{root,transform,leaf},"Shared left path lost exact local matrix composition.");
    check(std::bit_cast<std::uint32_t>(draws[1].model_view[1])==0x250d3000u,"Stored nonzero transform word was idealized.");
    auto clock_scope=rigid();clock_scope.prepared_transform_clock.reset();Execution clock({resource->node(transform)},clock_scope);
    unsigned queries{};const SceneSubmissionRequest* request{};
    service.sample_transform_clock=[&](const auto& r)->std::optional<std::uint64_t> {
        if(!request)request=&r;else check(request==&r,"Transform clock retry replaced its retained query owner.");
        return ++queries==1 ? std::nullopt : std::optional<std::uint64_t>{0};
    };
    check(clock.run(service).stop==BoneSelectStop::children_required && clock.scene.model_view()==identity(),
        "Transform clock pause occurred after matrix composition.");
    auto current=local;current[12]=83;*resource->mutable_node(transform)->matrix_owner()=current;
    draws.clear();
    check(clock.run(service).value==0 && queries==2,"Transform clock was skipped, resampled, or lost a zero time value.");
    check(draws.size()==1 && draws[0].model_view==original_matrix_multiply(identity(),current),
        "Transform callback resume must compose the actual current node matrix, not stale serialized bytes.");
    Fixture bad;const auto input=bad.make_node("igTransform",{},{},2,false,identity(),0,0);auto invalid=bad.resource();Execution b({invalid->node(input)});
    check(b.run(service).stop==BoneSelectStop::faulted,"Non-null transform input bypassed its actual update service.");
}
void mutable_children() {
    Fixture f;const auto first=f.make_node("ExternalNode"),second=f.make_node("ExternalNode");
    const auto group=f.make_node("igGroup",{first});auto resource=f.resource();auto node=resource->mutable_node(group);
    auto scope=rigid();scope.previsit_enabled=true;
    Execution e({node},scope);std::vector<std::uint32_t> visited;SceneTraversalServices services;
    services.previsit=[&](const SceneSubmissionRequest& request)->std::optional<std::int32_t> {
        if(request.node==node) {
            auto list=std::make_shared<ActorSceneNodeList>();list->replace({resource->node(second),resource->node(second)});
            node->set_child_list_owner(std::move(list));
        }
        return 0;
    };
    services.node_handler=[&](const SceneSubmissionRequest& request)->std::optional<std::int32_t> {
        visited.push_back(request.node->reference());
        node->set_child_list_owner({});return 0;
    };
    check(e.run(services).stop==BoneSelectStop::completed && visited==std::vector{second,second},
        "Child traversal must capture the live list after previsit and preserve its ordered storage through nested slot replacement.");
    check(node->children().empty(),"A completed child callback's actual list-slot publication was lost.");
}
void captured_child_backing() {
    Fixture f;const auto first=f.make_node("ExternalNode"),second=f.make_node("ExternalNode");
    const auto group=f.make_node("igGroup",{first,second});auto resource=f.resource();
    const auto root=resource->mutable_node(group);const auto first_node=resource->node(first);
    const auto list=root->child_list_owner();const auto backing=list->storage_owner();
    Execution in_place({root});std::vector<std::uint32_t> visited;SceneTraversalServices services;
    services.node_handler=[&](const SceneSubmissionRequest& request)->std::optional<std::int32_t> {
        visited.push_back(request.node->reference());
        if(visited.size()==1) {list->replace({first_node,first_node});list->clear();}
        return 0;
    };
    check(in_place.run(services).stop==BoneSelectStop::completed && visited==std::vector{first,first} &&
        list->storage_owner()==backing && list->values().empty(),
        "Captured child end must ignore later count clear while same-backing slot writes remain visible.");
    Execution after_clear({root});visited.clear();
    check(after_clear.run(services).stop==BoneSelectStop::completed && visited.empty(),
        "A later child traversal must observe the current zero logical count.");
    list->replace({first_node,resource->node(second)});Execution growing({root});visited.clear();
    services.node_handler=[&](const SceneSubmissionRequest& request)->std::optional<std::int32_t> {
        visited.push_back(request.node->reference());
        if(visited.size()==1) {list->append(first_node);list->replace({first_node,first_node,first_node});}
        return 0;
    };
    check(growing.run(services).stop==BoneSelectStop::completed && visited==std::vector{first,second} &&
        list->storage_owner()!=backing && backing->values()[1]->reference()==second,
        "List growth must preserve the captured old backing and end rather than redirect an active traversal.");
}
void flush_and_dispatch() {
    Fixture f;const auto a=f.attribute("igMaterialAttr"),leaf=f.make_node("ExternalNode"),later=f.make_node("ExternalNode");
    const auto root=f.make_node("igAttrSet",{leaf,later},{a},0,true);auto resource=f.resource();Execution e({resource->node(root)});
    std::vector<std::uint32_t> order;unsigned flushes{};SceneTraversalServices s;
    s.flush_attributes=[&](const auto& r){++flushes;check(r.attributes.size()==1 && r.effective_attributes.size()==1,"Trigger flush did not see its pushed attribute.");return flushes>1;};
    s.node_handler=[&](const auto& r)->std::optional<std::int32_t>{order.push_back(r.node->reference());return 1;};
    check(e.run(s).stop==BoneSelectStop::children_required && order.empty() && e.scene.attribute_depth()==1,"Triggered attr flush did not pause before children.");
    const auto result=e.run(s);check(result.stop==BoneSelectStop::completed && result.value==0 && order==std::vector{leaf},"Child return1 did not stop siblings and map to zero.");
    Execution propagate({resource->node(root)});s.flush_attributes=[](const auto&){return true;};s.node_handler=[](const auto&)->std::optional<std::int32_t>{return 2;};
    check(propagate.run(s).value==2 && propagate.scene.attribute_depth()==0,"Child return2 did not restore attrs and propagate.");
    auto scope=rigid();scope.previsit_enabled=true;Execution previsit({resource->node(root)},scope);unsigned visited{};
    s.previsit=[&](const auto& r)->std::optional<std::int32_t>{++visited;return r.node->reference()==root ? 2 : 7;};
    s.flush_attributes=[](const auto&)->bool {throw std::runtime_error("Previsit bypass entered AttrSet");};
    check(previsit.run(s).value==0 && visited==3 && previsit.scene.attribute_depth()==0,"Previsit return2 failed to bypass handler and traverse children.");
    const auto override_node=f.make_node("igOverrideAttrSet",{leaf},{a});resource=f.resource();Execution override_call({resource->node(override_node)});
    check(override_call.run().stop==BoneSelectStop::children_required && override_call.scene.attribute_depth()==0,"Override attribute set was conflated with the normal handler.");
}
void blend_and_failure() {
    Fixture f;const auto geo_attr=f.attribute("igGeometryAttr1_5"),enabled=f.attribute("igVertexBlendStateAttr",255),disabled=f.attribute("igVertexBlendStateAttr",0);
    const auto list=f.attribute("igVertexBlendMatrixListAttr"),leaf=f.make_node("igGeometry",{},{geo_attr},28,true);
    const auto root=f.make_node("igAttrSet",{leaf},{enabled});auto resource=f.resource();SceneTraversalServices s;AnimationMatrix view{};
    s.submit_geometry=[&](const auto& r){view=r.model_view;return true;};
    auto scope=rigid();scope.bone_root_known=true;scope.bone_root=identity();(*scope.bone_root)[12]=9;
    Execution enabled_call({resource->node(root)},scope);check(enabled_call.run(s).value==0 && view==*scope.bone_root,"Enabled local blend did not substitute actual bone root.");
    Execution paused_blend({resource->node(root)},scope);unsigned blend_calls{};
    s.submit_geometry=[&](const auto& r) {
        check(paused_blend.scene.model_view()==r.model_view && r.model_view==*scope.bone_root,
            "Geometry service did not observe its temporary original root override.");
        return ++blend_calls>1;
    };
    check(paused_blend.run(s).stop==BoneSelectStop::children_required && paused_blend.scene.model_view()==scope.bone_root,
        "Paused geometry lost its temporary enabled-blend model-view.");
    check(paused_blend.run(s).value==0 && !paused_blend.scene.model_view(),"Completed geometry retained its temporary model-view override.");
    s.submit_geometry=[&](const auto& r){view=r.model_view;return true;};
    scope.bone_root.reset();Execution null_bones({resource->node(root)},scope);check(null_bones.run(s).value==0 && view==identity(),"Null TOP bone pointer incorrectly replaced model-view.");
    scope.inherited_attributes.push_back({std::make_shared<const SceneAttributeOwner>(resource->node(root),disabled),true});
    scope.bone_root_known=false;Execution pinned({resource->node(root)},scope);check(pinned.run(s).value==0 && view==identity(),"Inherited override selection failed to win over a normal local blend push.");
    SceneGraphicsScope unknown;Execution missing({resource->node(leaf)},unknown);
    check(missing.run(s).stop==BoneSelectStop::faulted && missing.scene.faulted(),"Unknown inherited blend state silently became disabled.");
    const auto list_root=f.make_node("igAttrSet",{leaf},{list});resource=f.resource();Execution palette({resource->node(list_root)});
    check(palette.run(s).stop==BoneSelectStop::faulted && palette.scene.attribute_depth()==1,"Pushed matrix-list attribute ignored its stack-count branch.");
    const auto diagnostic=palette.scene.resume(*palette.call).diagnostic;
    check(!diagnostic.empty() && palette.scene.resume(*palette.call).diagnostic==diagnostic,"Fault inspection changed the retained scene failure.");
    rejects([&]{palette.scene.begin(palette.bones,palette.selector);});palette.scene.reset();
    check(palette.scene.attribute_depth()==0 && palette.scene.resume(*palette.call).stop==SceneTraversalStop::faulted,"Explicit reset did not invalidate the retained handle.");
    scope=rigid();scope.bone_root_known=true;unsigned invocations{};
    Execution throwing({resource->node(root)},scope);s.submit_geometry=[&](const auto&)->bool{++invocations;throw std::runtime_error("Submission fault");};
    check(throwing.run(s).stop==BoneSelectStop::faulted && throwing.scene.attribute_depth()==1 && invocations==1,
        "Faulted service was not reached or silently popped the unfinished scope.");
    check(throwing.scene.resume(*throwing.call,s).stop==SceneTraversalStop::faulted && invocations==1,
        "Fault inspection replayed an uncertain consumed geometry submission.");
    SceneGraphicsScope default_list=rigid();default_list.inherited_attributes.push_back({std::make_shared<const SceneAttributeOwner>(resource->node(leaf),list),false});
    Execution empty_stack({resource->node(leaf)},default_list);s.submit_geometry=[](const auto&){return true;};
    check(empty_stack.run(s).value==0,"A default matrix-list owner was mistaken for a pushed matrix-list stack entry.");
    Execution reentrant({resource->node(leaf)});s.submit_geometry=[&](const auto&){rejects([&]{reentrant.scene.reset();});return true;};
    check(reentrant.run(s).value==0,"Rejected reentrant reset damaged the completed service.");
}
void malformed() {
    Fixture f;const auto attr=f.attribute("igMaterialAttr"),leaf=f.make_node("igGeometry",{},{attr},28,true);auto doc=f.document();
    auto& object=doc.objects.at(doc.references[attr].value_index);object.fields.front().size=1;
    auto resource=std::make_shared<ActorSceneResource>(doc,"synthetic/invalid.igb",1);Execution e({resource->node(leaf)});
    SceneTraversalServices s;s.submit_geometry=[](const auto&){return true;};check(e.run(s).stop==BoneSelectStop::faulted,"Wrong-width attribute unit was accepted.");
    auto scope=rigid();scope.bone_root=identity();rejects([&]{SceneTraversalContext invalid(scope);});
    rejects([&]{SceneAttributeOwner missing({},0);});
    Fixture effects;const auto matrix=effects.attribute("igModelViewMatrixAttr"),group=effects.make_node("igAttrSet",{},{matrix});
    auto unsupported=effects.resource();Execution effect({unsupported->node(group)});
    check(effect.run().stop==BoneSelectStop::faulted,"Unimplemented matrix attribute silently left model-view unchanged.");
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    for(const auto& name:{"_common/w_common05.igb","_common/w_common04.igb","_common/w_common03.igb"}) {
        auto resource=std::make_shared<ActorSceneResource>(read_igb_document(mounted.read(name)),name,9);
        const auto weapon=decode_weapons(resource);const auto& info=weapon.infos().at(0);
        std::vector<SceneSubmissionRequest> draws;SceneTraversalServices s;s.submit_geometry=[&](const auto& r){draws.push_back(r);return true;};
        for(const auto& node:info.nodes) {Execution e({node.node});check(e.run(s).value==0,"Original attached child graph did not reach its consumed render submission.");}
        check(draws.size()==info.nodes.size() && draws[0].attributes.size()==1 && draws[0].geometry_flags==28,
            "Original weapon draw count, descriptor count or cache flags differ.");
        check(draws[0].effective_attributes.size()==4 && draws[0].model_view==identity(),"Original right path lost its four normal inherited attrs.");
        if(draws.size()==2) {
            check(draws[0].node==draws[1].node && draws[1].root_to_owner.size()==3 && draws[1].path[1]->type()=="igTransform",
                "Original left path did not retain its actual transform and shared geometry owner.");
            const auto& transform=*draws[1].path[1];const auto& doc=transform.document();const auto bytes=doc.field_bytes(*doc.field(doc.object(transform.reference()),8));
            AnimationMatrix local{};for(std::size_t i=0;i<16;++i) {std::uint32_t word{};for(unsigned b=0;b<4;++b)word|=std::uint32_t(bytes[i*4+b])<<(8*b);local[i]=std::bit_cast<float>(word);}
            check(draws[1].model_view==original_matrix_multiply(identity(),local),"Original left matrix differs from the exact stored words.");
        }
    }
}
}
int main(int argc,char** argv) {
    try {retained_geometry();gates_and_transform();mutable_children();captured_child_backing();flush_and_dispatch();blend_and_failure();malformed();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Common scene traversal checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Common scene traversal after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
