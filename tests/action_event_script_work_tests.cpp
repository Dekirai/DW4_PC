#include "dw4/action_event_runtime.hpp"
#include "dw4/model_script_runtime.hpp"
#include "dw4/model_status.hpp"
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
    check(a->hp() && a->max_hp() && a->hp()!=a->max_hp() && a->hp()!=b->hp() && a->max_hp()!=b->max_hp(),
        "Prepared targets shared their default HP wrappers.");
    check(a->hp()->data==0 && a->max_hp()->data==0 && b->hp()->data==0 && b->max_hp()->data==0 &&
          !a->hp()->name && !a->max_hp()->name && !b->hp()->name && !b->max_hp()->name && a->record()==-1 && b->record()==-1,
        "Script preparation invented player HP, names or record values.");
    check(a->matrix().get()==&runtime.sequence(first).model().pose.matrix() &&
          b->matrix().get()==&runtime.sequence(second).model().pose.matrix() && a->matrix()!=b->matrix(),
        "Script matrix is a detached pose copy or belongs to another RAM.");
    const auto* stable=a->matrix().get();runtime.prepare_model_state(first,state(13));
    check(a->matrix().get()==stable && (*a->matrix())[12]==13 && (*b->matrix())[12]==7,
        "Live model replacement invalidated the script matrix alias or affected another target.");
    (*a->matrix())[13]=19;
    check(runtime.sequence(first).model().pose.matrix()[13]==19,"Script matrix writes did not reach the owning live pose.");
    check(runtime.select_sequence(first,"SQ_WAIT").complete() && a->matrix().get()==stable && (*a->matrix())[13]==19,
        "Sequence entry invalidated the script's stable matrix owner.");
    const auto registrations=runtime.messenger().registration_count();
    rejects([&]{runtime.prepare_target_script_work(first);},"A second script work owner replaced live HP state.");
    check(runtime.messenger().registration_count()==registrations && a->hp()->data==0,
        "Rejected script re-preparation changed registrations or live work.");
}

void canonical_factory_registration() {
    Fixture fixture;ActionEventRuntime runtime;
    auto work=make_model_player_work();
    auto node=std::make_shared<AnimationMatrix>(matrix(37));
    auto timer=std::make_shared<ModelRamTimerSlot>();
    const auto target=runtime.register_model(fixture.binding(),work,node,timer,true);
    check(runtime.model_work(target)==work && runtime.sequence(target).fields().get()==&work->sequence &&
        runtime.sequence(target).model().fields_owner().get()==&work->control &&
        runtime.sequence(target).model().pose.fields_owner().get()==&work->motion,
        "MODELCTRL did not consume the actual factory work allocation.");
    check(runtime.sequence(target).model().pose.matrix_owner()==node && runtime.sequence(target).model().timer_slot_owner()==timer &&
        runtime.subcommands(target).wrapper_owner()==work->subcommands && work->sequence.wait.control_enabled==1,
        "Registration detached the scene/timer/queue owners or applied prepared wait defaults to factory work.");
    runtime.prepare_collision_target(target,fixture.definition(),true);
    auto script=runtime.prepare_target_script_work(target);
    auto status=std::make_shared<ModelStatusWork>(script,std::uint8_t{1});
    check(script->fields_owner().get()==&work->script && status->fields_owner().get()==&work->status && work->status.is_player==1,
        "Script/status preparation created parallel canonical field storage.");
    work->script.hp->data=67;work->script.record=-45;
    auto hp=runtime.messenger().begin_send_to("MODELCTRL",name,0,"GETHP");
    check(runtime.resume(hp).stop==MessengerStop::completed && runtime.messenger().buffer().integers()==std::vector<std::int32_t>{67},
        "The real MODELCTRL query did not read canonical factory HP.");
    check(runtime.select_sequence(target,"SQ_WAIT").complete() && work->sequence.name=="SQ_WAIT" && work->sequence.command_data &&
        &runtime.sequence(target).wait_state()==&work->sequence.wait,"Sequence selection did not update canonical command/wait fields.");
    auto values=state(81);values.mode()=3;
    runtime.prepare_model_state(target,std::move(values));
    check(work->control.mode==3 && (*node)[12]==81 && script->hp()->data==67 && work->script.record==-45 &&
        runtime.model_work(target)==work && runtime.sequence(target).fields().get()==&work->sequence,
        "Prepared value publication replaced work or detached a field group.");
    const auto previous=work->subcommands;
    work->subcommands=std::make_shared<ModelSubcommandWrapper>();
    runtime.messenger().select("MODELCTRL",name,0);
    runtime.messenger().buffer().add_float(2);
    runtime.messenger().buffer().add_string("fixture_sound");
    auto sound=runtime.messenger().begin_send("SETSOUND");
    check(runtime.resume(sound).stop==MessengerStop::completed && previous->list_owner()->empty() &&
        work->subcommands->list_owner()->size()==1 && runtime.subcommands(target).wrapper_owner()==work->subcommands,
        "MODELCTRL appended into a cached subcommand wrapper after its canonical slot changed.");
    check(runtime.update_subcommands(target,2).stop==ModelSubcommandStop::sound_play_required,
        "The canonical sound fixture did not retain its old update.");
    const auto pending_wrapper=work->subcommands;
    work->subcommands=std::make_shared<ModelSubcommandWrapper>();
    ModelSubcommandQueue fresh_queue(work->subcommands);fresh_queue.append_sound(0,"new_sound");
    rejects([&]{runtime.select_sequence(target,"SQ_WAIT");},"Selection bypassed an old update on another wrapper.");
    check(!runtime.sequence(target).selection_pending() && runtime.sequence(target).current_sequence()=="SQ_WAIT" &&
        fresh_queue.entries().size()==1 && work->control.mode==3,"Rejected cross-wrapper cleanup published sequence fields or removed fresh work.");
    rejects([&]{runtime.resume_sequence(target);},"An old UPDATE could be resumed as a new sequence cleanup.");
    ModelSubcommandServices finish;finish.play_sound=[](const auto&){return ModelSoundPlayResult{true,0,0};};
    check(runtime.resume_subcommands(target,finish).complete() && pending_wrapper->list_owner()->empty() &&
        runtime.select_sequence(target,"SQ_WAIT").complete() && fresh_queue.entries().empty(),
        "Finishing the retained update did not permit cleanup of the actual new wrapper.");
    work->subcommands.reset();
    rejects([&]{runtime.select_sequence(target,"SQ_WAIT");},"Selection accepted a null actual cleanup wrapper.");
    check(!runtime.sequence(target).selection_pending() && runtime.sequence(target).current_sequence()=="SQ_WAIT",
        "A null cleanup wrapper left a fabricated resumable selection.");
}

void live_canonical_effect_attachment() {
    Fixture fixture;
    std::weak_ptr<ModelPlayerWork> weak_work;
    std::shared_ptr<ModelSubcommandWrapper> retained_wrapper;
    {
        ActionEventRuntime runtime;auto work=make_model_player_work();weak_work=work;
        const auto target=runtime.register_model(fixture.binding(),work,std::make_shared<AnimationMatrix>(matrix()),
            std::make_shared<ModelRamTimerSlot>(),true);
        ModelSubcommandQueue queue(work->subcommands);retained_wrapper=work->subcommands;
        ModelEffectSubcommand effect;effect.model_name="effect";effect.sequence_name="SQ_WAIT";effect.sequence_dead=true;
        queue.append_effect(effect);work->hit_parent_name="before_find";work->hit_parent_ram=2;
        unsigned finds{},generates{};bool ready=false;
        ModelSubcommandServices services;
        services.find_effect_space=[&](const auto&) {
            ++finds;work->hit_parent_name="after_find";work->hit_parent_ram=-7;
            return ModelEffectSpaceResult{true,3};
        };
        services.generate_effect=[&](const ModelEffectGenerateRequest& request) {
            ++generates;
            check(request.source==ModelSubcommandOwner{name,0} && request.attachment==ModelSubcommandOwner{"after_find",-7},
                "Effect GENERATE captured attachment before FIND_SPACE or reread it after a paused payload.");
            return ready;
        };
        check(runtime.update_subcommands(target,0,services).stop==ModelSubcommandStop::effect_generate_required && finds==1 && generates==1,
            "The canonical effect did not stop at its retained GENERATE payload.");
        work->hit_parent_name="after_pause";work->hit_parent_ram=99;ready=true;
        check(runtime.resume_subcommands(target,services).complete() && finds==1 && generates==2,
            "Effect resume repeated FIND_SPACE or lost its original GENERATE payload.");
    }
    check(weak_work.expired() && retained_wrapper->list_owner()->size()==1,
        "A retained subcommand context created a strong cycle back into canonical work.");
}

void supplied_scene_matrix_lifetime() {
    Fixture fixture;
    auto node=std::make_shared<AnimationMatrix>(matrix(27));
    std::weak_ptr<AnimationMatrix> observed=node;
    std::shared_ptr<ModelScriptWork> retained;
    {
        ActionEventRuntime runtime;
        ModelControlState fresh{ModelMotionPose(node)};
        fresh.pose.turn().rate={0,7,0};
        check(fresh.pose.matrix_owner()==node,
            "ModelControlState construction detached its supplied scene matrix.");
        const auto target=runtime.register_model(fixture.binding(),std::move(fresh),true);
        runtime.prepare_collision_target(target,fixture.definition(),true);
        retained=runtime.prepare_target_script_work(target);
        check(retained->matrix()==node && runtime.sequence(target).model().pose.matrix_owner()==node,
            "Model/sequence registration did not retain the actual scene matrix.");
        auto candidate=runtime.sequence(target).model();candidate.pose.matrix()[12]=81;
        check((*node)[12]==27,"Detached prepared-state editing leaked into the scene node.");
        runtime.prepare_model_state(target,std::move(candidate));
        check(retained->matrix()==node && (*node)[12]==81,
            "Prepared-state publication replaced the actual scene matrix owner.");
        node.reset();
    }
    check(!observed.expired() && (*retained->matrix())[12]==81,
        "Retained script state lost its node matrix when the controller was destroyed.");
    (*retained->matrix())[14]=92;
    check(observed.lock()->at(14)==92,
        "Retained script matrix stopped being the actual mutable allocation.");
    retained.reset();
    check(observed.expired(),"Node matrix lifetime leaked after all actual owners were released.");
}

void live_signed_record() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(1),state(),true);
    runtime.prepare_collision_target(target,fixture.definition(),false);
    auto work=runtime.prepare_target_script_work(target);
    check(!runtime.sequence(target).selected(),"Record fixture accidentally selected a sequence.");
    for(const auto value:{std::int32_t{-1},std::numeric_limits<std::int32_t>::min(),std::int32_t{0},std::numeric_limits<std::int32_t>::max()}) {
        work->record()=value;
        auto send=runtime.messenger().begin_send_to("MODELCTRL",name,1,"GETRECNO");
        check(runtime.resume(send).stop==MessengerStop::completed &&
              runtime.messenger().buffer().integers()==std::vector<std::int32_t>{value},
            "GETRECNO dereferenced a wrapper or changed an active model's signed record.");
    }
    work->hp().reset();work->max_hp().reset();work->record()=-900;
    auto send=runtime.messenger().begin_send_to("MODELCTRL",name,1,"GETRECNO");
    check(runtime.resume(send).stop==MessengerStop::completed && runtime.messenger().buffer().integers()==std::vector<std::int32_t>{-900},
        "Direct record lookup incorrectly requires HP wrappers.");
}

void record_query_gates() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(1),state(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);
    auto work=runtime.prepare_target_script_work(target);work->record()=-401;
    auto active=std::make_shared<std::uint8_t>(std::uint8_t{1});
    auto available=std::make_shared<bool>(true);
    runtime.prepare_model_queries(target,{{3,fixture.model},source,
        std::make_shared<std::optional<std::string>>(name),std::make_shared<std::int32_t>(1),active,available});
    auto status=std::make_shared<ModelStatusWork>(work,std::uint8_t{1});runtime.prepare_model_status(target,status);
    status.reset(); // The original status decline requires none of these fields.
    auto query=[&](bool append) {
        auto& messenger=runtime.messenger();messenger.select("MODELCTRL",name,1);
        messenger.buffer().add_int(57);messenger.buffer().add_string("prefix");messenger.buffer().add_object({});
        auto call=messenger.begin_send("GETRECNO");
        check(runtime.resume(call).stop==MessengerStop::completed,
            "GETRECNO required irrelevant status fields or stopped its original gate.");
        check(messenger.buffer().integers()==(append?std::vector<std::int32_t>{57,-401}:std::vector<std::int32_t>{57}) &&
            messenger.buffer().strings()==std::vector<std::optional<std::string>>{"prefix"} &&
            messenger.buffer().objects().size()==1 && std::holds_alternative<std::monostate>(messenger.buffer().objects()[0]),
            "GETRECNO changed the existing packet or appended on an inactive/null-work exit.");
    };
    for(auto value:{std::uint8_t{1},std::uint8_t{128},std::uint8_t{255}}) {*active=value;query(true);}
    *active=0;query(false);
    *active=1;*available=false;query(false);
    work.reset(); // Expired preparation is not proof of a null original work.
    *available=true;*active=0;query(false);
    *active=1;*available=false;query(false);
    *available=true;
    auto& pending_messenger=runtime.messenger();
    auto unresolved=pending_messenger.begin_send_to("MODELCTRL",name,1,"GETRECNO");
    check(runtime.resume(unresolved).stop==MessengerStop::service_required && pending_messenger.buffer().integers().empty(),
        "An active query converted missing native work into a false null-work result.");
}

MessengerDispatch collision_send(ActionEventRuntime& runtime) {
    auto& messenger=runtime.messenger();messenger.select("HITPLAYERBULLET",name,0);
    messenger.buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{40,40,40},{41,41,41}}));
    messenger.buffer().add_string("p_source");
    return messenger.begin_send("CHKBULLET");
}
void original_hp_query() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(),state(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);
    auto work=runtime.prepare_target_script_work(target);
    auto hp=work->hp(),maximum=work->max_hp();hp->data=-17;maximum->data=900;
    const auto active=std::make_shared<std::uint8_t>(std::uint8_t{255});
    const auto available=std::make_shared<bool>(true);
    runtime.prepare_model_queries(target,{{3,fixture.model},source,
        std::make_shared<std::optional<std::string>>(name),std::make_shared<std::int32_t>(0),active,available});
    auto& messenger=runtime.messenger();messenger.select("MODELCTRL",name,0);
    messenger.buffer().add_int(77);messenger.buffer().add_object({});messenger.buffer().add_string("keep");
    messenger.buffer().set_enabled_only(1);auto call=messenger.begin_send("GETHP");
    check(runtime.resume(call).stop==MessengerStop::completed &&
        messenger.buffer().integers()==std::vector<std::int32_t>{77,-17} &&
        messenger.buffer().objects().size()==3 && std::holds_alternative<std::monostate>(messenger.buffer().objects()[0]) &&
        std::get<std::shared_ptr<GameDataInt>>(messenger.buffer().objects()[1])==hp &&
        std::get<std::shared_ptr<GameDataInt>>(messenger.buffer().objects()[2])==maximum &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>{"keep"} && messenger.buffer().enabled_only()==1,
        "GETHP copied wrappers, normalized signed HP or replaced the caller packet.");
    work->hp()=std::make_shared<GameDataInt>(GameDataInt{"HP",42});work->max_hp().reset();
    call=messenger.begin_send_to("MODELCTRL",name,0,"GETHP");
    check(runtime.resume(call).stop==MessengerStop::completed && messenger.buffer().integers()==std::vector<std::int32_t>{42} &&
        std::get<std::shared_ptr<GameDataInt>>(messenger.buffer().objects()[0])==work->hp() &&
        std::holds_alternative<std::monostate>(messenger.buffer().objects()[1]),
        "GETHP cached replaced HP or refused the original nullable maximum pointer.");
    work->hp().reset();
    for(const bool missing_work:{false,true}) {
        *active=missing_work ? std::uint8_t{255}:std::uint8_t{0};*available=!missing_work;
        call=messenger.begin_send_to("MODELCTRL",name,0,"GETHP");
        check(runtime.resume(call).stop==MessengerStop::completed && messenger.buffer().integers()==std::vector<std::int32_t>{0} &&
            messenger.buffer().objects().empty(),"GETHP read null HP before its inactive/null-base-work gate.");
    }
    *available=true;call=messenger.begin_send_to("MODELCTRL",name,0,"GETHP");
    check(runtime.resume(call).stop==MessengerStop::faulted && messenger.buffer().integers().empty() &&
        messenger.buffer().objects().empty(),"Active GETHP fabricated a value for a null HP pointer.");

    ActionEventRuntime unprepared;const auto unknown=unprepared.register_model(fixture.binding(),state(),true);
    auto& pending_buffer=unprepared.messenger();auto pending=pending_buffer.begin_send_to("MODELCTRL",name,0,"GETHP");
    check(unprepared.resume(pending).stop==MessengerStop::service_required && pending_buffer.buffer().integers().empty(),
        "GETHP inferred a null base pointer from unknown native preparation.");
    unprepared.set_active(unknown,false);
    check(unprepared.resume(pending).stop==MessengerStop::completed && pending_buffer.buffer().integers()==std::vector<std::int32_t>{0},
        "Retained GETHP did not re-evaluate its actual live active gate.");
}

void original_status_dispatch() {
    Fixture fixture;ActionEventRuntime runtime;GameDataState data;
    const auto target=runtime.register_model(fixture.binding(),state(),false);
    runtime.prepare_collision_target(target,fixture.definition(),true);
    const auto work=runtime.prepare_target_script_work(target);
    const auto active=std::make_shared<std::uint8_t>(std::uint8_t{0});
    const auto available=std::make_shared<bool>(true);
    runtime.prepare_model_queries(target,{{3,fixture.model},source,std::make_shared<std::optional<std::string>>(name),
        std::make_shared<std::int32_t>(0),active,available});
    runtime.register_player(target);
    const auto status=std::make_shared<ModelStatusWork>(work,std::uint8_t{1});
    runtime.prepare_model_status(target,status);
    rejects([&]{runtime.prepare_model_status(target,status);},"Published status work was replaced.");
    auto& messenger=runtime.messenger();auto hp=messenger.begin_send_to("MODELCTRL",name,0,"GETHP");
    check(runtime.resume(hp).stop==MessengerStop::completed && !status->display_ram_numbers() &&
        messenger.buffer().integers()==std::vector<std::int32_t>{0},"Unknown status command allocated lists or bypassed the generic active gate.");
    unsigned requests{};
    const auto outer_row=data.player(0),inner_row=data.player(1);
    const auto provider=ControllerObjectId::allocate_native();
    runtime.register_native_controller(provider,{[]{return std::optional<std::uint8_t>{std::uint8_t{1}};},[&] {
        return MessengerManager{[&,nested=std::shared_ptr<MessengerDispatch>{}](NativeMessenger& native,const MessengerDelivery& delivery) mutable {
            check(delivery.message.command=="GETPLAYER" && native.buffer().integers()==std::vector<std::int32_t>{0},
                "SETMETAL used the routing key instead of the actual live player RAM field.");
            ++requests;
            if(requests==1) {
                native.push_buffer();native.select("PLAYER",name,0);nested=std::make_shared<MessengerDispatch>(native.begin_send("SETMETAL"));
                check(runtime.resume(*nested).stop==MessengerStop::completed && status->down_data()==inner_row->require<GameDataIntList>("DOWNDATA")->data,
                    "Nested same-model status invocation reused its parent's continuation.");
                native.pop_buffer();native.buffer().add_data_list(outer_row);
            } else native.buffer().add_data_list(inner_row);
            return MessengerCallResult{true,1,{}};
        },{}};
    }},"GAMERAM","game",0);
    messenger.select("PLAYER",name,0);messenger.buffer().add_string("parent");const auto packet=messenger.buffer_owner();
    auto metal=messenger.begin_send("SETMETAL");
    check(runtime.resume(metal).stop==MessengerStop::completed && requests==2 && status->timers()->at(7)==1 && status->first_flags()->at(7)==1 &&
        status->down_data()==outer_row->require<GameDataIntList>("DOWNDATA")->data && messenger.buffer_owner()==packet &&
        messenger.buffer_depth()==0 && messenger.buffer().strings()==std::vector<std::optional<std::string>>{"parent"},
        "Status virtual applied an inactive gate or lost the outer fresh-row publication/packet after nesting.");
    *available=false;status->timers()->at(7)=99;
    metal=messenger.begin_send_to("MODELCTRL",name,0,"SETMETAL");
    check(runtime.resume(metal).stop==MessengerStop::completed && status->timers()->at(7)==99 && requests==2,
        "Null actual base work mutated a retained status allocation.");
}

void abandoned_status_continuations() {
    for(const bool broadcast:{false,true})for(const bool prepared:{false,true}) {
        std::weak_ptr<ModelScriptWork> weak;
        {
            Fixture fixture;ActionEventRuntime runtime;
            const auto target=runtime.register_model(fixture.binding(),state(),true);
            runtime.prepare_collision_target(target,fixture.definition(),true);
            auto work=runtime.prepare_target_script_work(target);weak=work;
            runtime.prepare_model_queries(target,{{3,fixture.model},source,std::make_shared<std::optional<std::string>>(name),
                std::make_shared<std::int32_t>(0),std::make_shared<std::uint8_t>(std::uint8_t{1}),std::make_shared<bool>(true)});
            auto status=std::make_shared<ModelStatusWork>(work,std::uint8_t{1});
            if(prepared)runtime.prepare_model_status(target,status);
            // Explicit synthetic self-recipient routing exercises ownership;
            // this is not an assertion about the original asset registrations.
            runtime.register_recipient(target,broadcast ? "ACTION_BROADCAST":"ACTION",name,0);
            check(broadcast ? runtime.broadcast(target,{source,name,"SETMETAL",0}).stop!=MessengerStop::completed :
                !runtime.query_action_complete(target).has_value(),"Lifetime case failed to retain an actual model-owned dispatch.");
        }
        check(weak.expired(),"Abandoned model-owned status dispatch retained a cycle through its live matrix owner.");
    }
}

void live_hp_presence_gate() {
    Fixture fixture;ActionEventRuntime runtime;
    const auto target=runtime.register_model(fixture.binding(),state(),true);
    runtime.prepare_collision_target(target,fixture.definition(),true);
    auto work=runtime.prepare_target_script_work(target);
    check(runtime.select_sequence(target,"SQ_WAIT").complete(),"Collision target sequence preparation failed.");
    runtime.prepare_wait_state(target,{std::nullopt,1,0,0});
    auto live=runtime.sequence(target).model();live.invulnerability_frames()=5;runtime.prepare_model_state(target,std::move(live));
    unsigned time_calls{};runtime.set_collision_services({[&]{++time_calls;return std::optional<std::uint64_t>{1};},{}});
    work->hp().reset();auto absent=collision_send(runtime);
    check(runtime.resume(absent).stop==MessengerStop::completed && time_calls==0 && !runtime.collision_target(target).body().initialized(),
        "Collision gate used stale prepared HP presence after the live wrapper was cleared.");
    check(runtime.messenger().buffer().booleans().empty(),"HP-gated collision invented a hit boolean.");
    work->hp()=std::make_shared<ModelScriptInt>();auto present=collision_send(runtime);
    check(runtime.resume(present).stop==MessengerStop::completed && time_calls==1 && runtime.collision_target(target).body().initialized(),
        "A present zero-HP wrapper failed the original pointer-presence gate.");
    check(runtime.messenger().buffer().booleans().empty(),"Separated body boxes fabricated collision success.");
    work->hp().reset();auto absent_again=collision_send(runtime);
    check(runtime.resume(absent_again).stop==MessengerStop::completed && time_calls==1,
        "Collision retained HP presence across distinct deliveries.");
    live=runtime.sequence(target).model();live.invulnerability_frames()=0;runtime.prepare_model_state(target,std::move(live));
    auto vulnerable=collision_send(runtime);
    check(runtime.resume(vulnerable).stop==MessengerStop::completed && time_calls==2,
        "Null HP incorrectly blocked the ordinary zero-invulnerability path.");
}

void expiration_and_lifetime() {
    Fixture fixture;
    for(const auto* command:{"GETRECNO","CHKBULLET","GETHP"}) {
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
    check((*retained->matrix())[12]==31 && retained->record()==-1,"External script work did not retain its live matrix backing owner.");
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
    auto source_work=events.prepare_target_script_work(source_id);source_work->record()=-700;
    const auto target=events.register_model(fixture.binding(1),state(),true);
    events.prepare_collision_target(target,fixture.definition(),true);
    auto target_work=events.prepare_target_script_work(target);target_work->hp()->data=50;target_work->max_hp()->data=100;
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
    check(target_work->hp()->data==50 && events.sequence(target).model().pose.matrix()[12]==15,
        "Pending script prematurely merged HP or lost its already committed matrix write.");
    rejects([&]{events.messenger();},"Pending script exposed its nested messenger buffer.");
    source_work->record()=-701;ready=true;
    check(events.resume(send).stop==MessengerStop::completed && !scripts.pending() && !scripts.faulted() && calls==2 && prefixes==1,
        "Collision retry replayed a script prefix or failed to finish the retained invocation.");
    check(target_work->hp()->data==43 && events.sequence(target).model().pose.matrix()[12]==15 &&
          events.messenger().buffer().booleans()==std::vector<std::uint8_t>{1} && events.messenger().buffer_depth()==0,
        "Completed script did not merge HP, honor direction zero or restore the parent collision buffer.");
    auto record=events.messenger().begin_send_to("MODELCTRL","e_fixture",0,"GETRECNO");
    check(events.resume(record).stop==MessengerStop::completed && events.messenger().buffer().integers()==std::vector<std::int32_t>{-701},
        "Later record query did not observe the live source owner after the retained script completed.");
    events.set_active(source_id,false);
    auto& next=events.messenger();next.select("HITPLAYERBULLET",name,1);
    // The earlier real script moved the target to X=15. Cover that live body
    // as well as its prior cache so this query actually reaches GETRECNO.
    next.buffer().add_box(std::make_shared<const CollisionBox>(CollisionBox{{-100,-100,-100},{100,100,100}}));
    next.buffer().add_string("e_fixture");next.buffer().add_int(10000);next.buffer().add_int(0);
    next.buffer().add_matrix(*source_work->matrix());
    auto inactive_source=next.begin_send("CHKBULLET");
    const auto inactive_result=events.resume(inactive_source);
    check(inactive_result.stop==MessengerStop::completed,
        ("The inactive-source collision did not complete its record-query gate: "+inactive_result.diagnostic).c_str());
    check(calls==2 && prefixes==1 && target_work->hp()->data==43 && !scripts.pending(),
        "An inactive source supplied a record and incorrectly ran the target hit script.");
    check(events.messenger().buffer_depth()==0 && events.collision_target(target).result().body_ordinal==0 &&
        events.messenger().buffer().booleans()==std::vector<std::uint8_t>{1},
        "The inactive-source regression did not reach the overlapping body or restore its packet.");
}
} // namespace
int main() {
    try {
        preparation_and_live_aliases();canonical_factory_registration();live_canonical_effect_attachment();supplied_scene_matrix_lifetime();live_signed_record();record_query_gates();original_hp_query();original_status_dispatch();abandoned_status_continuations();
        live_hp_presence_gate();expiration_and_lifetime();retained_collision_script_glue();
        std::cout<<"action_event_script_work: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"action_event_script_work failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
