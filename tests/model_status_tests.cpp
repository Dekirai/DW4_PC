#include "dw4/model_status.hpp"
#include "dw4/action_event_runtime.hpp"

#include <algorithm>
#include <iostream>
#include <type_traits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation,const char* message) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected,message);
}
template<class T> auto values(std::size_t count,T value) {return std::make_shared<std::vector<T>>(count,value);}
std::shared_ptr<ModelStatusWork> work(std::uint8_t player=0,std::int32_t ram=2) {
    const AnimationMatrix matrix{1,0,0,0,0,1,0,0,0,0,1,0,4,5,6,1};
    return std::make_shared<ModelStatusWork>(std::make_shared<ModelScriptWork>(ControllerObjectId{3,80+static_cast<std::uint32_t>(ram)},
        "synthetic/status.igb","p_test",ram,std::make_shared<AnimationMatrix>(matrix)),player);
}
ModelStatusOwner owner(std::shared_ptr<ModelStatusWork> value) {return {std::move(value),std::make_shared<std::int32_t>(2)};}
using Hook=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void provider(ActionEventRuntime& events,Hook hook) {
    events.register_native_controller(ControllerObjectId::allocate_native(),{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [hook=std::move(hook)]{return MessengerManager{hook,{}};}},"GAMERAM","prepared",0);
}
void prepared_lists(const std::shared_ptr<ModelStatusWork>& value,std::size_t count=12) {
    value->display_ram_numbers=values<std::int32_t>(0,0);
    value->display_positions=values<std::int32_t>(1,37);
    value->timers=values<std::int32_t>(count,-31);
    value->first_flags=values<std::uint8_t>(count,73);
}
void initialization_and_declines() {
    static_assert(!std::is_copy_constructible_v<ModelStatusWork> && !std::is_move_constructible_v<ModelStatusWork>);
    ActionEventRuntime events;auto& messenger=events.messenger();
    for(const auto command:{"GETHP","GETINFONAME","SETLABEL","setmetal",""}) {
        const auto target=work();ModelStatusInvocation invocation(owner(target),command);
        const auto result=invocation.resume(messenger,events);
        check(result.completed && result.value==0 && invocation.completed() && !target->display_ram_numbers,
            "Unknown status command did not decline before allocations.");
    }
    for(const auto command:{"CHECKSTATUS","CLEARSTATUS","SETPOISON","GETPEBASEWORK"}) {
        const auto target=work();ModelStatusInvocation invocation(owner(target),command);
        const auto result=invocation.resume(messenger,events);
        check(!result.completed && !invocation.completed() && !invocation.faulted(),"Known unsupported status command falsely completed.");
        const bool initializes=std::string_view(command)!="CHECKSTATUS" && std::string_view(command)!="CLEARSTATUS";
        check(static_cast<bool>(target->display_ram_numbers)==initializes,"Status command used the wrong initialization gate.");
        const auto first=target->display_ram_numbers;
        check(!invocation.resume(messenger,events).completed && first==target->display_ram_numbers,"Unsupported status retry repeated its allocation prefix.");
    }
    const auto target=work();const auto old_positions=target->display_positions=values<std::int32_t>(3,91);
    const auto old_flags=target->first_flags=values<std::uint8_t>(4,81);
    const auto old_up=target->up_data=values<std::int32_t>(9,71);
    ModelStatusInvocation invocation(owner(target),"SETMETAL");
    check(invocation.resume(messenger,events).completed && invocation.completed(),"Fresh non-player SETMETAL did not complete.");
    check(*target->display_ram_numbers==std::vector<std::int32_t>(12,-1) && *target->display_positions==std::vector<std::int32_t>(12,-1),
        "Original status list initialization changed counts or fill values.");
    check(target->display_positions!=old_positions && *old_positions==std::vector<std::int32_t>(3,91) && target->first_flags!=old_flags,
        "Null F0 did not replace independent old status list owners.");
    check(target->timers->size()==12 && (*target->timers)[7]==1 && (*target->timers)[6]==0 &&
        target->first_flags->size()==11 && (*target->first_flags)[7]==1 && (*target->first_flags)[6]==0,
        "SETMETAL did not apply its original timer/first-flag slot after initialization.");
    check(*target->down_data==std::vector<std::int32_t>(6,0) && target->up_data==old_up,
        "Non-player DOWNDATA allocation changed its size, fill or unrelated UPDATA owner.");
    (*target->timers)[7]=99;check(invocation.resume(messenger,events).value==1 && (*target->timers)[7]==99,
        "Completed invocation replayed its committed SETMETAL stores.");
    ModelStatusInvocation null_work({{},{}},"SETMETAL");
    check(null_work.resume(messenger,events).value==1 && null_work.completed(),"Null actual base work did not take the original handled exit.");
}
void short_shared_lists() {
    for(const std::size_t count:{0u,1u,7u,8u,12u}) {
        ActionEventRuntime events;auto& messenger=events.messenger();const auto target=work(255);prepared_lists(target,count);
        const auto timer=target->timers,down=target->down_data=values<std::int32_t>(1,-51);
        const auto flags=target->first_flags;ModelStatusInvocation invocation(owner(target),"SETMETAL");
        check(invocation.resume(messenger,events).value==1 && target->timers==timer && target->first_flags==flags && target->down_data==down,
            "Non-null empty F0 caused reinitialization or existing DOWNDATA was replaced.");
        check(timer->size()==count && flags->size()==count && (count<=7 || ((*timer)[7]==1 && (*flags)[7]==1)),
            "Short status list was grown or its valid slot was skipped.");
        check(count==0 || (*timer)[0]==-31,"SETMETAL modified a different status slot.");
    }
    ActionEventRuntime events;auto& messenger=events.messenger();const auto target=work();prepared_lists(target);
    target->timers.reset();ModelStatusInvocation invocation(owner(target),"SETMETAL");
    rejects([&]{invocation.resume(messenger,events);},"Null existing timer list passed its original dereference.");
    check(invocation.faulted() && (*target->first_flags)[7]==1,"Later list failure rolled back the first original store.");
    rejects([&]{invocation.resume(messenger,events);},"Faulted status invocation replayed its prefix.");
}
void retained_player_query() {
    for(bool null_payload:{false,true}) {
        GameDataState game;ActionEventRuntime events;auto& messenger=events.messenger();const auto target=work(255);auto fields=owner(target);
        const auto returned_row=game.player(2);
        const auto result=returned_row->require<GameDataIntList>("DOWNDATA");
        if(null_payload)result->data.reset();else *result->data={51,52,53};
        messenger.select("OUTER","kept",2);messenger.buffer().add_string("original");messenger.buffer().add_int(-77);
        messenger.buffer().set_enabled_only(255);const auto parent=messenger.buffer_owner();const auto depth=messenger.buffer_depth();
        unsigned calls{};std::shared_ptr<std::vector<std::int32_t>> nested;
        provider(events,[&](NativeMessenger& native,const MessengerDelivery& delivery) {
            ++calls;check(delivery.message.category=="GAMERAM" && !delivery.message.name && delivery.message.index==-1 &&
                delivery.message.command=="GETPLAYER" && native.buffer().integers()==std::vector<std::int32_t>{2},
                "DOWNDATA query lost its retained original category/int packet.");
            if(calls==1) {
                check((*target->first_flags)[7]==1 && (*target->timers)[7]==1,"GETPLAYER ran before the SETMETAL stores.");
                native.buffer().add_data_list(returned_row);game.reset_player(2);*fields.live_ram_no=3;target->is_player=0;
                (*target->timers)[7]=89;target->down_data=nested=values<std::int32_t>(2,61);
                return MessengerCallResult{false,0,"prepared GETPLAYER paused"};
            }
            return MessengerCallResult{true,1,{}};
        });
        ModelStatusInvocation invocation(fields,"SETMETAL");
        check(!invocation.resume(messenger,events).completed && !invocation.captured_row() && !invocation.faulted(),
            "Status query completed or captured a result before its callback finished.");
        check(messenger.buffer_depth()==depth+1 && target->down_data==nested,"Paused DOWNDATA lost its child frame or nested mutation.");
        ActionEventRuntime foreign;auto& other=foreign.messenger();
        rejects([&]{invocation.resume(other,foreign);},"Pending status query accepted foreign messenger ownership.");
        check(!invocation.faulted(),"Rejected foreign owner poisoned the valid pending status query.");
        check(invocation.resume(messenger,events).value==1 && calls==2 && target->down_data==result->data &&
            invocation.captured_row()==returned_row && game.player(2)!=returned_row && (*target->timers)[7]==89,
            "Resumed DOWNDATA reparsed live fields, repeated stores or retargeted the current game row.");
        check(messenger.buffer_depth()==depth && messenger.buffer_owner()==parent && messenger.buffer().enabled_only()==255 &&
            messenger.buffer().integers()==std::vector<std::int32_t>{-77} && messenger.buffer().strings()==std::vector<std::optional<std::string>>{"original"},
            "DOWNDATA helper did not restore the actual parent packet owner.");
    }
}
void nested_same_work_and_faults() {
    GameDataState game;ActionEventRuntime events;auto& messenger=events.messenger();const auto target=work(1);auto fields=owner(target);
    messenger.select("PLAYER",std::nullopt,2);
    const auto row=game.player(2);auto result=row->require<GameDataIntList>("DOWNDATA")->data;*result={3,5,7};
    provider(events,[&](NativeMessenger& native,const MessengerDelivery&) {
        target->is_player=0;ModelStatusInvocation child(fields,"SETMETAL");
        check(child.resume(native,events).value==1 && *target->down_data==std::vector<std::int32_t>(6,0),
            "Separate nested invocation on the same work did not execute its own helper.");
        native.buffer().add_data_list(row);return MessengerCallResult{true,1,{}};
    });
    ModelStatusInvocation parent(fields,"SETMETAL");
    check(parent.resume(messenger,events).value==1 && target->down_data==result,
        "Parent DOWNDATA result did not overwrite the nested same-work helper publication.");
    for(unsigned variant=0;variant<5;++variant) {
        ActionEventRuntime runtime;auto& native=runtime.messenger();auto value=work(1);auto supplied=owner(value);
        native.select("PLAYER",std::nullopt,2);
        GameDataState data;auto returned=data.player(2);
        if(variant==2)returned->require<GameDataIntList>("DOWNDATA")->name="missing";
        if(variant==3){auto entries=returned->entries();entries.insert(entries.begin(),std::make_shared<GameDataInt>(GameDataInt{"DOWNDATA",7}));*returned=GameDataList(std::move(entries));}
        if(variant==4)supplied.live_ram_no.reset();
        provider(runtime,[&](NativeMessenger& current,const MessengerDelivery&) {
            if(variant==0)current.buffer().add_data_list({});
            else if(variant==1)current.buffer().add_int(1);
            else current.buffer().add_data_list(returned);
            return MessengerCallResult{true,1,{}};
        });
        const auto depth=native.buffer_depth();ModelStatusInvocation invocation(supplied,"SETMETAL");
        rejects([&]{invocation.resume(native,runtime);},"Missing/wrong DOWNDATA result passed its checked original read.");
        check(invocation.faulted() && (*value->timers)[7]==1 && native.buffer_depth()==depth+1,
            "Failed DOWNDATA query rolled back status or automatically restored its child frame.");
    }
    ActionEventRuntime no_game;auto& native=no_game.messenger();auto value=work(1);ModelStatusInvocation pending(owner(value),"SETMETAL");
    native.select("PLAYER",std::nullopt,2);
    check(!pending.resume(native,no_game).completed && (*value->timers)[7]==1 && !pending.faulted(),
        "Missing GAMERAM was mistaken for a completed status service.");
    value->is_player=0;check(!pending.resume(native,no_game).completed && !value->down_data,
        "Missing-service retry reselected the helper's already captured player branch.");
    rejects([&]{ModelStatusInvocation bad(owner(value),std::nullopt);},"Null native command was silently classified as unknown.");
    rejects([&]{ModelStatusInvocation bad(owner(value),std::string("A\0B",3));},"Embedded-NUL native command passed its bounded profile.");
    ModelStatusInvocation moved(std::move(pending));check(pending.faulted() && !pending.completed() && moved.owner().work==value,"Move lost the retained status work.");
}
}
int main() {
    try {initialization_and_declines();short_shared_lists();retained_player_query();nested_same_work_and_faults();
        std::cout<<"Model status tests passed: "<<checks<<" checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model status tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
