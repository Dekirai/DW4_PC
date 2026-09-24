#include "dw4/igb_named_type_registration.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbNamedTypeRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using E=IgbNamedTypeKind;using L=IgbNamedTypeLookup;
constexpr std::array profiles{
    IgbNamedTypeProfile{"igNamedObject",0x80561744,0x80029d30,0x80029d58,0,0x0c,0x80066b08,0x800237d0,0x80029dc8,0x80029ca8,0x80029de8,0x80029c6c,0x80029c30,0x80023cf4},
    IgbNamedTypeProfile{"igThread",0x80561504,0x80023c40,0x80023c68,1,0x30,0x80029d58,0x80023cf4,0x80023cd4,0,0x80023cfc,0x80023c04,0x80023bc8,0},
    IgbNamedTypeProfile{"igSemaphore",0x805615ec,0x80026098,0x800260c0,1,0x0c,0x80029d58,0x80023cf4,0x8002612c,0,0x8002614c,0x8002605c,0x80026020,0}
};
std::optional<E> registration_function(std::uint32_t function,bool wrapper){
    for(std::size_t i=0;i<profiles.size();++i)if(function==(wrapper?profiles[i].wrapper:profiles[i].body))return static_cast<E>(i);return {};
}
struct Lookup {E kind;L mode;};
std::optional<Lookup> lookup_function(std::uint32_t function){
    if(!function)return {};
    if(function==0x800940b8)return Lookup{E::named_object,L::plain};
    for(std::size_t i=0;i<profiles.size();++i){const auto& p=profiles[i];const auto kind=static_cast<E>(i);
        if(function==p.full_getter || function==p.field_callback)return Lookup{kind,L::fully_registered};
        if(function==p.plain_getter)return Lookup{kind,L::plain};
        if(function==p.lazy_getter)return Lookup{kind,L::allocate_if_null};
    }return {};
}
}
const IgbNamedTypeProfile& igb_named_type_profile(E kind){
    const auto index=static_cast<std::size_t>(kind);if(index>=profiles.size())fail("Unknown named-type registration class.");return profiles[index];
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_named_type_metadata_slot(const std::shared_ptr<IgbNamedTypeRegistrationServices>& services,E kind){
    igb_named_type_profile(kind);if(!services)return {};
    if(kind==E::named_object)return services->named_metadata;
    if(kind==E::thread)return services->thread_metadata;
    return services->dependencies?services->dependencies->semaphore:nullptr;
}
IgbTypeRegistrationArguments igb_named_type_registration_arguments(const std::shared_ptr<IgbNamedTypeRegistrationServices>& services,E kind){
    const auto& p=igb_named_type_profile(kind);IgbTypeRegistrationArguments args;
    args.metadata_slot=igb_named_type_metadata_slot(services,kind);if(!args.metadata_slot)fail("Named-type registration requires its canonical metadata cell.");
    args.name=std::make_shared<const std::string>(p.name);args.flag=p.flag;args.native_size=p.native_size;
    args.parent_registration=p.parent;args.parent_getter=p.parent_getter;args.field_callback=p.field_callback;
    args.layout_probe=p.probe;args.defaults_callback=p.defaults;return args;
}
struct IgbNamedTypeRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;E kind{};
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbNamedTypeRegistrationInvocation::IgbNamedTypeRegistrationInvocation(E kind):impl_(std::make_unique<Impl>()){igb_named_type_profile(kind);impl_->kind=kind;}
IgbNamedTypeRegistrationInvocation::~IgbNamedTypeRegistrationInvocation()=default;
bool IgbNamedTypeRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbNamedTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbNamedTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete named-type registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbNamedTypeRegistrationInvocation::resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Named-type registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Named-type registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!igb_named_type_metadata_slot(services,s.kind))return pending("Named-type registration requires its canonical metadata cell after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_named_type_registration_arguments(services,s.kind));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbNamedTypeLookupInvocation::Impl {
    enum class P{read,handle,pool,construct,publish,registration,reload,semaphore_lazy,done};P phase=P::read;
    E kind{};L mode{};std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbNamedTypeRegistrationServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<ModelSceneObject> pool;std::shared_ptr<IgbRuntimeMetaType> result;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::unique_ptr<IgbThreadDependencyLookupInvocation> semaphore;
    bool running{},fault{};
};
IgbNamedTypeLookupInvocation::IgbNamedTypeLookupInvocation(E kind,L mode):impl_(std::make_unique<Impl>()){
    const auto& p=igb_named_type_profile(kind);
    if(mode!=L::plain && mode!=L::fully_registered && mode!=L::allocate_if_null)fail("Unknown named-type metadata lookup mode.");
    if(mode==L::plain && !p.plain_getter)fail("No recovered plain getter exists for this named-type profile.");
    impl_->kind=kind;impl_->mode=mode;
    if(kind==E::semaphore && mode==L::allocate_if_null){impl_->semaphore=std::make_unique<IgbThreadDependencyLookupInvocation>(IgbThreadDependencyEntry::semaphore);impl_->phase=Impl::P::semaphore_lazy;}
}
IgbNamedTypeLookupInvocation::~IgbNamedTypeLookupInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.result)s.owner->retained.push_back(s.result);if(s.pool)s.owner->retained.push_back(s.pool);if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
}}
bool IgbNamedTypeLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbNamedTypeLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbNamedTypeLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete named-type lookup has no metadata result.");return impl_->result;
}
MessengerCallResult IgbNamedTypeLookupInvocation::resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Named-type lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Named-type lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_named_type_metadata_slot(services,s.kind);if(!s.slot)return pending("Named-type lookup requires its canonical metadata cell.");}
        if(!*s.slot)return pending("Named-type lookup requires represented canonical metadata.");
        if(s.mode==L::plain)s.phase=P::reload;
        else if(s.mode==L::allocate_if_null)s.phase=**s.slot?P::reload:P::handle;
        else if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(igb_named_type_profile(s.kind).body);s.phase=P::registration;}break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Named-type lazy getter requires metadata pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::construct;break;}
    case P::construct:
        if(!s.factory){if(!services || !services->metadata)return pending("Named-type lazy getter requires actual metadata factory29E64.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);}
        {const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status;}
        s.result=s.factory->result();s.allocation=s.factory->allocation();if(s.owner && s.allocation.storage)s.owner->allocations.push_back(s.allocation);
        s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Named-type lookup requires its reloaded canonical metadata.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::semaphore_lazy:{const auto status=s.semaphore->resume(services?services->dependencies:nullptr);check();if(!status.completed)return status;
        s.result=s.semaphore->result();s.semaphore.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbNamedObjectProbeInvocation::Impl {
    enum class P{reset,core,derived,name,offset,word,cleanup,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbNamedTypeRegistrationServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::uint32_t offset{},value{};bool running{},fault{};
};
IgbNamedObjectProbeInvocation::IgbNamedObjectProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbNamedObjectProbeInvocation::~IgbNamedObjectProbeInvocation()=default;
bool IgbNamedObjectProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbNamedObjectProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbNamedObjectProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete named-object probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbNamedObjectProbeInvocation::resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Named-object probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Named-object probe was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Named-object probe requires ArkCore80562100 before derived publication.");
        s.core=*services->types->runtime->singleton;s.phase=P::derived;break;
    case P::derived:igb_bootstrap_write_word(s.local,0,0x8047650c);s.phase=P::name;break;
    case P::name:igb_bootstrap_write_word(s.local,8,0);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Named-object probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Named-object probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>8 || (s.offset&3u)!=0)fail("Named-object probe exceeds its aligned12-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::cleanup;break;
    case P::cleanup:
        igb_bootstrap_write_word(s.local,0,0x8047650c);
        // The original cleanup tests saved literal-zero r5, not local+8. Even
        // a later represented name mutation does not decrement or release it.
        s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_named_type_registration_function_factory(std::shared_ptr<IgbNamedTypeRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Named-type registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct){auto invocation=std::make_shared<IgbNamedTypeRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(igb_named_type_profile(*wrapper).body);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_named_type_registration_type_factory(std::shared_ptr<IgbNamedTypeRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<E>{};
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        const bool probe=call.kind==IgbTypeRegistrationServiceKind::layout_probe && call.function==0x80029ca8;
        if(!registration && !lookup && !probe)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Named-type registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbNamedTypeRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(lookup){auto invocation=std::make_shared<IgbNamedTypeLookupInvocation>(lookup->kind,lookup->mode);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else operation->resume=[selected,invocation=std::shared_ptr<IgbNamedObjectProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Named-object probe requires its represented12-byte stack local.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Named-object probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbNamedObjectProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
} // namespace dw4
