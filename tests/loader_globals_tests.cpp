#include "dw4/loader_globals.hpp"
#include "dw4/igb_runtime_bootstrap.hpp"

#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const LoaderGlobalsError&) {return;}throw std::runtime_error(reason);
}
void canonical_owner_and_reads() {
    const auto bootstrap=make_original_ndmw_runtime_bootstrap();auto services=bootstrap.type_services();
    const auto core=*services->singleton;check(!core->loader,"Metadata bootstrap invented a constructed loader field.");
    const auto first=std::make_shared<SystemStageLoader>(),second=std::make_shared<SystemStageLoader>();
    auto direct=std::make_shared<SystemStageServices>();direct->loader=second;
    core->enabled=0;core->types.reset();core->loader=first;
    first->option_48=0x89abcdefu;second->option_48=0x12345678u;
    LoaderGlobalInvocation call;
    check(call.resume(services).completed && call.result()==first,"Getter used direct80562104, enabled flag or metadata list instead of core+40.");
    check(first->option_48==0x89abcdefu && second->option_48==0x12345678u && direct->loader==second,
        "Getter changed the loader or the separate direct global.");
    core->loader=second;services->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    check(call.resume().completed && call.result()==first,"Completed getter reloaded mutable owner/global state.");
    services->singleton=core;LoaderGlobalInvocation next;
    check(next.resume(services).completed && next.result()==second,"Fresh getter reused a stale prior result.");
    core->loader=std::shared_ptr<SystemStageLoader>{};LoaderGlobalInvocation null_field;
    check(null_field.resume(services).completed && !null_field.result(),"Represented null loader was treated as missing initialization.");
}
void missing_and_null_state() {
    auto services=std::make_shared<IgbRuntimeTypesServices>();LoaderGlobalInvocation call;
    check(!call.resume().completed && !call.resume(services).completed,"Missing core global became a represented null pointer.");
    rejects([&]{call.result();},"Incomplete getter exposed a fabricated result.");
    const auto captured=std::make_shared<IgbRuntimeTypeRegistry>(),replacement=std::make_shared<IgbRuntimeTypeRegistry>();
    const auto first=std::make_shared<SystemStageLoader>(),second=std::make_shared<SystemStageLoader>();
    services->singleton=captured;check(!call.resume(services).completed,"Unrepresented core+40 was treated as null.");
    replacement->loader=second;services->singleton=replacement;
    check(!call.resume(services).completed,"Field-read suspension reselected the core global.");
    captured->loader=first;
    check(call.resume().completed && call.result()==first,"Field-read resume lost its captured original core.");
    LoaderGlobalInvocation invalid;services->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{invalid.resume(services);},"Represented null core completed as a null loader.");
    check(invalid.faulted(),"Null core dereference did not poison the invocation.");
    services->singleton=replacement;
    rejects([&]{invalid.resume(services);},"Faulted getter restarted on a new global owner.");
}
void dependency_factory() {
    auto services=std::make_shared<IgbRuntimeTypesServices>();unsigned delegated{};
    const auto sentinel=std::make_shared<IgbDependenciesOperation>();
    auto factory=make_native_loader_globals_factory(services,[&](const IgbDependenciesCall&) {++delegated;return sentinel;});
    IgbDependenciesCall request;request.kind=IgbDependenciesServiceKind::load_dependency;
    check(factory(request)==sentinel && delegated==1,"Getter adapter consumed another dependency operation.");
    request.kind=IgbDependenciesServiceKind::loader;auto operation=factory(request);
    check(operation && operation!=sentinel && !operation->resume().completed && delegated==1,
        "Missing core getter delegated to a successful fallback or fabricated a result.");
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();services->singleton=core;
    check(!operation->resume().completed,"Getter adapter collapsed unavailable core+40 to null.");
    const auto loader=std::make_shared<SystemStageLoader>();core->loader=loader;
    check(operation->resume().completed && operation->loader_result==loader,"Getter adapter did not publish the canonical loader identity.");
    core->loader=std::shared_ptr<SystemStageLoader>{};
    check(operation->resume().completed && operation->loader_result==loader,"Completed adapter operation replayed the getter.");
    auto next=factory(request);check(next->resume().completed && !next->loader_result,"Fresh adapter did not return represented null.");
    auto missing=make_native_loader_globals_factory(services);request.kind=IgbDependenciesServiceKind::intern_name;
    check(!missing(request),"Getter adapter fabricated an unbound string operation.");
}
}
int main(int argc,char**) {
    try {
        if(argc!=1)throw std::runtime_error("Usage: loader_globals_tests");
        canonical_owner_and_reads();missing_and_null_state();dependency_factory();
        std::cout<<"loader_globals_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
