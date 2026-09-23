#include "dw4/igb_thread_dependencies.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbThreadDependenciesError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
bool thread_list(IgbThreadDependencyEntry entry){
    if(entry!=IgbThreadDependencyEntry::thread_list && entry!=IgbThreadDependencyEntry::semaphore)fail("Unknown ThreadManager dependency getter.");
    return entry==IgbThreadDependencyEntry::thread_list;
}
}
struct IgbThreadDependencyLookupInvocation::Impl {
    enum class P {read,handle,pool,construct,publish,reload,done};P phase=P::read;
    IgbThreadDependencyEntry entry{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbThreadDependenciesServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRuntimeMetaType> result;
    std::shared_ptr<ModelSceneObject> pool;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    bool running{},fault{};
};
IgbThreadDependencyLookupInvocation::IgbThreadDependencyLookupInvocation(IgbThreadDependencyEntry entry):impl_(std::make_unique<Impl>()){
    thread_list(entry);impl_->entry=entry;
}
IgbThreadDependencyLookupInvocation::~IgbThreadDependencyLookupInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.result)s.owner->retained.push_back(s.result);if(s.pool)s.owner->retained.push_back(s.pool);
    if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
}}
bool IgbThreadDependencyLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadDependencyLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbThreadDependencyLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete ThreadManager dependency getter has no result.");return impl_->result;
}
IgbBootstrapPointer IgbThreadDependencyLookupInvocation::allocation()const{
    const auto metadata=result();if(!metadata)return {};
    if(impl_->allocation.storage)return impl_->allocation;
    IgbBootstrapPointer found;
    if(impl_->owner)for(const auto& pointer:impl_->owner->allocations){
        if(!pointer.storage)continue;
        for(const auto& binding:pointer.storage->bindings)if(binding.offset==pointer.offset && binding.metadata==metadata){
            if(found.storage && (found.storage!=pointer.storage || found.offset!=pointer.offset))fail("Dependency metadata has conflicting physical allocations.");
            if(igb_bootstrap_metadata(pointer)!=metadata)fail("Dependency metadata changed its canonical physical binding.");found=pointer;
        }
    }return found;
}
MessengerCallResult IgbThreadDependencyLookupInvocation::resume(std::shared_ptr<IgbThreadDependenciesServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("ThreadManager dependency getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("ThreadManager dependency getter was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){if(!services)return pending("Dependency getter requires its canonical metadata cell.");
            s.slot=thread_list(s.entry)?services->thread_list:services->semaphore;
            if(!s.slot)return pending("Dependency getter requires its canonical metadata cell.");}
        if(!*s.slot)return pending("Dependency getter requires represented canonical metadata.");
        s.phase=**s.slot?P::reload:P::handle;break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Dependency getter requires metadata pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::construct;break;}
    case P::construct:
        if(!s.factory){if(!services || !services->metadata)return pending("Dependency getter requires actual metadata factory29E64.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);}
        {const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status;}
        s.result=s.factory->result();s.allocation=s.factory->allocation();
        if(s.owner && s.allocation.storage)s.owner->allocations.push_back(s.allocation);
        s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::reload:
        if(!*s.slot)return pending("Dependency getter requires its reloaded canonical metadata.");
        s.result=**s.slot;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbBootstrapCallbackAppendInvocation::Impl {
    enum class P {core,list,append,done};P phase=P::core;
    std::uint32_t function{};std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::unique_ptr<IgbRegistrationCallbackAppendInvocation> append;
    bool running{},fault{};
};
IgbBootstrapCallbackAppendInvocation::IgbBootstrapCallbackAppendInvocation(std::uint32_t function):impl_(std::make_unique<Impl>()){impl_->function=function;}
IgbBootstrapCallbackAppendInvocation::~IgbBootstrapCallbackAppendInvocation()=default;
bool IgbBootstrapCallbackAppendInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbBootstrapCallbackAppendInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbBootstrapCallbackAppendInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Bootstrap callback append requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Bootstrap callback append was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::core:
        if(!services || !services->runtime || !services->runtime->singleton)return pending("Bootstrap callback append requires ArkCore80562100.");
        s.core=*services->runtime->singleton;s.phase=P::list;break;
    case P::list:
        if(!s.core)fail("Bootstrap callback append dereferences null ArkCore at+28.");
        if(!s.core->bootstrap_callbacks)return pending("Bootstrap callback append requires captured ArkCore list+28.");
        s.append=std::make_unique<IgbRegistrationCallbackAppendInvocation>(*s.core->bootstrap_callbacks,s.function);s.phase=P::append;break;
    case P::append:{const auto status=s.append->resume(services);check();if(!status.completed)return status;
        s.append.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
