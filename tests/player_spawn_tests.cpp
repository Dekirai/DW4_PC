#include "player_runtime_binding_fixture.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_placement.hpp"
#include "dw4/player_point.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/resources.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <set>

namespace {
using namespace dw4;
namespace fixture=player_runtime_fixture;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
AnimationMatrix translated(float x,float y=0,float z=0) {auto value=identity();value[12]=x;value[13]=y;value[14]=z;return value;}
fixture::Bytes matrix_bytes(const AnimationMatrix& value) {
    fixture::Bytes bytes;for(const auto element:value)fixture::push32(bytes,std::bit_cast<std::uint32_t>(element));return bytes;
}
bool same(const AnimationMatrix& a,const AnimationMatrix& b) {
    for(std::size_t i=0;i<a.size();++i)if(std::bit_cast<std::uint32_t>(a[i])!=std::bit_cast<std::uint32_t>(b[i]))return false;
    return true;
}
AnimationMatrix authored_matrix(const IgbDocument& doc,std::uint32_t reference) {
    const auto& object=doc.object(reference);const auto* field=doc.field(object,8);
    check(field && field->size==64 && doc.metadata.field_types.at(field->type_index).name=="igMatrix44fMetaField",
        "Spawn node does not have its actual typed serialized matrix.");
    const auto bytes=doc.field_bytes(*field);AnimationMatrix value{};
    for(std::size_t i=0;i<value.size();++i) {
        const auto at=i*4;const auto word=std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|
            (std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
        value[i]=std::bit_cast<float>(word);check(std::isfinite(value[i]),"Spawn source matrix contains a non-finite word.");
    }
    return value;
}

// The existing full synthetic MODELCTRL/ACTION graph supplies two independent
// RAM/ONE/node chains. Only the generated point-manager records are added here.
struct SpawnFixture : fixture::RuntimeFixture {
    struct Extra {std::uint32_t ref;std::string type,parent;std::map<std::uint16_t,std::pair<std::uint16_t,std::uint32_t>> fields;};
    std::vector<Extra> extra;
    std::uint32_t point_info{},point_ram{};
    std::vector<std::uint32_t> points;
    std::uint32_t custom(const char* type,const char* parent="igObject") {
        const auto ref=add(fixture::object);extra.push_back({ref,type,parent,{}});return ref;
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,fixture::Bytes bytes) {
        const auto mem=memory(std::move(bytes));
        for(auto& object:extra)if(object.ref==ref){object.fields[slot]={type,mem};return;}
        throw std::runtime_error("Synthetic spawn owner was not added.");
    }
    void value(std::uint32_t ref,std::uint16_t slot,std::uint16_t type,std::uint32_t word) {value(ref,slot,type,fixture::words({word}));}
    void label(std::uint32_t ref,const std::string& text) {
        auto bytes=fixture::words({static_cast<std::uint32_t>(text.size()+1)});fixture::name(bytes,text);value(ref,2,0,std::move(bytes));
    }
    SpawnFixture() {
        std::vector<std::uint32_t> groups;
        for(unsigned group=0;group<2;++group) {
            std::vector<std::uint32_t> members;
            for(unsigned player=0;player<2;++player) {
                const auto point=custom("igTransform","igGroup");points.push_back(point);members.push_back(point);
                label(point,"Point");value(point,5,1,0);value(point,7,2,list(fixture::node_list,{}));
                value(point,8,7,matrix_bytes(translated(float(100+group*10+player),float(player),float(group))));
                value(point,9,1,0);value(point,11,2,fixture::null_ref);
            }
            const auto group_ref=custom("beGroupKeep","igGroup");groups.push_back(group_ref);label(group_ref,"Entry");
            value(group_ref,7,2,list(fixture::node_list,members));
        }
        const auto group_list=list(fixture::object_list,groups);extra.push_back({group_list,"beGroupKeepList","igObjectList",{}});
        point_ram=custom("beBaseInfoRam");value(point_ram,2,1,50);value(point_ram,5,4,fixture::Bytes{1});
        for(std::uint16_t slot=6;slot<=9;++slot)value(point_ram,slot,1,0);
        value(point_ram,10,2,fixture::null_ref);
        point_info=custom("bePoint01Info","igInfo");label(point_info,"");value(point_info,4,4,fixture::Bytes{1});
        value(point_info,5,2,fixture::null_ref);value(point_info,6,2,list(fixture::ram_list,{point_ram}));
        value(point_info,7,2,fixture::null_ref);value(point_info,8,2,group_list);
        contents(info_reference,{model,action,point_info});
        for(std::size_t i=0;i<nodes.size();++i)records[nodes[i]].values[8]=matrix_bytes(translated(float(-600-int(i)),22,33));
    }
    IgbDocument document() const {
        auto doc=fixture::RuntimeFixture::document();
        const auto type=[&](const std::string& name,const std::string& parent) {
            for(std::size_t i=0;i<doc.metadata.object_types.size();++i)if(doc.metadata.object_types[i].name==name)return static_cast<std::uint32_t>(i);
            std::int32_t inherited=-1;
            for(std::size_t i=0;i<doc.metadata.object_types.size();++i)if(doc.metadata.object_types[i].name==parent)inherited=static_cast<std::int32_t>(i);
            const auto added=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back({name,1,0,inherited,32,{}});return added;
        };
        for(const auto& extra_object:extra) {
            auto& object=doc.objects.at(doc.references.at(extra_object.ref).value_index);object.type_index=type(extra_object.type,extra_object.parent);
            for(const auto& [slot,entry]:extra_object.fields) {
                const auto& memory=doc.memories.at(doc.references.at(entry.second).value_index);
                object.fields.push_back({slot,entry.first,memory.offset,memory.size});
            }
        }
        return doc;
    }
};

// These are prepared bindings to actual loaded node identities, not the
// original player-work factory or RAM/ONE construction implementation.
struct PreparedBody {
    PlayerRuntimeBinding binding;
    std::shared_ptr<ActorSceneResource> resource;
    std::shared_ptr<const ActorSceneNode> node;
    AnimationMatrix authored;
    std::shared_ptr<AnimationMatrix> matrix;
    std::shared_ptr<ModelPlacementNode> placement;
    std::shared_ptr<ModelMotionPose> old_work,fresh_work;
    PreparedBody(std::shared_ptr<ActorSceneResource> source,PlayerRuntimeRequest request):
        binding(decode_player_runtime_binding(source->document(),request,source->source(),source->source_occurrence())),resource(std::move(source)),
        node(resource->node(binding.node_reference())),authored(authored_matrix(resource->document(),binding.node_reference())),
        matrix(std::make_shared<AnimationMatrix>(authored)),placement(make_model_ctrl_node2_placement(matrix)),
        old_work(std::make_shared<ModelMotionPose>(matrix)),fresh_work(std::make_shared<ModelMotionPose>(matrix)) {
        check(node->type()=="beModelCtrlNode2" && node->reference()==binding.node_reference(),"Prepared spawn setter is not bound to its concrete original node.");
        old_work->turn()={{0.25f,0.5f,0.75f},{1,2,3},{4,5,6}};old_work->position_locked()=true;
        set_player_turn_rates(*fresh_work);
        check(placement->matrix==old_work->matrix_owner() && placement->matrix==fresh_work->matrix_owner(),
            "Retained and new work did not share actual prepared node storage.");
        check(same(*matrix,authored),"Prepared spawn replaced the authored node matrix before the query.");
    }
};
void select_group(ActionEventRuntime& events,std::int32_t group) {
    auto& messenger=events.messenger();messenger.select("PLAYERPOINT",{},-1);messenger.buffer().add_int(group);
    auto dispatch=messenger.begin_send("SETGENERATEPOS");const auto result=events.resume(dispatch);
    check(result.stop==MessengerStop::completed && result.recipients_completed>0,"Original saved-group command did not reach the actual provider.");
}
void caller_packet(ActionEventRuntime& events,const PreparedBody& body) {
    auto& messenger=events.messenger();messenger.select("MODELCTRL",body.binding.model_name(),static_cast<std::int32_t>(body.binding.instance_index()));
    messenger.buffer().add_string("retained sequence argument");
}
// Test-only original argument-buffer exchange at8034D8DC..8034D950. Its caller
// has explicitly prepared the RAM and current packet; this is not GENERATE.
void query_point(ActionEventRuntime& events,std::int32_t live_ram) {
    auto& messenger=events.messenger();const auto parent=messenger.buffer_owner();
    messenger.push_buffer();messenger.select("PLAYERPOINT",{},-1);messenger.buffer().add_int(0);messenger.buffer().add_int(live_ram);
    const auto child=messenger.buffer_owner();auto dispatch=messenger.begin_send("GETMATRIX");const auto result=events.resume(dispatch);
    check(result.stop==MessengerStop::completed && result.recipients_completed>0,"Prepared spawn did not receive a real PLAYERPOINT response.");
    check(child==messenger.buffer_owner() && child->matrices().size()==1,"Point query replaced its packet owner or returned unexpected matrices.");
    const auto selected=child->matrices()[0];messenger.pop_buffer();
    check(parent==messenger.buffer_owner() && parent->strings()==std::vector<std::optional<std::string>>{"retained sequence argument"},
        "Spawn query failed to restore the caller's exact argument owner.");
    messenger.buffer().add_matrix(selected);
}
void place(ActionEventRuntime& events,PreparedBody& body,const AnimationMatrix& expected) {
    const auto before_old=body.old_work->turn(),before_fresh=body.fresh_work->turn();
    unsigned reads{};ModelPlacementInvocation placement([&]{++reads;return body.placement;});
    query_point(events,static_cast<std::int32_t>(body.binding.instance_index()));
    const auto result=placement.resume(events.messenger().buffer());
    check(result.completed && placement.published() && reads==1,"Matrix spawn did not invoke exactly the actual node setter branch.");
    check(same(*body.matrix,expected) && same(body.old_work->matrix(),expected) && same(body.fresh_work->matrix(),expected),
        "Spawn did not replace shared node storage with the original point matrix.");
    check(body.old_work->turn()==before_old && body.fresh_work->turn()==before_fresh && body.old_work->position_locked() && !body.fresh_work->position_locked(),
        "Placement incorrectly shared or reset the separate old/new work fields.");
    check(body.fresh_work->turn().rate==std::array<float,3>{0,7,0} && body.old_work->turn().rate==std::array<float,3>{1,2,3},
        "New-work player turn rates modified the retained old work.");
    check(placement.resume(events.messenger().buffer()).completed && reads==1,"Completed placement replayed its source lookup or setter.");
    check(same(authored_matrix(body.resource->document(),body.binding.node_reference()),body.authored),
        "Placement mutated the immutable original IGB document.");
}
void synthetic() {
    SpawnFixture fixture;auto resource=std::make_shared<ActorSceneResource>(fixture.document(),"synthetic/spawn.igb",7);
    ActionEventRuntime events;PlayerPointRuntime points(events);points.register_source(decode_player_points(resource));
    PreparedBody first(resource,fixture.runtime_request(0,1)),second(resource,fixture.runtime_request(1,1));
    check(first.matrix!=second.matrix && first.node!=second.node && first.binding.one_reference()!=second.binding.one_reference(),
        "Independent player RAM/ONE/node chains were collapsed.");
    for(std::int32_t group=0;group<2;++group) {
        select_group(events,group);caller_packet(events,first);place(events,first,translated(float(100+10*group),0,float(group)));
        caller_packet(events,second);place(events,second,translated(float(101+10*group),1,float(group)));
        check(points.saved_group()==group,"Spawn changed the manager's saved point group.");
    }
    select_group(events,999);caller_packet(events,second);place(events,second,translated(101,1,0));
    check(points.saved_group()==999,"Positive fallback unexpectedly rewrote saved group state.");
    const auto retained=first.old_work;first.old_work.reset();const auto owner=retained->matrix_owner();
    first.fresh_work->turn().desired[0]=0.125f;
    check(retained->turn().desired[0]==0.25f && owner==first.fresh_work->matrix_owner(),"Released old-work facade lost its matrix alias or gained new turn state.");
    auto candidate=*first.fresh_work;candidate.matrix()[12]=1234;
    check((*owner)[12]==110 && candidate.matrix_owner()!=owner,"A candidate mutated the live spawned node before publication.");
    *first.fresh_work=candidate;
    check(first.fresh_work->matrix_owner()==owner && retained->matrix()[12]==1234 && second.matrix->at(12)==101,
        "Candidate publication replaced the actual node owner or changed another player.");
}
void retained_response_and_setter() {
    SpawnFixture fixture;auto resource=std::make_shared<ActorSceneResource>(fixture.document(),"synthetic/spawn.igb",7);
    ActionEventRuntime events;PlayerPointRuntime points(events);points.register_source(decode_player_points(resource));
    PreparedBody body(resource,fixture.runtime_request());caller_packet(events,body);query_point(events,0);
    const auto expected=translated(100,0,0);body.placement->set_matrix={};unsigned reads{};
    ModelPlacementInvocation paused([&]{++reads;return body.placement;});
    check(!paused.resume(events.messenger().buffer()).completed && !paused.published() && same(*body.matrix,body.authored),
        "Missing concrete setter mutated the node or completed prepared spawn.");
    points.set_matrix({7,fixture.points[0]},translated(777));
    events.messenger().select("changed packet",{},-1);events.messenger().buffer().add_matrix(translated(888));
    body.placement->set_matrix=make_model_ctrl_node2_placement(body.matrix)->set_matrix;
    check(paused.resume(events.messenger().buffer()).completed && reads==1 && same(*body.matrix,expected) && same(body.old_work->matrix(),expected),
        "Paused spawn did not retain its queried matrix and original setter-node owner.");
    caller_packet(events,body);place(events,body,translated(777));
    select_group(events,-1);caller_packet(events,body);const auto before=*body.matrix;
    auto& messenger=events.messenger();messenger.select("PLAYERPOINT",{},-1);messenger.buffer().add_int(0);messenger.buffer().add_int(0);
    auto invalid=messenger.begin_send("GETMATRIX");
    check(events.resume(invalid).stop==MessengerStop::faulted && same(*body.matrix,before),
        "Invalid original group access fabricated a successful placement or changed the player node.");
}
void original(const char* root) {
    const auto resources=MountedResources::open(root);
    auto stage=std::make_shared<ActorSceneResource>(read_igb_document(resources.archives().at(0).read(179)),"dng101b1.igb#[0.179]",179);
    auto player=std::make_shared<ActorSceneResource>(read_igb_document(resources.archives().at(0).read(850)),"_common/p_agumon.igb#[0.850]",850);
    ActionEventRuntime events;PlayerPointRuntime points(events);const auto program=decode_player_points(stage);points.register_source(program);
    check(program.infos().size()==1 && program.infos()[0].reference==115 && program.infos()[0].groups.size()==2,
        "Original stage did not provide its actual point info/groups.");
    constexpr std::array<std::uint32_t,4> rams{1725,6417,6420,6622},nodes{6390,6457,6524,6590};
    std::set<std::uint32_t> one_owners;std::vector<std::unique_ptr<PreparedBody>> bodies;
    for(std::uint32_t index=0;index<4;++index) {
        auto body=std::make_unique<PreparedBody>(player,PlayerRuntimeRequest{1386,546,index,1});
        check(body->binding.model_ram_reference()==rams[index] && body->binding.node_reference()==nodes[index] &&
            one_owners.insert(body->binding.one_reference()).second,"Original four player RAM/ONE/node chains differ or alias.");
        check(body->node==player->node(nodes[index]) && body->node->source_occurrence()==850,
            "Original spawn retained another source or scene-node owner.");
        bodies.push_back(std::move(body));
    }
    for(std::int32_t group=0;group<2;++group) {
        select_group(events,group);
        for(std::uint32_t index=0;index<4;++index) {
            auto& body=*bodies[index];caller_packet(events,body);
            const auto point=decode_igb_player_placement(stage->document(),115,static_cast<std::uint32_t>(group),index);
            check(!same(body.authored,point.matrix),"Original editor-layout transform unexpectedly equals the requested spawn point.");
            place(events,body,point.matrix);
            for(std::uint32_t other=0;other<4;++other)if(other!=index)
                check(body.matrix!=bodies[other]->matrix,"Original player matrices share an unrelated RAM owner.");
        }
        check(points.saved_group()==group,"Original four-player spawn changed the saved stage entry group.");
    }
    select_group(events,0x7fffffff);
    for(std::uint32_t index=0;index<4;++index) {
        caller_packet(events,*bodies[index]);place(events,*bodies[index],decode_igb_player_placement(stage->document(),115,0,index).matrix);
    }
    check(points.saved_group()==0x7fffffff,"Original positive group fallback changed manager-wide state.");
}
}
int main(int argc,char** argv) {
    try {
        synthetic();retained_response_and_setter();const auto synthetic_checks=checks;
        if(argc>1)original(argv[1]);
        std::cout<<"Prepared player spawn tests passed: "<<checks<<" checks ("<<synthetic_checks<<" synthetic).\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Prepared player spawn failure after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
