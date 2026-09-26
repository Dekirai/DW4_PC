#include "dw4/igb_error_handler_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbErrorHandlerError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
void check_fault(bool fault){if(fault)fail("Error-handler continuation was invalidated by a callback.");}
struct Call {
    IgbErrorHandlerCall request;
    IgbErrorHandlerFactory factory;
    std::shared_ptr<IgbErrorHandlerOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbErrorHandlerServices>& services,Check check){
        check();if(!factory && services)factory=services->factory;
        if(!factory)return pending("Error-handler startup requires its selected pool, constructor or destructor service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Error-handler startup reached an unbound pool, constructor or destructor.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Error-handler startup requires its retained service continuation.");
        const auto status=checked_callback?checked_callback(check):callback();check();return status;
    }
};
void retain_singleton(const std::shared_ptr<IgbErrorHandlerState>& state){
    if(state && state->singleton && state->singleton->storage)state->retained.push_back(*state->singleton);
}
}
IgbTypeRegistrationArguments igb_error_handler_type_registration_arguments(const std::shared_ptr<IgbErrorHandlerServices>& services){
    if(!services || !services->metadata)fail("Error-handler registration requires canonical metadata storage80561C2C.");
    IgbTypeRegistrationArguments args;args.metadata_slot=services->metadata;
    args.name=std::make_shared<const std::string>("igErrorHandler");args.flag=0;args.native_size=8;
    args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;args.field_callback=0x800312f0;
    args.layout_probe=0x80031210;args.defaults_callback=0x80031310;args.trailing_callback=0x80031338;return args;
}
struct IgbErrorHandlerRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbErrorHandlerRegistrationInvocation::IgbErrorHandlerRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbErrorHandlerRegistrationInvocation::~IgbErrorHandlerRegistrationInvocation()=default;
bool IgbErrorHandlerRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbErrorHandlerRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete error-handler registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbErrorHandlerRegistrationInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Error-handler registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->metadata)return pending("Error-handler registration requires canonical metadata after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_error_handler_type_registration_arguments(services));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbErrorHandlerLookupInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;IgbErrorHandlerLookup mode{};
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::shared_ptr<IgbRuntimeMetaType> result;bool running{},fault{};
};
IgbErrorHandlerLookupInvocation::IgbErrorHandlerLookupInvocation(IgbErrorHandlerLookup mode):impl_(std::make_unique<Impl>()){
    if(mode!=IgbErrorHandlerLookup::fully_registered && mode!=IgbErrorHandlerLookup::plain)fail("Unknown error-handler metadata lookup mode.");impl_->mode=mode;
}
IgbErrorHandlerLookupInvocation::~IgbErrorHandlerLookupInvocation()=default;
bool IgbErrorHandlerLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbErrorHandlerLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete error-handler lookup has no metadata result.");return impl_->result;
}
MessengerCallResult IgbErrorHandlerLookupInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Error-handler lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!services || !services->metadata || !*services->metadata)return pending("Error-handler lookup requires represented metadata80561C2C.");
        if(s.mode==IgbErrorHandlerLookup::plain){s.result=**services->metadata;s.phase=P::done;break;}
        if(const auto metadata=**services->metadata;metadata && (metadata->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x80031278);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!services || !services->metadata || !*services->metadata)return pending("Error-handler lookup requires its fresh metadata reload.");
        s.result=**services->metadata;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbErrorHandlerLayoutProbeInvocation::Impl {
    enum class P{reset,core,vtable,offset,word,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;std::uint32_t offset{},value{};bool running{},fault{};
};
IgbErrorHandlerLayoutProbeInvocation::IgbErrorHandlerLayoutProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbErrorHandlerLayoutProbeInvocation::~IgbErrorHandlerLayoutProbeInvocation()=default;
bool IgbErrorHandlerLayoutProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerLayoutProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbErrorHandlerLayoutProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete error-handler probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbErrorHandlerLayoutProbeInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Error-handler probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Error-handler probe requires ArkCore80562100 after its base reset.");
        s.core=*services->types->runtime->singleton;s.phase=P::vtable;break;
    case P::vtable:igb_bootstrap_write_word(s.local,0,0x804759d8);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Error-handler probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Error-handler probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>4 || (s.offset&3u)!=0)fail("Error-handler probe exceeds its aligned8-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::done;return {true,s.value,{}};
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbErrorHandlerObjectFactoryInvocation::Impl {
    enum class P{registration,metadata,construct,done};P phase=P::registration;
    std::shared_ptr<ModelSceneObject> pool;IgbBootstrapPointer result;
    IgbRegistrationCallbacksInvocation registration{0x80031278};
    std::unique_ptr<IgbReflectedFactoryInvocation> reflected;bool running{},fault{};
};
IgbErrorHandlerObjectFactoryInvocation::IgbErrorHandlerObjectFactoryInvocation(std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){impl_->pool=std::move(pool);}
IgbErrorHandlerObjectFactoryInvocation::~IgbErrorHandlerObjectFactoryInvocation()=default;
bool IgbErrorHandlerObjectFactoryInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerObjectFactoryInvocation::faulted()const noexcept{return impl_->fault;}
IgbBootstrapPointer IgbErrorHandlerObjectFactoryInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete error-handler object factory has no returned object.");return impl_->result;
}
MessengerCallResult IgbErrorHandlerObjectFactoryInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Error-handler object factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.result),{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::registration:{const auto status=s.registration.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;s.phase=P::metadata;break;}
    case P::metadata:
        if(!services || !services->metadata || !*services->metadata)return pending("Error-handler factory requires fresh metadata80561C2C after registration.");
        s.reflected=std::make_unique<IgbReflectedFactoryInvocation>(**services->metadata,s.pool);s.phase=P::construct;break;
    case P::construct:{const auto status=s.reflected->resume(services?services->reflected:nullptr,check);check();if(!status.completed)return status;
        const auto object=s.reflected->result_instance();s.result={object.storage,object.offset};s.reflected.reset();check();s.phase=P::done;break;}
    case P::done:return {true,igb_bootstrap_address(s.result),{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbErrorHandlerSingletonInvocation::Impl {
    enum class P{singleton,metadata,guard,handle,after_pool,after_create,publish,reload,call,done};P phase=P::singleton,after=P::done;
    IgbBootstrapPointer constructed,result;std::shared_ptr<IgbErrorHandlerState> retention;
    Call call;bool running{},fault{};
};
IgbErrorHandlerSingletonInvocation::IgbErrorHandlerSingletonInvocation():impl_(std::make_unique<Impl>()){}
IgbErrorHandlerSingletonInvocation::~IgbErrorHandlerSingletonInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.retention && s.constructed.storage)s.retention->retained.push_back(s.constructed);
}
bool IgbErrorHandlerSingletonInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerSingletonInvocation::faulted()const noexcept{return impl_->fault;}
IgbBootstrapPointer IgbErrorHandlerSingletonInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete error-handler singleton lookup has no returned object.");return impl_->result;
}
MessengerCallResult IgbErrorHandlerSingletonInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbErrorHandlerServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Error-handler singleton requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.result),{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbErrorHandlerCall&{
        clear();s.call.request.kind=kind;if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;return s.call.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::singleton:
        if(!services || !services->state || !services->state->singleton)return pending("Error-handler lookup requires singleton80562120.");
        s.retention=services->state;
        s.phase=igb_bootstrap_address(*services->state->singleton)?P::reload:P::metadata;break;
    case P::metadata:
        if(!services || !services->metadata || !*services->metadata)return pending("Error-handler lookup requires metadata80561C2C after its null singleton read.");
        s.phase=**services->metadata?P::guard:P::reload;break;
    case P::guard:
        if(!services || !services->state || !services->state->teardown_guard)return pending("Error-handler lookup requires guard byte80562124.");
        s.phase=*services->state->teardown_guard?P::reload:P::handle;break;
    case P::handle:
        if(!services || !services->pool_handle || !*services->pool_handle)return pending("Error-handler lookup requires pool handle805621F0.");
        select(K::pool_for_handle,P::after_pool).handle=**services->pool_handle;break;
    case P::after_pool:{auto pool=s.call.operation->pool_result;select(K::create_object,P::after_create).pool=std::move(pool);break;}
    case P::after_create:
        s.constructed=s.call.operation->object_result;clear();s.phase=P::publish;break;
    case P::publish:
        if(!services || !services->state)return pending("Error-handler constructor requires canonical singleton storage80562120.");
        s.retention=services->state;retain_singleton(s.retention);check();
        services->state->singleton=s.constructed;s.constructed={};s.phase=P::reload;break;
    case P::reload:
        if(!services || !services->state || !services->state->singleton)return pending("Error-handler lookup requires its fresh singleton80562120 reload.");
        s.result=*services->state->singleton;s.phase=P::done;break;
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,igb_bootstrap_address(s.result),{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbErrorHandlerTeardownInvocation::Impl {
    enum class P{singleton,read,write,recheck,destroy,clear,guard,done};P phase=P::singleton;
    IgbBootstrapPointer object;std::uint32_t reference{};Call call;
    std::shared_ptr<IgbErrorHandlerState> retention;bool running{},fault{};
};
IgbErrorHandlerTeardownInvocation::IgbErrorHandlerTeardownInvocation():impl_(std::make_unique<Impl>()){}
IgbErrorHandlerTeardownInvocation::~IgbErrorHandlerTeardownInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.retention && s.object.storage)s.retention->retained.push_back(s.object);
}
bool IgbErrorHandlerTeardownInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbErrorHandlerTeardownInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbErrorHandlerTeardownInvocation::resume(std::shared_ptr<IgbErrorHandlerServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Error-handler teardown requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::singleton:
        if(!services || !services->state || !services->state->singleton)return pending("Error-handler teardown requires singleton80562120.");
        s.retention=services->state;s.object=*services->state->singleton;s.phase=igb_bootstrap_address(s.object)?P::read:P::clear;break;
    case P::read:s.reference=igb_bootstrap_read_word(s.object,4);s.phase=P::write;break;
    case P::write:igb_bootstrap_write_word(s.object,4,s.reference-1u);s.phase=P::recheck;break;
    case P::recheck:
        if((igb_bootstrap_read_word(s.object,4)&0x007fffffu)!=0){s.phase=P::clear;break;}
        s.call.request.kind=IgbErrorHandlerServiceKind::destroy_object;s.call.request.object=s.object;
        if(services)s.call.factory=services->factory;s.phase=P::destroy;break;
    case P::destroy:{const auto status=s.call.poll(services,check);if(!status.completed)return status;
        s.call.clear();check();s.phase=P::clear;break;}
    case P::clear:
        if(!services || !services->state)return pending("Error-handler teardown requires canonical singleton storage80562120.");
        retain_singleton(services->state);check();services->state->singleton=IgbBootstrapPointer{};s.phase=P::guard;break;
    case P::guard:
        if(!services || !services->state)return pending("Error-handler teardown requires canonical guard storage80562124 after clearing the singleton.");
        services->state->teardown_guard=std::uint8_t{1};s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbErrorHandlerFactory make_native_igb_error_handler_service_factory(std::shared_ptr<IgbErrorHandlerServices> services,IgbErrorHandlerFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbErrorHandlerCall& call)->std::shared_ptr<IgbErrorHandlerOperation>{
        using K=IgbErrorHandlerServiceKind;if(call.kind!=K::pool_for_handle && call.kind!=K::create_object)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Error-handler services expired before native service selection.");
        auto operation=std::make_shared<IgbErrorHandlerOperation>();
        if(call.kind==K::pool_for_handle){auto invocation=std::make_shared<IgbPoolByHandleInvocation>(call.handle);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected->pools);
                if(status.completed)out->pool_result=invocation->result();return status;};
        }else{auto invocation=std::make_shared<IgbErrorHandlerObjectFactoryInvocation>(call.pool);
            operation->resume_checked=[selected,invocation,out=operation.get()](const std::function<void()>& check){const auto status=invocation->resume(selected,check);
                if(status.completed)out->object_result=invocation->result();return status;};
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->object_result=invocation->result();return status;};
        }return operation;
    };
}
IgbRegistrationCallbacksFactory make_native_igb_error_handler_function_factory(std::shared_ptr<IgbErrorHandlerServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        const auto function=call.function;
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function ||
            (function!=0x80031250 && function!=0x80031278 && function!=0x80031310 && function!=0x80031338 &&
             function!=0x80047080 && function!=0x800470f4 && function!=0x80031358 && function!=0x800470a0))
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Error-handler services expired before function selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(function==0x80031250){auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x80031278);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};
        }else if(function==0x80031278){auto invocation=std::make_shared<IgbErrorHandlerRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else if(function==0x80031310){auto invocation=std::make_shared<IgbBootstrapCallbackAppendInvocation>(0x80031358);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};
        }else if(function==0x80031358 || function==0x800470a0){auto invocation=std::make_shared<IgbErrorHandlerTeardownInvocation>();
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
        }else{auto invocation=std::make_shared<IgbErrorHandlerSingletonInvocation>();
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
        }return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_error_handler_type_factory(std::shared_ptr<IgbErrorHandlerServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x80031278;
        const bool lookup=call.kind==K::get_parent && (call.function==0x800311d4 || call.function==0x800312f0 || call.function==0x80094010);
        const bool probe=call.kind==K::layout_probe && call.function==0x80031210;
        const bool defaults=call.kind==K::defaults && call.function==0x80031310;
        if(!registration && !lookup && !probe && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Error-handler services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbErrorHandlerRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else if(lookup){auto invocation=std::make_shared<IgbErrorHandlerLookupInvocation>(call.function==0x80094010?IgbErrorHandlerLookup::plain:IgbErrorHandlerLookup::fully_registered);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};
        }else if(defaults){auto invocation=std::make_shared<IgbBootstrapCallbackAppendInvocation>(0x80031358);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};
        }else operation->resume=[selected,invocation=std::shared_ptr<IgbErrorHandlerLayoutProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Error-handler probe requires its represented8-byte stack local.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Error-handler probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbErrorHandlerLayoutProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
IgbObjectInitFactory make_native_igb_error_handler_object_init_factory(std::shared_ptr<IgbErrorHandlerServices> services,IgbObjectInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation>{
        if(call.kind!=IgbObjectInitServiceKind::object_virtual || call.vtable_word!=0x804759d8 ||
            (call.virtual_slot!=0x0c && call.virtual_slot!=0x24 && call.virtual_slot!=0x2c && call.virtual_slot!=0x34 && call.virtual_slot!=0x58))
            return remaining?remaining(call):nullptr;
        auto operation=std::make_shared<IgbObjectInitOperation>();
        if(call.virtual_slot!=0x58){operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;}
        const auto selected=weak.lock();if(!selected)fail("Error-handler services expired before plain metadata getter selection.");
        auto invocation=std::make_shared<IgbErrorHandlerLookupInvocation>(IgbErrorHandlerLookup::plain);
        operation->resume_checked=[selected,invocation,out=operation.get()](const std::function<void()>& check){const auto status=invocation->resume(selected,check);
            if(status.completed)out->metadata_result=invocation->result();return status;};
        operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
            if(status.completed)out->metadata_result=invocation->result();return status;};return operation;
    };
}
} // namespace dw4
