#include "dw4/action_event_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
static_assert(std::is_copy_constructible_v<MessengerBuffer>);
static_assert(!std::is_move_constructible_v<MessengerBuffer>);
static_assert(!std::is_copy_assignable_v<MessengerBuffer>);
static_assert(!std::is_move_assignable_v<MessengerBuffer>);
unsigned checks{};
void check(bool condition,const char* reason) {
    ++checks;
    if(!condition)throw std::runtime_error(reason);
}
template<class F> void rejects(F&& operation,const char* reason) {
    bool rejected=false;
    try {operation();}catch(const std::exception&){rejected=true;}
    check(rejected,reason);
}
using Handler=std::function<MessengerCallResult(NativeMessenger&,const MessengerDelivery&)>;
void install(ActionEventRuntime& events,ControllerObjectId id,Handler handler) {
    events.register_native_controller(id,{
        []()->std::optional<std::uint8_t>{return std::uint8_t{1};},
        [handler=std::move(handler)]{return MessengerManager{handler,{}};}});
}
MessengerBuffer* schedule(NativeMessenger& messenger,std::string category,std::int32_t index,
    std::string command,std::int32_t argument,std::uint64_t time) {
    messenger.prepare_deferred(std::move(category),"quad",index);
    auto* packet=&messenger.buffer();
    packet->set_enabled_only(1);
    packet->add_int(argument);
    messenger.schedule_deferred(std::move(command),0,time);
    return packet;
}
std::size_t active_count(const NativeMessenger& messenger) {
    const auto rows=messenger.deferred_snapshot();
    return static_cast<std::size_t>(std::count_if(rows.begin(),rows.end(),[](const auto& row){return row.active;}));
}

void missing_provider_and_four_indices() {
    ActionEventRuntime events;
    auto completed=events.begin_deferred_update(0);
    check(events.resume(completed).stop==MessengerStop::completed,"Empty prepared update did not complete.");
    auto& messenger=events.messenger();
    auto unrelated=messenger.begin_send_to("UNRELATED",std::nullopt,0,"other");
    std::array<ControllerObjectId,4> ids;
    std::array<unsigned,4> calls{};
    std::array<MessengerBuffer*,4> saved_packets{};
    std::vector<std::int32_t> order;
    for(std::size_t index=0;index<ids.size();++index) {
        ids[index]=ControllerObjectId::allocate_native();
        events.register_recipient(ids[index],"QUEUED","quad",static_cast<std::int32_t>(index));
    }
    // Original active gating precedes index filtering. The unowned third RAM
    // has known active state while only its actual manager is unavailable.
    events.set_external_services({[](ControllerObjectId)->std::optional<std::uint8_t>{return std::uint8_t{1};},{}});
    const auto provider=[&](std::size_t index) {
        install(events,ids[index],[&,index](NativeMessenger& current,const MessengerDelivery& delivery) {
            ++calls[index];order.push_back(static_cast<std::int32_t>(index));
            check(delivery.recipient==ids[index] && delivery.message.index==static_cast<std::int32_t>(index) &&
                delivery.message.command=="queued", "Deferred registration lost its actual RAM/index or command.");
            check(current.buffer().integers()==std::vector<std::int32_t>{static_cast<std::int32_t>(index)+10} &&
                current.buffer().enabled_only()==1,"Deferred delivery selected the wrong argument owner.");
            current.buffer().add_int(100+static_cast<std::int32_t>(index));
            return MessengerCallResult{true,1,{}};
        });
    };
    provider(0);provider(1);provider(3);
    for(std::int32_t index=0;index<4;++index)
        saved_packets[static_cast<std::size_t>(index)]=schedule(messenger,"QUEUED",index,"queued",index+10,100);
    auto update=events.begin_deferred_update(100);
    rejects([&]{events.messenger();},"Beginning an unfinished update left mutable access open before its first resume.");
    rejects([&]{events.resume(unrelated);},"An ordinary send entered between begin-update and first resume.");
    rejects([&]{saved_packets[0]->add_int(99);},"A saved queued packet changed before first resume.");

    // Match the numeric handle ID while retaining a different actual context.
    ActionEventRuntime alien_events;
    std::vector<MessengerDeferredUpdate> alien_history;
    for(std::uint64_t id=1;id<update.id();++id)alien_history.push_back(alien_events.begin_deferred_update(0));
    auto alien=alien_events.begin_deferred_update(0);
    check(alien.id()==update.id(),"Foreign-handle fixture did not match the local identifier.");
    const auto first=events.resume(update);
    check(first.stop==MessengerStop::service_required && first.recipients_completed==2 && first.recipient==ids[2] &&
        update.messages_completed()==2 && !update.completed() && calls==std::array<unsigned,4>{1,1,0,0},
        "Missing third provider did not retain the ordered completed prefix.");
    const NativeMessenger& observed=messenger;
    const auto pending_packet=observed.buffer_owner();
    check(pending_packet->integers()==std::vector<std::int32_t>{12} && active_count(observed)==2,
        "Provider resolution pause changed the current packet or active queue.");
    rejects([&]{events.messenger();},"Mutable wrapper access bypassed a pending deferred update.");
    rejects([&]{events.begin_deferred_update(9999);},"A later clock replaced the retained deferred update.");
    rejects([&]{events.resume(unrelated);},"An unrelated ordinary send bypassed deferred ownership.");
    rejects([&]{events.resume(completed);},"An older completed update released another update's ownership.");
    rejects([&]{events.resume(alien);},"A foreign same-ID update bypassed the actual context check.");
    rejects([&]{saved_packets[2]->add_int(99);},"Saved current packet reference bypassed a deferred pause.");
    rejects([&]{saved_packets[3]->clear();},"Saved future packet reference bypassed a deferred pause.");
    rejects([&]{saved_packets[0]->set_enabled_only(0);},"Saved recycled packet reference bypassed a deferred pause.");
    check(calls==std::array<unsigned,4>{1,1,0,0} && pending_packet==observed.buffer_owner() &&
        pending_packet->integers()==std::vector<std::int32_t>{12},"Caught ownership rejection changed the pending delivery.");
    provider(2); // Provider registration is permitted while the existing row waits.
    const auto result=events.resume(update);
    check(result.stop==MessengerStop::completed && result.recipients_completed==4 && update.completed() &&
        update.messages_completed()==4 && order==std::vector<std::int32_t>{0,1,2,3},
        "Supplying a missing provider replayed recipients or changed queue order.");
    check(calls==std::array<unsigned,4>{1,1,1,1} && active_count(observed)==0 && observed.buffer().integers().empty(),
        "Completed update retained live messages or omitted its final buffer clear.");
    check(events.resume(update).stop==MessengerStop::completed && calls==std::array<unsigned,4>{1,1,1,1},
        "Retrying a completed deferred update replayed an actual provider.");
    check(events.resume(unrelated).stop==MessengerStop::completed,"Completed queue did not release ordinary sending.");
}

void nested_send_continuation() {
    ActionEventRuntime events;
    auto& messenger=events.messenger();
    const auto parent=ControllerObjectId::allocate_native(),child=ControllerObjectId::allocate_native();
    events.register_recipient(parent,"PARENT","quad",0);
    events.register_recipient(child,"CHILD","leaf",3);
    std::optional<MessengerDispatch> nested;
    std::optional<MessengerDeferredUpdate> update;
    unsigned prefixes{},parent_calls{},child_calls{},finishes{};
    std::shared_ptr<const MessengerBuffer> child_packet;
    install(events,parent,[&](NativeMessenger& current,const MessengerDelivery& delivery) {
        ++parent_calls;
        check(&events.messenger()==&current && delivery.recipient==parent,
            "Actual queued callback did not receive nested wrapper access.");
        rejects([&]{events.begin_deferred_update(999);},"A callback recursively started a manager update.");
        rejects([&]{events.resume(*update);},"A callback recursively resumed its own manager update.");
        if(!nested) {
            ++prefixes;
            check(current.buffer().integers()==std::vector<std::int32_t>{42},"Outer packet was lost before its callback prefix.");
            current.select("CHILD","leaf",3);current.buffer().add_int(901);
            child_packet=current.buffer_owner();
            nested.emplace(current.begin_send("nested"));
        }
        const auto result=events.resume(*nested);
        if(result.stop!=MessengerStop::completed)return MessengerCallResult{false,0,result.diagnostic};
        ++finishes;
        check(current.buffer_owner()==child_packet && current.buffer().integers()==std::vector<std::int32_t>{901,902},
            "Nested completion restored a private packet or lost the live result.");
        return MessengerCallResult{true,1,{}};
    });
    schedule(messenger,"PARENT",0,"outer",42,100);
    update.emplace(events.begin_deferred_update(100));
    const auto blocked=events.resume(*update);
    check(blocked.stop==MessengerStop::service_required && prefixes==1 && parent_calls==1 && !finishes && !child_calls &&
        update->messages_completed()==0,"Nested missing provider did not retain its caller's exact prefix.");
    rejects([&]{events.resume(*nested);},"Top-level access resumed a child outside its deferred owner.");
    install(events,child,[&](NativeMessenger& current,const MessengerDelivery& delivery) {
        ++child_calls;
        check(delivery.recipient==child && delivery.message.index==3 && delivery.message.command=="nested" &&
            current.buffer_owner()==child_packet,"Deferred child resumed with a different endpoint or packet.");
        current.buffer().add_int(902);
        return MessengerCallResult{true,1,{}};
    });
    check(events.resume(*update).stop==MessengerStop::completed && prefixes==1 && parent_calls==2 && child_calls==1 && finishes==1 &&
        update->messages_completed()==1,"Nested send replayed its parent prefix or failed to complete once.");
    check(events.resume(*update).stop==MessengerStop::completed && parent_calls==2 && child_calls==1 && finishes==1,
        "Completed nested deferred work was invoked again.");
}

void frozen_clock_and_initial_budget() {
    ActionEventRuntime events;
    auto& messenger=events.messenger();
    const auto seed=ControllerObjectId::allocate_native();
    std::array<ControllerObjectId,4> leaves;
    std::vector<std::int32_t> delivered;
    for(std::size_t index=0;index<leaves.size();++index) {
        leaves[index]=ControllerObjectId::allocate_native();
        events.register_recipient(leaves[index],"LEAF","quad",static_cast<std::int32_t>(index));
        install(events,leaves[index],[&,index](NativeMessenger& current,const MessengerDelivery& delivery) {
            check(delivery.recipient==leaves[index] && current.buffer().integers().size()==1,
                "Expanded queue changed the actual leaf or typed packet.");
            const auto number=current.buffer().integers().front();
            check(number%4==static_cast<std::int32_t>(index),"Expanded queue changed its registered index selection.");
            delivered.push_back(number);return MessengerCallResult{true,1,{}};
        });
    }
    events.register_recipient(seed,"SEED","quad",0);
    bool scheduled=false,ready=false;
    unsigned prefixes{},seed_calls{};
    install(events,seed,[&](NativeMessenger& current,const MessengerDelivery&) {
        ++seed_calls;
        if(!scheduled) {
            scheduled=true;++prefixes;
            // Preparation initially owns 32 records. Growing while the seed
            // remains the active head must not enlarge this invocation's budget.
            for(std::int32_t value=0;value<33;++value)schedule(current,"LEAF",value%4,"leaf",value,100);
            schedule(current,"LEAF",0,"future",100,101);
        }
        return MessengerCallResult{ready,ready?1u:0u,"seed service is pending"};
    });
    schedule(messenger,"SEED",0,"seed",0,100);
    check(messenger.deferred_snapshot().size()==32,"Prepared queue fixture does not have the original 32-record budget.");
    auto update=events.begin_deferred_update(100);
    check(events.resume(update).stop==MessengerStop::service_required && prefixes==1 && seed_calls==1 &&
        active_count(messenger)==35 && delivered.empty(),"Callback did not retain its grown queue before suspension.");
    rejects([&]{events.begin_deferred_update(10000);},"Later external time replaced the update's captured clock and count.");
    ready=true;
    const auto result=events.resume(update);
    check(result.stop==MessengerStop::completed && update.messages_completed()==32 && result.recipients_completed==32 &&
        prefixes==1 && seed_calls==2 && delivered.size()==31 && active_count(messenger)==3,
        "Retry refreshed the initial budget or replayed the seed's scheduling prefix.");
    for(std::size_t index=0;index<delivered.size();++index)
        check(delivered[index]==static_cast<std::int32_t>(index),"Expanded equal-time queue lost FIFO order.");
    auto same_time=events.begin_deferred_update(100);
    check(events.resume(same_time).stop==MessengerStop::completed && same_time.messages_completed()==2 &&
        delivered.size()==33 && delivered.back()==32 && active_count(messenger)==1,
        "A fresh invocation at the same clock delivered a future record or missed the remaining due records.");
    auto later=events.begin_deferred_update(101);
    check(events.resume(later).stop==MessengerStop::completed && later.messages_completed()==1 &&
        delivered.size()==34 && delivered.back()==100 && active_count(messenger)==0,
        "Only a later explicit update should deliver the future record.");
}

void faulted_update_does_not_replay() {
    ActionEventRuntime events;
    auto& messenger=events.messenger();
    const auto good=ControllerObjectId::allocate_native(),bad=ControllerObjectId::allocate_native();
    events.register_recipient(good,"FAULT","quad",0);events.register_recipient(bad,"FAULT","quad",1);
    unsigned good_calls{},bad_effects{};
    install(events,good,[&](NativeMessenger&,const MessengerDelivery&){++good_calls;return MessengerCallResult{true,1,{}};});
    install(events,bad,[&](NativeMessenger& current,const MessengerDelivery&)->MessengerCallResult {
        ++bad_effects;current.buffer().add_int(77);throw std::runtime_error("uncertain queued effect");
    });
    schedule(messenger,"FAULT",0,"good",0,10);schedule(messenger,"FAULT",1,"bad",1,10);
    auto update=events.begin_deferred_update(10);
    check(events.resume(update).stop==MessengerStop::faulted && !update.completed() && update.messages_completed()==1 &&
        good_calls==1 && bad_effects==1,"Uncertain queued callback completed or lost its committed earlier message.");
    const NativeMessenger& observed=messenger;
    check(observed.buffer().integers()==std::vector<std::int32_t>{1,77},"Fault discarded an uncertain callback's live packet.");
    rejects([&]{events.messenger();},"Faulted deferred invocation released mutable wrapper access.");
    rejects([&]{events.begin_deferred_update(20);},"Faulted invocation was replaced by a fresh manager update.");
    check(events.resume(update).stop==MessengerStop::faulted && good_calls==1 && bad_effects==1 &&
        observed.buffer().integers()==std::vector<std::int32_t>{1,77},"Faulted deferred retry replayed uncertain effects.");
}
} // namespace

int main() {
    try {
        missing_provider_and_four_indices();nested_send_continuation();frozen_clock_and_initial_budget();faulted_update_does_not_replay();
        std::cout<<"Deferred messenger runtime checks: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Deferred messenger runtime after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
