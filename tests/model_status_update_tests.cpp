#include "dw4/model_status_update.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_dispatch.hpp"
#include "dw4/model_timer.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::optional<std::string> name,int number,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},std::move(category),std::move(name),number);
}
struct Fixture {
    player_runtime_fixture::RuntimeFixture source;
    std::shared_ptr<ActorSceneResource> scene=std::make_shared<ActorSceneResource>(source.document(),"synthetic/status.igb",9);
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneInfo> parent=std::make_shared<ModelSceneInfo>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    ActionEventRuntime events;NativeMessenger& messenger=events.messenger();ModelPlayerCreateServices creation;
    std::shared_ptr<ModelSceneManager> manager=std::make_shared<ModelSceneManager>();
    Fixture() {
        parent->name="player";parent->kind="KIND_PLAYER";ram->parent=parent;ram->number=2;
        manager->context=std::make_shared<ModelSceneContext>();ram->manager=manager;
        one->name="ONE";one->work=work;one->node=scene->mutable_node(source.nodes[0]);
        ensure_model_data(*ram)->append(one);work->sequence.name="SQ_RUN";
        work->script.hp->data=100;work->script.max_hp->data=200;
    }
    void status() {
        work->status.timers=std::make_shared<std::vector<std::int32_t>>(12,0);
        work->status.display_ram_numbers=std::make_shared<std::vector<std::int32_t>>(12,-1);
        work->status.display_positions=std::make_shared<std::vector<std::int32_t>>(12,-1);
        work->status.first_flags=std::make_shared<std::vector<std::uint8_t>>(11,std::uint8_t{0});
        work->status.down_data=std::make_shared<std::vector<std::int32_t>>(7,9);
        work->status.up_data=std::make_shared<std::vector<std::int32_t>>(11,9);
    }
    MessengerCallResult run(ModelStatusUpdateInvocation& call,std::shared_ptr<ModelStatusUpdateServices> services={}) {
        return call.resume(messenger,events,creation,std::move(services));
    }
    void empty_icons() {
        provider(events,"MODELCTRL",{},-1,[](NativeMessenger&,const MessengerDelivery& delivery) {
            if(delivery.message.command!="FIND_SPACE")throw std::runtime_error("Unexpected synthetic icon message.");
            return MessengerCallResult{true,1,{}};
        });
    }
    void actual_registration() {
        auto document=source.document();const auto zero=*document.field(document.object(source.ones[0]),3);
        auto flag=*document.field(document.object(source.state),4);
        flag.slot=4;document.objects.at(document.references.at(source.model).value_index).fields.push_back(flag);
        for(const auto reference:source.model_rams) {
            auto& object=document.objects.at(document.references.at(reference).value_index);
            for(const auto slot:{2u,6u,7u,8u,9u}) {auto field=zero;field.slot=static_cast<std::uint16_t>(slot);object.fields.push_back(field);}
            flag.slot=5;object.fields.push_back(flag);
        }
        scene=std::make_shared<ActorSceneResource>(std::move(document),"synthetic/status-entry.igb",9);
        parent=scene->models().info(source.model);ram=scene->models().ram(source.model_rams[0]);
        one=std::dynamic_pointer_cast<ModelSceneOne>(scene->models().data(source.ones[0]));one->work=work;
        events.register_model_scene_ram(scene,source.model,source.model_rams[0],2);ram->active=1;
        events.set_model_context(manager->context);
        const auto seq=find_model_seq(*parent);
        for(const auto* name:{"SQ_SLEP","SQ_STUN","SQ_DEAD"}) {
            auto row=std::make_shared<ModelSceneSequence>();row->name=name;
            row->commands=std::make_shared<ModelSceneList>("igNamedObjectList");seq->sequences->append(row);
        }
    }
};
void gates_and_death_capture() {
    Fixture f;f.work->script.hp.reset();ModelStatusUpdateInvocation no_status(f.ram);
    check(f.run(no_status).completed && no_status.captured_work()==f.work,
        "Null timers must return before consuming the HP wrapper.");
    Fixture missing;missing.one->work.reset();ModelStatusUpdateInvocation bad(missing.ram);
    rejects([&]{missing.run(bad);},"Null captured status work must reject.");check(bad.faulted(),"Capture failure must poison invocation.");
    Fixture death;death.status();death.work->script.hp->data=0;
    (*death.work->status.display_ram_numbers)[0]=7;(*death.work->status.display_ram_numbers)[1]=8;
    (*death.work->status.timers)[0]=14;
    auto replacement=make_model_player_work();replacement->script.hp->data=99;
    ModelStatusUpdateInvocation call(death.ram);unsigned sends{};
    provider(death.events,"MODELCTRL",{},-1,[&](NativeMessenger& packet,const MessengerDelivery& delivery) {
        ++sends;check(delivery.message.command=="SQ_ERAS","Dead status must use the original icon erase sequence.");
        if(sends==1) {
            check(delivery.message.name=="i_stdownic01" && delivery.message.index==7,"First death icon arguments wrong.");
            death.one->work=replacement;(*death.work->status.display_ram_numbers)[1]=19;
            packet.buffer().add_int(42);return MessengerCallResult{false,0,"Retained death message"};
        }
        if(sends==2)check(packet.buffer().integers()==std::vector<std::int32_t>{42},"Death retry rebuilt its packet.");
        if(sends==3)check(delivery.message.name=="i_stdownic02" && delivery.message.index==19,"Death loop did not reload the current display list.");
        return MessengerCallResult{true,1,{}};
    });
    check(!death.run(call).completed && (*death.work->status.timers)[0]==14,"Suspended erase must precede reset fields.");
    check(death.run(call).completed && sends==3 && call.captured_work()==death.work && death.one->work==replacement,
        "Status callback failed to retain its independently captured work.");
    check(*death.work->status.timers==std::vector<std::int32_t>(12,0) &&
        *death.work->status.display_ram_numbers==std::vector<std::int32_t>(12,-1) && replacement->script.hp->data==99,
        "Dead reset changed a replacement work or omitted the captured work lists.");
}
void poison_live_messages_and_capture() {
    Fixture f;f.status();f.work->status.is_player=1;f.work->status.poison_damage=7;
    (*f.work->status.timers)[0]=1; // Missing timer is expired without first/start.
    ModelStatusUpdateInvocation call(f.ram);unsigned meters{},numbers{};
    check(!f.run(call).completed && f.work->script.hp->data==93 && f.work->messenger==&f.events.messenger(),
        "Poison damage/messenger publication must precede missing meter service.");
    f.ram->number=6;
    provider(f.events,"METERCTRL","hpmt",2,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++meters;check(delivery.message.command=="SETPERCENT" && live.buffer().floats()==std::vector<float>{46.5f},
            "Poison meter packet lost its original float percentage or captured index.");
        f.work->script.hp->data=80;f.ram->number=4;return MessengerCallResult{true,1,{}};
    });
    provider(f.events,"NUMBERCTRL","hpnm",4,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++numbers;check(delivery.message.command=="SETVALUE" && live.buffer().integers()==std::vector<std::int32_t>{80},
            "Poison number must read current HP and live RAM after the meter returns.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && meters==1 && numbers==1 && (*f.work->status.timers)[0]==0,
        "Poison update did not finish after its real UI effects.");
    Fixture nonplayer;nonplayer.status();nonplayer.work->script.hp->data=3;nonplayer.work->status.poison_damage=8;
    (*nonplayer.work->status.timers)[0]=1;ModelStatusUpdateInvocation npc(nonplayer.ram);
    check(nonplayer.run(npc).completed && nonplayer.work->script.hp->data==1 && (*nonplayer.work->status.timers)[0]==0,
        "Nonplayer poison must clamp lethal damage back to one and clear the status.");
}
void status_expiry_refresh_and_bounded_lists() {
    Fixture f;f.status();f.work->status.is_player=1;(*f.work->status.timers)[1]=8;(*f.work->status.timers)[2]=9;
    ModelStatusUpdateInvocation call(f.ram);auto& messenger=f.events.messenger();
    messenger.select("PREVIOUS",{},0);messenger.buffer().add_string("retained parent");const auto old=messenger.buffer_owner();
    check(!f.run(call).completed && messenger.buffer_depth()==1 &&
        *f.work->status.down_data==std::vector<std::int32_t>({0,0,0,0,9,9,9}),
        "ATKDOWN clear and buffer push must precede missing recalculation service.");
    unsigned recalc{},add_hp{};
    provider(f.events,"GAMERAM",{},0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        if(delivery.message.command=="RECALCPLAYER") {
            ++recalc;check(live.buffer().integers().size()==1,"RECALCPLAYER packet must contain one selected RAM number.");
            f.ram->number=5;
        }else if(delivery.message.command=="ADDHP") {
            ++add_hp;check(live.buffer().integers()==std::vector<std::int32_t>({5,0}),"ADDHP must read RAM again and append zero.");
        }else throw std::runtime_error("Unexpected recalculation command.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && recalc==2 && add_hp==2 && messenger.buffer_depth()==0 && messenger.buffer_owner()==old &&
        messenger.buffer().strings()==std::vector<std::optional<std::string>>{"retained parent"},
        "Expired statuses did not restore the exact parent buffer after each recalculation.");
    Fixture short_list;short_list.status();short_list.work->status.timers->resize(1);short_list.work->sequence.wait.control_enabled=0;
    ModelStatusUpdateInvocation short_call(short_list.ram);
    rejects([&]{short_list.run(short_call);},"Final fixed icon reads must not silently repair a short status list.");
    check(short_call.faulted(),"Reached short-list failure must poison update.");
}
void start_order_bind_and_metal() {
    Fixture bind;bind.status();(*bind.work->status.timers)[3]=60;(*bind.work->status.first_flags)[3]=1;
    for(unsigned i=0;i<4;++i) {auto channel=std::make_shared<MotionState>();channel->active=true;channel->speed=4;
        bind.work->control.movement->mutable_entries().push_back(channel);}
    bind.empty_icons();ModelStatusUpdateInvocation bound(bind.ram);
    check(bind.run(bound).completed && find_model_timer(*bind.ram,"BIND")->timer.end_time==1000000000 &&
        (*bind.work->status.first_flags)[3]==0,"BIND first pulse did not start its exact duration and clear its flag.");
    for(unsigned i=0;i<4;++i)check(bind.work->control.movement->entry_owner(i)->active==(i==1) &&
        bind.work->control.movement->entry_owner(i)->speed==(i==1 ? 4:0),"BIND must stop all captured channels except index one.");
    Fixture metal;metal.status();(*metal.work->status.timers)[7]=1;(*metal.work->status.first_flags)[7]=1;
    metal.empty_icons();unsigned clocks{};
    auto access=std::make_shared<ModelRamTimerAccess>(metal.ram,[&](const ModelSceneRam&) {
        ++clocks;return clocks==1 ? std::uint64_t{0}:std::uint64_t{1u<<18};
    });
    GameDataState data;data.player(0)->require<GameDataInt>("ATTACKDELAY")->data=3;
    provider(metal.events,"GAMERAM",{},0,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        check(delivery.message.command=="GETPLAYER","Nonplayer metal must not request player recalculation.");
        check((*metal.work->status.down_data)[6]==4,"Metal increment must precede GETPLAYER.");
        live.buffer().add_data_list(data.player(0));return MessengerCallResult{true,1,{}};
    });
    ModelStatusUpdateInvocation plated(metal.ram,access);
    check(metal.run(plated).completed && clocks==3 && find_model_timer(*metal.ram,"METAL")->timer.end_time==6000000000,
        "METAL must use integer ATTACKDELAY*120/60 duration and restart after the row query.");
}
void icon_live_owners_and_resolver() {
    Fixture f;f.status();f.work->sequence.wait.control_enabled=0;(*f.work->status.timers)[10]=1;
    auto node=f.one->node;auto matrix=node->matrix_owner();(*matrix)[12]=3;(*matrix)[13]=4;(*matrix)[14]=5;
    const auto returned_id=ControllerObjectId::allocate_native();auto returned=std::make_shared<ModelSceneRam>();returned->number=7;
    unsigned finds{},generates{};
    Hook icon_hook=[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        if(delivery.message.command=="GENERATE") {
            ++generates;check(delivery.message.index==7 && live.buffer().strings()==std::vector<std::optional<std::string>>{"SQ_OPEN01"} &&
                live.buffer().vectors()==std::vector<std::array<float,3>>{{3,4,5}},"Icon generation did not retain pre-FIND_SPACE node translation.");
            returned->number=9;
        }else throw std::runtime_error("Unexpected rare icon request.");
        return MessengerCallResult{true,1,{}};
    };
    f.events.register_native_controller(returned_id,{
        [&]()->std::optional<std::uint8_t>{++finds;(*matrix)[12]=30;return std::uint8_t{0};},
        [icon_hook]{return MessengerManager{icon_hook,{}};}},"MODELCTRL","i_rareicon",7);
    ModelStatusUpdateInvocation call(f.ram);
    const auto initial=f.run(call);
    check(!initial.completed && finds==1 && generates==0 && (*f.work->status.display_ram_numbers)[10]==-1,
        "Missing returned-RAM resolver must retain FIND_SPACE result without publishing an icon number.");
    auto services=std::make_shared<ModelStatusUpdateServices>();services->resolve_ram=[&](ControllerObjectId id) {
        check(id==returned_id,"Icon resolver received the wrong actual object identity.");return returned;
    };
    check(f.run(call,services).completed && finds==1 && generates==1 && (*f.work->status.display_ram_numbers)[10]==9,
        "Icon helper must read returned RAM.number again after GENERATE and publish through live display list.");
}
void faults_and_prefixes() {
    Fixture f;f.status();(*f.work->status.timers)[0]=2;(*f.work->status.first_flags)[0]=1;
    auto access=std::make_shared<ModelRamTimerAccess>(f.ram,[](const ModelSceneRam&)->std::uint64_t {
        throw std::runtime_error("Unavailable real clock");
    });
    ModelStatusUpdateInvocation call(f.ram,access);
    rejects([&]{f.run(call);},"Clock failure must reject after named timer append.");
    check(call.faulted() && find_model_timer(*f.ram,"POISON") && (*f.work->status.timers)[0]==2 &&
        (*f.work->status.first_flags)[0]==1,"Failed first poison start must retain its append before decrement/flag stores.");
    Fixture recursive;recursive.status();recursive.work->script.hp->data=0;(*recursive.work->status.display_ram_numbers)[0]=1;
    ModelStatusUpdateInvocation reentered(recursive.ram);
    provider(recursive.events,"MODELCTRL",{},-1,[&](NativeMessenger&,const MessengerDelivery&) {
        try{recursive.run(reentered);}catch(const std::exception&){}return MessengerCallResult{true,1,{}};
    });
    rejects([&]{recursive.run(reentered);},"Caught recursive callback must poison outer status invocation.");
    check(reentered.faulted() && (*recursive.work->status.display_ram_numbers)[0]==1,
        "Caught reentry must prevent subsequent death reset publication.");
}
void actual_sleep_stun_and_other_expiry() {
    Fixture f;f.status();f.actual_registration();(*f.work->status.timers)[8]=60;(*f.work->status.first_flags)[8]=1;
    ModelStatusUpdateInvocation sleep(f.ram);
    check(f.run(sleep).completed && f.work->sequence.name=="SQ_SLEP" && (*f.work->status.first_flags)[8]==0 &&
        (*f.work->status.timers)[8]==60 && find_model_timer(*f.ram,"SLEEP"),
        "Actual SLEEP must finish nested ordinary SQ_SLEP/+B8 before clearing first flag and testing expiry.");
    // A second real recipient pauses SQ_WAIT's +B8 SETEFFECT delivery after
    // the canonical first recipient has appended the actual effect.
    provider(f.events,"MODELCTRL",f.parent->name,2,[](NativeMessenger&,const MessengerDelivery&) {
        return MessengerCallResult{false,0,"Retained SETEFFECT recipient"};
    });
    f.manager->context->world_time=2000000000;ModelStatusUpdateInvocation wake(f.ram);
    const auto waking=f.run(wake);
    check(!waking.completed && f.work->sequence.name=="SQ_WAIT" && wake.direct_entry() &&
        find_model_timer(*f.ram,"SLEEP") && (*f.work->status.timers)[8]==60,
        "Expired SLEEP must not remove its timer before the real SQ_WAIT/+B8 effect returns.");
    Fixture stun;stun.status();stun.actual_registration();stun.work->sequence.name="SQ_STUN";
    provider(stun.events,"MODELCTRL",stun.parent->name,2,[](NativeMessenger&,const MessengerDelivery&) {
        return MessengerCallResult{false,0,"Retained SETEFFECT recipient"};
    });
    (*stun.work->status.timers)[9]=60; // No actual timer means already expired.
    ModelStatusUpdateInvocation unstun(stun.ram);
    check(!stun.run(unstun).completed && stun.work->sequence.name=="SQ_WAIT" &&
        (*stun.work->status.timers)[9]==0 && !find_model_timer(*stun.ram,"STUN"),
        "STUN expiry must clear/remove before the real SQ_WAIT/+B8 effect boundary.");
    for(const auto index:{4u,5u,6u}) {
        Fixture effect;effect.status();(*effect.work->status.timers)[index]=3;
        ModelStatusUpdateInvocation update(effect.ram);
        check(effect.run(update).completed && (*effect.work->status.timers)[index]==0,
            "Missing named beneficial timer must expire and clear its actual status slot.");
        const std::size_t first=index==4 ? 7:index==5 ? 0:4;
        const std::size_t count=index==6 ? 3:4;
        for(std::size_t at=0;at<11;++at)check((*effect.work->status.up_data)[at]==(at>=first && at<first+count ? 0:9),
            "Beneficial expiry reset the wrong original up-data indices.");
    }
}
}
int main() {
    try {gates_and_death_capture();poison_live_messages_and_capture();status_expiry_refresh_and_bounded_lists();
        start_order_bind_and_metal();icon_live_owners_and_resolver();faults_and_prefixes();actual_sleep_stun_and_other_expiry();
        std::cout<<"Model status update checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Model status update after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
