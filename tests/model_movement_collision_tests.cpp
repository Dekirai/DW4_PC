#include "dw4/model_movement_collision.hpp"
#include "dw4/action_event_runtime.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F call) {
    ++checks;try{call();}catch(const std::exception&){return;}throw std::runtime_error("Invalid movement collision was accepted.");
}
IgbDocument document(const pf::RuntimeFixture& fixture) {
    auto doc=fixture.document();const auto zero=*doc.field(doc.object(fixture.ones[0]),3);
    auto active=*doc.field(doc.object(fixture.state),4);active.slot=4;
    doc.objects.at(doc.references.at(fixture.model).value_index).fields.push_back(active);
    for(const auto ref:fixture.model_rams) {
        auto& ram=doc.objects.at(doc.references.at(ref).value_index);
        for(auto slot:{2u,6u,7u,8u,9u}) {auto field=zero;field.slot=static_cast<std::uint16_t>(slot);ram.fields.push_back(field);}
        active.slot=5;ram.fields.push_back(active);
    }
    return doc;
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
struct Fixture {
    pf::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(document(source),"synthetic/movement-collision.igb",55);
    ActionEventRuntime events;
    ControllerObjectId id=events.register_model_scene_ram(scene,source.model,source.model_rams[0],0);
    std::shared_ptr<ModelSceneRam> ram=events.model_ram(id);
    std::shared_ptr<ModelSceneInfo> parent=ram->parent.lock();
    std::shared_ptr<ModelSceneOne> one=find_model_one(*ram);
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    NativeMessenger& messenger=events.messenger();
    std::shared_ptr<ModelMovementCollisionServices> services=std::make_shared<ModelMovementCollisionServices>();
    Fixture() {
        one->work=work;ram->active=1;work->sequence.name="SQ_WAIT";work->script.original_is_main_lobby=std::uint8_t{0};
        events.set_model_context(std::make_shared<ModelSceneContext>(ModelSceneContext{12}));
    }
    ModelMovementCollisionInvocation call() {return {ram,parent,work};}
    MessengerCallResult run(ModelMovementCollisionInvocation& call) {return call.resume(messenger,events,services);}
    void provider(Hook hook,std::optional<std::string> name={},std::int32_t number=0) {
        if(!name)name=parent->name;
        events.register_native_controller(ControllerObjectId::allocate_native(),{
            []()->std::optional<std::uint8_t>{return std::uint8_t{1};},[hook=std::move(hook)]{return MessengerManager{hook,{}};}},
            "HITLANDDELIVER",std::move(name),number);
    }
    std::shared_ptr<MotionState> vertical(bool active,MotionMode mode,float speed=7) {
        auto motion=std::make_shared<MotionState>();motion->active=active;motion->mode=mode;motion->speed=speed;
        motion->value=3;motion->acceleration=2;motion->end_time=17;
        work->control.movement=std::make_shared<MotionChannelList>(MotionChannelList::Entries{{},motion});return motion;
    }
    void fall_sequence() {
        auto data=std::make_shared<ModelSceneSequence>();data->name="SQ_FALL";
        data->commands=std::make_shared<ModelSceneList>("igNamedObjectList");
        auto command=std::make_shared<ModelSceneCommand>("beModelCtrlCTRL");command->name="isLive";data->commands->append(command);
        find_model_seq(*parent)->sequences->append(data);
    }
};
void physical_motion_and_unused_owners() {
    const auto empty=std::make_shared<MotionChannelList>();
    for(const auto mode:{ModelFloorMotion::rising,ModelFloorMotion::falling,ModelFloorMotion::land}) {
        check(!model_floor_motion(empty,mode),"Empty movement list did not bypass all vertical reads.");
        rejects([&]{model_floor_motion({},mode);});
    }
    auto vertical=std::make_shared<MotionState>();vertical->mode=static_cast<MotionMode>(999);
    vertical->speed=std::numeric_limits<float>::quiet_NaN();
    auto list=std::make_shared<MotionChannelList>(MotionChannelList::Entries{{},vertical,{}});
    check(!model_floor_motion(list,ModelFloorMotion::rising) && !model_floor_motion(list,ModelFloorMotion::falling),
        "Inactive vertical query validated unused mode, speed or other channels.");
    vertical->active=true;vertical->mode=MotionMode::impulse_positive;
    check(model_floor_motion(list,ModelFloorMotion::rising) && !model_floor_motion(list,ModelFloorMotion::falling),"Mode3 selection changed.");
    vertical->mode=MotionMode::accelerate_negative;vertical->end_time=11;vertical->value=3;
    check(model_floor_motion(list,ModelFloorMotion::falling),"Mode4 selection changed.");
    check(model_floor_motion(list,ModelFloorMotion::land) && !vertical->active &&
        std::bit_cast<std::uint32_t>(vertical->speed)==0 && vertical->mode==MotionMode::accelerate_negative &&
        vertical->end_time==11 && vertical->value==3,"Mode5 did more than deactivate then clear speed to positive zero.");
    rejects([&]{model_floor_motion(std::make_shared<MotionChannelList>(MotionChannelList::Entries{vertical}),ModelFloorMotion::land);});
    rejects([&]{model_floor_motion(std::make_shared<MotionChannelList>(MotionChannelList::Entries{vertical,{}}),ModelFloorMotion::falling);});
    Fixture f;f.work->control.wall_check=f.work->control.floor_check=0;f.work->control.movement.reset();
    ModelMovementCollisionInvocation none({}, {},f.work);
    check(f.run(none).completed,"Disabled wall/floor flags consumed unused RAM, parent or movement data.");
    ModelMovementCollisionInvocation invalid(f.ram,f.parent,{});rejects([&]{f.run(invalid);});
}
void wall_capture_and_script_prefix() {
    Fixture f;unsigned wall_calls{},floor_calls{};bool ready{};
    const auto replacement=make_model_player_work();replacement->control.floor_check=0;
    const auto other_parent=std::make_shared<ModelSceneInfo>();other_parent->name="unrelated";
    f.provider([&](NativeMessenger&,const MessengerDelivery& message) {
        ++wall_calls;check(message.message.command=="HITWALL" && message.message.index==0,"Pending wall packet was retargeted.");
        f.ram->parent=other_parent;f.one->work=replacement;f.parent->name="changed";f.ram->number=7;
        return MessengerCallResult{ready,1,"Synthetic wall reply is pending."};
    });
    f.provider([&](NativeMessenger&,const MessengerDelivery& message) {
        ++floor_calls;check(message.message.command=="HITFLOORCHECK" && message.message.index==7,
            "Floor query did not reload retained-parent name/current RAM number.");return MessengerCallResult{true,1,{}};
    },"changed",7);
    auto call=f.call();check(!f.run(call).completed && floor_calls==0,"A pending wall send ran the floor branch.");
    ready=true;check(f.run(call).completed && wall_calls==2 && floor_calls==1 && call.captured().work==f.work &&
        call.captured().parent==f.parent,"Wall resume lost captured work/parent or required an unused response boolean.");

    Fixture script;script.work->control.wall_hit_script="old";script.work->control.floor_check=0;
    unsigned sends{},calls{};bool script_ready{};
    script.provider([&](NativeMessenger& live,const MessengerDelivery&) {
        ++sends;live.buffer().add_bool(0);live.buffer().add_bool(255);script.parent->name="after-wall";
        script.work->control.wall_hit_script="actual-prefix";return MessengerCallResult{true,1,{}};
    });
    auto hit=script.call();check(!script.run(hit).completed && sends==1,"A reached wall script became an invented success.");
    script.work->control.wall_hit_script="later";
    script.services->hit_script=[&](const ModelMovementCollisionScope& scope,const ModelMovementHitRequest& request) {
        ++calls;check(scope.work==script.work && request.prefix=="actual-prefix" && request.event=="WHit" &&
            request.source_name=="after-wall","Wall script retry rebuilt its captured arguments.");
        return MessengerCallResult{script_ready,0,{}};
    };
    check(!script.run(hit).completed && sends==1,"Pending wall script replayed its completed send.");
    script_ready=true;check(script.run(hit).completed && sends==1 && calls==2,"Wall script did not resume its own operation.");

    Fixture malformed;malformed.work->control.wall_hit_script="required";
    malformed.provider([](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,1,{}};});
    auto broken=malformed.call();rejects([&]{malformed.run(broken);});
    check(broken.faulted(),"Reached missing wall boolean did not retain permanent failure.");
}
void floor_response_gates() {
    Fixture rising;rising.work->control.wall_check=0;rising.vertical(true,MotionMode::impulse_positive);
    ModelMovementCollisionInvocation no_parent(rising.ram,{},rising.work);
    check(rising.run(no_parent).completed,"Rising impulse gate consumed a parent or floor reply.");
    for(int shape=0;shape<5;++shape) {
        Fixture f;f.work->control.wall_check=0;f.work->control.check_fall=0;
        f.provider([shape](NativeMessenger& live,const MessengerDelivery&) {
            if(shape!=0)live.buffer().add_bool(0);
            if(shape>=2)live.buffer().add_bool(0);
            if(shape==2)live.buffer().add_string(std::nullopt);
            if(shape==3)live.buffer().add_string("ride");
            if(shape==4)live.buffer().add_string("");
            return MessengerCallResult{true,1,{}};
        });
        auto call=f.call();
        if(shape==0 || shape==4)check(f.run(call).completed,"Empty bool list or empty ride name consumed unused reply fields.");
        else {rejects([&]{f.run(call);});check(call.faulted(),"Malformed reached floor reply was silently repaired.");}
    }
    Fixture captured;captured.work->control.wall_check=0;captured.work->control.floor_hit_script="floor";
    const auto old_vertical=captured.vertical(true,MotionMode::accelerate_negative);
    std::shared_ptr<MotionState> replacement;unsigned checks_sent{},adjusts{},scripts{};
    captured.provider([&](NativeMessenger& live,const MessengerDelivery& message) {
        if(message.message.command=="HITFLOORCHECK") {
            ++checks_sent;live.buffer().add_bool(0);live.buffer().add_bool(254);
        } else {
            ++adjusts;replacement=captured.vertical(true,MotionMode::impulse_negative,99);
            // No response booleans: caller must retain the pre-adjust values.
        }
        return MessengerCallResult{true,1,{}};
    });
    captured.services->hit_script=[&](const ModelMovementCollisionScope&,const ModelMovementHitRequest& request) {
        ++scripts;check(request.event=="FHit" && request.prefix=="floor","Captured nonzero floor byte was lost after adjust cleared the packet.");
        check(replacement && !replacement->active && replacement->speed==0 && replacement->mode==MotionMode::impulse_negative &&
            old_vertical->active && old_vertical->speed==7,"Landing did not operate on the fresh vertical owner with mode5 semantics.");
        return MessengerCallResult{true,0,{}};
    };
    auto landing=captured.call();check(captured.run(landing).completed && checks_sent==1 && adjusts==1 && scripts==1,
        "Landing failed to retain its reply through actual adjust and floor script.");
    Fixture grounded;grounded.work->control.wall_check=0;grounded.work->control.floor_hit_script="must-skip";
    const auto grounded_vertical=grounded.vertical(false,MotionMode::accelerate_positive);
    unsigned grounded_adjusts{};
    grounded.provider([&](NativeMessenger& live,const MessengerDelivery& message) {
        if(message.message.command=="HITFLOORCHECK") {
            live.buffer().add_bool(255);live.buffer().add_bool(1);
            live.buffer().add_string("unused-after-return");live.buffer().add_int(9);
        } else {++grounded_adjusts;grounded_vertical->active=true;grounded_vertical->speed=14;}
        return MessengerCallResult{true,1,{}};
    });
    grounded.services->hit_script=[](const auto&,const auto&)->MessengerCallResult {
        throw std::runtime_error("Grounded adjustment incorrectly entered floor script.");
    };
    grounded.services->ride_update=[](const auto&,const auto&)->MessengerCallResult {
        throw std::runtime_error("Grounded adjustment incorrectly entered ride handling.");
    };
    auto on_ground=grounded.call();check(grounded.run(on_ground).completed && grounded_adjusts==1 &&
        !grounded_vertical->active && grounded_vertical->speed==0 && grounded_vertical->mode==MotionMode::accelerate_positive,
        "Nonfalling ground adjustment did not land and return before floor-script/ride effects.");
}
void fall_and_ride_boundaries() {
    Fixture f;f.work->control.wall_check=0;f.fall_sequence();
    f.provider([](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(0);live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
    });
    auto fall=f.call();check(f.run(fall).completed && f.work->sequence.name=="SQ_FALL" && f.work->control.mode==4,
        "Unsupported ground did not run the real SQ_FALL ordinary entry.");
    Fixture sleep;sleep.work->control.wall_check=0;sleep.work->sequence.name="SQ_SLEP";
    sleep.provider([](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(0);live.buffer().add_bool(0);return MessengerCallResult{true,1,{}};
    });
    auto sleeping=sleep.call();check(sleep.run(sleeping).completed && sleep.work->sequence.name=="SQ_SLEP","Sleeping fall gate changed sequence.");
    Fixture ride;ride.work->control.wall_check=0;ride.vertical(true,MotionMode::accelerate_negative);
    unsigned queries{};
    ride.provider([&](NativeMessenger& live,const MessengerDelivery& message) {
        ++queries;if(message.message.command=="HITFLOORCHECK") {
            live.buffer().add_bool(1);live.buffer().add_bool(1);live.buffer().add_string("platform");live.buffer().add_int(12);
        }return MessengerCallResult{true,1,{}};
    });
    auto platform=ride.call();check(!ride.run(platform).completed && queries==2 && !ride.work->ride_data,
        "Missing real ride initialization manufactured a completed platform attachment.");
    bool ready{};unsigned ride_calls{};
    ride.services->ride_update=[&](const ModelMovementCollisionScope& scope,const auto& target) {
        ++ride_calls;check(scope.work==ride.work && target && target->name=="platform" && target->ram_no==12,
            "Ride continuation lost captured floor identity.");return MessengerCallResult{ready,0,{}};
    };
    check(!ride.run(platform).completed && queries==2,"Pending ride operation replayed collision messages.");
    ready=true;check(ride.run(platform).completed && queries==2 && ride_calls==2,"Ride operation did not resume at its boundary.");
}
void recursive_failures_and_missing_category() {
    Fixture missing;missing.work->control.floor_check=0;auto call=missing.call();
    check(!missing.run(call).completed,"Missing HITLANDDELIVER category became a successful original collision.");
    const auto original_name=missing.parent->name;missing.parent->name="replacement";missing.ram->number=9;
    missing.provider([](NativeMessenger&,const MessengerDelivery& delivery) {
        check(delivery.message.index==0,"Missing-category continuation retargeted the already-captured query.");
        return MessengerCallResult{true,1,{}};
    },original_name,0);
    check(missing.run(call).completed,"Supplying the actual missing category failed to resume captured wall arguments.");
    Fixture recursive;recursive.work->control.wall_hit_script="wall";recursive.work->control.floor_check=0;
    recursive.provider([](NativeMessenger& live,const MessengerDelivery&) {
        live.buffer().add_bool(0);live.buffer().add_bool(1);return MessengerCallResult{true,1,{}};
    });
    auto bad=recursive.call();recursive.services->hit_script=[&](const auto&,const auto&) {
        try{recursive.run(bad);}catch(const std::exception&){}
        return MessengerCallResult{true,0,{}};
    };
    rejects([&]{recursive.run(bad);});check(bad.faulted(),"Caught recursive hit script resumed an uncertain collision prefix.");
}
}
int main() {
    try {
        physical_motion_and_unused_owners();wall_capture_and_script_prefix();floor_response_gates();
        fall_and_ride_boundaries();recursive_failures_and_missing_category();
        std::cout<<"Actual model movement collision checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Movement collision after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
