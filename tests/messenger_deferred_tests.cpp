#include "dw4/messenger.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) { ++checks; if(!value) throw std::runtime_error(reason); }
template<class F> void rejects(F&& call) {
    bool rejected=false;
    try { call(); } catch(const MessengerError&) { rejected=true; }
    check(rejected,"Unsafe deferred operation was accepted");
}
std::size_t active(const NativeMessenger& messenger) {
    const auto rows=messenger.deferred_snapshot();
    return std::count_if(rows.begin(),rows.end(),[](const auto& row){return row.active;});
}
MessengerDeferredUpdate tick(NativeMessenger& messenger,std::uint64_t time,const MessengerServices& services={}) {
    auto update=messenger.begin_deferred_update(time);
    check(messenger.resume(update,services).stop==MessengerStop::completed,"Deferred update did not complete");
    return update;
}
void preparation_and_packets() {
    NativeMessenger messenger;
    check(tick(messenger,7).messages_completed()==0 && messenger.deferred_snapshot().empty(),"Uninitialized tick allocated a queue");
    rejects([&]{messenger.buffer();}); rejects([&]{messenger.schedule_deferred("GO",0,0);});
    messenger.set_deferred_pause(255,42);
    messenger.select("SELECTED","old",-2);
    check(messenger.deferred_snapshot().size()==32 && messenger.deferred_pause_anchor()==42,"Preparation lost pool size or preexisting pause anchor");
    messenger.set_deferred_pause(0x100,90);
    auto old=messenger.buffer_owner();
    messenger.buffer().add_int(99);
    messenger.prepare_deferred("OLD",{},1);
    check(old==messenger.buffer_owner() && old->integers().empty() && messenger.selected().category=="SELECTED","Deferred preparation selected recipients or replaced CURRENT");
    messenger.buffer().add_int(88);
    messenger.prepare_deferred("NEW","",3);
    check(active(messenger)==0 && messenger.deferred_snapshot().size()==32 && old->integers().empty(),"Repeated preparation activated or grew an inactive tail");
    auto tail=messenger.deferred_snapshot().back().arguments;
    messenger.select("INTERVENING",{},7);
    messenger.buffer().set_enabled_only(255); messenger.buffer().add_bool(128);
    messenger.buffer().add_int(-3); messenger.buffer().add_float(-0.0f);
    messenger.buffer().add_int64(std::numeric_limits<std::int64_t>::min());
    messenger.buffer().add_vector({1,2,3}); messenger.buffer().add_matrix({});
    messenger.buffer().add_string({}); messenger.buffer().add_string("");
    auto box=std::make_shared<const CollisionBox>(); const auto weak=std::weak_ptr(box);
    messenger.buffer().add_box(box); box.reset();
    messenger.schedule_deferred("GENERATE",1,100);
    auto rows=messenger.deferred_snapshot();
    check(messenger.buffer_owner()==tail && rows.front().arguments==old && !weak.expired(),"Scheduling copied packet fields instead of swapping owners");
    check(rows.front().deadline==16666766 && rows.front().message.category=="NEW" && rows.front().message.name==""
        && rows.front().message.index==3 && rows.front().message.command=="GENERATE","Commit lost last prepared keys or exact integer delay");
    check(messenger.selected().category=="INTERVENING" && active(messenger)==1,"Commit changed ordinary selection");
    check(old->enabled_only()==255 && old->booleans()[0]==128 && old->integers()[0]==-3
        && std::bit_cast<std::uint32_t>(old->floats()[0])==0x80000000u
        && old->integers64()[0]==std::numeric_limits<std::int64_t>::min() && old->vectors()[0][2]==3
        && old->matrices().size()==1 && old->strings().size()==2 && old->objects().size()==1,"Queued typed payload changed");
    messenger.buffer().add_int(11);
    check(tick(messenger,16666765).messages_completed()==0 && messenger.buffer_owner()==tail && tail->integers().empty(),"Future tick cleared the queued packet or skipped CURRENT clear");
    check(tick(messenger,16666766).messages_completed()==1 && messenger.buffer_owner()==old
        && old->objects().empty() && weak.expired() && messenger.selected().category=="NEW","Due delivery restored a previous packet or failed to clear its live result");
    check(active(messenger)==0 && messenger.deferred_snapshot().back().arguments==tail,"Recycled record lost its replacement packet");
}
void signed_order_and_growth() {
    const std::array<std::uint64_t,8> deadlines={0,1,0x7fffffffffffffffull,0x8000000000000000ull,
        0xffffffffffffffffull,0xffffffffull,0x100000000ull,1};
    NativeMessenger messenger;
    for(std::size_t i=0;i<deadlines.size();++i)messenger.defer_to("Q",{},static_cast<int>(i),std::to_string(i),0,deadlines[i]);
    std::array<std::size_t,8> order{3,4,0,1,7,5,6,2};
    const auto rows=messenger.deferred_snapshot();
    for(std::size_t i=0;i<order.size();++i)check(rows[i].message.index==static_cast<int>(order[i]),"Signed deadline ordering or equal-time FIFO changed");
    check(tick(messenger,0x8000000000000000ull).messages_completed()==1,"Signed minimum equality was not ready");
    check(tick(messenger,0).messages_completed()==2,"Signed negative deadline did not precede zero");
    check(tick(messenger,0x7fffffffffffffffull).messages_completed()==5,"Signed maximum did not drain remaining messages");
    messenger.defer_to("WRAP",{},0,"GO",std::numeric_limits<std::uint64_t>::max(),5);
    check(messenger.deferred_snapshot()[0].deadline==std::uint64_t{5}-16666666,"Delay multiplication did not wrap modulo64");
    tick(messenger,0x7fffffffffffffffull);
    for(int i=0;i<40;++i)messenger.defer_to("POOL",{},i,"GO",0,9);
    check(messenger.deferred_snapshot().size()==40 && active(messenger)==40,"Deferred pool imposed a fixed32-message capacity");
    messenger.prepare_deferred("ABANDONED",{},0);
    messenger.prepare_deferred("REPLACED",{},1);
    check(messenger.deferred_snapshot().size()==41 && active(messenger)==40,"Repeated abandoned preparation grew more than once");
    check(tick(messenger,9).messages_completed()==40 && messenger.deferred_snapshot().size()==41,"Recycling changed pool size or missed live head messages");
    rejects([&]{messenger.schedule_deferred("INVALID_TAIL",0,9);});
    check(active(messenger)==0,"Commit incorrectly reserved an earlier prepared tail across a drain");
}
void pause_arithmetic() {
    for(const auto raw:std::array<std::uint32_t,6>{0,1,2,255,256,257}) {
        NativeMessenger messenger;
        messenger.defer_to("Q",{},0,"GO",0,100);
        messenger.set_deferred_pause(raw,40);
        auto update=tick(messenger,80);
        const bool paused=(raw&255)!=0;
        check(update.messages_completed()==0 && messenger.deferred_pause_anchor()==(paused?80u:0u)
            && messenger.deferred_snapshot()[0].deadline==(paused?140u:100u),"Pause low-byte handling or deadline shift changed");
    }
    NativeMessenger messenger;
    messenger.defer_to("Q",{},0,"GO",0,100);
    messenger.set_deferred_pause(1,90);
    tick(messenger,20);
    check(messenger.deferred_snapshot()[0].deadline==30 && messenger.deferred_pause_anchor()==20,"Backward paused clock did not wrap its subtraction");
    tick(messenger,0);
    check(messenger.deferred_snapshot()[0].deadline==10 && messenger.deferred_pause_anchor()==0,"Zero paused update did not restore the original sentinel");
    messenger.set_deferred_pause(1,0);
    check(tick(messenger,10).messages_completed()==1,"Pause at zero incorrectly disabled ordinary delivery");
}
void live_callbacks_and_budget() {
    NativeMessenger messenger;
    auto id=ControllerObjectId::allocate_native();
    messenger.register_recipient(id,"Q",{},-1);
    unsigned calls{};
    MessengerServices services;
    services.manager=[&](ControllerObjectId) { return MessengerManager{[&](NativeMessenger& owner,const MessengerDelivery&) {
        ++calls; owner.defer_to("Q",{},0,"AGAIN",0,7);
        return MessengerCallResult{true,1,{}};
    },{}}; };
    messenger.defer_to("Q",{},0,"FIRST",0,7);
    auto update=tick(messenger,7,services);
    check(calls==32 && update.messages_completed()==32 && active(messenger)==1,"Live callback enqueue did not use the initial total pool count as its budget");
    messenger.reset(); messenger.register_recipient(id,"Q",{},-1); calls=0;
    for(int i=0;i<32;++i)messenger.defer_to("Q",{},i,"ORIGINAL",0,7);
    services.manager=[&](ControllerObjectId) { return MessengerManager{[&](NativeMessenger& owner,const MessengerDelivery&) {
        if(++calls==1) { owner.defer_to("Q",{},33,"NEW",0,7); owner.set_deferred_pause(1,7); }
        return MessengerCallResult{true,1,{}};
    },{}}; };
    update=tick(messenger,7,services);
    check(calls==32 && update.messages_completed()==32 && active(messenger)==1 && messenger.deferred_snapshot().size()==33
        && messenger.deferred_pause_anchor()==7,"Pool growth or callback pause changed the already captured update branch/budget");
    messenger.reset(); messenger.register_recipient(id,"Q",{},-1); calls=0;
    services.manager=[&](ControllerObjectId) { return MessengerManager{[&](NativeMessenger& owner,const MessengerDelivery&) {
        ++calls; rejects([&]{owner.begin_deferred_update(7);});
        owner.prepare_deferred("Q",{},0); owner.buffer().add_int(15); auto packet=owner.buffer_owner();
        rejects([&]{owner.schedule_deferred("BEFORE",0,6);});
        check(packet==owner.buffer_owner() && packet->integers()[0]==15 && active(owner)==1,"Unsafe insertion committed packet or queue changes");
        owner.schedule_deferred("FUTURE",0,8);
        return MessengerCallResult{true,1,{}};
    },{}}; };
    messenger.defer_to("Q",{},0,"FIRST",0,7);
    check(tick(messenger,7,services).messages_completed()==1 && calls==1 && active(messenger)==1,"Callback live-clock enqueue bypassed captured readiness time");
}
void continuation_and_cleanup() {
    NativeMessenger messenger;
    auto id=ControllerObjectId::allocate_native(); messenger.register_recipient(id,"Q",{},0);
    messenger.prepare_deferred("Q",{},0); messenger.buffer().add_int(17);
    auto& mutable_packet=messenger.buffer();
    auto retained=messenger.buffer_owner(); messenger.schedule_deferred("GO",0,9);
    auto update=messenger.begin_deferred_update(9);
    rejects([&]{mutable_packet.add_int(101);}); rejects([&]{mutable_packet.clear();});
    check(messenger.resume(update,{}).stop==MessengerStop::service_required && retained->integers()[0]==17
        && active(messenger)==1 && update.messages_completed()==0,"Missing manager recycled or cleared the delivered packet");
    rejects([&]{messenger.select("UNRELATED",{},0);}); rejects([&]{messenger.buffer();});
    rejects([&]{mutable_packet.add_bool(1);}); rejects([&]{mutable_packet.set_enabled_only(1);});
    rejects([&]{messenger.begin_deferred_update(100);}); rejects([&]{messenger.set_deferred_pause(1,100);});
    bool ready=false,started=false; unsigned calls{};
    MessengerServices services;
    services.manager=[&](ControllerObjectId) { return MessengerManager{[&](NativeMessenger& owner,const MessengerDelivery&) {
        ++calls;
        if(!started) { owner.buffer().add_int(18); started=true; }
        if(!ready)return MessengerCallResult{false,1,"Prepared callback pending"};
        check(owner.buffer().integers()==std::vector<std::int32_t>{17,18},"Suspended callback lost its committed packet prefix");
        return MessengerCallResult{true,1,{}};
    },{}}; };
    check(messenger.resume(update,services).stop==MessengerStop::service_required && calls==1,"Prepared callback did not pause");
    ready=true;
    check(messenger.resume(update,services).stop==MessengerStop::completed && calls==2 && update.messages_completed()==1
        && retained->integers().empty(),"Resuming callback failed to finish exactly once");
    messenger.resume(update,services); check(calls==2,"Completed deferred update replayed callbacks");
    messenger.defer_to("Q",{},0,"FAIL",0,10); auto failed=messenger.begin_deferred_update(10);
    services.manager=[&](ControllerObjectId) { return MessengerManager{[&](NativeMessenger& owner,const MessengerDelivery&)->MessengerCallResult {
        ++calls; owner.buffer().add_int(91); throw std::runtime_error("Uncertain provider completion");
    },{}}; };
    check(messenger.resume(failed,services).stop==MessengerStop::faulted,"Throwing deferred callback did not fault");
    auto final_packet=messenger.buffer_owner();
    messenger.resume(failed,services);
    check(calls==3 && final_packet->integers()[0]==91 && active(messenger)==1,"Fault replayed or cleared uncertain effects");
    const auto weak=std::weak_ptr(final_packet);
    messenger.reset();
    check(!weak.expired() && final_packet->integers()[0]==91 && messenger.deferred_snapshot().empty(),"Cleanup mutated a separately retained original packet owner");
    check(messenger.resume(failed,services).stop==MessengerStop::faulted,"Cleanup did not invalidate a deferred handle");
    // Ordinary record reuse can install the very first retained packet again.
    retained.reset(); final_packet.reset();
    check(weak.expired(),"Reset retained a detached current packet");
    messenger.select("FRESH",{},0);
    check(messenger.buffer().integers().empty() && messenger.deferred_snapshot().size()==32,"Cleanup failed to lazily reprepare fresh owners");

    auto& released=messenger.buffer();
    unsigned blocked{};
    messenger.buffer().add_box(MessengerBox(new CollisionBox,[&](const CollisionBox* value) {
        try { released.add_int(1); } catch(const MessengerError&) { ++blocked; }
        try { released.clear(); } catch(const MessengerError&) { ++blocked; }
        try { messenger.select("REENTER",{},0); } catch(const MessengerError&) { ++blocked; }
        delete value;
    }));
    messenger.reset();
    check(blocked==3,"Packet destruction let a retained reference mutate members during release");
}
void faulted_broadcast_progress() {
    NativeMessenger messenger;
    std::array<ControllerObjectId,4> ids;
    std::array<unsigned,4> calls{};
    for(auto& id:ids) { id=ControllerObjectId::allocate_native(); messenger.register_recipient(id,"Q",{},-1); }
    MessengerServices services;
    services.manager=[&](ControllerObjectId id) {
        const auto index=static_cast<std::size_t>(std::find(ids.begin(),ids.end(),id)-ids.begin());
        return MessengerManager{[&,index](NativeMessenger&,const MessengerDelivery&)->MessengerCallResult {
            ++calls[index];
            if(index==2) throw std::runtime_error("Third recipient failed");
            return {true,1,{}};
        },{}};
    };
    messenger.defer_to("Q",{},-1,"BROADCAST",0,5);
    auto update=messenger.begin_deferred_update(5);
    const auto first=messenger.resume(update,services);
    check(first.stop==MessengerStop::faulted && first.recipients_completed==2 && first.recipient==ids[2],
        "Faulted broadcast lost its completed recipient prefix");
    const auto again=messenger.resume(update,services);
    check(again.stop==first.stop && again.recipients_completed==first.recipients_completed && again.recipient==first.recipient
        && again.diagnostic==first.diagnostic && calls==std::array<unsigned,4>{1,1,1,0} && update.messages_completed()==0,
        "Faulted retry changed progress reporting or replayed broadcast callbacks");
}
}
int main() {
    try {
        preparation_and_packets(); signed_order_and_growth(); pause_arithmetic(); live_callbacks_and_budget(); continuation_and_cleanup();
        faulted_broadcast_progress();
        std::cout<<"Deferred messenger tests passed: "<<checks<<" checks\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
