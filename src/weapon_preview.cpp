#include "weapon_preview.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/materials.hpp"
#include "dw4/scene_traversal.hpp"
#include "dw4/weapon_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dw4::viewer {
namespace {
constexpr AnimationMatrix identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
constexpr auto maximum_time=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
[[noreturn]] void fail(const std::string& reason) { throw std::runtime_error("Prepared weapon inspection: "+reason); }
void finite(const AnimationMatrix& value) {
    for(const auto element:value)if(!std::isfinite(element))fail("a supplied view or placement matrix is not finite.");
}
PlayerRuntimeBinding bind_player(const std::shared_ptr<ActorSceneResource>& body,WeaponPreviewSelection selection) {
    if(!body)fail("the actual loaded body resource is required.");
    if(selection.ram_index>3)fail("the player RAM ordinal is outside 0..3.");
    const auto& doc=body->document();
    auto result=decode_player_runtime_binding(doc,{selection.model_reference,selection.action_reference,selection.ram_index,0},
        body->source(),body->source_occurrence());
    if(selection.actor_index>=result.actors().size())fail("the actor ordinal is outside the selected ACTION RAM.");
    if(result.selected_actor_index()!=selection.actor_index) {
        const auto list=doc.field_reference(doc.object(result.lod_reference()),7);
        if(!list)fail("the selected player's LOD has no actual branch list.");
        const auto branches=doc.object_list(*list);
        const auto target=result.actors()[selection.actor_index].actor_reference;
        const auto found=std::find(branches.begin(),branches.end(),std::optional<std::uint32_t>{target});
        if(found==branches.end())fail("the requested ACTION actor is absent from its LOD.");
        result=decode_player_runtime_binding(doc,{selection.model_reference,selection.action_reference,selection.ram_index,
            static_cast<std::uint32_t>(found-branches.begin())},body->source(),body->source_occurrence());
    }
    if(result.selected_actor_index()!=selection.actor_index || result.skin_binding().geometry_reference()!=selection.geometry_reference)
        fail("the explicitly selected geometry does not belong to the selected player's actor.");
    return result;
}
AnimationSourceRuntime load_sources(const ActorSceneResource& resource,const ActorSkinBinding& binding) {
    std::vector<AnimationClip> clips;
    for(const auto& info:list_igb_animations(resource.document(),binding.skin()))
        clips.push_back(decode_igb_animation(resource.document(),binding.skin(),info.reference_index));
    AnimationSourceRuntime result(resource.source(),std::move(clips));result.initialize_action_offset();return result;
}
MeshTextureOptions options(const MaterialInfo& material) {
    const auto wrap=[](MaterialWrap value) {
        switch(value) {
        case MaterialWrap::repeat:return MeshTextureWrap::repeat;
        case MaterialWrap::clamp_to_edge:return MeshTextureWrap::clamp_to_edge;
        case MaterialWrap::mirrored_repeat:return MeshTextureWrap::mirrored_repeat;
        }
        fail("the material wrap mode is unsupported.");
    };
    const auto filter=[](MaterialFilter value) {
        switch(value) {
        case MaterialFilter::nearest:return MeshTextureFilter::nearest;
        case MaterialFilter::linear:return MeshTextureFilter::linear;
        }
        fail("the material filter is unsupported.");
    };
    return {wrap(material.wrap_s),wrap(material.wrap_t),filter(material.min_filter),filter(material.mag_filter)};
}
void require_action(const ActionResult& result) {
    if(result.stop!=ActionStop::end && result.stop!=ActionStop::waiting)
        fail("ACTION stopped at an unimplemented service or command (stop "+std::to_string(static_cast<int>(result.stop))+
            (result.command_reference ? ", reference "+std::to_string(*result.command_reference) : "")+").");
}
std::uint32_t geometry(const SceneSubmissionRequest& request) {
    std::optional<std::uint32_t> result;
    for(const auto& attribute:request.attributes)
        if(attribute->document().is_a(attribute->document().object(attribute->reference()),"igGeometryAttr1_5")) {
            if(result)fail("a geometry submission contains multiple geometry attributes.");
            result=attribute->reference();
        }
    if(!result)fail("a geometry submission has no represented geometry attribute.");
    return *result;
}
using AttributeKey=std::pair<const IgbDocument*,std::uint32_t>;
std::vector<AttributeKey> attributes(const std::vector<std::shared_ptr<const SceneAttributeOwner>>& values) {
    std::vector<AttributeKey> result;
    for(const auto& value:values)result.emplace_back(&value->document(),value->reference());
    return result;
}
void require_material_attributes(const SceneSubmissionRequest& request,bool consumed_skin_blend=false) {
    // The current texture pass represents the neutral authored material and
    // smooth-shade profile. Changed colors/lighting and other render attributes
    // must reach their own implementation rather than disappearing here.
    for(const auto& attribute:request.attributes)
        if(attribute->type()!="igGeometryAttr1_5" || attribute->unit()!=0)
            fail("a local geometry attribute needs an unimplemented submission service: "+attribute->type());
    for(const auto& attribute:request.effective_attributes) {
        if(attribute->unit()!=0)fail("a material attribute selects an unsupported unit.");
        const auto& doc=attribute->document();const auto& object=doc.object(attribute->reference());
        const auto& type=attribute->type();
        if(type=="igTextureBindAttr" || type=="igTextureStateAttr")continue; // Checked by the material resolver.
        if(type=="igVertexBlendStateAttr" && consumed_skin_blend) {
            const auto* value=doc.field(object,4);
            if(!value || value->size!=1 || doc.metadata.field_types.at(value->type_index).name!="igBoolMetaField" ||
                doc.field_bytes(*value)[0]!=1)fail("the body selector differs from its consumed enabled CPU skinning state.");
            continue;
        }
        const auto check_words=[&](std::uint16_t slot,std::string_view meta,std::span<const std::uint32_t> expected) {
            const auto* value=doc.field(object,slot);
            if(!value || value->size!=expected.size()*4 || doc.metadata.field_types.at(value->type_index).name!=meta)
                fail("a represented material field has an unsupported type or width.");
            const auto bytes=doc.field_bytes(*value);
            for(std::size_t i=0;i<expected.size();++i) {
                std::uint32_t word{};for(unsigned b=0;b<4;++b)word|=std::uint32_t(bytes[i*4+b])<<(b*8);
                if(word!=expected[i])fail("a material attribute differs from the represented neutral texture profile.");
            }
        };
        const std::array<std::uint32_t,1> zero{0},smooth{1};
        const std::array<std::uint32_t,4> white{0x3f800000,0x3f800000,0x3f800000,0x3f800000},black{0,0,0,0x3f800000};
        if(type=="igShadeModelAttr")check_words(4,"igEnumMetaField",smooth);
        else if(type=="igMaterialAttr") {
            check_words(4,"igFloatMetaField",zero);check_words(5,"igVec4fMetaField",white);check_words(6,"igVec4fMetaField",white);
            check_words(7,"igVec4fMetaField",black);check_words(8,"igVec4fMetaField",black);check_words(9,"igUnsignedIntMetaField",zero);
        } else fail("an inherited material attribute needs an unimplemented submission service: "+type);
    }
}
MaterialInfo body_material(ActorSceneResource& resource,const ActorSkinBinding& binding) {
    if(!binding.skin().info.geometry_node_reference)fail("the selected skin has no original geometry-node owner.");
    const auto selector=resource.node(binding.selector_reference());
    const auto owner=resource.node(*binding.skin().info.geometry_node_reference);
    if(selector->children().size()!=1 || selector->children().front()!=owner)
        fail("the body material path requires the verified direct selector-to-geometry skin profile.");
    SceneSubmissionRequest request;request.node=owner;request.root_to_owner={selector->reference(),owner->reference()};
    const auto collect=[&](const auto& node,auto& output) {
        const auto& doc=node->document();const auto list=doc.field_reference(doc.object(node->reference()),8);
        if(!list)fail("a body attribute scope has no actual attribute list.");
        for(const auto ref:doc.object_list(*list)) {
            if(!ref)fail("a body attribute list contains a null owner.");
            output.push_back(std::make_shared<const SceneAttributeOwner>(node,*ref));
        }
    };
    collect(owner,request.attributes);collect(selector,request.effective_attributes);
    require_material_attributes(request,true);
    return resolve_igb_material_on_path(resource.document(),binding.geometry_reference(),request.root_to_owner);
}
}

struct WeaponPreview::Impl {
    struct Draw {
        MeshData mesh;
        std::shared_ptr<const RgbaImage> image;
        MaterialInfo material;
        std::shared_ptr<const ActorSceneNode> node;
        std::vector<std::uint32_t> path;
        std::vector<AttributeKey> local_attributes,effective_attributes;
        std::uint32_t flags{};
    };
    std::shared_ptr<ActorSceneResource> body,weapon;
    WeaponPreviewSelection selection;
    PlayerRuntimeBinding binding;
    AnimationSourceRuntime sources;
    ActionEventRuntime events;
    WeaponRuntime weapons;
    std::shared_ptr<ActionRuntime> action;
    ActorSceneAction scene;
    std::shared_ptr<ActorSceneAppearance> appearance;
    ControllerObjectId model_id;
    ActionServices action_services;
    AnimationMatrix placement;
    BoneSelectContext bones;
    std::vector<std::unique_ptr<Draw>> draws;
    std::vector<MeshBatch> batches;
    std::map<AttributeKey,std::shared_ptr<const RgbaImage>> images;
    std::size_t total_vertices{},total_triangles{};
    std::uint64_t total_image_bytes{};
    WeaponPreviewFrame current;
    std::uint64_t runtime_time{};
    bool published{},failed{};

    Impl(std::shared_ptr<ActorSceneResource> body_resource,std::shared_ptr<ActorSceneResource> weapon_resource,
        WeaponPreviewSelection selected,std::string weapon_name,std::string action_name,AnimationMatrix fit)
        :body(std::move(body_resource)),weapon(std::move(weapon_resource)),selection(selected),binding(bind_player(body,selected)),
         sources(load_sources(*body,binding.skin_binding())),weapons(events),
         action(std::make_shared<ActionRuntime>(decode_action_actor_runtime(body->document(),binding.action_program(),sources,
             static_cast<std::int32_t>(selected.ram_index)))),
         scene(body->action(binding.action_reference(),binding.action_ram_reference())),placement(fit) {
        finite(placement);
        if(!weapon || body->source_occurrence()==weapon->source_occurrence())
            fail("body and weapon require distinct actual loaded source occurrences.");
        model_id=events.register_model(binding,ModelControlState(ModelMotionPose(identity)),true);
        const auto& info=body->document().object(binding.model_reference());
        const auto* name_field=body->document().field(info,2);
        if(!name_field)fail("the actual model info has no name field.");
        // IGB inline strings include a checked four-byte payload length. A
        // size-four field therefore has zero payload, not a three-character
        // string. Even null strings pass the shared type/length validation.
        const auto parent_text=body->document().field_string(info,2);
        const auto name=std::make_shared<std::optional<std::string>>(name_field->size==4 ? std::nullopt :
            std::optional<std::string>{parent_text});
        events.prepare_model_queries(model_id,{{body->source_occurrence(),binding.model_reference()},body->source(),name,
            std::make_shared<std::int32_t>(static_cast<std::int32_t>(selected.ram_index)),
            std::make_shared<std::uint8_t>(std::uint8_t{1}),std::make_shared<bool>(true)});
        events.register_player(model_id);
        const auto action_id=events.register_action(model_id,action);events.prepare_action_scene(action_id,scene);
        action_services=events.action_services(model_id);
        require_action(action->dispatch(sources,action_services,action_name,0));
        if(!action->active() || !action->visible())fail("the selected ACTION did not activate a visible actor.");
        const auto program=decode_weapons(weapon);
        const auto found=std::find_if(program.infos().begin(),program.infos().end(),[&](const auto& value) {
            return value.name.value_or("")==weapon_name;
        });
        if(found==program.infos().end() || selected.ram_index>=found->rams.size())
            fail("the requested weapon name/RAM does not identify an actual exported weapon.");
        weapons.register_source(program);
        auto generate=events.messenger().begin_send_to("PLAYERARMS",weapon_name,static_cast<std::int32_t>(selected.ram_index),"GENERATE");
        const auto generated=events.resume(generate);
        if(generated.stop!=MessengerStop::completed)fail("original GENERATE requires a service: "+generated.diagnostic);
        const auto selected_appearance=binding.actors()[selected.actor_index].appearance_reference;
        for(const auto& value:scene.appearances()->values())if(value->reference()==selected_appearance) {
            if(appearance)fail("the actual appearance list repeats the selected appearance.");
            appearance=value;
        }
        if(!appearance)fail("the selected actor's appearance is absent from its ACTION query result.");
        if(weapons.attachments(selected.ram_index).empty() || appearance->bone_selectors()->values().empty())
            fail("original GENERATE did not attach a weapon to the selected appearance.");
        for(const auto& attachment:weapons.attachments(selected.ram_index))
            if(attachment.appearances!=scene.appearances() ||
                std::find(appearance->bone_selectors()->values().begin(),appearance->bone_selectors()->values().end(),attachment.selector)==
                    appearance->bone_selectors()->values().end())
                fail("generated tracking and the selected appearance do not retain the same original selector owners.");
        append_draw(body->document(),selected.geometry_reference,body_material(*body,binding.skin_binding()));
        capture(identity,identity,0);
    }
    void append_draw(const IgbDocument& doc,std::uint32_t reference,MaterialInfo material) {
        if(draws.size()>=4096)fail("the prepared pass exceeds 4096 draw occurrences.");
        if(!material.supported || !material.image_reference)fail("material submission is unsupported: "+material.diagnostic);
        auto draw=std::make_unique<Draw>();
        draw->mesh=decode_igb_mesh(doc,reference);draw->mesh.texcoords=decode_igb_texcoords(doc,reference);
        if(draw->mesh.positions.size()>4194304-total_vertices || draw->mesh.indices.size()/3>4194304-total_triangles)
            fail("the prepared pass exceeds its aggregate vertex or triangle budget.");
        total_vertices+=draw->mesh.positions.size();total_triangles+=draw->mesh.indices.size()/3;
        const auto key=AttributeKey{&doc,*material.image_reference};
        auto image=images.find(key);
        if(image==images.end()) {
            auto decoded=std::make_shared<const RgbaImage>(decode_igb_texture(doc,*material.image_reference));
            if(decoded->rgba.size()>256ULL*1024*1024-total_image_bytes)fail("the prepared pass exceeds its decoded image budget.");
            total_image_bytes+=decoded->rgba.size();image=images.emplace(key,std::move(decoded)).first;
        }
        draw->image=image->second;draw->material=std::move(material);
        const auto sampler=options(draw->material);
        batches.push_back({&draw->mesh,draw->image.get(),sampler,false});draws.push_back(std::move(draw));
    }
    const WeaponPreviewFrame& capture(const AnimationMatrix& view,const AnimationMatrix& inverse_view,std::uint64_t time) {
        if(failed)fail("a prior runtime/traversal failure retained this inspection's incomplete work.");
        finite(view);finite(inverse_view);
        if(time>maximum_time || time<runtime_time)fail("time must advance within the nonnegative signed clock range.");
        try {
            if(!published || time!=current.time)require_action(action->update(sources,action_services,time));
            if(!action->active() || !action->visible())fail("the current ACTION no longer has a visible actor.");
            const auto& state=action->appearance();
            if(state.color || state.blend || state.texture || state.lighting)
                fail("the current ACTION requires its original appearance override submission service.");
            action->traverse_actor(sources,selection.actor_index,original_matrix_multiply(view,placement),inverse_view,time);
            const auto& actor=action->actors()->states().at(selection.actor_index);
            const auto skin=capture_actor_skin(binding.skin_binding(),*action->actors(),action->combiner());
            WeaponPreviewFrame next;next.time=time;next.appearance_reference=appearance->reference();
            next.body_pose=evaluate_actor_skin(binding.skin_binding(),skin);next.model_views.push_back(skin.model_view);
            // Preserve the actor's selected TOP/F8 data by value while both the
            // body and every common attachment are submitted in this scope.
            const auto bone_array=actor.cache_used ? actor.bone_matrices : action->combiner().bone_matrices();
            if(bone_array.empty())fail("the selected actor has no published bone array.");
            SceneGraphicsScope scope;
            // Explicit reset inspection context: original default vertex-blend
            // state false and matrix-list stack count zero, not inferred from
            // the absence of a convenient attribute on the weapon node.
            scope.inherited_vertex_blend_enabled=false;scope.inherited_vertex_blend_list_present=false;
            scope.bone_root_known=true;scope.bone_root=bone_array.front();
            scope.prepared_transform_clock=action->node_time(time);
            bones.rewind_pool();std::size_t cursor=1;
            const auto selectors=appearance->bone_selectors()->values();next.attachment_count=selectors.size();
            for(const auto& selector:selectors) {
                SceneTraversalContext child(scope);
                SceneTraversalServices services;
                services.submit_geometry=[&](const SceneSubmissionRequest& request) {
                    require_material_attributes(request);
                    const auto reference=geometry(request);
                    if(!published) {
                        append_draw(request.node->document(),reference,
                            resolve_igb_material_on_path(request.node->document(),reference,request.root_to_owner));
                        auto& draw=*draws.back();draw.node=request.node;draw.path=request.root_to_owner;
                        draw.local_attributes=attributes(request.attributes);draw.effective_attributes=attributes(request.effective_attributes);
                        draw.flags=request.geometry_flags;
                    }
                    if(cursor>=draws.size())fail("attachment traversal changed the stable uploaded draw count.");
                    const auto& draw=*draws[cursor++];
                    if(draw.node!=request.node || draw.path!=request.root_to_owner || draw.material.geometry_reference!=reference ||
                        draw.local_attributes!=attributes(request.attributes) || draw.effective_attributes!=attributes(request.effective_attributes) ||
                        draw.flags!=request.geometry_flags)
                        fail("attachment traversal changed an uploaded geometry or material owner.");
                    next.model_views.push_back(request.model_view);return true;
                };
                auto selected=bones.begin(selector,std::span<const AnimationMatrix>(bone_array));
                const auto result=bones.resume(selected,[&](BoneSelectContext& owner,const auto& actual)->std::optional<std::int32_t> {
                    auto traversal=child.begin(owner,actual);const auto completed=child.resume(traversal,services);
                    if(completed.stop!=SceneTraversalStop::completed)fail("common child traversal requires a service: "+completed.diagnostic);
                    return completed.value;
                });
                if(result.stop!=BoneSelectStop::completed)fail("common bone selection stopped: "+result.diagnostic);
                if(result.value==2)fail("the original child requested outer traversal termination.");
            }
            if(cursor!=draws.size() || next.model_views.size()!=batches.size())fail("attachment traversal changed the uploaded pass layout.");
            draws.front()->mesh.positions=next.body_pose.positions;
            current=std::move(next);runtime_time=time;published=true;return current;
        } catch(...) {failed=true;throw;}
    }
};

WeaponPreview::WeaponPreview(std::shared_ptr<ActorSceneResource> body,std::shared_ptr<ActorSceneResource> weapon,
    WeaponPreviewSelection selection,std::string weapon_name,std::string action_name,AnimationMatrix placement)
    :impl_(std::make_unique<Impl>(std::move(body),std::move(weapon),selection,std::move(weapon_name),std::move(action_name),placement)) {}
WeaponPreview::~WeaponPreview()=default;
WeaponPreview::WeaponPreview(WeaponPreview&&) noexcept=default;
WeaponPreview& WeaponPreview::operator=(WeaponPreview&&) noexcept=default;
const WeaponPreviewFrame& WeaponPreview::frame(const AnimationMatrix& view,const AnimationMatrix& inverse_view,std::uint64_t time) {
    if(!impl_)fail("the inspection owner was moved.");return impl_->capture(view,inverse_view,time);
}
void WeaponPreview::set_placement(const AnimationMatrix& placement) {
    if(!impl_)fail("the inspection owner was moved.");
    if(impl_->failed)fail("a prior runtime/traversal failure retained incomplete work.");
    finite(placement);impl_->placement=placement;
}
void WeaponPreview::dispatch_action(std::string_view name,std::uint64_t time) {
    if(!impl_)fail("the inspection owner was moved.");
    if(impl_->failed)fail("a prior runtime/traversal failure retained incomplete work.");
    if(time>maximum_time || time<impl_->runtime_time)fail("ACTION time must advance within the nonnegative signed clock range.");
    try {
        require_action(impl_->action->dispatch(impl_->sources,impl_->action_services,name,time));
        impl_->runtime_time=time;
    } catch(...) {impl_->failed=true;throw;}
}
std::span<const MeshBatch> WeaponPreview::batches() const {if(!impl_)fail("the inspection owner was moved.");return impl_->batches;}
const PlayerRuntimeBinding& WeaponPreview::binding() const {if(!impl_)fail("the inspection owner was moved.");return impl_->binding;}
const ActionRuntime& WeaponPreview::action() const {if(!impl_)fail("the inspection owner was moved.");return *impl_->action;}
} // namespace dw4::viewer
