#include "dw4/context_info.hpp"
#include "dw4/system_stage.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& f,const char* reason) {
    ++checks;try {f();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<ModelSceneObject> object() {return std::make_shared<ModelSceneData>();}
std::shared_ptr<ModelSceneList> list(std::initializer_list<std::shared_ptr<ModelSceneObject>> values={}) {
    auto result=std::make_shared<ModelSceneList>();
    result->backing=std::make_shared<ModelSceneList::Storage>(values);
    result->count=result->capacity=static_cast<std::int32_t>(values.size());return result;
}
std::shared_ptr<SerialSchedulerRegistry> registry(std::initializer_list<std::shared_ptr<ModelSceneObject>> values={}) {
    auto result=std::make_shared<SerialSchedulerRegistry>();result->entries=list(values);return result;
}
std::shared_ptr<ModelSceneContext> context() {
    auto result=std::make_shared<ModelSceneContext>();result->managers=registry();result->infos=list();return result;
}
std::shared_ptr<SerialSchedulerManager> manager(std::function<MessengerCallResult(const std::shared_ptr<ModelSceneObject>&)> call) {
    auto result=std::make_shared<SerialSchedulerManager>();
    result->info_factory=[call](const auto& info) {return [call,info] {return call(info);};};return result;
}
void ordered_acceptance_and_occurrences() {
    for(std::int32_t value:{0,256,65536,-256,1,255,-1,257}) {
        auto owner=context();const auto info=object();unsigned first{},second{};
        const auto a=manager([&](const auto& input) {
            check(input==info,"Registration changed the captured info argument.");++first;return MessengerCallResult{true,std::bit_cast<std::uint32_t>(value),{}};
        });
        const auto b=manager([&](const auto&) {++second;return MessengerCallResult{true,1,{}};});
        owner->managers=registry({a,b});ContextInfoInvocation call(owner,info);
        check(call.resume().completed && first==1 && second==((value&255)==0 ? 1u:0u),
            "Manager search did not stop on precisely the first nonzero low byte.");
        check(owner->infos->count==1 && owner->infos->backing->at(0)==info && call.appended_backing()==owner->infos->backing,
            "Accepted info was not appended to the actual application list.");
        check(call.resume().completed && owner->infos->count==1 && first==1,"Completed registration replayed.");
    }
    auto owner=context();const auto info=object();
    owner->managers->entries->backing.reset();owner->managers->entries->capacity=-7;
    ContextInfoInvocation first(owner,info),second(owner,info),null(owner,{});
    check(first.resume().completed && second.resume().completed && null.resume().completed,
        "Zero manager count read its unused capacity/backing or rejected a null info.");
    check(owner->infos->count==3 && owner->infos->backing->at(0)==info && owner->infos->backing->at(1)==info &&
        !owner->infos->backing->at(2),"Info append filtered nulls or deduplicated physical occurrences.");
}
void live_registry_and_retained_call() {
    auto owner=context();const auto info=object();std::vector<int> visits;
    const auto skipped=manager([&](const auto&) {visits.push_back(9);return MessengerCallResult{true,1,{}};});
    const auto last=manager([&](const auto&) {visits.push_back(2);return MessengerCallResult{true,1,{}};});
    const auto first=manager([&](const auto&) {
        visits.push_back(1);owner->managers=registry({skipped,last});return MessengerCallResult{true,0,{}};
    });
    owner->managers=registry({first});ContextInfoInvocation changing(owner,info);
    check(changing.resume().completed && visits==std::vector<int>{1,2},"Manager registry/count were captured across a declined callback.");

    auto waiting=context();auto selected=std::make_shared<SerialSchedulerManager>();waiting->managers=registry({selected});
    ContextInfoInvocation call(waiting,info);check(!call.resume().completed,"Missing manager handler was treated as a declined info.");
    waiting->managers=registry({skipped});unsigned factories{},calls{};bool ready{};
    selected->info_factory=[&](const auto& input) {
        ++factories;check(input==info,"Missing handler retry changed its info.");
        return ModelManagerRamCall{[&] {++calls;return MessengerCallResult{ready,1,"Prepared registration is waiting."};}};
    };
    check(!call.resume().completed && factories==1 && calls==1,"Retry did not enter the retained manager.");
    selected->info_factory=skipped->info_factory;ready=true;
    check(call.resume().completed && factories==1 && calls==2 && visits==std::vector<int>{1,2},
        "Pending handler was redirected by provider or registry replacement.");
    check(waiting->infos->count==1,"Retained handler completion did not publish exactly one info.");
}
void unsigned_count_and_reached_failures() {
    auto owner=context();const auto info=object();unsigned visits{};
    auto accepted=manager([&](const auto&) {++visits;return MessengerCallResult{true,1,{}};});
    owner->managers=registry({accepted});owner->managers->entries->count=-1;owner->managers->entries->capacity=0;
    ContextInfoInvocation succeeds(owner,info);
    check(succeeds.resume().completed && visits==1,"Negative raw manager count was treated as a signed empty list.");
    accepted->info_factory=manager([&](const auto&) {++visits;return MessengerCallResult{true,0,{}};})->info_factory;
    ContextInfoInvocation fails(owner,info);rejects([&] {fails.resume();},"Unsigned traversal did not reach its missing physical second manager.");
    check(fails.faulted() && visits==2 && owner->infos->count==1,"Failed traversal undid its prefix or appended prematurely.");
    rejects([&] {fails.resume();},"Faulted registration restarted its manager prefix.");
    for(unsigned kind=0;kind<5;++kind) {
        auto broken=context();
        if(kind==0)broken.reset();
        else if(kind==1)broken->managers.reset();
        else if(kind==2)broken->managers->entries.reset();
        else broken->managers=registry({kind==3 ? nullptr:object()});
        ContextInfoInvocation invalid(broken,info);rejects([&] {invalid.resume();},"Malformed context/manager was silently skipped.");
    }
}
void nested_registration_and_live_destination() {
    auto owner=context();const auto outer=object(),inner=object();auto old_infos=owner->infos;
    unsigned calls{};auto new_infos=list();
    owner->managers=registry({manager([&](const auto& info) {
        ++calls;
        if(info==outer) {
            owner->infos=new_infos;ContextInfoInvocation nested(owner,inner);
            check(nested.resume().completed,"Distinct nested registration was blocked by a context-wide call lock.");
        }
        return MessengerCallResult{true,1,{}};
    })});
    ContextInfoInvocation call(owner,outer);
    check(call.resume().completed && calls==2 && old_infos->count==0 && new_infos->count==2 &&
        new_infos->backing->at(0)==inner && new_infos->backing->at(1)==outer,
        "Nested registration or live destination publication lost original call order.");
}
void append_failure_prefixes_and_raw_overwrite() {
    for(unsigned profile=0;profile<4;++profile) {
        auto owner=context();auto info=object();std::weak_ptr<ModelSceneObject> retained=info;
        auto destination=owner->infos;
        if(profile==0)owner->infos.reset();
        if(profile==1) {destination->capacity=1;destination->backing.reset();}
        if(profile==2) {destination->count=-1;destination->capacity=4;}
        if(profile==3)destination->capacity=-1;
        const auto old_backing=destination->backing;
        {
            ContextInfoInvocation call(owner,info);rejects([&] {call.resume();},"Malformed append unexpectedly completed.");
            if(profile==1)check(destination->count==1,"Fast append did not publish count before the missing backing fault.");
            if(profile==2)check(destination->count==0,"Negative fast count did not wrap before the reached slot failure.");
            if(profile==3)check(destination->backing!=old_backing && destination->capacity==-1 && destination->count==0,
                "Growth copy failure did not preserve exactly the new-backing publication prefix.");
        }
        info.reset();check(!retained.expired(),"Destroying a failed call released its already acquired original reference.");
        owner.reset();check(retained.expired(),"Retained failed append reference escaped its context lifetime.");
    }
    auto owner=context();auto old=object();std::weak_ptr<ModelSceneObject> held=old;
    owner->infos=list({old});owner->infos->count=0;old.reset();
    {ContextInfoInvocation call(owner,{});check(call.resume().completed,"Raw spare overwrite did not complete.");}
    check(!held.expired() && !owner->infos->backing->at(0),"Raw overwrite incorrectly released its old spare reference.");
    owner.reset();check(held.expired(),"Retained overwritten reference survived its context.");
}
void growth_and_recursion() {
    auto owner=context();const auto first=object(),second=object(),incoming=object();
    owner->infos=list({first,second});const auto old=owner->infos->backing;
    ContextInfoInvocation growth(owner,incoming);
    check(growth.resume().completed && owner->infos->count==3 && owner->infos->capacity==4 && owner->infos->backing!=old &&
        old->size()==2 && old->at(0)==first && owner->infos->backing->at(1)==second && owner->infos->backing->at(2)==incoming,
        "Growth did not preserve old backing aliases, capacity contents and append position.");
    auto recursive_owner=context();ContextInfoInvocation recursive(recursive_owner,incoming);
    recursive_owner->managers=registry({manager([&](const auto&) {
        rejects([&] {recursive.resume();},"Recursive resume was permitted.");return MessengerCallResult{true,1,{}};
    })});
    rejects([&] {recursive.resume();},"Caught recursive failure allowed outer publication.");
    check(recursive.faulted() && recursive_owner->infos->count==0,"Recursive fault published the info.");
    recursive_owner->managers=registry();ContextInfoInvocation independent(recursive_owner,first);
    check(independent.resume().completed,"A local invocation failure invented a context-wide original gate.");
}
void stage_registration_adapter() {
    auto owner=context();const auto first=object(),second=object();unsigned calls{},releases{},finishes{};bool ready{};
    owner->managers=registry({manager([&](const auto& info) {
        ++calls;return MessengerCallResult{info!=second || ready,1,"Prepared second info waits."};
    })});
    auto system=std::make_shared<ModelSceneSystem>();system->context=owner;
    auto state=std::make_shared<SystemStageState>();auto loaded=std::make_shared<SystemStageDirectory>();
    loaded->filename=std::make_shared<const std::string>("stage.igb");loaded->infos=list({first,second,first});state->boot_directory=loaded;
    auto services=std::make_shared<SystemStageServices>();services->loader=std::make_shared<SystemStageLoader>();
    services->factory=[&](const SystemStageRequest& request) {
        auto operation=std::make_shared<SystemStageOperation>();
        if(request.kind==SystemStageServiceKind::register_info)
            operation->resume=make_context_info_registration(request.context,request.info);
        else if(request.kind==SystemStageServiceKind::release_claim)
            operation->resume=[&] {++releases;return MessengerCallResult{true,0,{}};};
        else if(request.kind==SystemStageServiceKind::finish_loading)
            operation->resume=[&] {++finishes;return MessengerCallResult{true,0,{}};};
        else throw std::runtime_error("Unexpected stage endpoint in registration fixture.");
        return operation;
    };
    SystemStageInvocation stage(system,state);
    check(!stage.resume(services).completed && owner->infos->count==1 && calls==2 && releases==0,
        "Stage registration did not preserve its first info before a pending second handler.");
    ready=true;
    check(stage.resume(services).completed && owner->infos->count==3 && calls==4 && releases==1 && finishes==1 &&
        owner->infos->backing->at(0)==first && owner->infos->backing->at(1)==second && owner->infos->backing->at(2)==first,
        "Actual context registration did not resume through repeated stage exports in authored order.");
    check(stage.resume(services).completed && owner->infos->count==3 && calls==4,"Completed stage repeated context registrations.");
}
}
int main() {
    try {
        ordered_acceptance_and_occurrences();live_registry_and_retained_call();unsigned_count_and_reached_failures();
        nested_registration_and_live_destination();append_failure_prefixes_and_raw_overwrite();growth_and_recursion();stage_registration_adapter();
        std::cout<<"Context info registration checks: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
