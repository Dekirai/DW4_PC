#include "weapon_preview.hpp"
#include "dw4/resources.hpp"
#include "dw4/weapon_resource.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <bit>
#include <filesystem>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using namespace dw4::viewer;
unsigned checks{};
constexpr AnimationMatrix identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation,std::string_view diagnostic) {
    bool rejected=false;
    try {operation();}catch(const std::exception& error) {
        rejected=true;
        ++checks;
        if(!diagnostic.empty() && std::string_view(error.what()).find(diagnostic)==std::string_view::npos)
            throw std::runtime_error("Expected weapon preview diagnostic '"+std::string(diagnostic)+"', received '"+error.what()+"'.");
    }
    check(rejected,"Weapon preview accepted an invalid prepared owner or operation.");
}
bool matrices_equal(const AnimationMatrix& left,const AnimationMatrix& right) {
    for(std::size_t i=0;i<left.size();++i)
        if(std::bit_cast<std::uint32_t>(left[i])!=std::bit_cast<std::uint32_t>(right[i]))return false;
    return true;
}
bool positions_equal(const std::vector<Vec3>& left,const std::vector<Vec3>& right) {
    if(left.size()!=right.size())return false;
    for(std::size_t i=0;i<left.size();++i)
        if(left[i].x!=right[i].x || left[i].y!=right[i].y || left[i].z!=right[i].z)return false;
    return true;
}
void synthetic() {
    rejects([]{WeaponPreview invalid({},{},{},"weapon","action",identity);},"actual loaded body resource");
    player_runtime_fixture::RuntimeFixture fixture;
    auto body=std::make_shared<ActorSceneResource>(fixture.document(),"synthetic/player.igb",3);
    WeaponPreviewSelection selection{fixture.model,fixture.action,0,0,fixture.geom};
    auto bad=selection;bad.ram_index=4;
    rejects([&]{WeaponPreview invalid(body,{},bad,"weapon","action",identity);},"outside 0..3");
    bad=selection;bad.actor_index=3;
    rejects([&]{WeaponPreview invalid(body,{},bad,"weapon","action",identity);},"actor ordinal");
    bad=selection;bad.geometry_reference=fixture.vertices;
    rejects([&]{WeaponPreview invalid(body,{},bad,"weapon","action",identity);},"selected geometry");
    auto other=fixture;other.text(other.model,7,"KIND_ENEMY");
    auto enemy=std::make_shared<ActorSceneResource>(other.document(),"synthetic/enemy.igb",4);
    rejects([&]{WeaponPreview invalid(enemy,{},selection,"weapon","action",identity);},"KIND_PLAYER");
    // A same-sized graph is not sufficient: the requested model and ACTION
    // must retain their actual shared ONE/node relationship.
    other=fixture;other.scalar(other.action_rams[0],11,other.nodes[1]);other.scalar(other.action_rams[1],11,other.nodes[0]);
    auto alien=std::make_shared<ActorSceneResource>(other.document(),"synthetic/alien.igb",5);
    rejects([&]{WeaponPreview invalid(alien,{},selection,"weapon","action",identity);},"ONE node differs");
}

AnimationMatrix attached_view(const ActorBoneSelector& selector,const std::vector<AnimationMatrix>& bones) {
    auto view=original_matrix_multiply(bones.at(0),bones.at(static_cast<std::size_t>(selector.matrix_index())));
    auto node=selector.children().at(0);
    // The actual common05/04/03 child paths have one geometry, with one
    // target-zero local Transform in each dual weapon's left occurrence.
    while(node->type()!="igGeometry") {
        if(node->type()=="igTransform") {
            const auto& doc=node->document();const auto& object=doc.object(node->reference());
            check(doc.field_i32(object,9)==0 && !doc.field_reference(object,11),"Original weapon transform profile changed.");
            const auto bytes=doc.field_bytes(*doc.field(object,8));AnimationMatrix local{};
            for(std::size_t i=0;i<16;++i) {
                std::uint32_t word{};for(unsigned byte=0;byte<4;++byte)word|=std::uint32_t(bytes[i*4+byte])<<(byte*8);
                local[i]=std::bit_cast<float>(word);
            }
            view=original_matrix_multiply(view,local);
        }
        check(node->children().size()==1,"Original weapon path is no longer the tested single-child chain.");
        node=node->children()[0];
    }
    return view;
}
void original(const std::filesystem::path& root) {
    const auto mounted=MountedResources::open(root);
    const auto body_document=read_igb_document(mounted.read("_common/p_agumon.igb"));
    struct WeaponCase {const char* resource;const char* name;const char* wait;const char* run;std::size_t nodes;};
    const std::array cases{
        WeaponCase{"_common/w_common05.igb","w_common05","WAITS1","RUNNS1",1},
        WeaponCase{"_common/w_common04.igb","w_common04","WAITG1","RUNNG1",2},
        WeaponCase{"_common/w_common03.igb","w_common03","WAITA1","RUNNA1",2}};
    for(const auto& weapon_case:cases) {
        auto body=std::make_shared<ActorSceneResource>(body_document,"_common/p_agumon.igb",100);
        auto weapon=std::make_shared<ActorSceneResource>(read_igb_document(mounted.read(weapon_case.resource)),weapon_case.resource,200);
        const auto definition=decode_weapons(weapon);
        check(definition.infos().at(0).nodes.size()==weapon_case.nodes,"Original weapon node count changed.");
        std::vector<std::unique_ptr<WeaponPreview>> previews;
        std::vector<AnimationMatrix> first_slot_matrices;
        for(std::uint32_t ram=0;ram<4;++ram) {
            const auto binding=decode_player_runtime_binding(body->document(),{1386,546,ram,1},body->source(),body->source_occurrence());
            check(binding.skin_binding().geometry_reference()==5665,"Explicit high-detail body branch changed.");
            const WeaponPreviewSelection selection{1386,546,ram,static_cast<std::uint32_t>(binding.selected_actor_index()),5665};
            auto preview=std::make_unique<WeaponPreview>(body,weapon,selection,weapon_case.name,weapon_case.wait,identity);
            check(preview->binding().model_ram_reference()==binding.model_ram_reference() &&
                preview->binding().action_ram_reference()==binding.action_ram_reference(),"Preview rebound a different original player RAM.");
            const auto scene=body->action(binding.action_reference(),binding.action_ram_reference());
            std::shared_ptr<ActorSceneAppearance> selected;
            std::vector<std::shared_ptr<const ActorBoneSelector>> selectors;
            for(const auto& appearance:scene.appearances()->values()) {
                check(appearance->bone_selectors()->values().size()==weapon_case.nodes,"GENERATE did not reach every original appearance.");
                if(selectors.empty())selectors=appearance->bone_selectors()->values();
                else check(selectors==appearance->bone_selectors()->values(),"Appearances lost the shared selector identities.");
                if(appearance->reference()==binding.skin_binding().appearance_reference())selected=appearance;
            }
            check(selected!=nullptr,"Selected actor's actual appearance was not retained.");
            for(std::size_t i=0;i<selectors.size();++i)
                check(selectors[i]->children().at(0)==definition.infos()[0].nodes[i].node,"GENERATE replaced an actual loaded weapon child owner.");
            const auto initial=preview->frame(identity,identity,0);
            check(initial.attachment_count==weapon_case.nodes && initial.model_views.size()==weapon_case.nodes+1 &&
                preview->batches().size()==weapon_case.nodes+1,"Weapon preview accepted a body-only or incomplete render pass.");
            check(initial.body_pose.positions.size()==1371,"Body deformation did not use the selected original geometry.");
            for(const auto& batch:preview->batches())check(batch.mesh && batch.texture && !batch.allow_solid_fallback &&
                !batch.mesh->positions.empty() && batch.mesh->texcoords.size()==batch.mesh->positions.size(),
                "Prepared draw lacks its actual geometry, image or UV stream.");
            const auto* first_mesh=preview->batches()[0].mesh;
            std::vector<std::vector<Vec3>> weapon_positions;
            for(std::size_t i=1;i<preview->batches().size();++i)weapon_positions.push_back(preview->batches()[i].mesh->positions);
            auto view=identity;view[12]=3;view[13]=-2;view[14]=5;
            auto inverse=identity;inverse[12]=-3;inverse[13]=2;inverse[14]=-5;
            const auto moved=preview->frame(view,inverse,0);
            check(positions_equal(moved.body_pose.positions,initial.body_pose.positions),"An equal-time view change resampled or moved actor-local body vertices.");
            check(!matrices_equal(moved.model_views[0],initial.model_views[0]),"Prepared view did not move the body once.");
            const auto& bones=preview->action().combiner().bone_matrices();
            check(matrices_equal(moved.model_views[0],bones[0]),"Body model-view did not consume the selected actor root.");
            for(std::size_t i=0;i<selectors.size();++i)
                check(matrices_equal(moved.model_views[i+1],attached_view(*selectors[i],bones)),"Attachment used a joint palette, stale actor root or wrong local matrix order.");
            auto placement=identity;placement[0]=placement[5]=placement[10]=0.5f;placement[12]=11;
            preview->set_placement(placement);
            preview->dispatch_action(weapon_case.run,33333333);
            const auto running=preview->frame(view,inverse,133333333);
            check(preview->action().current_action()==weapon_case.run && running.time==133333333,
                "Host locomotion selection bypassed the actual ACTION owner or clock.");
            check(preview->batches()[0].mesh==first_mesh && positions_equal(preview->batches()[0].mesh->positions,running.body_pose.positions),
                "Updated body draw lost its stable mesh owner or current deformation.");
            for(std::size_t i=0;i<weapon_positions.size();++i)
                check(positions_equal(weapon_positions[i],preview->batches()[i+1].mesh->positions),"A rigid weapon mesh was pretransformed or skinned in place.");
            const auto repeated=preview->frame(view,inverse,133333333);
            check(positions_equal(repeated.body_pose.positions,running.body_pose.positions) && repeated.model_views==running.model_views,
                "Equal-time recapture changed the prepared body or attachment pose.");
            rejects([&]{preview->frame(view,inverse,133333332);},"time must advance");
            auto invalid=identity;invalid[0]=std::numeric_limits<float>::infinity();
            rejects([&]{preview->set_placement(invalid);},"not finite");
            check(preview->frame(view,inverse,133333333).model_views==running.model_views,"Rejected input changed the last complete pose.");
            if(ram==0)first_slot_matrices=running.model_views;
            else check(previews.front()->frame(view,inverse,133333333).model_views==first_slot_matrices,
                "Another prepared player slot overwrote the first slot's captured caches.");
            previews.push_back(std::move(preview));
        }
        auto moved=std::move(*previews.back());
        rejects([&]{previews.back()->batches();},"moved");
        check(moved.batches().size()==weapon_case.nodes+1,"Moving preview ownership lost uploaded mesh/image owners.");
    }
}
}
int main(int argc,char** argv) {
    try {
        synthetic();const auto synthetic_checks=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<"Weapon preview checks: "<<checks<<" (synthetic "<<synthetic_checks<<")\n";return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Weapon preview after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
