#include "dw4/igb_thread_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbThreadRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
bool getter(std::uint32_t address){return address==0x80023844 || address==0x80023920;}
}
void publish_original_igb_metadata_pool_handle(IgbTypeRegistrationServices& services) noexcept{
    services.metadata_pool_handle=5;
}
IgbTypeRegistrationArguments igb_thread_type_registration_arguments(const std::shared_ptr<IgbThreadRegistrationState>& state){
    if(!state)fail("Thread registration requires canonical805614E8 storage.");
    IgbTypeRegistrationArguments args;
    args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(state,&state->metadata);
    args.name=std::make_shared<const std::string>("igThreadManager");args.flag=1;args.native_size=0x10;
    args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;args.field_callback=0x80023920;
    args.layout_probe=0;args.defaults_callback=0x80023940;args.trailing_callback=0x80023a30;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(
        std::initializer_list<std::uint32_t>{0x80023f28,0x80026098,0});
    return args;
}
struct IgbThreadTypeRegistrationInvocation::Impl {
    enum class P {prerequisites,arguments,registration,done};
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    P phase=P::prerequisites;bool running{},fault{};
};
IgbThreadTypeRegistrationInvocation::IgbThreadTypeRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbThreadTypeRegistrationInvocation::~IgbThreadTypeRegistrationInvocation()=default;
bool IgbThreadTypeRegistrationInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadTypeRegistrationInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbThreadTypeRegistrationInvocation::result() const{
    if(!completed() || faulted())fail("Incomplete ThreadManager registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbThreadTypeRegistrationInvocation::resume(std::shared_ptr<IgbThreadRegistrationState> state,
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,std::shared_ptr<IgbTypeRegistrationServices> types){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Thread registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);const auto check=[&]{if(s.fault)fail("Thread registration continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto r=s.prerequisites.resume(callbacks);check();if(!r.completed)return r;s.phase=P::arguments;break;}
    case P::arguments:
        if(!state)return pending("Thread registration requires canonical metadata cell805614E8.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_thread_type_registration_arguments(state));
        s.phase=P::registration;break;
    case P::registration:{
        const auto r=s.registration->resume(types);check();if(!r.completed)return r;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;return {true,0,{}};
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbThreadMetadataInvocation::Impl {
    enum class P {read,registration,reload,done};
    std::shared_ptr<IgbThreadRegistrationState> state;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::shared_ptr<IgbRuntimeMetaType> result;
    P phase=P::read;bool running{},fault{};
};
IgbThreadMetadataInvocation::IgbThreadMetadataInvocation(std::shared_ptr<IgbThreadRegistrationState> state)
    :impl_(std::make_unique<Impl>()){impl_->state=std::move(state);}
IgbThreadMetadataInvocation::~IgbThreadMetadataInvocation()=default;
bool IgbThreadMetadataInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadMetadataInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbThreadMetadataInvocation::result() const{
    if(!completed() || faulted())fail("Incomplete ThreadManager metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbThreadMetadataInvocation::resume(std::shared_ptr<IgbRegistrationCallbacksServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Thread metadata lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);const auto check=[&]{if(s.fault)fail("Thread metadata lookup continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.state)fail("Thread metadata lookup has no canonical805614E8 cell.");
        if(!s.state->metadata)return pending("Thread metadata lookup requires represented805614E8.");
        if(const auto meta=*s.state->metadata;meta && (meta->class_flags&4u)!=0)s.phase=P::reload;
        else {s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x800238a8);s.phase=P::registration;}
        break;
    case P::registration:{
        const auto r=s.registration->resume(services);check();if(!r.completed)return r;
        s.registration.reset();check();s.phase=P::reload;break;
    }
    case P::reload:
        if(!s.state->metadata)return pending("Thread metadata lookup requires reloaded805614E8.");
        s.result=*s.state->metadata;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_thread_registration_function_factory(
    std::shared_ptr<IgbThreadRegistrationState> state,std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,
    std::shared_ptr<IgbTypeRegistrationServices> types,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbThreadRegistrationState> weak_state=state;
    std::weak_ptr<IgbRegistrationCallbacksServices> weak_callbacks=callbacks;
    std::weak_ptr<IgbTypeRegistrationServices> weak_types=types;
    return [weak_state,weak_callbacks,weak_types,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x800238a8 && call.function!=0x80023880))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected_callbacks=weak_callbacks.lock();if(!selected_callbacks)fail("Thread registration callback services expired before selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x800238a8){
            const auto selected_state=weak_state.lock();const auto selected_types=weak_types.lock();
            if(!selected_state || !selected_types)fail("Thread registration state or registrar services expired before selection.");
            auto invocation=std::make_shared<IgbThreadTypeRegistrationInvocation>();
            operation->resume=[invocation,selected_state,selected_callbacks,selected_types,out=operation.get()]{
                const auto r=invocation->resume(selected_state,selected_callbacks,selected_types);if(r.completed)out->callbacks_result=invocation->result();return r;
            };
        }else{
            auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x800238a8);
            operation->resume=[invocation,selected_callbacks]{return invocation->resume(selected_callbacks);};
        }
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_thread_type_factory(
    std::shared_ptr<IgbThreadRegistrationState> state,std::shared_ptr<IgbRegistrationCallbacksServices> callbacks,
    std::shared_ptr<IgbTypeRegistrationServices> types,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbThreadRegistrationState> weak_state=state;
    std::weak_ptr<IgbRegistrationCallbacksServices> weak_callbacks=callbacks;
    std::weak_ptr<IgbTypeRegistrationServices> weak_types=types;
    return [weak_state,weak_callbacks,weak_types,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x800238a8;
        if(!registration && !(call.kind==K::get_parent && getter(call.function)))
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected_state=weak_state.lock();const auto selected_callbacks=weak_callbacks.lock();
        if(!selected_state || !selected_callbacks)fail("Thread metadata services expired before selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){
            const auto selected_types=weak_types.lock();if(!selected_types)fail("Thread registrar services expired before selection.");
            auto invocation=std::make_shared<IgbThreadTypeRegistrationInvocation>();
            operation->resume=[invocation,selected_state,selected_callbacks,selected_types,out=operation.get()]{
                const auto r=invocation->resume(selected_state,selected_callbacks,selected_types);if(r.completed)out->callbacks_result=invocation->result();return r;
            };
        }else{
            auto invocation=std::make_shared<IgbThreadMetadataInvocation>(selected_state);
            operation->resume=[invocation,selected_callbacks,out=operation.get()]{const auto r=invocation->resume(selected_callbacks);
                if(r.completed)out->metadata_result=invocation->result();return r;};
        }
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_registration_pool_factory(std::shared_ptr<IgbFilePoolsServices> pools,
    IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbFilePoolsServices> weak=pools;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::pool_for_handle)
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto services=weak.lock();if(!services)fail("Registration pool services expired before selection.");
        auto invocation=std::make_shared<IgbPoolByHandleInvocation>(call.handle);
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,services,out=operation.get()]{const auto r=invocation->resume(services);
            if(r.completed)out->object_result=invocation->result();return r;};return operation;
    };
}
IgbBootstrapFactory make_native_igb_bootstrap_pool_factory(std::shared_ptr<IgbFilePoolsServices> pools,
    IgbBootstrapFactory remaining){
    std::weak_ptr<IgbFilePoolsServices> weak=pools;
    return [weak,remaining=std::move(remaining)](const IgbBootstrapCall& call){
        if(call.kind!=IgbBootstrapServiceKind::pool_for_handle)
            return remaining?remaining(call):std::shared_ptr<IgbBootstrapOperation>{};
        const auto services=weak.lock();if(!services)fail("Bootstrap pool services expired before selection.");
        auto invocation=std::make_shared<IgbPoolByHandleInvocation>(call.handle);
        auto operation=std::make_shared<IgbBootstrapOperation>();
        operation->resume=[invocation,services,out=operation.get()]{const auto r=invocation->resume(services);
            if(r.completed)out->pool_result=invocation->result();return r;};return operation;
    };
}
} // namespace dw4
