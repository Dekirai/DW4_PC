#include "dw4/model_wait.hpp"
#include "dw4/pad.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid model wait operation accepted.");
}
MotionChannels channels(unsigned mask) {
    MotionChannels result;
    for(unsigned i=0;i<4;++i) if(mask&(1u<<i)) {
        MotionCommand command; command.type="beModelCtrlMOVEEX";command.channel=i;
        command.setup={MotionMode::accelerate_positive,1,1,0};result.apply(command);
    }
    return result;
}
void common_waits() {
    static_assert(!std::is_copy_constructible_v<ModelWaitRuntime>);
    const MotionChannels empty;
    ModelWaitRuntime runtime; ModelWaitState state{std::nullopt,255,-2,7};
    ModelWaitServices services; int pads{},queries{},runs{};
    services.held_buttons=[&]()->std::optional<std::uint32_t>{++pads;return 0;};
    services.action_complete=[&]()->std::optional<bool>{++queries;return false;};
    services.resume_commands=[&]{++runs;check(!state.name,"Wait clears before command resume.");state.name="next";return true;};
    check(runtime.update(state,empty,services).complete() && pads==0 && queries==0 && runs==0,"Null skips all wait work.");
    for(const auto name:{"TurnY","Landing","Moving","Rotation"}) {
        state.name=name; const auto before=state;
        check(runtime.update(state,channels(15),services).complete() && state==before,"Active common motion wait retains work.");
        state.name=name;runtime.update(state,empty,services);
        check(state.name=="next" && state.attack_state==-2 && state.attack_mode==7 && state.control_enabled==255,
            "Common completion preserves scalar fields and callback's newly assigned wait.");
    }
    check(pads==0 && queries==0 && runs==4,"Common motion waits do not sample pad or query ACTION.");
    state.name="Moving";runtime.update(state,channels(8),services);
    state.name="Rotation";runtime.update(state,channels(7),services);
    state.name="TurnY";runtime.update(state,channels(5),services);
    check(runs==7,"Common wait queries select XYZ, rotation and Y independently.");
    state.name="Action";runtime.update(state,empty,services);
    check(state.name=="Action" && queries==1 && pads==0,"Completed false query retains the wait and finishes this pass.");
    services.action_complete=[&]()->std::optional<bool>{++queries;return true;};
    runtime.update(state,empty,services);
    check(state.name=="next" && runs==8 && queries==2,"ACTION completion resumes the retained sequence.");
}
void service_continuations() {
    ModelWaitRuntime runtime;ModelWaitState state{"Action",1,4,5};MotionChannels motion;
    int queries{},runs{};ModelWaitServices services;
    check(runtime.update(state,motion).stop==ModelWaitStop::action_completion_required && runtime.pending(),"Missing query is explicit.");
    rejects([&]{runtime.update(state,motion);});
    auto different=state;rejects([&]{runtime.resume(different);});
    check(!runtime.faulted(),"Rejected owner mismatch must not poison a resumable pass.");
    services.action_complete=[&]()->std::optional<bool>{++queries;return std::nullopt;};
    check(runtime.resume(state,services).stop==ModelWaitStop::action_completion_required && state.name=="Action","Unavailable result is distinct from false.");
    services.action_complete=[&]()->std::optional<bool>{++queries;state.name="changed during query";return true;};
    check(runtime.resume(state,services).stop==ModelWaitStop::commands_required && !state.name,"Completed query clears live name before requiring command service.");
    state.name="new live work";
    services.resume_commands=[&]{++runs;check(state.name=="new live work","Pending command phase must not clear a replacement wait again.");return false;};
    check(runtime.resume(state,services).stop==ModelWaitStop::commands_required && queries==2 && runs==1,"Committed query does not repeat while command service pauses.");
    services.resume_commands=[&]{++runs;state.name="after commands";return true;};
    ModelWaitRuntime moved(std::move(runtime));
    check(!runtime.pending() && moved.pending(),"Moving transfers continuation without duplicating it.");
    check(moved.resume(state,services).complete() && state.name=="after commands" && queries==2,"Moved continuation stays bound to the same live owner.");
    ModelWaitRuntime pending;state.name="Action";pending.update(state,motion);
    rejects([&]{pending=ModelWaitRuntime{};});
    ModelWaitRuntime destination;destination=std::move(pending);
    check(destination.pending() && !pending.pending(),"Move assignment transfers into an idle destination.");
}
void player_pad_and_attack_one() {
    MotionChannels motion;ModelWaitRuntime runtime;ModelWaitState state{"unknown",1,0,8};
    ModelWaitServices services;int pads{},queries{},runs{},sends{};std::uint32_t held=pad_button::a;bool complete=false;
    services.held_buttons=[&]()->std::optional<std::uint32_t>{++pads;return held;};
    services.action_complete=[&]()->std::optional<bool>{++queries;return complete;};
    services.resume_commands=[&]{++runs;return true;};
    services.dispatch_sequence=[&](auto name){++sends;check(name=="SQ_WAIT","Exact fallback sequence.");return true;};
    check(runtime.update(state,motion).stop==ModelWaitStop::held_buttons_required,"Unknown enabled wait still requires pad first.");
    runtime.resume(state,services);check(pads==1 && queries==0 && state.name=="unknown","Unknown name retains work after sampling.");
    state.name="";state.control_enabled=0;runtime.update(state,motion);
    check(!runtime.pending() && state.name=="" && pads==1,"Empty differs from null but disabled control needs no pad.");
    state={"Atck01",255,0,8};runtime.update(state,motion,services);
    check(state.attack_state==1 && state.attack_mode==0 && state.name=="Atck01" && queries==1,"Atck01 state0 initializes mode and state before a false query.");
    held=0;runtime.update(state,motion,services);
    check(state.attack_mode==1 && state.attack_state==1,"A release latches mode1.");
    held=pad_button::a;complete=true;runtime.update(state,motion,services);
    check(!state.name && state.attack_state==0 && state.attack_mode==1 && sends==1 && runs==0,"Later A press preserves release latch and chooses SQ_WAIT on completion.");
    state={"Atck01",1,0,9};runtime.update(state,motion,services);
    check(!state.name && state.attack_state==0 && state.attack_mode==0 && runs==1,"Held state0 completes by resuming commands.");
    for(auto invalid:{std::numeric_limits<std::int32_t>::min(),-1,2,std::numeric_limits<std::int32_t>::max()}) {
        state={"Atck01",1,invalid,9};const auto before=state;const auto old_queries=queries,old_pads=pads;
        runtime.update(state,motion,services);
        check(state==before && queries==old_queries && pads==old_pads+1,"Other signed attack states still sample pad but make no progress.");
    }
    state={"Atck01",0,0,8};runtime.update(state,motion,services);
    check(state.attack_mode==1 && sends==2,"Disabled control supplies zero held buttons regardless of service sample.");
    state={"Atck01",1,0,8};services.action_complete={};
    runtime.update(state,motion,services);const auto old_pads=pads;
    state.attack_mode=6;
    services.action_complete=[&]()->std::optional<bool>{return true;};services.dispatch_sequence={};
    check(runtime.resume(state,services).stop==ModelWaitStop::sequence_required && !state.name && state.attack_state==0 && state.attack_mode==6,
        "Pending attack query retains initialized prefix and branches on live mode.");
    check(pads==old_pads,"Pad sample does not repeat after completed sample.");
}
void nested_attack_two_and_three() {
    MotionChannels motion;ModelWaitRuntime runtime;ModelWaitState state{"Atck02",1,7,9};
    ModelWaitServices services;std::vector<std::string> trace;bool send_ready=false,query_ready=false,query_value=true;
    services.held_buttons=[&]()->std::optional<std::uint32_t>{trace.push_back("pad");return 0;};
    services.dispatch_sequence=[&](auto name) {
        if(!send_ready)return false;
        trace.emplace_back(name);state.name="nested wait";state.attack_state=11;state.attack_mode=12;return true;
    };
    services.action_complete=[&]()->std::optional<bool>{
        if(!query_ready)return std::nullopt;
        trace.push_back("query");check(state.name=="nested wait","Atck02 query observes post-dispatch live work.");return query_value;
    };
    services.resume_commands=[&]{trace.push_back("commands");check(!state.name,"Atck02 clears nested wait before live command resume.");state.name="new cursor wait";return true;};
    check(runtime.update(state,motion,services).stop==ModelWaitStop::sequence_required && state.name=="Atck02","Uncommitted SQ_WAIT retains old work.");
    send_ready=true;
    check(runtime.resume(state,services).stop==ModelWaitStop::action_completion_required && state.name=="nested wait","Atck02 keeps nested send then pauses at query.");
    runtime.resume(state,services);
    check(trace==std::vector<std::string>{"pad","SQ_WAIT"},"Missing query does not replay pad or nested send.");
    query_ready=true;runtime.resume(state,services);
    check(trace==std::vector<std::string>{"pad","SQ_WAIT","query","commands"} && state.name=="new cursor wait" &&
        state.attack_state==11 && state.attack_mode==12,"Atck02 preserves callback state and uses its live cursor after nested send.");
    state.name="Atck02";query_value=false;trace.clear();runtime.update(state,motion,services);
    check(trace==std::vector<std::string>{"pad","SQ_WAIT","query"} && state.name=="nested wait","False post-send query preserves the newly installed wait.");
    state={"Atck03",1,9,6};trace.clear();
    services.held_buttons=[&]()->std::optional<std::uint32_t>{trace.push_back("pad");return pad_button::a;};
    runtime.update(state,motion,services);
    check(trace==std::vector<std::string>{"pad"} && state.name=="Atck03","Held Atck03 does not query or send.");
    state.control_enabled=0;runtime.update(state,motion,services);
    check(state.name=="new cursor wait" && state.attack_state==9 && state.attack_mode==6,"Released Atck03 resumes without changing attack state/mode.");
}
void reentrancy_and_faults() {
    MotionChannels motion;ModelWaitRuntime runtime;ModelWaitState state{"Action",0,0,0};ModelWaitServices services;
    services.action_complete=[&]()->std::optional<bool> {
        rejects([&]{runtime.resume(state);});
        rejects([&]{ModelWaitRuntime moved(std::move(runtime));});
        return true;
    };
    services.resume_commands=[]()->bool{throw std::runtime_error("Possibly committed command.");};
    rejects([&]{runtime.update(state,motion,services);});
    check(runtime.faulted() && runtime.pending() && !state.name,"Service exception preserves prefix and faults uncertain outcome.");
    rejects([&]{runtime.resume(state,services);});rejects([&]{runtime.update(state,motion,services);});
    ModelWaitRuntime other;ModelWaitState independent{"Atck03",0,2,3};
    ModelWaitServices done;done.resume_commands=[] {return true;};other.update(independent,motion,done);
    check(!independent.name && independent.attack_state==2 && independent.attack_mode==3,"Fault does not affect another owner's state.");
}
void live_pad_and_held_attack_two() {
    MotionChannels motion;ModelWaitRuntime runtime;ModelWaitState state{"unknown",1,5,6};
    ModelWaitServices services;int pads{},queries{},runs{},sends{};
    services.held_buttons=[&]()->std::optional<std::uint32_t>{++pads;return std::nullopt;};
    check(runtime.update(state,motion,services).stop==ModelWaitStop::held_buttons_required && state.name=="unknown",
        "Unavailable pad does not choose a player branch.");
    services.held_buttons=[&]()->std::optional<std::uint32_t>{++pads;state.name="Atck03";return 0;};
    services.resume_commands=[&]{++runs;return true;};
    runtime.resume(state,services);
    check(!state.name && pads==2 && runs==1 && state.attack_state==5 && state.attack_mode==6,
        "Player branch reads live name after the completed pad service.");
    state={"Atck02",1,5,6};
    services.held_buttons=[&]()->std::optional<std::uint32_t>{++pads;return pad_button::a;};
    services.action_complete=[&]()->std::optional<bool>{++queries;return true;};
    services.dispatch_sequence=[&](auto){++sends;return true;};
    runtime.update(state,motion,services);
    check(!state.name && queries==1 && runs==2 && sends==0,"Held Atck02 still queries ACTION and skips SQ_WAIT.");
    state={"Atck02",0,5,6};
    services.dispatch_sequence=[&](auto)->bool {++sends;state.name="possibly committed";throw std::runtime_error("Interrupted nested send.");};
    rejects([&]{runtime.update(state,motion,services);});
    check(runtime.faulted() && state.name=="possibly committed" && queries==1 && sends==1,
        "Uncertain nested send faults before querying and keeps its live side effects.");
    rejects([&]{runtime.resume(state,services);});
}
// Synthetic numeric protocol for an ignored local instruction comparison. The
// executable contains no original asset data or instruction bytes.
void oracle() {
    const std::array<std::string,14> names{"","","TurnY","Landing","Action","Moving","Rotation","Atck01","Atck02","Atck03","unknown","nested","changed","after"};
    unsigned name,control,allocated,mask,held,result,mutations;std::int32_t attack,mode;
    while(std::cin>>name>>control>>attack>>mode>>allocated>>mask>>held>>result>>mutations) {
        ModelWaitState state{name ? std::optional(names.at(name)) : std::nullopt,static_cast<std::uint8_t>(control),attack,mode};
        auto motion=channels(allocated ? mask : 0);
        if(allocated && mask==0) {motion=channels(8);motion.on_sequence_change();}
        std::vector<int> trace;ModelWaitServices services;
        services.held_buttons=[&]()->std::optional<std::uint32_t>{trace.push_back(1);if(mutations&8)state={"Atck01",2,1,7};return held;};
        services.action_complete=[&]()->std::optional<bool>{trace.push_back(2);if(mutations&2)state={"changed",255,17,-3};return (result&255)!=0;};
        services.resume_commands=[&]{trace.push_back(3);if(mutations&4)state={"after",7,23,24};return true;};
        services.dispatch_sequence=[&](auto value){if(value!="SQ_WAIT")throw std::runtime_error("Wrong sequence");trace.push_back(4);if(mutations&1)state={"nested",2,11,12};return true;};
        ModelWaitRuntime runtime;if(!runtime.update(state,motion,services).complete())throw std::runtime_error("Unexpected pending oracle result.");
        unsigned id=0;if(state.name) {id=1;while(id<names.size() && names[id]!=*state.name)++id;}
        std::cout<<id<<' '<<unsigned(state.control_enabled)<<' '<<state.attack_state<<' '<<state.attack_mode<<' '<<trace.size();
        for(auto event:trace)std::cout<<' '<<event;std::cout<<'\n';
    }
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="--oracle") {oracle();return 0;}
        common_waits();service_continuations();player_pad_and_attack_one();nested_attack_two_and_three();reentrancy_and_faults();
        live_pad_and_held_attack_two();
        std::cout<<"Model wait checks passed: "<<checks<<'\n';return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
