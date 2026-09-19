#include "dw4/native_stage_services.hpp"

#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& f,const char* reason) {
    ++checks;try {f();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<ModelSceneObject> object() {return std::make_shared<ModelSceneData>();}
void cached_stage_lifecycle() {
    const auto first=object(),second=object();const auto name=std::make_shared<const std::string>("prepared.igb");
    auto context=std::make_shared<ModelSceneContext>();context->managers=std::make_shared<SerialSchedulerRegistry>();
    context->managers->entries=std::make_shared<ModelSceneList>();context->infos=std::make_shared<ModelSceneList>();
    unsigned accepted{},prepared{},finished{},pool_lookups{};bool ready{};
    const auto manager=std::make_shared<SerialSchedulerManager>();
    manager->info_factory=[&](const auto& info) {return ModelManagerRamCall{[&,info] {
        if(info==second && !ready)return MessengerCallResult{false,0,"Prepared second info is pending."};
        ++accepted;return MessengerCallResult{true,1,{}};
    }};};
    context->managers->entries->append(manager);
    auto system=std::make_shared<ModelSceneSystem>();system->context=context;
    auto state=std::make_shared<SystemStageState>();state->next_stage=name;
    auto globals=std::make_shared<SystemStageServices>();globals->loader=std::make_shared<SystemStageLoader>();
    globals->use_object_pool=1;globals->string_pool=object();
    const auto loader=globals->loader;loader->directories=std::make_shared<LoaderDirectoryList>();
    auto loaded=std::make_shared<SystemStageDirectory>();loaded->filename=name;loaded->loader=loader;
    loaded->infos=std::make_shared<ModelSceneList>();
    loaded->infos->append(first);loaded->infos->append(second);loaded->infos->append(first);
    loader->directories->storage->push_back(loaded);loader->directories->count=loader->directories->capacity=1;
    auto services=std::make_shared<LoaderRequestServices>();services->globals=globals;
    services->directory=std::make_shared<LoaderDirectoryServices>();
    services->directory->filename_field=std::make_shared<LoaderDirectoryNameField>();
    std::weak_ptr<LoaderRequestServices> weak_services=services;
    globals->factory=make_native_stage_factory(services,[&](const SystemStageRequest& request) {
        auto operation=std::make_shared<SystemStageOperation>();
        operation->resume=[&,kind=request.kind] {
            if(kind==SystemStageServiceKind::object_pool)++pool_lookups;
            else if(kind==SystemStageServiceKind::prepare_loading)++prepared;
            else if(kind==SystemStageServiceKind::finish_loading)++finished;
            else throw std::runtime_error("Unexpected prepared stage lifecycle operation.");
            return MessengerCallResult{true,0,{}};
        };
        return operation;
    });
    {
        SystemStageInvocation request(system,state);
        check(request.resume(globals).completed && state->boot_directory==loaded && !state->next_stage &&
            loaded->claim_count==1 && context->infos->count==0 && pool_lookups==2 && prepared==1 && finished==0,
            "Native stage request failed to acquire the actual cached directory without early registration.");
    }
    {
        SystemStageInvocation consume(system,state);
        check(!consume.resume(globals).completed && context->infos->count==1 && accepted==1 && loaded->claim_count==1 &&
            loader->directories->count==1 && state->boot_directory==loaded,
            "Pending original info registration released or repeated the cached directory.");
        ready=true;
        check(consume.resume(globals).completed && context->infos->count==3 && accepted==3 && loaded->claim_count==0 &&
            loader->directories->count==0 && !loader->directories->storage->at(0) && !state->boot_directory && finished==1,
            "Native stage did not register all occurrences, release its real claim and remove the cache entry.");
        check(context->infos->backing->at(0)==first && context->infos->backing->at(1)==second && context->infos->backing->at(2)==first,
            "Stage adapter changed the canonical exported owners or occurrence order.");
        check(consume.resume(globals).completed && finished==1 && accepted==3,"Completed stage adapter replayed native effects.");
    }
    services.reset();check(weak_services.expired(),"Native stage factory created a globals/loader-services ownership cycle.");
}
void unavailable_services() {
    const auto factory=make_native_stage_factory({});
    SystemStageRequest request;request.kind=SystemStageServiceKind::prepare_loading;
    const auto pending=factory(request);
    check(pending && !pending->resume().completed,"Absent original loader preparation was reported as successful.");
    request.kind=SystemStageServiceKind::load_directory;
    rejects([&] {factory(request);},"Expired loader services were silently replaced by a loaded directory.");
    request.kind=SystemStageServiceKind::release_claim;
    rejects([&] {factory(request);},"Expired loader services silently released a directory.");
    request.kind=SystemStageServiceKind::register_info;
    request.context=std::make_shared<ModelSceneContext>();request.context->managers=std::make_shared<SerialSchedulerRegistry>();
    request.context->managers->entries=std::make_shared<ModelSceneList>();request.context->infos=std::make_shared<ModelSceneList>();
    request.info=object();const auto registration=factory(request);
    check(registration->resume().completed && request.context->infos->count==1,
        "Native context registration unnecessarily required loader services.");
}
}
int main() {
    try {cached_stage_lifecycle();unavailable_services();std::cout<<"Native stage service checks: "<<checks<<'\n';return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
