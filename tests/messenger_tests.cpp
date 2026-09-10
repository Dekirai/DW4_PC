#include "dw4/messenger.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <map>

namespace {
std::size_t checks{};
void check(bool condition,const char* message) { ++checks; if(!condition) throw std::runtime_error(message); }
template<class F> void rejects(F&& function) {
    ++checks; try { function(); } catch(const dw4::MessengerError&) { return; }
    throw std::runtime_error("Invalid messenger operation was accepted");
}
using Id=dw4::ControllerObjectId;
using dw4::MessengerObject;
constexpr Id a{0,7},b{1,7},c{0,8};
constexpr auto done=dw4::MessengerStop::completed;
struct Fixture {
    struct Ram { std::uint8_t active{}; unsigned structured{},fallback{},effect{}; std::uint32_t reply=1; };
    dw4::NativeMessenger messenger;
    std::map<std::pair<std::uint32_t,std::uint32_t>,Ram> rams;
    std::vector<Id> effects;
    dw4::MessengerServices services;
    Ram& ram(Id id) { return rams[{id.file_index,id.reference_index}]; }
    Fixture() {
        services.active=[this](Id id){return ram(id).active;};
        services.manager=[this](Id id) {
            auto* owner=&ram(id);
            dw4::MessengerManager manager;
            manager.structured=[this,owner](dw4::NativeMessenger&,const dw4::MessengerDelivery& delivery) {
                ++owner->structured;
                if(owner->reply&255) { ++owner->effect; effects.push_back(delivery.recipient); }
                return dw4::MessengerCallResult{true,owner->reply,{}};
            };
            manager.fallback=[this,owner](dw4::NativeMessenger&,const dw4::MessengerDelivery& delivery,std::string_view) {
                ++owner->fallback; ++owner->effect; effects.push_back(delivery.recipient);
                return dw4::MessengerCallResult{true,0,{}};
            };
            return manager;
        };
    }
    void add(Id id,std::optional<std::string> name={},std::int32_t index=-1,std::string category="ACTION") {
        messenger.register_recipient(id,std::move(category),std::move(name),index);
    }
    void send(std::string category="ACTION",std::optional<std::string> name={},std::int32_t index=-1,std::string command="GO") {
        auto handle=messenger.begin_send_to(std::move(category),std::move(name),index,std::move(command));
        check(messenger.resume(handle,services).stop==done && handle.completed(),"Ordinary messenger delivery did not complete");
    }
};
void matching() {
    Fixture f;
    f.add(a,"Hero",0); f.add(b,"Hero",1); f.add(a,"Hero",0); f.add(c,{},-1);
    f.add({2,9},"hero",0); f.add({2,10},"",0); f.add({2,11},{},-1,"action");
    f.send("ACTION","Hero",0);
    check(f.effects==std::vector<Id>{a,a,c},"Exact name/index matching lost duplicate rows or nullable wildcard");
    f.effects.clear(); f.send("ACTION","Hero",1);
    check(f.effects==std::vector<Id>{b,c},"Cache key incorrectly included index or excluded cross-file recipient");
    f.effects.clear(); f.send("ACTION","Hero",-1);
    check(f.effects==std::vector<Id>{a,b,a,c},"Requested index wildcard changed registration order");
    f.effects.clear(); f.send("ACTION","",0);
    check(f.effects==std::vector<Id>{c,{2,10}},"Empty name was treated as a wildcard");
    f.effects.clear(); f.send("ACTION",{},0);
    check(f.effects==std::vector<Id>{a,a,c,{2,9},{2,10}},"Null requested name did not match all registered names");
    f.effects.clear(); f.send("action",{},-1);
    check(f.effects==std::vector<Id>{{2,11}},"Category matching ignored case");
    f.effects.clear(); f.send("missing"); check(f.effects.empty(),"Missing category invoked a recipient");
    f.ram(a).reply=0x100; f.ram(c).reply=0x101;
    const auto a_hooks=f.ram(a).structured,c_fallbacks=f.ram(c).fallback;
    f.send("ACTION","Hero",0);
    check(f.ram(a).structured==a_hooks+2 && f.ram(a).fallback==2 && f.ram(c).fallback==c_fallbacks,
        "Hook/fallback selection did not use exactly the low return byte");
}
void activity_and_queries() {
    Fixture f; f.add(a,{},5); f.add(b,{},6); f.add(c,{},-1);
    f.ram(a).active=255; f.ram(b).active=0; f.ram(c).active=1;
    f.messenger.select("ACTION",{},-1); f.messenger.buffer().set_enabled_only(1);
    auto enabled=f.messenger.begin_send("GO");
    check(f.messenger.resume(enabled,f.services).stop==done && f.effects==std::vector<Id>{a,c},"Enabled-only gate did not read current active bytes");
    f.effects.clear(); f.messenger.select("ACTION",{},-1); f.messenger.buffer().set_enabled_only(2);
    auto any=f.messenger.begin_send("GO"); f.messenger.resume(any,f.services);
    check(f.effects==std::vector<Id>{a,b,c},"Enabled-only values other than one incorrectly gated recipients");
    f.effects.clear(); f.messenger.select("ACTION",{},-2); f.messenger.buffer().set_enabled_only(1);
    auto inactive=f.messenger.begin_send("GO"); f.messenger.resume(inactive,f.services);
    check(f.effects==std::vector<Id>{b},"Index minus two did not select the first inactive RAM independently of its index/gate");
    f.messenger.select("ACTION",{},6); f.messenger.buffer().set_enabled_only(1); f.messenger.buffer().add_int(99);
    auto search=f.messenger.begin_send("SEARCH");
    check(f.messenger.resume(search,{}).stop==done,"SEARCH required recipient callbacks");
    check(f.messenger.buffer().integers()==std::vector<std::int32_t>{99,2}
        && f.messenger.buffer().booleans()==std::vector<std::uint8_t>{1},"SEARCH did not append wildcard-inclusive count independently of activity");
    f.send("ACTION",{},-2,"SEARCH");
    check(f.messenger.buffer().integers()==std::vector<std::int32_t>{1},"SEARCH incorrectly interpreted index minus two as inactive selection");
    f.send("missing",{},-1,"SEARCH");
    check(f.messenger.buffer().integers()==std::vector<std::int32_t>{0}
        && f.messenger.buffer().booleans()==std::vector<std::uint8_t>{0},"SEARCH miss result was not false/zero");
    f.messenger.select("ACTION",{},123); f.messenger.buffer().set_enabled_only(1);
    auto space=f.messenger.begin_send("FIND_SPACE"); f.messenger.resume(space,f.services);
    check(f.messenger.buffer().objects()==std::vector<MessengerObject>{b},"FIND_SPACE used index/gate instead of the first inactive row");
    f.ram(b).active=1; f.send("ACTION",{},-1,"FIND_SPACE");
    check(f.messenger.buffer().objects().empty(),"FIND_SPACE miss appended a null result");
    dw4::MessengerServices unavailable=f.services;
    unavailable.active=[](Id)->std::optional<std::uint8_t>{return std::nullopt;};
    f.messenger.select("ACTION",{},-1);f.messenger.buffer().set_enabled_only(1);
    auto waiting=f.messenger.begin_send("GO");f.effects.clear();
    check(f.messenger.resume(waiting,unavailable).stop==dw4::MessengerStop::service_required && f.effects.empty(),
        "Unavailable activity was treated as inactive or permanently faulted");
    check(f.messenger.resume(waiting,f.services).stop==done && f.effects==std::vector<Id>{a,b,c},
        "Installing the live activity service could not resume selection");
    auto finding=f.messenger.begin_send_to("ACTION",{},0,"FIND_SPACE");
    check(f.messenger.resume(finding,unavailable).stop==dw4::MessengerStop::service_required,"FIND_SPACE ignored missing live activity");
    f.ram(b).active=0;check(f.messenger.resume(finding,f.services).stop==done
        && f.messenger.buffer().objects()==std::vector<MessengerObject>{b},"FIND_SPACE did not resume its original live row scan");
}
void cache() {
    Fixture f; f.add(a,"Hero",0); f.add(b,"Hero",1); f.send("ACTION","Hero",0);
    f.add(c,"Hero",1); f.effects.clear(); f.send("ACTION","Hero",1);
    check(f.effects==std::vector<Id>{b},"Registration invalidated an existing nonempty cache snapshot");
    f.send("LATE","",-1); f.add(c,"",-1,"LATE"); f.effects.clear(); f.send("LATE","",-1);
    check(f.effects==std::vector<Id>{c},"An empty cache record did not recollect later registrations");
    f.messenger.select("ACTION","Hero",0);
    check(f.messenger.selected().command=="GO","Selection unexpectedly cleared a captured command");
    for(int i=0;i<62;++i) f.messenger.select("MISS"+std::to_string(i),{},0);
    f.effects.clear(); f.send("ACTION","Hero",1);
    check(f.effects==std::vector<Id>{b},"The 64-entry cache evicted a still-resident key");
    f.messenger.select("ONE_MORE",{},0); f.effects.clear(); f.send("ACTION","Hero",1);
    check(f.effects==std::vector<Id>{b},"Cache hit did not move its record to the front");
    for(int i=0;i<64;++i) f.messenger.select("EVICT"+std::to_string(i),{},0);
    f.effects.clear(); f.send("ACTION","Hero",1);
    check(f.effects==std::vector<Id>{b,c},"Evicted cache rows did not recollect current registration occurrences");
    std::string category="OWNED",name="name";
    f.messenger.register_recipient(a,category,name,-1); category.clear();name.clear();
    f.effects.clear();f.send("OWNED","name");
    check(f.effects==std::vector<Id>{a},"Registration retained borrowed category/name storage");
}
void nested() {
    Fixture f; f.add(a); f.add(b); f.add(c,{},-1,"INNER");
    f.ram(a).active=1; f.ram(b).active=0;
    auto ordinary=f.services.manager;
    f.services.manager=[&](Id id) {
        auto manager=ordinary(id);
        if(id==a) manager.structured=[&](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery& delivery) {
            check(delivery.message.category=="ACTION" && delivery.message.command=="OUTER","Outer structured message was not live");
            auto inner=messenger.begin_send_to("INNER",{},-1,"NESTED");
            check(messenger.buffer().integers().empty(),"Nested selection did not clear the shared argument buffer");
            messenger.buffer().add_int(41); messenger.buffer().set_enabled_only(1);
            check(messenger.resume(inner,f.services).stop==done,"Nested delivery did not complete");
            check(delivery.message.category=="ACTION","Different cached selection replaced the outer message object");
            ++f.ram(a).effect;return dw4::MessengerCallResult{true,1,{}};
        };
        return manager;
    };
    f.ram(c).active=1;f.messenger.select("ACTION",{},-1);f.messenger.buffer().add_int(7);
    auto outer=f.messenger.begin_send("OUTER"); f.messenger.resume(outer,f.services);
    check(f.ram(a).effect==1 && f.ram(b).effect==0 && f.ram(c).effect==1,"Nested enabled-only mutation was restored before a later outer recipient");
    check(f.messenger.buffer().integers()==std::vector<std::int32_t>{41},"Nested argument/result writes were restored away");

    Fixture same; same.add(a,{},0);same.add(b,{},1);same.add(c,{},2);
    bool nested_once=false;
    std::vector<std::string> commands;
    same.services.manager=[&](Id id) {
        dw4::MessengerManager manager;
        manager.structured=[&,id](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery& delivery) {
            if(id==a && !nested_once) {
                nested_once=true;auto inner=messenger.begin_send_to("ACTION",{},1,"INNER");
                check(messenger.resume(inner,same.services).stop==done,"Same-key nested dispatch failed");
                check(delivery.message.index==1 && delivery.message.command=="INNER","Nested same-key send did not mutate the outer cache record");
            }
            return dw4::MessengerCallResult{true,0,{}};
        };
        manager.fallback=[&](dw4::NativeMessenger&,const dw4::MessengerDelivery& delivery,std::string_view command) {
            commands.push_back(std::to_string(delivery.recipient.file_index)+":"+std::string(command)+":"+*delivery.message.command);
            return dw4::MessengerCallResult{true,0,{}};
        };
        return manager;
    };
    same.send("ACTION",{},-1,"OUTER");
    check(commands==std::vector<std::string>{"1:INNER:INNER","0:OUTER:INNER","1:OUTER:INNER"},
        "Outer sender snapshotted its live index/command or lost its original fallback command");
    Fixture stop;stop.add(a);stop.add(b);
    stop.services.manager=[&](Id){return dw4::MessengerManager{[&](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&) {
        messenger.select("ACTION",{},-2);++stop.ram(a).effect;return dw4::MessengerCallResult{true,1,{}};
    },{}};};
    stop.send();check(stop.ram(a).effect==1,"Post-callback index minus two did not stop the outer loop");
}
void continuation() {
    Fixture f; f.add(a);f.add(b);f.add(c);
    bool ready=false;unsigned a_calls=0,b_calls=0,c_calls=0,fallback_calls=0;
    auto ordinary=f.services.manager;
    f.services.manager=[&](Id id) {
        dw4::MessengerManager manager;
        manager.structured=[&,id](dw4::NativeMessenger&,const dw4::MessengerDelivery&) {
            if(id==a) ++a_calls;
            if(id==b) {++b_calls;return dw4::MessengerCallResult{true,0,{}};}
            if(id==c) ++c_calls;
            return dw4::MessengerCallResult{true,1,{}};
        };
        manager.fallback=[&](dw4::NativeMessenger&,const dw4::MessengerDelivery&,std::string_view) {
            ++fallback_calls;return dw4::MessengerCallResult{ready,0,"Effect service is required"};
        };
        return manager;
    };
    auto handle=f.messenger.begin_send_to("ACTION",{},-1,"GO");
    auto first=f.messenger.resume(handle,f.services);
    check(first.stop==dw4::MessengerStop::service_required && first.recipients_completed==1 && first.recipient==b,
        "Suspended delivery lost its successful prefix/current recipient");
    // Fallback must retain the parent manager captured before the completed hook.
    f.services.manager=ordinary;ready=true;
    auto second=f.messenger.resume(handle,f.services);
    check(second.stop==done && second.recipients_completed==3 && a_calls==1 && b_calls==1
        && fallback_calls==2 && c_calls==0 && f.ram(c).effect==1,"Resume replayed a completed phase or refreshed the captured manager");
    f.messenger.resume(handle,f.services);
    check(fallback_calls==2 && f.ram(c).effect==1,"Completed dispatch replayed effects");
    auto missing=f.messenger.begin_send_to("ACTION",{},-1,"GO");
    check(f.messenger.resume(missing,{}).stop==dw4::MessengerStop::service_required,"Missing manager service silently succeeded");
    dw4::MessengerServices unresolved;
    unsigned resolution_attempts=0;
    unresolved.manager=[&](Id){++resolution_attempts;return dw4::MessengerManager{};};
    check(f.messenger.resume(missing,unresolved).stop==dw4::MessengerStop::service_required && resolution_attempts==1,
        "Unavailable manager resolution was accepted as a captured parent manager");
    check(f.messenger.resume(missing,f.services).stop==done,"Installing an unavailable manager could not resume its untouched delivery");
    missing=f.messenger.begin_send_to("ACTION",{},-1,"GO");
    f.messenger.reset();
    check(f.messenger.registration_count()==0 && f.messenger.resume(missing,f.services).stop==dw4::MessengerStop::faulted,
        "Cleanup retained registration/cache state or accepted a stale handle");
    rejects([&]{f.messenger.buffer();});f.effects.clear();f.send();check(f.effects.empty(),"Cleanup/reprepare restored stale recipients");
    Fixture thrown;thrown.add(a);unsigned effects=0;
    thrown.services.manager=[&](Id){return dw4::MessengerManager{[&](dw4::NativeMessenger&,const dw4::MessengerDelivery&)->dw4::MessengerCallResult {
        ++effects;throw std::runtime_error("Uncertain callback effect");
    },{}};};
    auto failed=thrown.messenger.begin_send_to("ACTION",{},-1,"GO");
    check(thrown.messenger.resume(failed,thrown.services).stop==dw4::MessengerStop::faulted,"Throwing callback did not fault its continuation");
    thrown.messenger.resume(failed,thrown.services);check(effects==1,"Faulted delivery retried uncertain effects");
}
void nested_eviction() {
    Fixture f;f.add(a);f.add(b);f.add(a,{},-1,"REPLACEMENT");f.add(c,{},-1,"REPLACEMENT");f.add(b,{},-1,"REPLACEMENT");
    auto ordinary=f.services.manager; bool first=true;
    f.services.manager=[&](Id id) {
        if(id==a && first) return dw4::MessengerManager{[&](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery& delivery) {
            first=false;f.effects.push_back(a);
            for(int i=0;i<63;++i) messenger.select("MISS"+std::to_string(i),{},0);
            messenger.select("REPLACEMENT",{},-1);
            check(delivery.message.category=="REPLACEMENT","Retained selected record was not reused on cache eviction");
            return dw4::MessengerCallResult{true,1,{}};
        },{}};
        return ordinary(id);
    };
    f.send();check(f.effects==std::vector<Id>{a,c},"Outer sender did not retain its original count while reading live replacement rows");
    Fixture invalid;invalid.add(a);invalid.add(b);
    invalid.services.manager=[&](Id){return dw4::MessengerManager{[](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&) {
        for(int i=0;i<64;++i) messenger.select("MISS"+std::to_string(i),{},0);
        return dw4::MessengerCallResult{true,1,{}};
    },{}};};
    auto handle=invalid.messenger.begin_send_to("ACTION",{},-1,"GO");
    check(invalid.messenger.resume(handle,invalid.services).stop==dw4::MessengerStop::faulted,
        "Invalidated retained row access was silently skipped or read out of bounds");
}
void storage_and_bounds() {
    dw4::NativeMessenger messenger;
    rejects([&]{messenger.begin_send("GO");});messenger.select("",{},-1);
    auto& buffer=messenger.buffer();
    const auto tiny=std::bit_cast<float>(1u),negative_tiny=std::bit_cast<float>(0x80000001u);
    buffer.add_bool(2);buffer.add_int(-2147483647-1);buffer.add_int64(std::numeric_limits<std::int64_t>::min());
    buffer.add_float(tiny);buffer.add_vector({negative_tiny,-0.0f,1});
    std::array<float,16> matrix{};matrix[0]=tiny;matrix[1]=negative_tiny;matrix[15]=1;buffer.add_matrix(matrix);
    std::string text="owned";buffer.add_string(text);text.clear();buffer.add_string({});buffer.add_string("");
    buffer.add_object(a);buffer.add_object({});
    check(buffer.booleans()[0]==2 && buffer.integers()[0]==std::numeric_limits<std::int32_t>::min()
        && buffer.integers64()[0]==std::numeric_limits<std::int64_t>::min(),"Typed integer/byte payload lost its width");
    check(std::bit_cast<std::uint32_t>(buffer.floats()[0])==1
        && std::bit_cast<std::uint32_t>(buffer.vectors()[0][0])==0x80000001u,"Scalar/vector copy flushed finite subnormal inputs");
    check(std::bit_cast<std::uint32_t>(buffer.matrices()[0][0])==0
        && std::bit_cast<std::uint32_t>(buffer.matrices()[0][1])==0x80000000u,"PSQ matrix copy did not flush tiny words with signed zero");
    check(buffer.strings()==std::vector<std::optional<std::string>>{"owned",{},""}
        && buffer.objects()==std::vector<MessengerObject>{a,std::monostate{}},"Argument/result strings or nullable object IDs lost ownership/null distinctions");
    const auto invalid=std::numeric_limits<float>::infinity();
    rejects([&]{buffer.add_float(invalid);});rejects([&]{buffer.add_vector({0,invalid,0});});
    matrix[15]=invalid;rejects([&]{buffer.add_matrix(matrix);});
    check(buffer.floats().size()==1 && buffer.vectors().size()==1 && buffer.matrices().size()==1,"Rejected numeric payload changed typed storage");
    buffer.set_enabled_only(1);messenger.select("NEXT","",123);
    check(buffer.enabled_only()==0 && buffer.booleans().empty() && buffer.integers().empty() && buffer.floats().empty()
        && buffer.integers64().empty() && buffer.vectors().empty() && buffer.matrices().empty()
        && buffer.strings().empty() && buffer.objects().empty(),"Selection did not reset every shared typed list/gate");
    rejects([&]{messenger.select(std::string("A\0B",3),{},0);});
    rejects([&]{messenger.select("A",std::string("N\0B",3),0);});
    rejects([&]{messenger.begin_send(std::string("G\0O",3));});
    rejects([&]{messenger.register_recipient(a,"A",std::string(65536,'a'),0);});
    for(std::size_t i=0;i<32768;++i) buffer.add_bool(1);
    rejects([&]{buffer.add_int(1);});check(buffer.booleans().size()==32768,"Argument count limit changed the valid prefix");
    buffer.clear();
    for(int i=0;i<127;++i) buffer.add_string(std::string(65535,'x'));
    rejects([&]{buffer.add_string(std::string(65535,'x'));});
    check(buffer.strings().size()==127,"Argument byte limit changed the valid prefix");
    std::vector<dw4::MessengerDispatch> handles;
    for(int i=0;i<256;++i) handles.push_back(messenger.begin_send("GO"));
    rejects([&]{messenger.begin_send("GO");});handles.clear();
    auto handle=messenger.begin_send("GO");auto moved=std::move(messenger);
    check(moved.resume(handle,{}).stop==done,"Moving a messenger invalidated its owned cache/dispatch identity");
}
void registration_and_stack_bounds() {
    dw4::NativeMessenger rows;
    for(std::uint32_t i=0;i<65536;++i) rows.register_recipient({0,i},"",{},0);
    rejects([&]{rows.register_recipient(a,"",{},0);});
    check(rows.registration_count()==65536,"Registration row bound changed the committed prefix");
    dw4::NativeMessenger text;
    for(int i=0;i<128;++i) text.register_recipient(a,std::string(65535,'x'),{},0);
    rejects([&]{text.register_recipient(a,std::string(65535,'x'),{},0);});
    check(text.registration_count()==128,"Registration text bound changed the committed prefix");
    Fixture nested; nested.add(a);unsigned calls=0;
    nested.services.manager=[&](Id) {
        return dw4::MessengerManager{[&](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&) {
            ++calls;auto next=messenger.begin_send_to("ACTION",{},-1,"RECURSE");
            const auto result=messenger.resume(next,nested.services);
            return dw4::MessengerCallResult{result.stop==done,1,result.diagnostic};
        },{}};
    };
    auto root=nested.messenger.begin_send_to("ACTION",{},-1,"RECURSE");
    check(nested.messenger.resume(root,nested.services).stop==dw4::MessengerStop::service_required && calls==64,
        "Recursive delivery did not stop at its explicit host stack bound");
    auto empty=nested.messenger.begin_send_to("NONE",{},-1,"GO");
    check(nested.messenger.resume(empty,{}).stop==done,"Suspended recursion retained a stale active stack depth");
    Fixture cleanup;cleanup.add(a);
    cleanup.services.manager=[](Id) {return dw4::MessengerManager{[](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&) {
        messenger.reset();return dw4::MessengerCallResult{true,1,{}};
    },{}};};
    auto invalid=cleanup.messenger.begin_send_to("ACTION",{},-1,"GO");
    check(cleanup.messenger.resume(invalid,cleanup.services).stop==dw4::MessengerStop::faulted
        && cleanup.messenger.registration_count()==1,"Active cleanup invalidated retained guest-equivalent ownership");
}
void retained_box_objects() {
    dw4::NativeMessenger messenger;
    messenger.select("HITPLAYERBULLET",{},-1);
    auto box=std::make_shared<const dw4::CollisionBox>(dw4::CollisionBox{{1,-0.0f,3},{-1,4,5}});
    const std::weak_ptr<const dw4::CollisionBox> lifetime=box;
    auto& buffer=messenger.buffer();
    buffer.add_object(a);buffer.add_box(box);buffer.add_object({});
    check(buffer.objects().size()==3 && std::get<Id>(buffer.objects()[0])==a &&
        std::get<dw4::MessengerBox>(buffer.objects()[1])==box && std::holds_alternative<std::monostate>(buffer.objects()[2]),
        "Retained geometric and controller objects lost their shared list order or actual type");
    check(std::bit_cast<std::uint32_t>(std::get<dw4::MessengerBox>(buffer.objects()[1])->minimum[1])==0x80000000u,
        "AABox transport changed finite signed-zero coordinates");
    box.reset();check(!lifetime.expired(),"Shared message did not retain its temporary AABox owner");
    rejects([&]{buffer.add_box({});});
    auto invalid=std::make_shared<const dw4::CollisionBox>(dw4::CollisionBox{{0,0,0},{0,std::numeric_limits<float>::infinity(),0}});
    rejects([&]{buffer.add_box(invalid);});
    check(buffer.objects().size()==3,"Invalid geometric payload changed the original object list prefix");
    messenger.select("NEXT",{},-1);
    check(lifetime.expired() && buffer.objects().empty(),"Selection did not release the temporary geometric owner");
}
bool empty(const dw4::MessengerBuffer& buffer) {
    return buffer.enabled_only()==0 && buffer.booleans().empty() && buffer.integers().empty()
        && buffer.floats().empty() && buffer.integers64().empty() && buffer.vectors().empty()
        && buffer.matrices().empty() && buffer.objects().empty() && buffer.strings().empty();
}
void explicit_buffer_stack() {
    dw4::NativeMessenger messenger;
    rejects([&]{messenger.push_buffer();}); rejects([&]{messenger.pop_buffer();});
    messenger.select("OUTER","",7);
    auto& parent=messenger.buffer(); const auto* selected=&messenger.selected();
    parent.set_enabled_only(255); parent.add_bool(2); parent.add_int(-17);
    parent.add_float(std::bit_cast<float>(0x80000001u)); parent.add_int64(std::numeric_limits<std::int64_t>::min());
    parent.add_vector({1,-0.0f,3}); std::array<float,16> matrix{}; matrix[3]=-0.0f; matrix[15]=1;
    parent.add_matrix(matrix); parent.add_object(a); parent.add_object({});
    parent.add_string({}); parent.add_string(""); parent.add_string("parent");
    auto box=std::make_shared<const dw4::CollisionBox>(dw4::CollisionBox{{1,2,3},{4,5,6}});
    const std::weak_ptr<const dw4::CollisionBox> lifetime=box;
    parent.add_box(box); box.reset();
    messenger.push_buffer(); auto& child=messenger.buffer();
    check(&child!=&parent && messenger.buffer_depth()==1 && empty(child),"Push did not install a distinct prepared spare");
    check(&messenger.selected()==selected && messenger.selected().category=="OUTER","Push changed the selected cache record");
    parent.add_int(23); // A retained buffer reference still names the parked object.
    messenger.select("CHILD",{},-2); child.add_bool(1); child.add_int(99); child.add_string("discard");
    child.set_enabled_only(1); child.add_object(b);
    messenger.pop_buffer();
    check(&messenger.buffer()==&parent && messenger.buffer_depth()==0 && empty(child),"Pop did not clear the child and restore parent object identity");
    check(parent.enabled_only()==255 && parent.booleans()==std::vector<std::uint8_t>{2}
        && parent.integers()==std::vector<std::int32_t>{-17,23}
        && std::bit_cast<std::uint32_t>(parent.floats()[0])==0x80000001u
        && parent.integers64()[0]==std::numeric_limits<std::int64_t>::min()
        && parent.vectors()[0][0]==1 && std::bit_cast<std::uint32_t>(parent.vectors()[0][1])==0x80000000u
        && parent.matrices()[0][15]==1 && std::bit_cast<std::uint32_t>(parent.matrices()[0][3])==0x80000000u
        && parent.objects().size()==3 && std::get<Id>(parent.objects()[0])==a
        && std::holds_alternative<std::monostate>(parent.objects()[1]) && !lifetime.expired()
        && parent.strings()==std::vector<std::optional<std::string>>{{},"","parent"},
        "Buffer rotation copied, cleared or changed a parked typed value or owner");
    check(messenger.selected().category=="CHILD" && !messenger.selected().name && messenger.selected().index==-2,
        "Pop incorrectly restored the parent message selection");
    rejects([&]{messenger.pop_buffer();});
    check(&messenger.buffer()==&parent && parent.integers().size()==2,"Underflow changed the active buffer before rejection");
    // Push itself performs no clear, including when a retained reference changed a spare.
    child.add_int(41); messenger.push_buffer();
    check(&messenger.buffer()==&child && child.integers()==std::vector<std::int32_t>{41},"Push cleared or replaced the recycled buffer object");
    messenger.select("CLEAR_CHILD",{},0); check(empty(child),"Selection did not clear the currently installed child");
    messenger.pop_buffer(); messenger.select("CLEAR_PARENT",{},0);
    check(lifetime.expired() && empty(parent),"Restored parent did not release its retained object on selection");

    std::array<dw4::MessengerBuffer*,5> identities{&parent};
    for(std::size_t depth=1;depth<identities.size();++depth) {
        messenger.push_buffer(); identities[depth]=&messenger.buffer(); messenger.buffer().add_int(static_cast<int>(depth));
        for(std::size_t earlier=0;earlier<depth;++earlier)
            check(identities[depth]!=identities[earlier],"Nested push reused a parked ancestor");
    }
    rejects([&]{messenger.push_buffer();});
    check(messenger.buffer_depth()==4 && &messenger.buffer()==identities[4]
        && messenger.buffer().integers()==std::vector<std::int32_t>{4},"Overflow changed the valid stack prefix");
    auto moved=std::move(messenger);
    rejects([&]{messenger.push_buffer();}); rejects([&]{messenger.pop_buffer();});
    check(messenger.buffer_depth()==0,"Moved-from messenger exposed another owner's stack");
    for(std::size_t depth=4;depth;--depth) {
        moved.pop_buffer();
        check(empty(*identities[depth]) && &moved.buffer()==identities[depth-1] && moved.buffer_depth()==depth-1,
            "Nested pop or move did not preserve LIFO buffer identity");
        if(depth>1) check(moved.buffer().integers()==std::vector<std::int32_t>{static_cast<int>(depth-1)},
            "Nested pop lost a parent list");
    }
    std::array<std::weak_ptr<const dw4::CollisionBox>,5> retained;
    std::array<std::weak_ptr<const dw4::MessengerBuffer>,5> released_packets;
    for(std::size_t i=0;i<retained.size();++i) {
        if(i) moved.push_buffer();
        released_packets[i]=moved.buffer_owner();
        auto owned=std::make_shared<const dw4::CollisionBox>(); retained[i]=owned; moved.buffer().add_box(std::move(owned));
    }
    moved.reset();
    for(const auto& owner:retained) check(owner.expired(),"Reset retained an active or parked geometric object");
    for(const auto& owner:released_packets) check(owner.expired(),"Reset retained an original stack packet owner");
    check(moved.buffer_depth()==0,"Reset retained explicit stack depth");
    rejects([&]{moved.pop_buffer();}); moved.select("NEW",{},0);
    check(empty(moved.buffer()),"Reset did not prepare a fresh empty current buffer");
}
void stacked_delivery_continuation() {
    Fixture f; f.add(a); f.add(b); f.add(c,{},-1,"INNER");
    unsigned outer_calls=0,inner_calls=0,later_calls=0; bool ready=false,pushed=false;
    std::optional<dw4::MessengerDispatch> nested;
    dw4::MessengerBuffer* parent=nullptr; dw4::MessengerBuffer* child=nullptr;
    f.services.manager=[&](Id id) { return dw4::MessengerManager{[&,id](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&) {
        if(id==a) {
            ++outer_calls;
            if(!pushed) {
                parent=&messenger.buffer(); messenger.push_buffer(); pushed=true;
                nested.emplace(messenger.begin_send_to("INNER",{},-1,"QUERY"));
                child=&messenger.buffer(); child->add_int(51); child->set_enabled_only(2);
            }
            const auto result=messenger.resume(*nested,f.services);
            if(result.stop!=done) return dw4::MessengerCallResult{false,1,result.diagnostic};
            check(messenger.buffer().integers()==std::vector<std::int32_t>{51,52},"Nested retry lost its child results");
            messenger.pop_buffer(); messenger.buffer().add_bool(1);
        } else if(id==c) {
            ++inner_calls;
            if(!ready) return dw4::MessengerCallResult{false,1,"Inner query unavailable"};
            messenger.buffer().add_int(52);
        } else {
            ++later_calls;
            check(&messenger.buffer()==parent && messenger.buffer().integers()==std::vector<std::int32_t>{7}
                && messenger.buffer().enabled_only()==0,"Later outer recipient observed the nested child buffer");
        }
        return dw4::MessengerCallResult{true,1,{}};
    },{}};};
    f.messenger.select("ACTION",{},-1); f.messenger.buffer().add_int(7);
    auto outer=f.messenger.begin_send("OUTER");
    check(f.messenger.resume(outer,f.services).stop==dw4::MessengerStop::service_required
        && f.messenger.buffer_depth()==1 && &f.messenger.buffer()==child && parent->integers()[0]==7,
        "Suspension automatically restored or discarded a caller-owned pushed buffer");
    ready=true;
    check(f.messenger.resume(outer,f.services).stop==done && outer_calls==2 && inner_calls==2 && later_calls==1
        && f.messenger.buffer_depth()==0 && empty(*child) && parent->booleans()==std::vector<std::uint8_t>{1},
        "Stacked continuation replayed a completed prefix or failed to restore the buffer");
    check(f.messenger.selected().category=="INNER" && f.messenger.selected().command=="QUERY",
        "Successful outer delivery restored an explicitly nested selection");
    f.messenger.resume(outer,f.services); check(later_calls==1,"Completed stacked dispatch replayed");

    Fixture fault; fault.add(a); unsigned attempts=0;
    fault.services.manager=[&](Id) { return dw4::MessengerManager{[&](dw4::NativeMessenger& messenger,const dw4::MessengerDelivery&)->dw4::MessengerCallResult {
        ++attempts; messenger.push_buffer(); messenger.select("FAILED",{},0); messenger.buffer().add_int(9);
        throw std::runtime_error("Unknown completion");
    },{}};};
    auto failed=fault.messenger.begin_send_to("ACTION",{},-1,"GO"); fault.messenger.buffer().add_int(8);
    check(fault.messenger.resume(failed,fault.services).stop==dw4::MessengerStop::faulted && fault.messenger.buffer_depth()==1,
        "Throwing callback silently popped its explicit buffer");
    fault.messenger.resume(failed,fault.services);
    check(attempts==1 && fault.messenger.buffer().integers()==std::vector<std::int32_t>{9},"Faulted nested effects were retried or cleared");
    fault.messenger.pop_buffer(); check(fault.messenger.buffer().integers()==std::vector<std::int32_t>{8},"Owner could not explicitly restore a faulted child's parent");
}
void buffer_release_reentrancy() {
    dw4::NativeMessenger messenger; messenger.select("ROOT",{},0); messenger.buffer().add_int(7); messenger.push_buffer();
    auto& child=messenger.buffer();
    unsigned blocked=0;
    auto object=dw4::MessengerBox(new dw4::CollisionBox,[&](const dw4::CollisionBox* value) {
        auto attempt=[&](auto function) { try { function(); } catch(const dw4::MessengerError&) { ++blocked; } };
        attempt([&]{messenger.push_buffer();}); attempt([&]{messenger.pop_buffer();});
        attempt([&]{messenger.select("REENTER",{},0);}); attempt([&]{messenger.reset();});
        attempt([&]{messenger.buffer().add_int(99);});
        attempt([&]{child.add_int(99);}); attempt([&]{child.set_enabled_only(1);}); attempt([&]{child.clear();});
        delete value;
    });
    messenger.buffer().add_box(std::move(object)); messenger.pop_buffer();
    check(blocked==8 && empty(child) && messenger.buffer_depth()==0 && messenger.buffer().integers()==std::vector<std::int32_t>{7}
        && messenger.selected().category=="ROOT","Object release reentered a partially rotated buffer stack");
}
}
int main() {
    try {
        matching();activity_and_queries();cache();nested();continuation();nested_eviction();storage_and_bounds();registration_and_stack_bounds();retained_box_objects();
        explicit_buffer_stack();stacked_delivery_continuation();buffer_release_reentrancy();
        std::cout<<"Messenger tests passed: "<<checks<<" checks\n";return 0;
    } catch(const std::exception& error) { std::cerr<<"Messenger test failure: "<<error.what()<<'\n';return 1; }
}
