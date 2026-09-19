#include "dw4/loader_globals.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw LoaderGlobalsError(message);}
}
struct LoaderGlobalInvocation::Impl {
    std::shared_ptr<IgbRuntimeTypeRegistry> owner;
    std::shared_ptr<SystemStageLoader> result;
    bool captured{},done{},fault{};
};
LoaderGlobalInvocation::LoaderGlobalInvocation():impl_(std::make_unique<Impl>()) {}
LoaderGlobalInvocation::~LoaderGlobalInvocation()=default;
bool LoaderGlobalInvocation::completed() const noexcept {return impl_->done;}
bool LoaderGlobalInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<SystemStageLoader> LoaderGlobalInvocation::result() const {
    if(!completed())fail("Incomplete original loader getter has no result.");return impl_->result;
}
MessengerCallResult LoaderGlobalInvocation::resume(std::shared_ptr<IgbRuntimeTypesServices> services) {
    auto& s=*impl_;
    if(s.fault)fail("Original loader getter requires an unfaulted invocation.");
    if(s.done)return {true,0,{}};
    try {
        if(!s.captured) {
            if(!services || !services->singleton)
                return {false,0,"Original loader getter requires igArkCore global80562100."};
            s.owner=*services->singleton;s.captured=true;
        }
        if(!s.owner)fail("Original loader getter dereferences null igArkCore at +40.");
        if(!s.owner->loader)return {false,0,"Original loader getter requires captured igArkCore field+40."};
        s.result=*s.owner->loader;s.done=true;return {true,0,{}};
    }catch(...) {s.fault=true;throw;}
}
IgbDependenciesFactory make_native_loader_globals_factory(
    std::shared_ptr<IgbRuntimeTypesServices> services,IgbDependenciesFactory remaining) {
    return [services=std::move(services),remaining=std::move(remaining)](const IgbDependenciesCall& call) {
        if(call.kind!=IgbDependenciesServiceKind::loader)
            return remaining?remaining(call):std::shared_ptr<IgbDependenciesOperation>{};
        auto operation=std::make_shared<IgbDependenciesOperation>();
        const auto weak=std::weak_ptr(operation);auto getter=std::make_shared<LoaderGlobalInvocation>();
        operation->resume=[services,getter,weak] {
            auto output=weak.lock();if(!output)fail("Original loader getter operation owner expired.");
            const auto progress=getter->resume(services);
            if(progress.completed)output->loader_result=getter->result();
            return progress;
        };
        return operation;
    };
}
} // namespace dw4
