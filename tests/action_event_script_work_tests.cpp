#include "dw4/action_event_runtime.hpp"
#include "dw4/model_script_runtime.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F operation,const char* message) {
    ++checks;try {operation();}catch(const std::exception&) {return;}throw std::runtime_error(message);
}
AnimationMatrix matrix(float x=0) { return {1,0,0,0,0,1,0,0,0,0,1,0,x,0,0,1}; }
ModelControlState state(float x=0) { ModelMotionPose pose(matrix(x));set_player_turn_rates(pose);return ModelControlState(pose); }
constexpr const char* source="synthetic/script-work.igb";
constexpr const char* name="p_fixture";

// The checked player fixture provides both actual node chains. Add only the
// authored HIT graph and the typed MODELCTRL fields required for registration.
struct Fixture : pf::RuntimeFixture {
    std::uint32_t hit,hit_storage,box,box_storage,box_list;
    std::vector<std::uint32_t> ram_storage;
    Fixture() {
        text(pf::RuntimeFixture::model,2,name);text(action,2,name);
        box=add(pf::object);pf::Bytes coordinates;
        for(float value:{-1.f,-1.f,-1.f,1.f,1.f,1.f})pf::push32(coordinates,std::bit_cast<std::uint32_t>(value));
        box_storage=memory(std::move(coordinates));box_list=list(pf::object_list,{box});
        hit=add(pf::base_data_type);text(hit,2,"HIT");hit_storage=memory(pf::words({1,box_list}));
        contents(model_data,{sequence,hit});
        for(std::size_t i=0;i<model_rams.size();++i)ram_storage.push_back(memory(pf::words({0,0,0,0,0,0})));
    }
    IgbDocument document() const {
        auto doc=pf::RuntimeFixture::document();
        auto vector=doc.metadata.field_types[8];vector.name="igVec3fMetaField";
        const auto vector_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());doc.metadata.field_types.push_back(std::move(vector));
        auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        auto offset=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
        auto type=[&](std::uint32_t ref,const char* label,std::int32_t parent) {
            auto descriptor=doc.metadata.object_types.at(object(ref).type_index);descriptor.name=label;descriptor.parent_index=parent;
            object(ref).type_index=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(descriptor));
        };
        type(hit,"beModelCtrlInfoDataHit",pf::base_data_type);const auto h=offset(hit_storage);
        object(hit).fields.insert(object(hit).fields.end(),{{4,4,h,1},{5,4,h+1,1},{6,2,h+4,4},{7,4,h+2,1}});
        type(box,"igAABox",pf::object);const auto b=offset(box_storage);
        object(box).fields={{2,vector_type,b,12},{3,vector_type,b+12,12}};type(box_list,"igAABoxList",pf::object_list);
        object(pf::RuntimeFixture::model).fields.push_back({4,4,offset(ram_storage[0])+4,1});
        for(std::size_t i=0;i<model_rams.size();++i) {
            const auto at=offset(ram_storage[i]);auto& fields=object(model_rams[i]).fields;
            fields.insert(fields.end(),{{2,1,at,4},{5,4,at+4,1},{6,1,at+8,4},{7,1,at+12,4},{8,1,at+16,4},{9,1,at+20,4}});
        }
        return doc;
    }
    PlayerRuntimeBinding binding(std::uint32_t instance=0) const {
        return decode_player_runtime_binding(document(),runtime_request(instance),source,3);
    }
    ModelHitBodyDefinition definition() const {return decode_model_hit_body(document(),pf::RuntimeFixture::model,source,3);}
};

void preparation_and_live_aliases() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto first=runtime.register_model(fixture.binding(),state(2),false);
    const auto second=runtime.register_model(fixture.binding(1),state(7),true);
    rejects([&]{runtime.prepare_target_script_work(first);},"Script work bypassed authored collision ownership.");
    runtime.prepare_collision_target(first,fixture.definition(),false);
    runtime.prepare_collision_target(second,fixture.definition(),true);
    auto a=runtime.prepare_target_script_work(first),b=runtime.prepare_target_script_work(second);
    check(a->target()==first && a->source()==source && a->name()==name && a->ram_no()==0 &&
          b->target()==second && b->ram_no()==1,"Script work lost its qualified target or actual RAM ordinal.");
    check(a->hp && a->max_hp && a->hp!=a->max_hp && a->hp!=b->hp && a->max_hp!=b->max_hp,
        "Prepared targets shared their default HP wrappers.");
    check(a->hp->data==0 && a->max_hp->data==0 && b->hp->data==0 && b->max_hp->data==0 &&
          !a->hp->name && !a->max_hp->name && !b->hp->name && !b->max_hp->name && a->record==-1 && b->record==-1,
        "Script preparation invented player HP, names or record values.");
    check(a->matrix().get()==&runtime.sequence(first).model().pose.matrix &&
          b->matrix().get()==&runtime.sequence(second).model().pose.matrix && a->matrix()!=b->matrix(),
        "Script matrix is a detached pose copy or belongs to another RAM.");
    const auto* stable=a->matrix().get();runtime.prepare_model_state(first,state(13));
    check(a->matrix().get()==stable && (*a->matrix())[12]==13 && (*b->matrix())[12]==7,
        "Live model replacement invalidated the script matrix alias or affected another target.");
    (*a->matrix())[13]=19;
    check(runtime.sequence(first).model().pose.matrix[13]==19,"Script matrix writes did not reach the owning live pose.");
    check(runtime.select_sequence(first,"SQ_WAIT").complete() && a->matrix().get()==stable && (*a->matrix())[13]==19,
        "Sequence entry invalidated the script's stable matrix owner.");
    const auto registrations=runtime.messenger().registration_count();
    rejects([&]{runtime.prepare_target_script_work(first);},"A second script work owner replaced live HP state.");
    check(runtime.messenger().registration_count()==registrations && a->hp->data==0,
        "Rejected script re-preparation changed registrations or live work.");
}

void live_signed_record() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(1),state(),false);
    runtime.prepare_collision_target(target,fixture.definition(),false);
    auto work=runtime.prepare_target_script_work(target);
    check(!runtime.sequence(target).selected(),"Record fixture accidentally selected a sequence.");
    for(const auto value:{std::int32_t{-1},std::numeric_limits<std::int32_t>::min(),std::int32_t{0},std::numeric_limits<std::int32_t>::max()}) {
        work->record=value;
        auto send=runtime.messenger().begin_send_to("MODELCTRL",name,1,"GETRECNO");
        check(runtime.resume(send).stop==MessengerStop::completed &&
              runtime.messenger().buffer().integers()==std::vector<std::int32_t>{value},
            "GETRECNO gated inactive work, dereferenced a wrapper or changed a signed record.");
    }
    work->hp.reset();work->max_hp.reset();work->record=-900;
    auto send=runtime.messenger().begin_send_to("MODELCTRL",name,1,"GETRECNO");
    check(runtime.resume(send).stop==MessengerStop::completed && runtime.messenger().buffer().integers()==std::vector<std::int32_t>{-900},
        "Direct record lookup incorrectly requires HP wrappers.");
}

MessengerDispatch collision_send(ActionEventRuntime& runtime) {
    auto& messenger=runtime.messenger();messenger.select("HITPLAYERBULLET",name,0);
    messenger.buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{40,40,40},{41,41,41}}));
    messenger.buffer().add_string("p_source");
    return messenger.begin_send("CHKBULLET");
}
void live_hp_presence_gate() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(),state(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);
    auto work=runtime.prepare_target_script_work(target);
    check(runtime.select_sequence(target,"SQ_WAIT").complete(),"Collision target sequence preparation failed.");
    runtime.prepare_wait_state(target,{std::nullopt,1,0,0});
    auto live=runtime.sequence(target).model();live.invulnerability_frames=5;runtime.prepare_model_state(target,std::move(live));
    unsigned time_calls{};runtime.set_collision_services({[&]{++time_calls;return std::optional<std::uint64_t>{1};},{}});
    work->hp.reset();auto absent=collision_send(runtime);
    check(runtime.resume(absent).stop==MessengerStop::completed && time_calls==0 && !runtime.collision_target(target).body().initialized(),
        "Collision gate used stale prepared HP presence after the live wrapper was cleared.");
    check(runtime.messenger().buffer().booleans().empty(),"HP-gated collision invented a hit boolean.");
    work->hp=std::make_shared<ModelScriptInt>();auto present=collision_send(runtime);
    check(runtime.resume(present).stop==MessengerStop::completed && time_calls==1 && runtime.collision_target(target).body().initialized(),
        "A present zero-HP wrapper failed the original pointer-presence gate.");
    check(runtime.messenger().buffer().booleans().empty(),"Separated body boxes fabricated collision success.");
    work->hp.reset();auto absent_again=collision_send(runtime);
    check(runtime.resume(absent_again).stop==MessengerStop::completed && time_calls==1,
        "Collision retained HP presence across distinct deliveries.");
    live=runtime.sequence(target).model();live.invulnerability_frames=0;runtime.prepare_model_state(target,std::move(live));
    auto vulnerable=collision_send(runtime);
    check(runtime.resume(vulnerable).stop==MessengerStop::completed && time_calls==2,
        "Null HP incorrectly blocked the ordinary zero-invulnerability path.");
}

void expiration_and_lifetime() {
    Fixture fixture;
    for(const auto* command:{"GETRECNO","CHKBULLET"}) {
        ActionEventRuntime runtime;const auto target=runtime.register_model(fixture.binding(),state(),true);
        runtime.prepare_collision_target(target,fixture.definition(),true);
        auto work=runtime.prepare_target_script_work(target);std::weak_ptr<ModelScriptWork> weak=work;work.reset();
        check(weak.expired(),"Model retained a strong script owner and formed a lifetime cycle.");
        rejects([&]{runtime.prepare_target_script_work(target);},"Expired script work was silently recreated with default values.");
        auto send=runtime.messenger().begin_send_to("MODELCTRL",name,0,command);
        check(runtime.resume(send).stop==MessengerStop::service_required && !send.completed() &&
              !runtime.collision_target(target).pending(),"Expired script work fell back to fabricated record or collision work.");
        check(runtime.resume(send).stop==MessengerStop::service_required && !runtime.collision_target(target).body().initialized(),
            "Retry of an expired script owner fabricated progress.");
    }
    std::shared_ptr<ModelScriptWork> retained;
    {
        ActionEventRuntime runtime;const auto target=runtime.register_model(fixture.binding(),state(31),true);
        runtime.prepare_collision_target(target,fixture.definition(),true);retained=runtime.prepare_target_script_work(target);
    }
    check((*retained->matrix())[12]==31 && retained->record==-1,"External script work did not retain its live matrix backing owner.");
    (*retained->matrix())[12]=42;check((*retained->matrix())[12]==42,"Retained matrix backing became unusable after runtime destruction.");
    std::weak_ptr<ModelScriptWork> weak=retained;retained.reset();
    check(weak.expired(),"Released external work left a strong Model/script ownership cycle.");
}

void retained_collision_script_glue() {
    Fixture fixture,source_fixture;
    source_fixture.text(source_fixture.model,2,"e_fixture");source_fixture.text(source_fixture.action,2,"e_fixture");
    const auto source_doc=source_fixture.document();
    ActionEventRuntime events;
    const auto source_id=events.register_model(decode_player_runtime_binding(source_doc,source_fixture.runtime_request(),"synthetic/source.igb",4),state(4),true);
    events.prepare_collision_target(source_id,decode_model_hit_body(source_doc,source_fixture.model,"synthetic/source.igb",4),true);
    auto source_work=events.prepare_target_script_work(source_id);source_work->record=-700;
    const auto target=events.register_model(fixture.binding(1),state(),true);
    events.prepare_collision_target(target,fixture.definition(),true);
    auto target_work=events.prepare_target_script_work(target);target_work->hp->data=50;target_work->max_hp->data=100;
    check(events.select_sequence(target,"SQ_WAIT").complete(),"Script glue target selection failed.");
    events.prepare_wait_state(target,{std::nullopt,1,0,0});
    ModelScriptRuntime scripts{ScriptRegistry{}};scripts.register_work(target_work);
    unsigned calls{},prefixes{};bool ready=false;
    // This is an explicitly supplied synthetic filesystem effect, not an
    // implementation of the original hit script or its damage natives.
    scripts.set_file_service([&](Lua4Vm&,ModelScriptContextRuntime& context,const ModelScriptCall& call,bool nested)->std::optional<std::int32_t> {
        ++calls;const auto& published=context.context();
        check(!nested && call.filename=="hit_p_fixture.lua" && call.request.target==target &&
              call.request.target_ram==1 && call.request.source_name=="e_fixture" && call.request.source_ram==0 &&
              call.request.source_record==-700 && call.request.kind==10000 && call.request.body_ordinal==0,
            "Collision/script bridge lost its actual request or reread the source record on retry.");
        check(published.matrix.get()==target_work->matrix().get() && published.maximum_hp==100 &&
              published.source_record==-700 && published.target_record==-1,
            "Script glue did not publish the live matrix and wrapper/record context.");
        if(prefixes==0) {
            ++prefixes;context.mutable_context().hp=43;context.mutable_context().direction=0;
            (*published.matrix)[12]=15;
        }
        if(!ready)return std::nullopt;
        return 0;
    });
    events.set_collision_services({[]{return std::optional<std::uint64_t>{1};},
        [&](const ModelTargetHitRequest& request){return scripts.handle(request);}});
    auto& messenger=events.messenger();messenger.select("HITPLAYERBULLET",name,1);
    messenger.buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{-0.5f,-0.5f,-0.5f},{0.5f,0.5f,0.5f}}));
    messenger.buffer().add_string("e_fixture");messenger.buffer().add_int(10000);messenger.buffer().add_int(0);
    messenger.buffer().add_matrix(matrix(4));auto send=messenger.begin_send("CHKBULLET");
    check(events.resume(send).stop==MessengerStop::service_required && scripts.pending() &&
          events.collision_target(target).result().stop==ModelCollisionStop::hit_script_required && calls==1 && prefixes==1,
        "Collision script service did not retain its incomplete nested prefix.");
    check(target_work->hp->data==50 && events.sequence(target).model().pose.matrix[12]==15,
        "Pending script prematurely merged HP or lost its already committed matrix write.");
    rejects([&]{events.messenger();},"Pending script exposed its nested messenger buffer.");
    source_work->record=-701;ready=true;
    check(events.resume(send).stop==MessengerStop::completed && !scripts.pending() && !scripts.faulted() && calls==2 && prefixes==1,
        "Collision retry replayed a script prefix or failed to finish the retained invocation.");
    check(target_work->hp->data==43 && events.sequence(target).model().pose.matrix[12]==15 &&
          events.messenger().buffer().booleans()==std::vector<std::uint8_t>{1} && events.messenger().buffer_depth()==0,
        "Completed script did not merge HP, honor direction zero or restore the parent collision buffer.");
    auto record=events.messenger().begin_send_to("MODELCTRL","e_fixture",0,"GETRECNO");
    check(events.resume(record).stop==MessengerStop::completed && events.messenger().buffer().integers()==std::vector<std::int32_t>{-701},
        "Later record query did not observe the live source owner after the retained script completed.");
}
} // namespace
int main() {
    try {
        preparation_and_live_aliases();live_signed_record();live_hp_presence_gate();expiration_and_lifetime();retained_collision_script_glue();
        std::cout<<"action_event_script_work: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"action_event_script_work failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
