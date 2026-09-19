#include "dw4/model_manager_frame.hpp"
#include "dw4/serial_scheduler.hpp"

#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& call,const char* reason) {
    ++checks;try {call();}catch(const std::exception&) {return;}throw std::runtime_error(reason);
}
MessengerCallResult done() {return {true,77,{}};}
std::shared_ptr<ModelSceneList> list(std::vector<std::shared_ptr<ModelSceneObject>> values) {
    auto result=std::make_shared<ModelSceneList>();
    result->count=result->capacity=static_cast<std::int32_t>(values.size());
    result->backing=std::make_shared<ModelSceneList::Storage>(std::move(values));return result;
}
std::shared_ptr<ModelSceneInfo> info(std::vector<std::shared_ptr<ModelSceneObject>> values) {
    auto result=std::make_shared<ModelSceneInfo>();result->rams=list(std::move(values));return result;
}
std::shared_ptr<ModelSceneManager> manager() {
    auto result=std::make_shared<ModelSceneManager>();result->system=std::make_shared<ModelSceneSystem>();
    result->system->is_active=1;result->update_enabled=1;result->infos=list({});return result;
}
void defaults_and_gates() {
    ModelSceneManager initial;ModelSceneSystem system;
    check(initial.update_enabled==0 && initial.infos && initial.infos->type()=="beBaseInfoList" &&
        initial.infos->count==0 && initial.infos->capacity==0 && !initial.infos->backing,
        "Reflected manager defaults lost the auto-created empty info list or zero enable byte.");
    check(system.is_active==0 && system.is_first_time==0 && system.is_frame_skip==0,
        "System gate bytes differ from their original zero defaults.");
    for(unsigned active=0;active<256;++active) {
        auto owner=manager();owner->system->is_active=static_cast<std::uint8_t>(active);owner->infos.reset();
        ModelManagerFrameInvocation frame(owner);
        if(active&1)rejects([&] {frame.resume();},"Odd active byte failed to reach the null info-list dereference.");
        else check(frame.resume().completed,"Even active byte was treated as a generic nonzero boolean.");
    }
    for(const auto first:{std::uint8_t{1},std::uint8_t{255}}) {
        auto owner=manager();owner->system->is_first_time=first;owner->infos.reset();
        ModelManagerFrameInvocation frame(owner);
        check(frame.resume().completed,"Nonzero first-time byte failed to gate the original loop.");
    }
    for(const auto skip:{std::uint8_t{1},std::uint8_t{255}}) {
        auto owner=manager();owner->system->is_frame_skip=skip;owner->infos.reset();
        ModelManagerFrameInvocation frame(owner);
        check(frame.resume().completed,"Nonzero frame-skip byte failed to gate the original loop.");
    }
    auto disabled=manager();disabled->update_enabled=0;disabled->infos.reset();
    ModelManagerFrameInvocation skipped(disabled);
    check(skipped.resume().completed,"Disabled manager consumed its missing info list.");
    auto missing=manager();missing->update_enabled=0;missing->system.reset();
    ModelManagerFrameInvocation broken(missing);
    rejects([&] {broken.resume();},"Disabled manager bypassed the earlier null-system dereference.");
    ModelManagerFrameInvocation null(nullptr);
    rejects([&] {null.resume();},"Null manager completed.");
}
void signed_counts_and_physical_order() {
    for(const auto count:{0,-1,std::numeric_limits<std::int32_t>::min()}) {
        auto owner=manager();owner->infos->count=count;owner->infos->backing.reset();
        ModelManagerFrameInvocation frame(owner);
        check(frame.resume().completed,"Nonpositive signed info count accessed its missing backing.");
        auto child=info({});child->rams->count=count;child->rams->backing.reset();owner=manager();owner->infos=list({child});
        ModelManagerFrameInvocation inner(owner);
        check(inner.resume().completed,"Nonpositive signed RAM count accessed its missing backing or callback.");
    }
    auto first=std::make_shared<ModelSceneRam>();auto second=std::make_shared<ModelSceneRam>();
    auto model=info({first,nullptr,second,first});
    auto land=std::make_shared<ModelHitLandInfo>();land->active=0;land->rams=list({second});
    auto geometry=std::make_shared<ModelHitLandGeometryInfo>();geometry->active=0;geometry->rams=list({first});
    auto owner=manager();owner->update_enabled=255;owner->infos=list({model,land,geometry,model});
    owner->infos->capacity=-100;model->rams->capacity=0;
    std::vector<std::shared_ptr<ModelSceneObject>> calls;
    owner->ram_frame_factory=[&](const auto& actual,const auto& ram) {
        check(actual==owner,"Per-RAM virtual received another manager owner.");
        return [&,ram] {calls.push_back(ram);return done();};
    };
    ModelManagerFrameInvocation frame(owner);
    check(frame.resume().completed && calls==std::vector<std::shared_ptr<ModelSceneObject>>{
        first,nullptr,second,first,second,first,first,nullptr,second,first},
        "Manager filtered inactive/null/duplicate occurrences or inspected logical capacity.");
    check(frame.resume().completed && calls.size()==10,"Completed manager frame replayed per-RAM calls.");
    auto alias=info({});auto shared=list({alias});alias->rams=shared;owner=manager();owner->infos=shared;
    owner->ram_frame_factory=[&](const auto&,const auto& argument) {
        check(argument==alias,"Aliased info/RAM list added a caller-side RAM type check.");return done;
    };
    ModelManagerFrameInvocation aliased(owner);
    check(aliased.resume().completed,"Info/RAM list identity alias failed.");
    alias->rams.reset(); // Break the deliberately cyclic prepared fixture.
}
void live_reload_and_capture() {
    auto a=std::make_shared<ModelSceneRam>();auto b=std::make_shared<ModelSceneRam>();
    auto c=std::make_shared<ModelSceneRam>();auto d=std::make_shared<ModelSceneRam>();
    auto first=info({a,b});auto old_second=info({b});auto late=info({d});
    auto captured=first->rams;auto owner=manager();owner->infos=list({first,old_second});
    std::vector<std::shared_ptr<ModelSceneObject>> calls;unsigned new_virtual_calls{};
    owner->ram_frame_factory=[&](const auto&,const auto& ram) {
        return [&,ram] {
            calls.push_back(ram);
            owner->system.reset();owner->update_enabled=0;
            first->rams=info({b})->rams;captured->count=-3;captured->capacity=-4;
            captured->backing=std::make_shared<ModelSceneList::Storage>(ModelSceneList::Storage{b,c});
            owner->infos=list({nullptr,late});owner->infos->count=0;owner->infos->capacity=-5;
            owner->ram_frame_factory=[&](const auto&,const auto& next) {
                ++new_virtual_calls;return [&,next] {calls.push_back(next);return done();};
            };
            return done();
        };
    };
    ModelManagerFrameInvocation frame(owner);
    check(frame.resume().completed && calls==std::vector<std::shared_ptr<ModelSceneObject>>{a,c,d} && new_virtual_calls==2,
        "Manager changed captured counts/RAM-list owner or failed to reload live backing/info list/virtual.");
}
void pending_target_and_missing_binding() {
    auto a=std::make_shared<ModelSceneRam>();auto b=std::make_shared<ModelSceneRam>();auto child=info({a,b});
    auto owner=manager();owner->infos=list({child});unsigned factories{},calls{},replacement_calls{};bool ready{};
    owner->ram_frame_factory=[&](const auto&,const auto& ram) {
        ++factories;check(ram==a,"First reached RAM identity changed.");
        return [&] {++calls;return MessengerCallResult{ready,0,"Prepared RAM is pending."};};
    };
    ModelManagerFrameInvocation frame(owner);
    check(!frame.resume().completed && factories==1 && calls==1,"Pending per-RAM call failed to stop the manager.");
    owner->ram_frame_factory=[&](const auto&,const auto& ram) {
        ++replacement_calls;check(ram==b,"Next physical RAM lookup lost its live argument.");return done;
    };
    (*child->rams->backing)[0]=b;child->rams->count=0;ready=true;
    check(frame.resume().completed && factories==1 && calls==2 && replacement_calls==1,
        "Pending call was rebound/recreated or captured RAM count was replaced.");
    owner=manager();child=info({a});owner->infos=list({child});ModelManagerFrameInvocation missing(owner);
    check(!missing.resume().completed,"Missing virtual implementation completed silently.");
    (*child->rams->backing)[0]=b;owner->infos.reset();owner->system.reset();
    owner->ram_frame_factory=[&](const auto&,const auto& ram) {
        check(ram==a,"Installing a missing implementation reloaded the already captured RAM.");return done;
    };
    check(missing.resume().completed,"Missing implementation could not resume its retained call.");
}
void reached_faults() {
    auto ram=std::make_shared<ModelSceneRam>();auto first=info({ram});auto owner=manager();
    owner->infos=list({first,nullptr});unsigned calls{};
    owner->ram_frame_factory=[&](const auto&,const auto&) {return [&] {++calls;return done();};};
    ModelManagerFrameInvocation frame(owner);
    rejects([&] {frame.resume();},"Null later info completed without its reached dereference.");
    check(calls==1 && frame.faulted(),"Late info failure lost or replayed an earlier callback prefix.");
    rejects([&] {frame.resume();},"Faulted manager invocation retried its visible prefix.");
    ModelManagerFrameInvocation retry(owner);rejects([&] {retry.resume();},"New manager invocation bypassed prior failure.");
    check(calls==1,"Failure recovery replayed a completed RAM.");
    owner=manager();owner->infos=list({std::make_shared<ModelSceneData>()});ModelManagerFrameInvocation wrong(owner);
    rejects([&] {wrong.resume();},"Unsupported info type fabricated a RAM-list field.");
    owner=manager();first=info({});first->rams.reset();owner->infos=list({first});ModelManagerFrameInvocation no_rams(owner);
    rejects([&] {no_rams.resume();},"Inactive info skipped its null RAM-list dereference.");
    owner=manager();first=info({ram});first->rams->count=2;owner->infos=list({first});calls=0;
    owner->ram_frame_factory=[&](const auto&,const auto&) {return [&] {++calls;return done();};};
    ModelManagerFrameInvocation short_backing(owner);
    rejects([&] {short_backing.resume();},"Truncated RAM backing completed.");
    check(calls==1,"Truncated later RAM slot preflighted away the reached callback prefix.");
    owner=manager();first=info({ram});first->rams->backing.reset();owner->infos=list({first});
    ModelManagerFrameInvocation before_binding(owner);
    rejects([&] {before_binding.resume();},"Missing virtual implementation hid the earlier RAM-slot fault.");
    owner=manager();owner->infos=list({info({ram})});unsigned factory_calls{};
    owner->ram_frame_factory=[&](const auto&,const auto&) {++factory_calls;return ModelManagerRamCall{};};
    ModelManagerFrameInvocation malformed(owner);
    rejects([&] {malformed.resume();},"Malformed selected factory silently suspended.");
    rejects([&] {malformed.resume();},"Malformed selected factory was replayed.");
    check(factory_calls==1,"Selected factory side effects ran more than once after failure.");
}
void continuation_and_service_contracts() {
    auto owner=manager();owner->infos=list({info({std::make_shared<ModelSceneRam>()})});bool ready{};
    owner->ram_frame_factory=[&](const auto&,const auto&) {return [&] {return MessengerCallResult{ready,0,{}};};};
    ModelManagerFrameInvocation first(owner);check(!first.resume().completed,"Prepared operation did not suspend.");
    ModelManagerFrameInvocation overlap(owner);rejects([&] {overlap.resume();},"Overlapping manager frame was accepted.");
    ready=true;check(first.resume().completed,"Overlap rejection invalidated the prior owner.");
    owner=manager();owner->infos=list({info({nullptr})});ModelManagerFrameInvocation recursive(owner);
    owner->ram_frame_factory=[&](const auto&,const auto&) {return [&] {
        try {recursive.resume();}catch(const ModelManagerFrameError&) {}return done();
    };};
    rejects([&] {recursive.resume();},"Caught recursive manager entry escaped the outer fault check.");
    owner=manager();owner->infos=list({info({nullptr})});
    {ModelManagerFrameInvocation abandoned(owner);check(!abandoned.resume().completed,"Missing binding failed to suspend.");}
    ModelManagerFrameInvocation after_abandon(owner);
    rejects([&] {after_abandon.resume();},"Abandoned manager prefix was restarted.");
    owner=manager();owner->infos=list({info({nullptr})});unsigned calls{};ready=false;
    owner->ram_frame_factory=[&](const auto&,const auto&) {return [&] {++calls;return MessengerCallResult{ready,0,{}};};};
    auto scheduler=std::make_shared<SerialSchedulerState>();
    scheduler->registered->entries=list({std::make_shared<SerialSchedulerManager>(make_model_manager_update(owner))});
    SerialSchedulerInvocation scheduled(scheduler);
    check(!scheduled.resume().completed && calls==1,"Actual scheduler-to-manager service did not retain a pending RAM call.");
    ready=true;check(scheduled.resume().completed && calls==2 && scheduler->cache->count==0,
        "Scheduler-to-manager resumption failed to complete/clear its original batch.");
    SerialSchedulerInvocation next(scheduler);
    check(next.resume().completed && calls==3,"Reusable manager service did not create the next actual frame.");
}
} // namespace
int main() {
    try {
        defaults_and_gates();signed_counts_and_physical_order();live_reload_and_capture();
        pending_target_and_missing_binding();reached_faults();continuation_and_service_contracts();
        std::cout<<"Passed "<<checks<<" native manager frame checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Manager frame test failed: "<<error.what()<<'\n';return 1;}
}
