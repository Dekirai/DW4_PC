#include "dw4/system_stage.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
using Kind=SystemStageServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const SystemStageError&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(const char* value) {return std::make_shared<const std::string>(value);}
std::shared_ptr<ModelSceneObject> object() {return std::make_shared<ModelSceneData>();}
std::shared_ptr<ModelSceneList> list(std::initializer_list<std::shared_ptr<ModelSceneObject>> values) {
    auto result=std::make_shared<ModelSceneList>("igInfoList");
    result->backing=std::make_shared<ModelSceneList::Storage>(values);
    result->count=result->capacity=static_cast<std::int32_t>(values.size());return result;
}
std::shared_ptr<SystemStageDirectory> directory(const char* name="loaded.igb") {
    auto result=std::make_shared<SystemStageDirectory>();result->filename=text(name);result->infos=list({});return result;
}
struct Fixture {
    std::shared_ptr<ModelSceneSystem> system=std::make_shared<ModelSceneSystem>();
    std::shared_ptr<SystemStageState> state=std::make_shared<SystemStageState>();
    std::shared_ptr<SystemStageServices> services=std::make_shared<SystemStageServices>();
    std::shared_ptr<ModelSceneObject> own_pool=object(),common_pool=object(),stage_pool=object(),string_pool=object();
    std::shared_ptr<SystemStageDirectory> loaded=directory();
    std::vector<SystemStageRequest> selected,called;
    std::optional<Kind> paused;
    bool ready{};
    std::function<void(const SystemStageRequest&)> effect;
    Fixture() {
        system->context=std::make_shared<ModelSceneContext>();
        services->loader=std::make_shared<SystemStageLoader>();services->string_pool=string_pool;
        services->common_pool_handle=object();services->stage_pool_handle=object();
        services->factory=[this](const SystemStageRequest& request) {
            selected.push_back(request);auto result=std::make_shared<SystemStageOperation>();
            const std::weak_ptr<SystemStageOperation> weak=result;
            result->resume=[this,request,weak] {
                called.push_back(request);
                if(paused==request.kind && !ready)return MessengerCallResult{false,0,"Prepared stage operation is pending."};
                const auto result=weak.lock();
                if(request.kind==Kind::object_pool)result->object_result=own_pool;
                if(request.kind==Kind::named_pool)
                    result->object_result=request.handle==services->common_pool_handle ? common_pool:stage_pool;
                if(request.kind==Kind::load_directory)result->directory_result=loaded;
                if(request.kind==Kind::create_string_pool)result->object_result=string_pool;
                if(effect)effect(request);
                return MessengerCallResult{true,27,{}};
            };
            return result;
        };
    }
    std::vector<Kind> order() const {
        std::vector<Kind> result;for(const auto& request:selected)result.push_back(request.kind);return result;
    }
};
void no_stage_and_loading_leaf() {
    Fixture f;f.state->stage_name=text("held-stage");
    SystemStageInvocation none(f.system,f.state);
    check(none.resume().completed && f.selected.empty() && *f.state->stage_name=="held-stage",
        "No-stage branch required services or cleared the existing stage name.");
    check(none.resume().completed,"Completed no-stage invocation replayed.");
    for(unsigned loading=1;loading<256;++loading) {
        Fixture busy;busy.loaded->loading=static_cast<std::uint8_t>(loading);busy.state->boot_directory=busy.loaded;
        SystemStageInvocation wait(busy.system,busy.state);
        check(wait.resume().completed && busy.state->boot_directory==busy.loaded && busy.selected.empty(),
            "Nonzero loading byte did not complete this frame's stage portion without consuming the directory.");
    }
}
void requests_and_pool_selection() {
    for(const char* name:{"ndmw.igb","NDMW.igb","","stage.igb"})for(unsigned use_own:{0,1,255}) {
        Fixture f;f.state->next_stage=text(name);f.state->stage_name=text("old-stage");
        f.services->use_object_pool=static_cast<std::uint8_t>(use_own);
        SystemStageInvocation request(f.system,f.state);
        check(request.resume(f.services).completed,"Complete prepared directory request remained pending.");
        const bool common=std::string(name)=="ndmw.igb";
        const auto pool=use_own ? f.own_pool:common ? f.common_pool:f.stage_pool;
        check(f.services->loader->pool_40==pool && f.services->loader->pool_3c==pool,
            "Case-sensitive request selection or noncanonical pool-mode byte changed.");
        check(f.state->boot_directory==f.loaded && !f.state->next_stage && *f.state->stage_name=="old-stage",
            "Request publication did not preserve current-stage/clear-next ordering.");
        const std::vector<Kind> expected=use_own ?
            std::vector<Kind>{Kind::object_pool,Kind::object_pool,Kind::prepare_loading,Kind::load_directory}:
            std::vector<Kind>{Kind::object_pool,Kind::named_pool,Kind::object_pool,Kind::named_pool,Kind::prepare_loading,Kind::load_directory};
        check(f.order()==expected,"Request reused a pool selection or consumed the new directory in the same update.");
        const auto calls=f.called.size();check(request.resume(f.services).completed && f.called.size()==calls,
            "Completed request invocation replayed its services.");
    }
}
void request_reloads_and_string_pool() {
    Fixture f;f.state->next_stage=text("ndmw.igb");f.services->string_pool.reset();
    auto first_loader=f.services->loader,second_loader=std::make_shared<SystemStageLoader>();
    auto replacement_handle=object();unsigned lookups{};
    f.effect=[&](const SystemStageRequest& request) {
        if(request.kind==Kind::object_pool && ++lookups==1)f.state->next_stage=text("changed-after-compare");
        if(request.kind==Kind::named_pool && lookups==1)f.services->common_pool_handle=replacement_handle;
        if(request.kind==Kind::prepare_loading) {f.services->loader=second_loader;f.state->next_stage=text("changed-before-load");}
        if(request.kind==Kind::load_directory)f.state->next_stage=text("changed-before-clear");
    };
    SystemStageInvocation frame(f.system,f.state);
    check(frame.resume(f.services).completed,"Live-global request fixture failed.");
    check(f.selected[1].handle!=replacement_handle && f.selected[3].handle==replacement_handle,
        "Pool handle was reused instead of independently reloaded for the second assignment.");
    const auto load=std::find_if(f.selected.begin(),f.selected.end(),[](const auto& r){return r.kind==Kind::load_directory;});
    check(load!=f.selected.end() && load->loader==second_loader && *load->filename=="changed-before-load",
        "Load request used the compared filename or old loader capture.");
    check(f.order().back()==Kind::create_string_pool && f.services->string_pool==f.string_pool && !f.state->next_stage,
        "Lazy string-pool creation or live request clearing changed.");
    check(first_loader->pool_40 && first_loader->pool_3c && !second_loader->pool_40,
        "Earlier pool assignments followed a later loader replacement.");
}
void pending_operations_capture() {
    for(Kind kind:{Kind::object_pool,Kind::named_pool,Kind::prepare_loading,Kind::load_directory,Kind::create_string_pool}) {
        Fixture f;f.state->next_stage=text("ndmw.igb");f.services->string_pool.reset();f.paused=kind;
        SystemStageInvocation frame(f.system,f.state);
        check(!frame.resume(f.services).completed,"Prepared request operation did not suspend.");
        const auto selections=f.selected.size();const auto bound=f.services->factory;
        f.services->factory=[](const SystemStageRequest&) -> std::shared_ptr<SystemStageOperation> {
            throw SystemStageError("Unexpected replacement factory.");
        };
        check(!frame.resume(f.services).completed && f.selected.size()==selections,
            "Pending stage operation reselected its factory or repeated prior operations.");
        f.services->factory=bound;f.ready=true;
        check(frame.resume(f.services).completed,"Retained request operation did not resume.");
    }
    Fixture f;f.state->next_stage=text("ndmw.igb");const auto bind=f.services->factory;f.services->factory={};
    SystemStageInvocation missing(f.system,f.state);
    check(!missing.resume(f.services).completed && f.state->next_stage && !f.state->boot_directory,
        "Missing reached object-pool lookup became fabricated success.");
    f.services->factory=bind;check(missing.resume(f.services).completed,"Installing a missing stage implementation did not resume.");
}
void completion_live_lists_and_owners() {
    Fixture f;auto a=object(),b=object(),c=object(),d=object();
    auto held=list({a,b,c});f.loaded->infos=held;f.state->boot_directory=f.loaded;
    auto first_context=f.system->context,second_context=std::make_shared<ModelSceneContext>();
    auto replacement=directory("replacement.igb"),last=directory("last.igb");
    auto final_loader=std::make_shared<SystemStageLoader>();unsigned registered{};
    f.effect=[&](const SystemStageRequest& request) {
        if(request.kind==Kind::register_info) {
            if(++registered==1) {
                held->count=2;held->backing=std::make_shared<ModelSceneList::Storage>(ModelSceneList::Storage{c,d});
                f.loaded->infos=list({});f.system->context=second_context;
            }else f.state->boot_directory=replacement;
        }
        if(request.kind==Kind::release_claim) {f.state->boot_directory=last;f.services->loader=final_loader;}
    };
    SystemStageInvocation frame(f.system,f.state);
    check(frame.resume(f.services).completed,"Prepared completed-directory fixture failed.");
    check(f.order()==std::vector<Kind>{Kind::register_info,Kind::register_info,Kind::release_claim,Kind::finish_loading},
        "Directory completion used captured count, replaced info-list owner, or incorrect cleanup order.");
    check(f.selected[0].info==a && f.selected[1].info==d && f.selected[0].context==first_context &&
        f.selected[1].context==second_context,"Info registration missed live backing/context reloads.");
    check(*f.selected[2].filename=="replacement.igb" && f.selected[3].loader==final_loader && !f.state->boot_directory,
        "Completion failed to reload directory name, directory clear owner, or final loader.");
    for(int count:{-2,0,1}) {
        Fixture g;g.loaded->infos=list({{}});g.loaded->infos->count=count;g.state->boot_directory=g.loaded;
        SystemStageInvocation call(g.system,g.state);check(call.resume(g.services).completed,"Signed/null directory fixture failed.");
        check(g.selected.size()==(count>0 ? 3u:2u),"Signed info count or null physical info filtering changed.");
        if(count>0)check(g.selected.front().kind==Kind::register_info && !g.selected.front().info,
            "A null physical info was not forwarded to its actual registration operation.");
    }
}
void completion_pending_and_failure_prefixes() {
    for(Kind kind:{Kind::register_info,Kind::release_claim,Kind::finish_loading}) {
        Fixture f;auto info=object();f.loaded->infos=list({info});f.state->boot_directory=f.loaded;f.paused=kind;
        SystemStageInvocation frame(f.system,f.state);
        check(!frame.resume(f.services).completed,"Prepared completion service did not pause.");
        check(bool(f.state->boot_directory)==(kind!=Kind::finish_loading),"Directory clear crossed its original service boundary.");
        const auto count=f.selected.size();check(!frame.resume(f.services).completed && f.selected.size()==count,
            "Completion suspension replayed an entered service.");
        f.ready=true;check(frame.resume(f.services).completed,"Completion service failed to resume.");
    }
    Fixture short_list;short_list.loaded->infos=list({object()});short_list.loaded->infos->count=2;
    short_list.state->boot_directory=short_list.loaded;
    SystemStageInvocation truncated(short_list.system,short_list.state);
    rejects([&]{truncated.resume(short_list.services);},"Truncated directory backing fabricated an info.");
    check(short_list.selected.size()==1 && short_list.state->boot_directory==short_list.loaded && short_list.state->faulted(),
        "Late physical failure lost the prior registration or ran cleanup.");

    Fixture pool_failure;pool_failure.services->use_object_pool=1;pool_failure.state->next_stage=text("stage");
    pool_failure.services->loader->pool_40=object();const auto references=pool_failure.own_pool.use_count();
    {SystemStageInvocation frame(pool_failure.system,pool_failure.state);
        rejects([&]{frame.resume(pool_failure.services);},"Unknown final old-pool destructor was silently executed.");}
    check(pool_failure.services->loader->pool_40 && pool_failure.own_pool.use_count()==references+1,
        "Pool failure undid its retained incoming-owner prefix.");

    Fixture directory_failure;directory_failure.state->next_stage=text("stage");directory_failure.state->boot_directory=directory();
    const auto incoming_references=directory_failure.loaded.use_count();
    {SystemStageInvocation frame(directory_failure.system,directory_failure.state);
        rejects([&]{frame.resume(directory_failure.services);},"Unknown final old-directory destructor was silently executed.");}
    check(directory_failure.state->next_stage && directory_failure.state->boot_directory!=directory_failure.loaded &&
        directory_failure.loaded.use_count()==incoming_references+1,"Directory replacement fault lost retain-before-release ordering.");
}
void continuation_contracts() {
    Fixture f;f.state->next_stage=text("stage");f.paused=Kind::prepare_loading;
    SystemStageInvocation frame(f.system,f.state);check(!frame.resume(f.services).completed,"Overlap fixture failed to pause.");
    SystemStageInvocation overlap(f.system,f.state);
    rejects([&]{overlap.resume(f.services);},"Two stage invocations acquired the same owner.");
    check(!f.state->faulted(),"Rejected overlap faulted the original pending invocation.");
    f.ready=true;check(frame.resume(f.services).completed,"Original stage owner could not resume after rejected overlap.");

    Fixture abandoned;abandoned.state->next_stage=text("stage");abandoned.services->factory={};
    {SystemStageInvocation partial(abandoned.system,abandoned.state);check(!partial.resume(abandoned.services).completed,
        "Abandonment fixture did not reach a missing service.");}
    check(abandoned.state->faulted(),"Abandoned stage could replay its prefix.");
    Fixture recursive;recursive.state->next_stage=text("stage");SystemStageInvocation call(recursive.system,recursive.state);
    recursive.effect=[&](const SystemStageRequest&) {try {call.resume(recursive.services);}catch(const SystemStageError&) {}};
    rejects([&]{call.resume(recursive.services);},"Caught recursive service completed its outer stage operation.");
    check(recursive.state->faulted() && recursive.state->next_stage,"Recursive failure cleared the outstanding request.");
}
}
int main() {
    try {
        no_stage_and_loading_leaf();requests_and_pool_selection();request_reloads_and_string_pool();
        pending_operations_capture();completion_live_lists_and_owners();completion_pending_and_failure_prefixes();continuation_contracts();
        std::cout<<"Passed "<<checks<<" native system stage checks.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"System stage test failed: "<<error.what()<<'\n';return 1;}
}
