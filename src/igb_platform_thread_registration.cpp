#include "dw4/igb_platform_thread_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbPlatformThreadRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
using Lookup=IgbPlatformThreadLookup;
void validate(Lookup mode){if(mode!=Lookup::fully_registered && mode!=Lookup::plain_parent)fail("Unknown platform-thread metadata lookup mode.");}
std::optional<IgbBootstrapPointer> physical_metadata(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbPlatformThreadRegistrationServices>& services){
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata){
        if(igb_bootstrap_metadata(pointer)!=metadata)continue;
        if(pointer.storage->faulted())fail("Platform-thread defaults reached faulted metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("Platform-thread metadata has conflicting physical identities.");
        result=pointer;
    }return result;
}
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_platform_thread_metadata_slot(
    const std::shared_ptr<IgbPlatformThreadRegistrationServices>& services,Lookup mode){
    validate(mode);if(!services)return {};
    if(mode==Lookup::plain_parent)return services->parent?std::shared_ptr<IgbRegistrationMetaSlot>(services->parent,&services->parent->metadata):nullptr;
    return services->state?std::shared_ptr<IgbRegistrationMetaSlot>(services->state,&services->state->metadata):nullptr;
}
IgbTypeRegistrationArguments igb_platform_thread_registration_arguments(const std::shared_ptr<IgbPlatformThreadRegistrationState>& state){
    if(!state)fail("Platform-thread registration requires canonical80562070 storage.");
    IgbTypeRegistrationArguments args;args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(state,&state->metadata);
    args.name=std::make_shared<const std::string>("igGamecubeThreadManager");args.flag=0;args.native_size=0x10;
    args.parent_registration=0x800238a8;args.parent_getter=0x8003af68;args.field_callback=0x80023a10;
    args.layout_probe=0x8003ae84;args.defaults_callback=0x8003af70;return args;
}
struct IgbPlatformThreadRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbPlatformThreadRegistrationInvocation::IgbPlatformThreadRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbPlatformThreadRegistrationInvocation::~IgbPlatformThreadRegistrationInvocation()=default;
bool IgbPlatformThreadRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformThreadRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbPlatformThreadRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete platform-thread registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbPlatformThreadRegistrationInvocation::resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-thread registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Platform-thread registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->state)return pending("Platform-thread registration requires canonical80562070 after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_platform_thread_registration_arguments(services->state));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformThreadMetadataInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;Lookup mode{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;bool running{},fault{};
};
IgbPlatformThreadMetadataInvocation::IgbPlatformThreadMetadataInvocation(Lookup mode):impl_(std::make_unique<Impl>()){validate(mode);impl_->mode=mode;}
IgbPlatformThreadMetadataInvocation::~IgbPlatformThreadMetadataInvocation()=default;
bool IgbPlatformThreadMetadataInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformThreadMetadataInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbPlatformThreadMetadataInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete platform-thread metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbPlatformThreadMetadataInvocation::resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-thread metadata lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Platform-thread metadata lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_platform_thread_metadata_slot(services,s.mode);if(!s.slot)return pending("Platform-thread metadata lookup requires its canonical global cell.");}
        if(!*s.slot)return pending("Platform-thread metadata lookup requires represented canonical state.");
        if(s.mode==Lookup::plain_parent || (**s.slot && ((**s.slot)->class_flags&4u)!=0))s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8003aef8);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Platform-thread metadata lookup requires its reloaded canonical pointer.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformThreadProbeInvocation::Impl {
    enum class P{reset,core,base,derived,offset,word,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbPlatformThreadRegistrationServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::uint32_t offset{},value{};bool running{},fault{};
};
IgbPlatformThreadProbeInvocation::IgbPlatformThreadProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbPlatformThreadProbeInvocation::~IgbPlatformThreadProbeInvocation()=default;
bool IgbPlatformThreadProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformThreadProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbPlatformThreadProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete platform-thread probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbPlatformThreadProbeInvocation::resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-thread probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Platform-thread probe was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Platform-thread probe requires ArkCore80562100 before manager vtable stores.");
        s.core=*services->types->runtime->singleton;s.phase=P::base;break;
    case P::base:igb_bootstrap_write_word(s.local,0,0x80470c2c);s.phase=P::derived;break;
    case P::derived:igb_bootstrap_write_word(s.local,0,0x804700a8);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Platform-thread probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Platform-thread probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>12 || (s.offset&3u)!=0)fail("Platform-thread probe exceeds its aligned16-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformThreadDefaultsInvocation::Impl {
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> metadata;
    bool done{},running{},fault{};
};
IgbPlatformThreadDefaultsInvocation::IgbPlatformThreadDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbPlatformThreadDefaultsInvocation::~IgbPlatformThreadDefaultsInvocation()=default;
bool IgbPlatformThreadDefaultsInvocation::completed()const noexcept{return impl_->done;}
bool IgbPlatformThreadDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbPlatformThreadDefaultsInvocation::resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("Platform-thread defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    try{
        if(!s.slot){s.slot=igb_platform_thread_metadata_slot(services);if(!s.slot)return pending("Platform-thread defaults require canonical80562070 storage.");}
        if(!*s.slot)return pending("Platform-thread defaults require represented canonical metadata.");
        s.metadata=**s.slot;if(!s.metadata)fail("Platform-thread defaults store through null metadata at+40.");
        if(const auto physical=physical_metadata(s.metadata,services))igb_bootstrap_write_word(*physical,0x40,0x80023920);
        s.metadata->word_40=0x80023920;s.done=true;return {true,0,{}};
    }catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_platform_thread_registration_function_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x8003aef8 && call.function!=0x8003aed0))
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-thread registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8003aef8){auto invocation=std::make_shared<IgbPlatformThreadRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8003aef8);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_platform_thread_registration_type_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x8003aef8;
        const bool getter=call.kind==K::get_parent && (call.function==0x8003ae48 || call.function==0x8003af68);
        const bool probe=call.kind==K::layout_probe && call.function==0x8003ae84;
        const bool defaults=call.kind==K::defaults && call.function==0x8003af70;
        if(!registration && !getter && !probe && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-thread registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbPlatformThreadRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(getter){auto invocation=std::make_shared<IgbPlatformThreadMetadataInvocation>(call.function==0x8003af68?Lookup::plain_parent:Lookup::fully_registered);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else if(defaults){auto invocation=std::make_shared<IgbPlatformThreadDefaultsInvocation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};}
        else operation->resume=[selected,invocation=std::shared_ptr<IgbPlatformThreadProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Platform-thread probe requires its represented16-byte stack local.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Platform-thread probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbPlatformThreadProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
IgbFileFactoryFactory make_native_igb_platform_thread_redirect_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices> services,IgbFileFactoryFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFileFactoryCall& call)->std::shared_ptr<IgbFileFactoryOperation>{
        if(call.kind!=IgbFileFactoryServiceKind::redirect_type || call.redirect!=0x80023a10)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-thread redirect services expired before selection.");
        auto invocation=std::make_shared<IgbPlatformThreadMetadataInvocation>();auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
            if(status.completed)out->metadata_result=invocation->result();return status;};return operation;
    };
}
} // namespace dw4
