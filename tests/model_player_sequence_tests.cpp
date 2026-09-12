#include "dw4/model_player_sequence.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_dispatch.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* reason) {++checks;if(!condition)throw std::runtime_error(reason);}
template<class F> void rejects(F operation,const char* reason) {
    ++checks;try{operation();}catch(const std::exception&){return;}throw std::runtime_error(reason);
}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,std::string category,std::optional<std::string> name,int index,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},std::move(category),std::move(name),index);
}
struct Fixture {
    std::shared_ptr<ModelSceneInfo> parent=std::make_shared<ModelSceneInfo>();
    std::shared_ptr<ModelSceneRam> ram=std::make_shared<ModelSceneRam>();
    std::shared_ptr<ModelSceneOne> one=std::make_shared<ModelSceneOne>();
    std::shared_ptr<ModelPlayerWork> work=make_model_player_work();
    ActionEventRuntime events;
    NativeMessenger& messenger=events.messenger();
    ModelPlayerCreateServices services;
    Fixture() {
        parent->name="player";ram->parent=parent;ram->number=3;
        one->name="ONE";one->work=work;ram->data=std::make_shared<ModelSceneList>();ram->data->append(one);
        work->sequence.name="SQ_RUN";
    }
    void status() {
        work->script.hp->data=100;
        work->status.display_ram_numbers=std::make_shared<std::vector<std::int32_t>>(12,-1);
        work->status.timers=std::make_shared<std::vector<std::int32_t>>(12,0);
        work->status.first_flags=std::make_shared<std::vector<std::uint8_t>>(11,std::uint8_t{1});
    }
    MessengerCallResult run(ModelPlayerSequenceInvocation& call) {return call.resume(messenger,events,services);}
};

void independent_captures_and_retained_packets() {
    Fixture f;f.work->sequence.name="SQ_WAIT";f.work->panel=1;
    ModelPlayerSequenceInvocation call(f.ram);unsigned effects{},panels{};
    auto second=make_model_player_work();second->sequence.name="SQ_RUN";
    auto third=make_model_player_work();third->sequence.name="SQ_RUN";
    f.events.messenger().select("MODELCTRL","player",3);
    const auto initial_buffer=f.events.messenger().buffer_owner();
    std::shared_ptr<const MessengerBuffer> effect_packet;
    provider(f.events,"MODELCTRL","player",3,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++effects;
        check(delivery.message.command=="SETEFFECT" && delivery.message.index==3 && delivery.message.name=="player",
            "Idle effect selected the wrong original identity.");
        check(live.buffer().strings()==std::vector<std::optional<std::string>>{"i_pcnumic","SQ_OPENP3"} &&
            live.buffer().floats()==std::vector<float>{480} &&
            live.buffer().vectors()==std::vector<std::array<float,3>>{{0,10,0}} &&
            live.buffer().booleans()==std::vector<std::uint8_t>{1,1} && live.buffer().matrices().empty(),
            "Idle effect packet does not match the original two strings/float/vector/booleans.");
        if(effects==1) {
            effect_packet=live.buffer_owner();live.buffer().add_int(91);
            f.work->sequence.name="SQ_STUN";f.one->work=second;f.ram->number=5;
            f.parent->name="changed";f.ram->parent.reset();
            return MessengerCallResult{false,0,"Synthetic retained effect delivery."};
        }
        check(live.buffer_owner()==effect_packet && live.buffer().integers()==std::vector<std::int32_t>{91},
            "Idle effect retry rebuilt its live packet.");
        return MessengerCallResult{true,1,{}};
    });
    check(!f.run(call).completed && call.captured_work()==f.work && call.captured_parent()==f.parent &&
        !call.reconciliation_work(),"Idle effect lost its separate captures before suspension.");
    check(!f.run(call).completed && f.work->panel==0 && effects==2 && !call.reconciliation_work(),
        "Panel clear did not precede the required panel provider boundary.");
    f.ram->number=7;
    provider(f.events,"NDMWPANEL",{},5,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++panels;check(f.work->panel==0 && delivery.message.command=="REMOVE" && !delivery.message.name &&
            delivery.message.index==5 && live.buffer().strings().empty() && live.buffer().booleans().empty(),
            "Panel removal lost its clear prefix or fresh RAM index.");
        f.one->work=third;return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && call.reconciliation_work()==third && call.captured_work()==f.work &&
        effects==2 && panels==1,"Status reconciliation reused the outer callback work.");
    check(f.events.messenger().buffer_depth()==0 && f.events.messenger().buffer_owner()==initial_buffer,
        "Player sequence callback invented a buffer push/pop.");
    check(f.run(call).completed && effects==2 && panels==1,"Completed callback replayed its sends.");
}

void selection_argument_capture_before_services() {
    Fixture f;f.work->sequence.name="SQ_WAIT";ModelPlayerSequenceInvocation call(f.ram);
    check(!f.run(call).completed,"Absent MODELCTRL provider was treated as a completed idle effect.");
    f.parent->name="changed";f.ram->number=9;unsigned calls{};
    provider(f.events,"MODELCTRL","player",3,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++calls;check(delivery.message.index==3 && delivery.message.name=="player" &&
            live.buffer().strings()==std::vector<std::optional<std::string>>{"i_pcnumic","SQ_OPENP9"},
            "Selection arguments or the separate later RAM read changed across provider suspension.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && calls==1,"Idle effect did not finish its retained selection arguments.");
    Fixture death;death.status();death.work->script.hp->data=0;(*death.work->status.display_ram_numbers)[0]=14;
    ModelPlayerSequenceInvocation removal(death.ram);
    check(!death.run(removal).completed,"Missing status icon provider completed a required send.");
    (*death.work->status.display_ram_numbers)[0]=99;
    provider(death.events,"MODELCTRL","i_stdownic01",14,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        check(delivery.message.index==14,"Status icon selection reloaded the consumed slot on retry.");
        return MessengerCallResult{true,1,{}};
    });
    check(death.run(removal).completed,"Status icon removal did not resume its consumed target index.");
}

void panel_dictionary_and_null_parent() {
    for(const auto* name:{"SQ_SDMG","SQ_MDMG","SQ_LDMG","SQ_DDMG","SQ_DEAD","SQ_SLEP","SQ_STUN","SQ_RUN","sq_stun"}) {
        Fixture f;f.work->sequence.name=name;f.work->panel=7;f.ram->parent.reset();unsigned calls{};
        provider(f.events,"NDMWPANEL",{},3,[&](NativeMessenger&,const MessengerDelivery&) {
            ++calls;return MessengerCallResult{true,1,{}};
        });
        ModelPlayerSequenceInvocation call(f.ram);const bool accepted=std::string_view(name)!="SQ_RUN" && std::string_view(name)!="sq_stun";
        check(f.run(call).completed && calls==static_cast<unsigned>(accepted) && f.work->panel==(accepted?0:7),
            "Panel dictionary confused success status with enum value or required an unused parent.");
    }
    Fixture null_name;null_name.parent->name.reset();null_name.work->sequence.name="SQ_WAIT";
    unsigned effects{};
    provider(null_name.events,"MODELCTRL","",3,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        ++effects;check(delivery.message.name=="","Null parent name did not use the original empty string.");
        return MessengerCallResult{true,1,{}};
    });
    ModelPlayerSequenceInvocation call(null_name.ram);check(null_name.run(call).completed && effects==1,
        "Null name fallback did not complete the real idle effect delivery.");
    Fixture missing_parent;missing_parent.work->sequence.name="SQ_WAIT";missing_parent.ram->parent.reset();
    ModelPlayerSequenceInvocation bad(missing_parent.ram);
    rejects([&]{missing_parent.run(bad);},"SQ_WAIT accepted a captured null parent.");
    check(bad.faulted() && bad.captured_work()==missing_parent.work,"Null parent failed before the original work capture.");
}

void death_loop_live_slots_and_clear_owners() {
    Fixture f;f.status();f.work->script.hp->data=0;
    const auto old_displays=f.work->status.display_ram_numbers;(*old_displays)[0]=30;
    const auto old_timers=f.work->status.timers;std::fill(old_timers->begin(),old_timers->end(),7);
    const auto new_displays=std::make_shared<std::vector<std::int32_t>>(12,-1);
    (*new_displays)[1]=41;(*new_displays)[11]=51;
    const auto new_timers=std::make_shared<std::vector<std::int32_t>>(4,9);
    ModelPlayerSequenceInvocation call(f.ram);std::vector<std::pair<std::string,int>> delivered;unsigned first_calls{};
    provider(f.events,"MODELCTRL","i_stdownic01",30,[&](NativeMessenger& live,const MessengerDelivery& delivery) {
        ++first_calls;
        check(delivery.message.command=="SQ_ERAS" && delivery.message.index==30,"Status erase retry changed captured index.");
        if(first_calls==1) {
            delivered.emplace_back(*delivery.message.name,delivery.message.index);
            f.work->status.display_ram_numbers=new_displays;f.work->status.timers=new_timers;
            f.one->work=make_model_player_work();live.buffer().add_int(8);
            return MessengerCallResult{false,0,"Synthetic icon removal pause."};
        }
        check(live.buffer().integers()==std::vector<std::int32_t>{8},"Status removal retry rebuilt the packet.");
        return MessengerCallResult{true,1,{}};
    });
    for(const auto& row:std::array<std::pair<const char*,int>,2>{{{"i_stdownic02",41},{"i_stdownic00",51}}}) {
        provider(f.events,"MODELCTRL",row.first,row.second,[&](NativeMessenger&,const MessengerDelivery& delivery) {
            delivered.emplace_back(*delivery.message.name,delivery.message.index);return MessengerCallResult{true,1,{}};
        });
    }
    check(!f.run(call).completed && call.reconciliation_work()==f.work,"Death icon loop did not retain its independent work.");
    check(f.run(call).completed && delivered==std::vector<std::pair<std::string,int>>{
        {"i_stdownic01",30},{"i_stdownic02",41},{"i_stdownic00",51}},"Death loop did not freshly read each display slot in original order.");
    check(std::all_of(new_timers->begin(),new_timers->end(),[](auto v){return v==0;}) &&
        std::all_of(new_displays->begin(),new_displays->end(),[](auto v){return v==-1;}) &&
        (*old_displays)[0]==30 && (*old_timers)[0]==7 && (*f.work->status.first_flags)[0]==1,
        "Death reset copied owners or touched first flags/old replaced lists.");
}

void reached_status_failures() {
    Fixture absent;absent.work->script.hp.reset();ModelPlayerSequenceInvocation none(absent.ram);
    check(absent.run(none).completed,"Null F0 did not exit before HP/F8 reads.");
    Fixture empty;empty.work->script.hp.reset();empty.work->status.display_ram_numbers=std::make_shared<std::vector<std::int32_t>>();
    ModelPlayerSequenceInvocation missing(empty.ram);rejects([&]{empty.run(missing);},"Allocated empty F0 skipped its required F8 read.");
    Fixture empty_both;empty_both.work->script.hp.reset();
    empty_both.work->status.display_ram_numbers=std::make_shared<std::vector<std::int32_t>>();
    empty_both.work->status.timers=std::make_shared<std::vector<std::int32_t>>();
    ModelPlayerSequenceInvocation safe(empty_both.ram);check(empty_both.run(safe).completed,"Empty F0/F8 consumed HP or indexed a status slot.");
    Fixture short_list;short_list.status();short_list.work->script.hp->data=0;
    short_list.work->status.display_ram_numbers=std::make_shared<std::vector<std::int32_t>>(1,3);unsigned sent{};
    provider(short_list.events,"MODELCTRL","i_stdownic01",3,[&](NativeMessenger&,const MessengerDelivery&) {
        ++sent;return MessengerCallResult{true,1,{}};
    });
    ModelPlayerSequenceInvocation short_call(short_list.ram);
    rejects([&]{short_list.run(short_call);},"Short death list avoided the fixed twelve physical reads.");
    check(sent==1 && short_call.faulted(),"Short death list was prevalidated before its first actual send.");
    Fixture null_flags;null_flags.status();null_flags.work->status.first_flags.reset();(*null_flags.work->status.timers)[8]=2;
    auto sleep=std::make_shared<ModelSceneData>();sleep->name="SLEEP";null_flags.ram->data->append(sleep);
    ModelPlayerSequenceInvocation flags(null_flags.ram);
    rejects([&]{null_flags.run(flags);},"Sleep reset accepted a null first-flag owner.");
    check(!find_model_named(null_flags.ram->data,"SLEEP") && (*null_flags.work->status.timers)[8]==2,
        "Sleep failure rolled back removal or prematurely cleared the timer field.");
}

void sleep_stun_removal_and_prefix() {
    Fixture f;f.status();(*f.work->status.timers)[8]=11;(*f.work->status.timers)[9]=12;
    f.work->status.first_flags=std::make_shared<std::vector<std::uint8_t>>(8,std::uint8_t{1});
    auto wrong=std::make_shared<ModelSceneData>();wrong->name="SLEEP";
    auto remaining=std::make_shared<ModelSceneTimer>();remaining->name="SLEEP";
    auto stun=std::make_shared<ModelSceneData>();stun->name="STUN";
    f.ram->data->append(wrong);f.ram->data->append(remaining);f.ram->data->append(stun);
    const auto backing=f.ram->data->backing;const auto capacity=f.ram->data->capacity;
    ModelPlayerSequenceInvocation call(f.ram);
    check(!f.run(call).completed && (*f.work->status.timers)[8]==0 && (*f.work->status.timers)[9]==0,
        "Missing stun icon provider lost the preceding sleep/stun mutations.");
    check(f.ram->data->backing==backing && f.ram->data->capacity==capacity && f.ram->data->count==2 &&
        (*backing)[0]==f.one && (*backing)[1]==remaining && !(*backing)[2] && !(*backing)[3],
        "Untyped first-name removal changed backing ownership, order or cleared the wrong alias.");
    unsigned erased{};
    provider(f.events,"MODELCTRL","i_stdownic08",3,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        ++erased;check(delivery.message.command=="SQ_ERAS" && delivery.message.index==-1 &&
            (*f.work->status.timers)[8]==0 && (*f.work->status.timers)[9]==0,
            "Stun icon removal skipped the original -1 index or preceding timer clears.");
        return MessengerCallResult{true,1,{}};
    });
    check(f.run(call).completed && erased==1 && find_model_named(f.ram->data,"SLEEP")==remaining,
        "Resuming icon service repeated the already completed named removal.");
    for(const auto* name:{"SQ_SLEP","SQ_STUN"}) {
        Fixture active;active.status();active.work->sequence.name=name;
        const auto index=std::string_view(name)=="SQ_SLEP" ? 8u:9u;(*active.work->status.timers)[index]=9;
        auto timer=std::make_shared<ModelSceneTimer>();timer->name=index==8 ? "SLEEP":"STUN";active.ram->data->append(timer);
        ModelPlayerSequenceInvocation keep(active.ram);
        check(active.run(keep).completed && (*active.work->status.timers)[index]==9 &&
            find_model_named(active.ram->data,*timer->name)==timer,"Current sleep/stun sequence cleared its own active status.");
    }
}

void recursion_and_null_capture() {
    Fixture f;f.work->sequence.name="SQ_WAIT";ModelPlayerSequenceInvocation call(f.ram);
    provider(f.events,"MODELCTRL","player",3,[&](NativeMessenger&,const MessengerDelivery&) {
        rejects([&]{f.run(call);},"Recursive callback was accepted.");return MessengerCallResult{true,1,{}};
    });
    rejects([&]{f.run(call);},"Caught recursion allowed the outer callback to continue.");
    check(call.faulted() && !call.reconciliation_work(),"Recursive failure did not retain the exact prefix.");
    rejects([&]{f.run(call);},"Faulted callback replayed its packet.");
    Fixture null_data;null_data.ram->data.reset();ModelPlayerSequenceInvocation absent(null_data.ram);
    rejects([&]{null_data.run(absent);},"Missing ONE was invented by the callback.");
    check(null_data.ram->data && null_data.ram->data->count==0 && absent.captured_parent()==null_data.parent,
        "Missing ONE failure lost the ensured data list or earlier parent capture.");
    Fixture null_name;null_name.work->sequence.name.reset();ModelPlayerSequenceInvocation name(null_name.ram);
    rejects([&]{null_name.run(name);},"Null sequence name was treated as an empty supported string.");
}

void actual_direct_stun_entry_on_inactive_ram() {
    Fixture f;f.status();f.work->sequence.name="SQ_WAIT";(*f.work->status.timers)[9]=8;
    f.ram->active=0;unsigned effects{};
    provider(f.events,"MODELCTRL","player",3,[&](NativeMessenger&,const MessengerDelivery& delivery) {
        ++effects;check(delivery.message.command=="SETEFFECT","Direct stun entry invented a messenger dispatch.");
        return MessengerCallResult{true,1,{}};
    });
    ModelPlayerSequenceInvocation call(f.ram);
    check(f.run(call).completed && call.direct_entry() && call.direct_entry()->completed() && effects==1,
        "Status reconciliation did not run the actual direct base invocation.");
    check(call.direct_entry()->captured_work()==f.work && call.direct_entry()->status_work()==f.work &&
        f.work->sequence.name=="SQ_WAIT" && (*f.work->status.timers)[9]==8 &&
        f.messenger.selected().command=="SETEFFECT" && f.messenger.buffer_depth()==0,
        "Inactive direct entry changed captured work, status fields or the existing packet.");
}
}
int main() {
    try {
        independent_captures_and_retained_packets();selection_argument_capture_before_services();panel_dictionary_and_null_parent();
        death_loop_live_slots_and_clear_owners();reached_status_failures();sleep_stun_removal_and_prefix();
        recursion_and_null_capture();
        actual_direct_stun_entry_on_inactive_ram();
        std::cout<<"Model player sequence tests passed: "<<checks<<" checks.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
