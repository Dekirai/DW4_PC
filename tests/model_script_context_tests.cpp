#include "dw4/model_script_context.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const ModelScriptError&){return;}
    throw std::runtime_error("Invalid model script ownership or continuation was accepted.");
}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::shared_ptr<ModelScriptWork> work(std::uint32_t ram=0) {
    return std::make_shared<ModelScriptWork>(ControllerObjectId{7,100+ram},"models/player.igb","p_test",
        static_cast<std::int32_t>(ram),std::make_shared<AnimationMatrix>(identity()));
}
ModelTargetHitRequest request(const ModelScriptWork& target) {
    return {target.target(),target.source(),target.name(),target.ram_no(),"hit","Hit",70126,3,"enemy",2,-9};
}
void defaults_and_publication() {
    static_assert(!std::is_copy_constructible_v<ModelScriptWork>);
    static_assert(!std::is_copy_constructible_v<ModelScriptContextRuntime>);
    static_assert(!std::is_copy_constructible_v<ModelScriptInvocation>);
    auto target=work();
    check(target->hp && target->max_hp && target->hp!=target->max_hp && !target->hp->name && !target->max_hp->name &&
        target->hp->data==0 && target->max_hp->data==0 && target->record==-1,"Fresh work lost distinct zero beDataObjInt owners or signed record default.");
    auto gameram_hp=std::make_shared<ModelScriptInt>(ModelScriptInt{"HP",125});
    auto gameram_max=std::make_shared<ModelScriptInt>(ModelScriptInt{"MHP",250});
    target->hp=gameram_hp;target->max_hp=gameram_max;target->record=-19;
    ModelScriptContextRuntime runtime;
    check(!runtime.pending() && !runtime.has_context() && !runtime.faulted() && runtime.depth()==0,"Fresh shared owner published fake context.");
    rejects([&]{runtime.context();});rejects([&]{runtime.mutable_context();});
    auto input=request(*target);auto call=runtime.begin(target,input);
    const auto& c=runtime.context();
    check(c.target==target->target() && c.target_source==target->source() && c.target_ram==0 && c.matrix==target->matrix(),
        "Publication lost qualified target identity or live matrix reference.");
    check(c.maximum_hp==250 && c.hp==125 && c.target_record==-19 && c.target_name=="p_test" && c.kind==70126 && c.body_ordinal==3,
        "Publication lost HP words, signed record, kind or body ordinal.");
    check(c.source_name=="enemy" && c.source_ram==2 && c.source_record==-9 && c.direction==1 && c.event=="Hit",
        "Publication lost source arguments or original direction default.");
    check(call.call().filename=="hit_p_test.lua" && !call.completed() && runtime.depth()==1,"Invocation filename or pending lifetime differs.");
    input.kind=1;input.source_name="changed";
    (*target->matrix())[12]=23;target->max_hp->data=900;target->record=44;
    check((*c.matrix)[12]==23 && c.maximum_hp==250 && c.target_record==-19,"Scalar publication was live-reloaded or matrix was copied.");
    unsigned invoked{};
    check(runtime.resume(call,[&](auto& owner,const auto& retained) {
        ++invoked;check(retained.request.kind==70126 && retained.request.source_name=="enemy","Call arguments were not value-owned.");
        owner.mutable_context().hp=100;owner.mutable_context().direction=0;return true;
    })==std::optional<std::uint8_t>{std::uint8_t{0}},"Zero direction was mistaken for an incomplete script.");
    check(gameram_hp->data==100 && target->hp==gameram_hp && target->max_hp==gameram_max,"HP merge detached the shared GAMERAM object.");
    check(call.completed() && !runtime.pending() && runtime.has_context(),"Completion discarded shared context or retained active depth.");
    check(runtime.resume(call,[&](auto&,const auto&){++invoked;return false;})==std::optional<std::uint8_t>{std::uint8_t{0}} && invoked==1,
        "Completed invocation replayed a script callback.");
    rejects([&]{runtime.mutable_context();});
}
void suspension_and_request_retention() {
    auto target=work();target->hp->data=10;
    ModelScriptContextRuntime runtime;auto call=runtime.begin(target,request(*target));
    check(!runtime.resume(call) && !runtime.resume(call) && runtime.context().hp==10,"Missing callback changed its published prefix.");
    unsigned calls{};const ModelScriptCall* first{};
    ModelScriptDispatch script=[&](auto& owner,const auto& retained) {
        ++calls;
        if(calls==1) {first=&retained;owner.mutable_context().hp=4;owner.mutable_context().direction=255;return false;}
        check(&retained==first && owner.context().hp==4 && owner.context().direction==255,"Retry rebuilt request or republished context.");
        return true;
    };
    check(!runtime.resume(call,script) && target->hp->data==10 && runtime.pending(),"False callback merged HP prematurely.");
    check(!runtime.resume(call) && calls==1 && runtime.context().hp==4,"Missing callback after invocation abandoned its prefix.");
    rejects([&]{runtime.begin(target,request(*target));});
    check(!runtime.faulted(),"Rejected top-level interference faulted a valid continuation.");
    check(runtime.resume(call,script)==std::optional<std::uint8_t>{std::uint8_t{255}} && calls==2 && target->hp->data==4,"Retry failed to commit the retained HP/direction state.");
}
void hp_merge_cases() {
    for(unsigned scenario=0;scenario<5;++scenario) {
        auto target=work();target->hp->data=10;const auto before=target->hp;
        ModelScriptContextRuntime runtime;auto call=runtime.begin(target,request(*target));
        runtime.resume(call,[&](auto& owner,const auto&) {
            owner.mutable_context().hp=3;
            if(scenario==1)target->hp->data=12;
            if(scenario==2)target->hp=std::make_shared<ModelScriptInt>(ModelScriptInt{std::string{},10});
            if(scenario==3)target->hp=std::make_shared<ModelScriptInt>(ModelScriptInt{"replacement",11});
            if(scenario==4)target->max_hp.reset(); // The epilogue never reads maximum HP again.
            return true;
        });
        const auto expected=scenario==1 ? 12 : scenario==3 ? 11 : 3;
        check(target->hp->data==expected,"HP merge ignored direct mutation or compared wrapper identity instead of entry value.");
        if(scenario==2 || scenario==3)check(before->data==10 && target->hp!=before,"HP epilogue wrote the saved old wrapper instead of reloading work.");
    }
    auto target=work();target->hp->data=std::numeric_limits<std::int32_t>::min();
    target->max_hp->data=std::numeric_limits<std::int32_t>::max();target->record=std::numeric_limits<std::int32_t>::min();
    ModelScriptContextRuntime runtime;auto call=runtime.begin(target,request(*target));
    check(runtime.context().hp==std::numeric_limits<std::int32_t>::min() && runtime.context().maximum_hp==std::numeric_limits<std::int32_t>::max() &&
        runtime.context().target_record==std::numeric_limits<std::int32_t>::min(),"Prepared wrapper/record words were clamped or converted to unsigned semantics.");
    runtime.resume(call,[](auto&,const auto&){return true;});
}
void nested_overwrite_and_retry() {
    auto outer=work(0),inner=work(1);outer->hp->data=10;inner->hp->data=50;
    ModelScriptContextRuntime runtime;auto parent=runtime.begin(outer,request(*outer));
    std::optional<ModelScriptInvocation> child;unsigned parent_calls{},child_calls{};
    ModelScriptDispatch child_script=[&](auto& owner,const auto& call) {
        ++child_calls;check(call.request.target_ram==1,"Child request lost its own target.");
        if(child_calls==1) {owner.mutable_context().hp=6;owner.mutable_context().direction=2;return false;}
        check(owner.context().hp==6 && owner.context().target_ram==1,"Child retry republished or restored parent context.");return true;
    };
    ModelScriptDispatch parent_script=[&](auto& owner,const auto& call) {
        ++parent_calls;check(call.request.target_ram==0,"Parent request followed overwritten shared context.");
        if(!child)child.emplace(owner.begin(inner,request(*inner)));
        if(!owner.resume(*child,child_script))return false;
        check(owner.context().target_ram==1 && owner.context().direction==2 && inner->hp->data==6,
            "Child completion restored the parent context or missed child HP merge.");
        return true;
    };
    check(!runtime.resume(parent,parent_script) && runtime.depth()==2 && outer->hp->data==10 && inner->hp->data==50,
        "Nested missing service lost a stack frame or merged an incomplete script.");
    rejects([&]{runtime.resume(*child,child_script);});
    rejects([&]{runtime.begin(work(2),request(*work(2)));});
    check(!runtime.faulted(),"LIFO rejection made a valid retained parent unresumable.");
    check(runtime.resume(parent,parent_script)==std::optional<std::uint8_t>{std::uint8_t{2}} && parent_calls==2 && child_calls==2 && runtime.depth()==0,
        "Parent retry could not re-enter its retained child exactly once.");
    check(outer->hp->data==6 && inner->hp->data==6 && runtime.context().target_ram==1 && runtime.context().matrix==inner->matrix(),
        "Outer epilogue did not consume the live overwritten child context.");
}
void validation_and_ownership() {
    auto target=work();ModelScriptContextRuntime runtime;
    for(unsigned field=0;field<4;++field) {
        auto bad=request(*target);
        if(field==0)++bad.target.file_index;
        if(field==1)bad.target_source="other.igb";
        if(field==2)bad.target_name="other";
        if(field==3)++bad.target_ram;
        rejects([&]{runtime.begin(target,bad);});
        check(!runtime.has_context() && !runtime.pending() && !runtime.faulted(),"Rejected qualification partially published shared state.");
    }
    auto bad=request(*target);bad.source_name=std::string("bad\0name",8);rejects([&]{runtime.begin(target,bad);});
    bad=request(*target);bad.script_prefix=std::string(4096,'x');rejects([&]{runtime.begin(target,bad);});
    bad=request(*target);bad.event=std::string(4097,'x');rejects([&]{runtime.begin(target,bad);});
    rejects([&]{runtime.begin({},request(*target));});
    const auto saved_hp=target->hp;target->hp.reset();rejects([&]{runtime.begin(target,request(*target));});target->hp=saved_hp;
    target->max_hp->name=std::string("bad\0name",8);rejects([&]{runtime.begin(target,request(*target));});target->max_hp->name.reset();
    (*target->matrix())[0]=std::numeric_limits<float>::infinity();rejects([&]{runtime.begin(target,request(*target));});(*target->matrix())[0]=1;
    rejects([&]{ModelScriptWork invalid({0,1},"source","name",0,{});});
    rejects([&]{ModelScriptWork invalid({0,1},"","name",0,std::make_shared<AnimationMatrix>(identity()));});
    rejects([&]{ModelScriptWork invalid({0,0xffffffffu},"source","name",0,std::make_shared<AnimationMatrix>(identity()));});
    auto call=runtime.begin(target,request(*target));ModelScriptContextRuntime alien;
    rejects([&]{alien.resume(call,[](auto&,const auto&){return true;});});
    check(!runtime.faulted() && !alien.has_context(),"Foreign resume poisoned either valid owner.");
    auto moved=std::move(call);check(!call.completed() && !runtime.faulted(),"Moving the retained handle abandoned its invocation.");
    rejects([&]{call.call();});
    runtime.resume(moved,[](auto&,const auto&){return true;});
    auto empty=std::make_shared<ModelScriptWork>(ControllerObjectId{0,1},"source","",-1,std::make_shared<AnimationMatrix>(identity()));
    auto empty_request=request(*empty);empty_request.source_name="";empty_request.event="";empty_request.script_prefix="";
    auto empty_call=runtime.begin(empty,empty_request);
    check(empty_call.call().filename=="_.lua" && runtime.context().target_ram==-1 && runtime.context().event.empty(),"Explicit empty names or signed RAM values were normalized.");
    runtime.resume(empty_call,[](auto&,const auto&){return true;});
}
void faults_and_stack_bound() {
    auto target=work();target->hp->data=9;ModelScriptContextRuntime runtime;auto call=runtime.begin(target,request(*target));unsigned calls{};
    bool caught=false;
    try {runtime.resume(call,[&](auto& owner,const auto&)->bool {++calls;owner.mutable_context().hp=2;throw std::runtime_error("uncertain effect");});}
    catch(const std::runtime_error&){caught=true;}
    check(caught && calls==1 && runtime.faulted() && runtime.pending() && target->hp->data==9 && runtime.context().hp==2,
        "Throwing script merged HP or discarded its uncertain published prefix.");
    rejects([&]{runtime.resume(call,[](auto&,const auto&){return true;});});
    rejects([&]{runtime.begin(target,request(*target));});rejects([&]{runtime.mutable_context();});
    ModelScriptContextRuntime removed;auto removed_call=removed.begin(target,request(*target));
    rejects([&]{removed.resume(removed_call,[&](auto&,const auto&){target->hp.reset();return true;});});
    check(removed.faulted() && removed.pending(),"Missing live HP at epilogue acknowledged completion.");
    target->hp=std::make_shared<ModelScriptInt>();
    ModelScriptContextRuntime abandoned;{auto lost=abandoned.begin(target,request(*target));}
    check(abandoned.faulted() && abandoned.pending(),"Dropped pending handle silently abandoned the shared invocation.");
    ModelScriptContextRuntime unfinished;auto p=unfinished.begin(target,request(*target));std::optional<ModelScriptInvocation> child;
    rejects([&]{unfinished.resume(p,[&](auto& owner,const auto&){child.emplace(owner.begin(target,request(*target)));return true;});});
    check(unfinished.faulted() && unfinished.depth()==2,"Parent completed before its pending child.");
    ModelScriptContextRuntime bounded;auto outer=bounded.begin(target,request(*target));unsigned maximum{};
    ModelScriptDispatch recurse;
    recurse=[&](auto& owner,const auto&) {
        maximum=static_cast<unsigned>(owner.depth());
        if(owner.depth()==32) {rejects([&]{owner.begin(target,request(*target));});return true;}
        auto nested=owner.begin(target,request(*target));return owner.resume(nested,recurse).has_value();
    };
    check(bounded.resume(outer,recurse)==std::optional<std::uint8_t>{std::uint8_t{1}} && maximum==32 && !bounded.pending() && !bounded.faulted(),
        "Bounded nesting failed to finish LIFO after a caught depth rejection.");
    ModelScriptContextRuntime recursive;auto same=recursive.begin(target,request(*target));
    recursive.resume(same,[&](auto& owner,const auto&){rejects([&]{owner.resume(same,{});});return true;});
    check(same.completed() && !recursive.faulted(),"Caught direct recursive resume corrupted the valid callback.");
}
void stable_matrix_lifetime() {
    struct Backing {
        AnimationMatrix matrix=identity();unsigned* destroyed;
        explicit Backing(unsigned& count):destroyed(&count) {}
        ~Backing(){++*destroyed;}
    };
    unsigned destroyed{};std::weak_ptr<Backing> weak;
    {
        ModelScriptContextRuntime runtime;
        auto backing=std::make_shared<Backing>(destroyed);weak=backing;
        auto matrix=std::shared_ptr<AnimationMatrix>(backing,&backing->matrix);
        auto target=std::make_shared<ModelScriptWork>(ControllerObjectId{3,5},"live.igb","p_live",0,matrix);
        auto call=runtime.begin(target,request(*target));backing.reset();matrix.reset();target.reset();
        check(!weak.expired() && destroyed==0,"Invocation or matrix alias failed to retain the real model backing.");
        runtime.resume(call,[&](auto& owner,const auto&){(*owner.context().matrix)[13]=41;return true;});
        check(weak.lock()->matrix[13]==41 && !weak.expired(),"Shared context exposed a matrix copy instead of live model storage.");
    }
    check(weak.expired() && destroyed==1,"Script work/context lifetime formed an ownership cycle.");
}
}
int main() {
    try {defaults_and_publication();suspension_and_request_retention();hp_merge_cases();nested_overwrite_and_retry();
        validation_and_ownership();faults_and_stack_bound();stable_matrix_lifetime();
        std::cout<<"model_script_context: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"Model script context after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
