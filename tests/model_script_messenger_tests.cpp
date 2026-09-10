#include "dw4/model_script_messenger.hpp"
#include "dw4/action_event_runtime.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* message) { ++checks;if(!condition)throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid script messenger operation was accepted.");
}
AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
std::shared_ptr<ModelScriptWork> work(std::int32_t ram=0) {
    return std::make_shared<ModelScriptWork>(ControllerObjectId{7,100+static_cast<std::uint32_t>(ram)},
        "prepared/player.igb","p_test",ram,std::make_shared<AnimationMatrix>(identity()));
}
ModelTargetHitRequest request(const ModelScriptWork& target,std::int32_t source=2,std::int32_t kind=80000) {
    return {target.target(),target.source(),target.name(),target.ram_no(),"hit","Hit",kind,3,"source",source,-9};
}
struct Host {
    std::vector<Lua4Value> args{Lua4Value::host_object("be")};
    std::string name="be.BLPcalcKoukaMagic";
    std::uint64_t execution=1,id=1;
    Host()=default;
    Host(double first,double second):args{Lua4Value::host_object("be"),Lua4Value::number(first),Lua4Value::number(second)},
        name("be.BLTcalcSkillUp") {}
    Lua4HostCall call() const { return {name,args,{"synthetic script","main",58},execution,id}; }
};
struct Fixture {
    ActionEventRuntime runtime;
    ModelScriptMessenger bridge{runtime};
    ModelScriptContextRuntime context;
    std::shared_ptr<ModelScriptWork> target;
    ModelScriptInvocation invocation;
    Lua4HostResult result;
    explicit Fixture(std::int32_t ram=0,std::int32_t source=2,std::int32_t kind=80000):
        target(work(ram)),invocation(context.begin(target,request(*target,source,kind))) {}
    void recipient(ControllerObjectId id={9,1}) { runtime.register_recipient(id,"GAMERAM","gameram",0); }
    std::optional<std::uint8_t> drive(const Host& host) {
        return context.resume(invocation,[&](auto& ctx,const auto&) {
            result=bridge.handle(ctx,host.call());return result.completed;
        });
    }
};

void packet_and_current_result() {
    for(std::int32_t ram=0;ram<4;++ram) {
        Fixture f(ram,-7,std::numeric_limits<std::int32_t>::min());f.recipient();f.recipient({9,2});
        auto& original=f.runtime.messenger();original.select("OLD","old",4);
        original.buffer().set_enabled_only(1);original.buffer().add_bool(1);original.buffer().add_float(2);
        original.buffer().add_int64(3);original.buffer().add_vector({4,5,6});original.buffer().add_matrix(identity());
        original.buffer().add_object(ControllerObjectId{1,2});original.buffer().add_string("old");
        unsigned calls{};
        f.runtime.set_external_services({{},[&](ControllerObjectId id) {
            return MessengerManager{[&,id](auto& messenger,const auto& delivery) {
                check(id==ControllerObjectId{9,++calls},"Duplicate registrations changed recipient order.");
                check(delivery.message.category=="GAMERAM" && !delivery.message.name && delivery.message.index==-1 &&
                    delivery.message.command=="FIGHTCALC","Wrong original category/name/index/command.");
                auto& buffer=messenger.buffer();
                check(buffer.integers()==std::vector<std::int32_t>{ram,-7,std::numeric_limits<std::int32_t>::min()} &&
                    buffer.strings()==std::vector<std::optional<std::string>>{"P2PKOUKAMAGIC"},"Wrong BLP packet order or signed context fields.");
                check(buffer.booleans().empty() && buffer.floats().empty() && buffer.integers64().empty() &&
                    buffer.vectors().empty() && buffer.matrices().empty() && buffer.objects().empty() && !buffer.enabled_only(),
                    "Original selection did not clear every typed input list.");
                if(calls==2) {messenger.select("AFTER",std::nullopt,23);messenger.buffer().add_int(std::numeric_limits<std::int32_t>::min());}
                return MessengerCallResult{true,1,{}};
            },{}};
        }});
        Host host;check(f.drive(host).has_value(),"BLP did not complete.");
        check(calls==2 && f.result.returned.size()==1 && f.result.returned[0].as_number()==std::numeric_limits<std::int32_t>::min(),
            "BLP did not read signed current int0 after every recipient completed.");
        check(f.runtime.messenger().selected().category=="AFTER" && f.runtime.messenger().buffer_depth()==0 && !f.bridge.pending(),
            "BLP restored selection, pushed a private buffer or retained a completed call.");
    }
    Fixture unchanged(3,1);unchanged.recipient();
    unchanged.runtime.set_external_services({{},[](auto){return MessengerManager{[](auto&,const auto&){return MessengerCallResult{true,1,{}};},{}};}});
    check(unchanged.drive(Host{}).has_value() && unchanged.result.returned[0].as_number()==3,
        "An unchanged integer list was replaced with a fabricated Boolean result.");
}

void missing_services_and_retained_packet() {
    Fixture f;auto& before=f.runtime.messenger();before.select("OLD","old",4);before.buffer().add_int(71);
    Host host;check(!f.drive(host) && f.bridge.pending() && !f.bridge.faulted(),"Missing category did not preserve the logical call.");
    check(f.result.diagnostic.find("GAMERAM registration")!=std::string::npos &&
        f.runtime.messenger().buffer().integers()==std::vector<std::int32_t>{71},"Missing category unexpectedly selected or cleared a packet.");
    f.recipient();check(!f.drive(host),"Missing manager incorrectly succeeded.");
    rejects([&]{f.runtime.messenger();});
    rejects([&]{f.context.mutable_context();});
    unsigned callbacks{},prefix{},replacement{};std::uint64_t dispatch{};
    f.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto& messenger,const auto& delivery) {
        ++callbacks;
        if(!dispatch) {
            dispatch=delivery.dispatch_id;++prefix;
            check(messenger.buffer().integers()==std::vector<std::int32_t>{0,2,80000},"Prepared packet was lost before first manager.");
            messenger.buffer().clear();messenger.buffer().add_int(-77);
            return MessengerCallResult{false,0,"actual manager child pending"};
        }
        check(dispatch==delivery.dispatch_id && messenger.buffer().integers()==std::vector<std::int32_t>{-77},
            "Retry reconstructed or reselected an already committed packet.");
        return MessengerCallResult{true,1,{}};
    },{}};}});
    check(!f.drive(host) && prefix==1,"Incomplete manager did not retain its committed prefix.");
    f.runtime.set_external_services({{},[&](auto){++replacement;return MessengerManager{};}});
    check(f.drive(host).has_value() && f.result.returned[0].as_number()==-77 && callbacks==2 && prefix==1 && replacement==0,
        "Retry replaced the captured manager or replayed its completed prefix.");
}

void byte_arguments_and_live_context() {
    const std::array numbers{0.0,-0.0,1.0,-1.5,std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()};
    for(const auto first:numbers)for(const auto second:numbers) {
        Fixture f;f.recipient();unsigned count{};
        f.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto& messenger,const auto&) {
            ++count;check(messenger.buffer().integers()==std::vector<std::int32_t>{2,80000,first!=0,second!=0} &&
                messenger.buffer().strings()==std::vector<std::optional<std::string>>{"P2ESKILLUP"},
                "BLT used Lua truthiness, integer truncation or the wrong context fields.");
            messenger.buffer().clear();return MessengerCallResult{true,1,{}};
        },{}};}});
        check(f.drive(Host{first,second}).has_value() && count==1 && f.result.returned.empty(),"BLT invented a result or required int0.");
    }
    Fixture f;f.recipient();unsigned calls{};
    f.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto& messenger,const auto&) {
        if(++calls==1) {
            auto& live=f.context.mutable_context();live.target_ram=23;live.source_ram=-12;live.kind=90001;
            messenger.buffer().clear();messenger.buffer().add_int(-3);
        } else check(messenger.buffer().integers()==std::vector<std::int32_t>{-12,90001,1,0},
            "Sequential BLT restored the earlier BLP context or appended target RAM.");
        return MessengerCallResult{true,1,{}};
    },{}};}});
    check(f.context.resume(f.invocation,[&](auto& ctx,const auto&) {
        auto first=f.bridge.handle(ctx,Host{}.call());check(first.completed,"Immediate BLP fixture failed.");
        Host second(first.returned[0].as_number(),0);second.id=2;
        auto next=f.bridge.handle(ctx,second.call());check(next.completed && next.returned.empty(),"BLT could not follow BLP in the same script.");
        return next.completed;
    }).has_value() && calls==2,"Sequential current-context calls did not complete.");
}

void nested_call_and_buffer_lifetime() {
    Fixture f;f.recipient();auto child_work=work(3);std::optional<ModelScriptInvocation> child;
    Host parent,child_host(0,1);child_host.execution=2;
    unsigned parent_calls{},child_calls{},parent_prefix{};
    // Give each captured manager a stable branch: parent/child may share the
    // live argument buffer, whose contents cannot identify a resumed parent.
    unsigned resolutions{};
    f.runtime.set_external_services({{},[&](auto){
        const bool is_child=++resolutions==2;
        return MessengerManager{[&,is_child](auto& messenger,const auto&) {
            if(is_child) {
                if(++child_calls==1)return MessengerCallResult{false,0,"child native service"};
                messenger.select("CHILD_RESULT","child",19);messenger.buffer().add_int(-33);
                return MessengerCallResult{true,1,{}};
            }
            ++parent_calls;
            if(!child) {++parent_prefix;child.emplace(f.context.begin(child_work,request(*child_work,88,81000)));}
            const auto value=f.context.resume(*child,[&](auto& ctx,const auto&) {return f.bridge.handle(ctx,child_host.call()).completed;});
            return MessengerCallResult{value.has_value(),1,"nested script incomplete"};
        },{}};
    }});
    check(!f.drive(parent) && f.context.depth()==2 && parent_prefix==1 && f.bridge.pending(),"Nested child was not retained through outer suspension.");
    rejects([&]{f.runtime.messenger();});
    check(f.drive(parent).has_value() && parent_calls==2 && child_calls==2 && parent_prefix==1 && resolutions==2,
        "Nested manager/script prefixes or captured service resolution replayed.");
    check(f.result.returned[0].as_number()==-33 && f.context.context().target_ram==3 && f.context.context().source_ram==88 &&
        f.runtime.messenger().selected().category=="CHILD_RESULT" && f.runtime.messenger().buffer_depth()==0,
        "Parent failed to consume the current child buffer/context without restoration.");
}

void exact_call_owner_and_reentry() {
    Fixture f;f.recipient();Host host(-0.0,1);unsigned calls{};
    f.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto&,const auto&) {
        ++calls;
        rejects([&]{f.bridge.handle(f.context,host.call());});
        return MessengerCallResult{calls>1,1,"one actual pause"};
    },{}};}});
    check(!f.drive(host),"First retained BLT did not pause.");
    check(f.context.resume(f.invocation,[&](auto& ctx,const auto&) {
        Host other=host;other.id=2;rejects([&]{f.bridge.handle(ctx,other.call());});
        other=host;other.execution=2;rejects([&]{f.bridge.handle(ctx,other.call());});
        other=host;other.args[1]=Lua4Value::number(0.0);rejects([&]{f.bridge.handle(ctx,other.call());});
        ModelScriptContextRuntime foreign;rejects([&]{f.bridge.handle(foreign,host.call());});
        f.result=f.bridge.handle(ctx,host.call());return f.result.completed;
    }).has_value() && calls==2 && !f.bridge.faulted(),"Rejected owner/argument/reentry attempts damaged the retained valid call.");
    ActionEventRuntime runtime;ModelScriptMessenger bridge(runtime);ModelScriptContextRuntime empty;
    check(!bridge.handle(empty,Host{}.call()).completed && !bridge.pending(),"Unpublished shared context invented valid fields.");
}

void malformed_and_faulted_outcomes() {
    for(unsigned kind=0;kind<5;++kind) {
        Fixture f;Host host(1,0);unsigned calls{};f.recipient();
        f.runtime.set_external_services({{},[&](auto){++calls;return MessengerManager{};}});
        if(kind==0)host.args.clear();
        if(kind==1)host.args[0]=Lua4Value::host_object("other");
        if(kind==2)host.args[1]=Lua4Value::string("1");
        if(kind==3)host.args[2]=Lua4Value{};
        if(kind==4)host.args.push_back(Lua4Value::number(2));
        check(!f.drive(host) && calls==0 && !f.bridge.faulted(),"Unverified native argument layout dispatched an effect or silently converted a wrong type.");
    }
    Fixture no_identity;Host missing;missing.execution=0;rejects([&]{no_identity.drive(missing);});
    Fixture missing_result;missing_result.recipient();unsigned calls{};
    missing_result.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto& messenger,const auto&) {
        ++calls;messenger.buffer().clear();return MessengerCallResult{true,1,{}};
    },{}};}});
    rejects([&]{missing_result.drive(Host{});});
    check(missing_result.bridge.faulted() && missing_result.context.faulted() && calls==1,"Missing completed int0 was treated as an asynchronous success/result repair.");
    rejects([&]{missing_result.bridge.handle(missing_result.context,Host{}.call());});
    check(calls==1,"Faulted result read replayed completed manager effects.");
    Fixture thrown;thrown.recipient();unsigned prefix{};
    thrown.runtime.set_external_services({{},[&](auto){return MessengerManager{[&](auto&,const auto&)->MessengerCallResult {
        ++prefix;throw std::runtime_error("uncertain actual manager outcome");
    },{}};}});
    rejects([&]{thrown.drive(Host{});});rejects([&]{thrown.bridge.handle(thrown.context,Host{}.call());});
    check(thrown.bridge.faulted() && prefix==1,"Throwing manager was replayed.");
}
}
int main() {
    try {
        packet_and_current_result();missing_services_and_retained_packet();byte_arguments_and_live_context();
        nested_call_and_buffer_lifetime();exact_call_owner_and_reentry();malformed_and_faulted_outcomes();
        std::cout<<"Model script messenger tests passed: "<<checks<<" checks\n";return 0;
    } catch(const std::exception& error) {std::cerr<<"Model script messenger tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
