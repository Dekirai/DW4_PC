#include "dw4/model_script_runtime.hpp"
#include "dw4/assets.hpp"
#include "dw4/player_runtime_binding.hpp"
#include "dw4/resources.hpp"
#include "script_fixtures.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace dw4;
using namespace dw4::test;
unsigned checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    ++checks;try{operation();}catch(const std::exception&){return;}
    throw std::runtime_error("Invalid model script operation was accepted.");
}
// Synthetic Lua 4.0 writer, matching the existing Lua-reader test format.
enum Op : std::uint32_t {
    End,Return,Call,Tailcall,Pushnil,Pop,Pushint,Pushstring,Pushnum,Pushnegnum,
    Pushupvalue,Getlocal,Getglobal,Gettable,Getdotted,Getindexed,Pushself,Createtable,
    Setlocal,Setglobal,Settable,Setlist,Setmap,Add,Addi
};
std::uint32_t u(Op op,std::uint32_t value=0) {return op|(value<<6);}
std::uint32_t s(Op op,std::int32_t value) {return u(op,static_cast<std::uint32_t>(value+33554431));}
std::uint32_t ab(Op op,std::uint32_t a,std::uint32_t b) {return op|(a<<15)|(b<<6);}
struct Chunk {
    std::vector<std::string> strings;
    std::vector<double> numbers;
    std::vector<std::uint32_t> code;
    std::uint32_t string(std::string value) {
        strings.push_back(std::move(value));return static_cast<std::uint32_t>(strings.size()-1);
    }
    void number(double value) {numbers.push_back(value);code.push_back(u(Pushnum,static_cast<std::uint32_t>(numbers.size()-1)));}
    void set(const char* name,double value) {number(value);code.push_back(u(Setglobal,string(name)));}
    void increment(const char* name,std::int32_t value) {
        const auto index=string(name);code.insert(code.end(),{u(Getglobal,index),s(Addi,value),u(Setglobal,index)});
    }
    void method(const char* name,std::optional<double> arg={},std::uint32_t results=0) {
        code.push_back(u(Getglobal,string("be")));code.push_back(u(Pushself,string(name)));
        if(arg)number(*arg);
        code.push_back(ab(Call,0,results));
    }
    void getter(const char* method_name,const char* global) {method(method_name,{},1);code.push_back(u(Setglobal,string(global)));}
    Bytes bytes() const {
        Bytes result{0x1b,'L','u','a',0x40,1,4,4,4,32,6,9,8};
        const auto integer=[&](std::uint64_t value,std::size_t size=4) {
            for(std::size_t i=0;i<size;++i)result.push_back(static_cast<std::uint8_t>(value>>(8*i)));
        };
        const auto text=[&](const std::string& value) {
            integer(value.size()+1);result.insert(result.end(),value.begin(),value.end());result.push_back(0);
        };
        integer(std::bit_cast<std::uint64_t>(314159265.3589793),8);text("@synthetic-model-hit.lua");
        integer(0);integer(0);result.push_back(0);integer(16);integer(0);integer(0);
        integer(strings.size());for(const auto& value:strings)text(value);
        integer(numbers.size());for(const auto value:numbers)integer(std::bit_cast<std::uint64_t>(value),8);
        integer(0);integer(code.size()+1);for(const auto value:code)integer(value);integer(End);
        return result;
    }
    Lua4Program program() const {return read_lua4_chunk(bytes(),"synthetic model script");}
};
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::shared_ptr<ModelScriptWork> work(std::uint32_t ram=0,std::string name="p_test") {
    auto result=std::make_shared<ModelScriptWork>(ControllerObjectId{7,100+ram},"prepared/player.igb",std::move(name),
        static_cast<std::int32_t>(ram),std::make_shared<AnimationMatrix>(identity()));
    result->hp->data=100;result->max_hp->data=200;return result;
}
ModelTargetHitRequest request(const ModelScriptWork& target) {
    return {target.target(),target.source(),target.name(),target.ram_no(),"hit","Hit",70126,3,"enemy",2,-9};
}
ScriptRegistry registry(const std::vector<ScriptFixture::Script>& scripts) {
    ScriptFixture fixture;const auto info=fixture.add_info(scripts);const auto document=fixture.document();
    const std::array registrations{ScriptRegistration{&document,"scripts.igb",44,0,info.reference}};
    return ScriptRegistry(registrations);
}
void initialize_counter(ModelScriptRuntime& runtime) {
    Chunk initial;initial.set("counter",0);check(runtime.initialize(initial.program()).completed,"VM initialization failed.");
}
double global(const ModelScriptRuntime& runtime,const char* name) {return runtime.vm().get_global(name).as_number();}

void ordered_selection_and_getters() {
    Chunk first;first.increment("counter",1);first.method("MDLsetDMGTurn",0);first.number(-1);first.code.push_back(Return);
    Chunk shadow;shadow.increment("counter",1000);
    Chunk second;second.increment("counter",10);second.method("MDLsetDMGTurn",1.9);
    second.getter("MDLgetRamNo","ram");second.getter("MDLgetRecNo","record");
    second.getter("MDLgetBulletKind","kind");second.getter("MDLgetBulletBoxNo","body");
    ScriptFixture fixture;
    const auto a=fixture.add_info({{"hit_p_test.lua",first.bytes()},{"hit_p_test.lua",shadow.bytes()}});
    const auto b=fixture.add_info({{"hit_p_test.lua",second.bytes()}});
    auto document=fixture.document();
    const std::array registrations{ScriptRegistration{&document,"first load",4,0,a.reference},
        ScriptRegistration{&document,"first load",4,1,b.reference},ScriptRegistration{&document,"first load",4,0,a.reference}};
    ModelScriptRuntime runtime{ScriptRegistry(registrations)};auto target=work(2);target->record=std::numeric_limits<std::int32_t>::min();
    runtime.register_work(target);initialize_counter(runtime);unsigned fallback{};
    runtime.set_file_service([&](auto&,auto&,const auto&,bool)->std::optional<std::int32_t>{++fallback;return 9;});
    auto input=request(*target);input.kind=std::numeric_limits<std::int32_t>::max();input.body_ordinal=-7;
    check(runtime.handle(input)==std::optional<std::uint8_t>{std::uint8_t{0}} && !runtime.pending() && !runtime.faulted(),
        "Ordered completed chunks did not finish with the last live direction byte.");
    check(global(runtime,"counter")==12 && fallback==0,"Repeated registrations or first-match shadowing changed, or Lua -1 invoked fallback.");
    check(runtime.last_lua_result().completed && runtime.last_lua_result().returned.size()==1 &&
        runtime.last_lua_result().returned[0].as_number()==-1,"Lua return values were confused with native protected-call status.");
    check(global(runtime,"ram")==2 && global(runtime,"record")==std::numeric_limits<std::int32_t>::min() &&
        global(runtime,"kind")==std::numeric_limits<std::int32_t>::max() && global(runtime,"body")==-7,
        "Native getters lost signed payloads or read source RAM/record instead of target values.");
    check(target->hp->data==100 && runtime.context_runtime().context().direction==0,"Completed read/setter scripts changed unrelated HP.");
    check(runtime.handle(input)==std::optional<std::uint8_t>{std::uint8_t{0}} && global(runtime,"counter")==24,
        "A fresh completed Hit request did not execute a fresh invocation.");
}

void missing_host_and_retained_service() {
    Chunk script;script.increment("counter",1);script.method("Pause",7);script.increment("counter",10);script.method("After");
    ModelScriptRuntime runtime(registry({{"hit_p_test.lua",script.bytes()}}));auto target=work();runtime.register_work(target);
    initialize_counter(runtime);const auto input=request(*target);
    check(!runtime.handle(input) && runtime.pending() && !runtime.faulted() && global(runtime,"counter")==1,
        "Missing native service did not retain the exact executed Lua prefix.");
    check(runtime.last_lua_result().stop==Lua4RunStop::host_required && runtime.diagnostic().find("be.Pause")!=std::string::npos,
        "Missing method was treated as successful or faulted Lua.");
    unsigned old_calls{},new_calls{};std::uint64_t execution{},call_id{};
    runtime.set_host_service([&](auto& context,const auto& call)->Lua4HostResult {
        ++old_calls;
        if(!execution){execution=call.execution_id;call_id=call.call_id;context.mutable_context().hp=55;}
        check(call.name=="be.Pause" && call.execution_id==execution && call.call_id==call_id &&
            call.arguments.size()==2 && call.arguments[1].as_number()==7,"Retried host call lost owned arguments or identity.");
        return {old_calls==2,{},"waiting for prepared native effect"};
    });
    check(!runtime.handle(input) && old_calls==1 && target->hp->data==100 && global(runtime,"counter")==1,
        "Partial service completion replayed code or merged HP before script completion.");
    auto other=input;other.kind++;
    rejects([&]{runtime.handle(other);});rejects([&]{runtime.register_work(work(1));});
    check(!runtime.faulted() && runtime.pending(),"Rejected interference poisoned an existing continuation.");
    runtime.set_host_service([&](auto& context,const auto& call)->Lua4HostResult {
        ++new_calls;check(call.name=="be.After","A replacement service stole an already invoked call.");
        context.mutable_context().direction=3;return {true,{},{}};
    });
    check(runtime.handle(input)==std::optional<std::uint8_t>{std::uint8_t{3}} && old_calls==2 && new_calls==1 &&
        global(runtime,"counter")==11 && target->hp->data==55,"Resume lost retained service ownership or repeated earlier effects.");
}

void filesystem_continuation() {
    Chunk fallback;fallback.increment("counter",5);fallback.method("FilePause");
    fallback.increment("counter",50);fallback.method("MDLsetDMGTurn",0);fallback.number(99);fallback.code.push_back(Return);
    const auto program=fallback.program();
    ModelScriptRuntime runtime{ScriptRegistry{}};auto target=work();runtime.register_work(target);initialize_counter(runtime);
    const auto input=request(*target);unsigned file_calls{},replacement_calls{},host_calls{};bool ready{};
    check(!runtime.handle(input) && runtime.pending() && runtime.diagnostic().find("filesystem")!=std::string::npos,
        "No embedded match did not require the actual filesystem owner.");
    std::optional<Lua4Execution> execution;
    runtime.set_host_service([&](auto& context,const auto& call)->Lua4HostResult {
        ++host_calls;check(call.name=="be.FilePause","Fallback ran an unexpected native method.");
        if(host_calls==1)context.mutable_context().hp=25;
        return {ready,{},"file host service pending"};
    });
    runtime.set_file_service([&](auto& vm,auto&,const auto& call,bool nested)->std::optional<std::int32_t> {
        ++file_calls;check(!nested && call.filename=="hit_p_test.lua" && call.request.target==target->target(),
            "Filesystem callback lost its original request or was mislabeled nested.");
        if(!execution)execution.emplace(vm.begin_execution(program));
        const auto result=vm.resume(*execution);
        if(!result.completed)return std::nullopt;
        check(result.returned.size()==1 && result.returned[0].as_number()==99,"Fallback Lua result ownership changed.");
        return -1; // Explicit owner's status; completion must not invoke fallback a second time.
    });
    check(!runtime.handle(input) && file_calls==1 && host_calls==1 && global(runtime,"counter")==5,
        "Actual filesystem Lua pause did not retain its execution prefix.");
    runtime.set_file_service([&](auto&,auto&,const auto&,bool)->std::optional<std::int32_t>{++replacement_calls;return 0;});
    ready=true;
    check(runtime.handle(input)==std::optional<std::uint8_t>{std::uint8_t{0}} && !runtime.pending() && file_calls==2 && host_calls==2 &&
        replacement_calls==0 && global(runtime,"counter")==55 && target->hp->data==25,
        "Fallback retry replaced its owner, replayed Lua, or treated final status -1 as another fallback.");
}

void nested_shared_vm_and_context() {
    Chunk outer;outer.increment("counter",1);outer.method("Child");outer.increment("counter",100);outer.getter("MDLgetRamNo","after_child_ram");
    Chunk inner;inner.increment("counter",10);inner.method("ChildPause");inner.increment("counter",1000);inner.method("MDLsetDMGTurn",0);
    ModelScriptRuntime runtime(registry({{"hit_p_outer.lua",outer.bytes()},{"hit_p_inner.lua",inner.bytes()}}));
    auto parent=work(0,"p_outer"),child=work(1,"p_inner");runtime.register_work(parent);runtime.register_work(child);initialize_counter(runtime);
    const auto parent_request=request(*parent),child_request=request(*child);
    unsigned parents{},children{};bool ready{};std::uint64_t parent_id{},child_id{};
    runtime.set_host_service([&](auto& context,const auto& call)->Lua4HostResult {
        if(call.name=="be.Child") {
            ++parents;if(!parent_id)parent_id=call.execution_id;
            check(call.execution_id==parent_id,"Parent invocation identity changed on nested resume.");
            const auto done=runtime.handle(child_request);return {done.has_value(),{},"retained child pending"};
        }
        check(call.name=="be.ChildPause","Nested script escaped its expected native service.");
        ++children;if(!child_id)child_id=call.execution_id;
        check(call.execution_id==child_id && child_id!=parent_id && context.context().target_ram==1,
            "Nested chunk did not retain its own execution and shared child context.");
        if(children==1)context.mutable_context().hp=7;
        return {ready,{},"child service pending"};
    });
    check(!runtime.handle(parent_request) && runtime.context_runtime().depth()==2 && global(runtime,"counter")==11 &&
        parent->hp->data==100 && child->hp->data==100,"Nested pause merged HP or replayed/discarded a parent frame.");
    rejects([&]{runtime.handle(child_request);});
    check(!runtime.faulted(),"Rejected detached child resume poisoned the retained parent.");
    ready=true;
    check(runtime.handle(parent_request)==std::optional<std::uint8_t>{std::uint8_t{0}} && parents==2 && children==2 &&
        global(runtime,"counter")==1111 && global(runtime,"after_child_ram")==1,
        "Nested resume replayed native prefixes or restored the parent shared context.");
    check(parent->hp->data==7 && child->hp->data==7 && !runtime.pending() && runtime.context_runtime().depth()==0,
        "Outer HP epilogue did not consume the child's live shared-context result.");
}

void barriers_and_faults() {
    Chunk after;after.increment("counter",1);
    for(unsigned kind=0;kind<2;++kind) {
        ScriptFixture fixture;const auto info=fixture.add_info({{"hit_p_test.lua",after.bytes()},{"hit_p_test.lua",after.bytes()}});
        if(kind==0)fixture.scalar(info.data_references[0],3,null_ref);
        else fixture.contents(info.list_reference,{null_ref,info.data_references[1]});
        const auto document=fixture.document();
        const std::array registrations{ScriptRegistration{&document,"broken.igb",4,0,info.reference}};
        ModelScriptRuntime runtime{ScriptRegistry(registrations)};auto target=work();runtime.register_work(target);initialize_counter(runtime);
        unsigned fallback{};runtime.set_file_service([&](auto&,auto&,const auto&,bool)->std::optional<std::int32_t>{++fallback;return 0;});
        check(!runtime.handle(request(*target)) && !runtime.handle(request(*target)) && runtime.pending() && !runtime.faulted() &&
            fallback==0 && global(runtime,"counter")==0,"A malformed first-name barrier skipped to duplicate bytes or filesystem fallback.");
    }
    ModelScriptRuntime invalid(registry({{"hit_p_test.lua",{1,2,3}},{"hit_p_test.lua",after.bytes()}}));
    auto target=work();invalid.register_work(target);initialize_counter(invalid);
    rejects([&]{invalid.handle(request(*target));});
    check(invalid.faulted() && global(invalid,"counter")==0,"Invalid first-match bytecode fell through to a duplicate or remained replayable.");
    Chunk failing;failing.increment("counter",1);failing.method("Effect");failing.increment("counter",100);
    ModelScriptRuntime fault(registry({{"hit_p_test.lua",failing.bytes()}}));fault.register_work(target);initialize_counter(fault);
    unsigned effects{};fault.set_host_service([&](auto&,const auto&)->Lua4HostResult {++effects;throw std::runtime_error("uncertain native effect");});
    rejects([&]{fault.handle(request(*target));});rejects([&]{fault.handle(request(*target));});
    check(fault.faulted() && effects==1 && global(fault,"counter")==1,"A thrown native effect replayed or ran later Lua instructions.");
    ModelScriptRuntime absent{ScriptRegistry{}};
    check(!absent.handle(request(*target)) && !absent.pending() && !absent.faulted(),"Missing work published fabricated target context.");
    absent.register_work(target);auto alien=request(*target);alien.target_source="alien.igb";
    rejects([&]{absent.handle(alien);});
    check(!absent.pending() && !absent.faulted() && !absent.context_runtime().has_context(),"Rejected target identity partially latched an invocation.");
}

void setter_conversion_and_boundaries() {
    const std::array<std::pair<double,int>,10> cases{{{0,0},{-0.0,0},{1,1},{1.9,1},{2,0},{-1.9,0},
        {2147483647.0,0},{-2147483648.0,0},{2147483647.5,0},{-2147483648.5,0}}};
    for(const auto& [value,expected]:cases) {
        Chunk script;script.method("MDLsetDMGTurn",value);
        ModelScriptRuntime runtime(registry({{"hit_p_test.lua",script.bytes()}}));auto target=work();runtime.register_work(target);
        const auto result=runtime.handle(request(*target));
        check(result && *result==expected && runtime.context_runtime().context().direction==expected && !runtime.pending(),
            "Setter did not truncate finite binary64 then compare the prepared integer with exactly one.");
        check(runtime.last_lua_result().completed && runtime.last_lua_result().returned.empty() && target->hp->data==100,
            "Setter exposed a Lua result or changed unrelated HP state.");
    }
    for(unsigned kind=0;kind<5;++kind) {
        Chunk script;script.increment("counter",1);
        if(kind==0) {
            script.code.push_back(u(Getglobal,script.string("be")));
            script.code.push_back(u(Pushself,script.string("MDLsetDMGTurn")));
            script.code.push_back(u(Pushstring,script.string("1")));script.code.push_back(ab(Call,0,0));
        } else script.method("MDLsetDMGTurn",kind==1 ? 2147483648.0 : kind==2 ? -2147483649.0 : kind==3 ? 1e30 : -1e30);
        script.increment("counter",10);
        ModelScriptRuntime runtime(registry({{"hit_p_test.lua",script.bytes()}}));auto target=work();runtime.register_work(target);
        initialize_counter(runtime);const auto input=request(*target);
        check(!runtime.handle(input) && runtime.pending() && !runtime.faulted() && global(runtime,"counter")==1 &&
            runtime.context_runtime().context().direction==1,"Unsupported setter conversion changed context or ran later Lua effects.");
        check(!runtime.handle(input) && global(runtime,"counter")==1 && runtime.last_lua_result().stop==Lua4RunStop::host_required,
            "Unsupported setter retry replayed its prefix or fabricated an original error status.");
    }
}

void original(const std::filesystem::path& root) {
    const auto archive=assets::AfsArchive::open(root/"files"/"ndmw.afs");
    const auto scripts=read_igb_document(archive.read(179));
    const std::array registrations{ScriptRegistration{&scripts,"ndmw entry 179",44,237,2783}};
    ScriptRegistry selected(registrations);const auto plan=selected.lookup("hit_p_agumon.lua");
    check(plan.entries[0].script->bytes.size()==3230 && plan.entries[0].script->info.data_index==26,
        "Original Hit script provenance changed before execution.");
    const auto resources=MountedResources::open(root);
    const auto model=read_igb_document(resources.read("_common/p_agumon.igb"));
    ModelScriptRuntime runtime(std::move(selected));std::vector<std::shared_ptr<ModelScriptWork>> targets;
    constexpr std::array<std::uint32_t,4> refs{1725,6417,6420,6622};
    for(std::uint32_t i=0;i<4;++i) {
        const auto binding=decode_player_runtime_binding(model,{1386,546,i,1},"_common/p_agumon.igb",7);
        check(binding.model_ram_reference()==refs[i] && binding.model_name()=="p_agumon",
            "Original target does not use the actual selected MODELCTRL RAM graph.");
        auto target=std::make_shared<ModelScriptWork>(ControllerObjectId{7,binding.model_ram_reference()},binding.source(),
            binding.model_name(),static_cast<std::int32_t>(i),std::make_shared<AnimationMatrix>(identity()));
        // Explicit prepared values, not a claim about original starting HP.
        target->hp->data=125;target->max_hp->data=250;runtime.register_work(target);targets.push_back(target);
        auto input=request(*target);input.kind=70126+2*static_cast<std::int32_t>((i+1)%4);
        check(runtime.handle(input)==std::optional<std::uint8_t>{std::uint8_t{1}} && runtime.last_lua_result().completed &&
            runtime.last_lua_result().host_calls==2 && !runtime.pending(),
            "Unchanged original Hit early-return path did not finish through both verified getters.");
        const auto& context=runtime.context_runtime().context();
        check(context.target==target->target() && context.target_ram==static_cast<std::int32_t>(i) &&
            context.kind==input.kind && target->hp->data==125,"Original early return lost qualified identity or changed HP.");
    }
    auto ordinary=request(*targets[0]);ordinary.kind=80000;
    check(!runtime.handle(ordinary) && runtime.pending() && !runtime.faulted() &&
        runtime.context_runtime().context().direction==0 && targets[0]->hp->data==125,
        "Original kind80000 skipped verified setDMGTurn0 or completed through an unavailable native effect.");
    check(runtime.diagnostic().find("be.BLPcalcKoukaMagic")!=std::string::npos &&
        runtime.last_lua_result().stop==Lua4RunStop::host_required,
        "Original ordinary Hit stopped at a different first unimplemented native effect.");
    const auto instructions=runtime.last_lua_result().instructions;
    check(!runtime.handle(ordinary) && runtime.last_lua_result().instructions==instructions &&
        runtime.context_runtime().context().direction==0,"Missing ordinary combat service replayed its earlier Lua/setter prefix.");
}
} // namespace
int main(int argc,char** argv) {
    try {
        ordered_selection_and_getters();missing_host_and_retained_service();filesystem_continuation();
        nested_shared_vm_and_context();barriers_and_faults();setter_conversion_and_boundaries();const auto synthetic=checks;
        if(argc==2)original(std::filesystem::path(argv[1]));
        else if(argc!=1)throw std::runtime_error("Usage: dw4_model_script_runtime_tests [EXTRACTED_ROOT]");
        std::cout<<"Model script runtime checks: "<<checks<<" (synthetic "<<synthetic<<")\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<"After "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}
