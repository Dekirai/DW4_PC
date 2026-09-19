#include "dw4/igb_metadata_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
struct ActiveRegistration {const IgbMetadataFactoryServices* cell;bool* fault;};
thread_local std::vector<ActiveRegistration> active_registrations;
// This bounds an impossible bootstrap self-cycle, not legitimate recursive
// registration of an already published busy metadata owner. It writes no
// original once flag or metadata. Suspended independent calls are not active.
struct RegistrationScope {
    explicit RegistrationScope(const std::shared_ptr<IgbMetadataFactoryServices>& cell,bool& fault){
        const auto runtime=cell->bootstrap?cell->bootstrap->runtime:nullptr;
        const bool enabled=runtime && runtime->singleton && *runtime->singleton && (*runtime->singleton)->enabled!=0;
        if(enabled && cell->metadata && !*cell->metadata){
            for(const auto& active:active_registrations)if(active.cell==cell.get()){
                *active.fault=true;fault=true;
                fail("Metadata registration recursively requires its still-null8056174C; the original startup prerequisite has not been represented.");
            }
        }
        active_registrations.push_back({cell.get(),&fault});
    }
    ~RegistrationScope(){active_registrations.pop_back();}
};
bool getter(std::uint32_t address){return address==0x80029f84 || address==0x80029fc0 || address==0x8002a144;}
IgbMetadataLookup entry(std::uint32_t address){return address==0x80029f84?IgbMetadataLookup::allocate_if_null:IgbMetadataLookup::fully_registered;}
}
IgbTypeRegistrationArguments igb_metadata_type_registration_arguments(const std::shared_ptr<IgbMetadataFactoryServices>& state){
    if(!state)fail("Metadata registration requires canonical8056174C storage.");
    IgbTypeRegistrationArguments args;
    args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(state,&state->metadata);
    args.name=std::make_shared<const std::string>("igMetaObject");args.flag=0;args.native_size=0x64;
    args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;args.field_callback=0x8002a144;
    args.layout_probe=0x80029ffc;args.defaults_callback=0x8002a164;args.trailing_callback=0;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(
        std::initializer_list<std::uint32_t>{0x8002dd18,0x8002e054,0x80066ae0,0});
    return args;
}
struct IgbMetadataTypeRegistrationInvocation::Impl {
    std::shared_ptr<IgbMetadataFactoryServices> state;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    bool done{},running{},fault{};
};
IgbMetadataTypeRegistrationInvocation::IgbMetadataTypeRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbMetadataTypeRegistrationInvocation::~IgbMetadataTypeRegistrationInvocation()=default;
bool IgbMetadataTypeRegistrationInvocation::completed()const noexcept{return impl_->done;}
bool IgbMetadataTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbMetadataTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete metadata registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbMetadataTypeRegistrationInvocation::resume(std::shared_ptr<IgbMetadataRegistrationServices> services){
    auto& s=*impl_;
    if(s.running || s.fault){s.fault=true;fail("Metadata registration requires an unfaulted nonrecursive invocation.");}
    if(s.done)return {true,0,{}};
    Running running(s.running);
    try{
        if(!s.state){if(!services || !services->metadata)return pending("Metadata registration requires canonical8056174C storage.");s.state=services->metadata;}
        RegistrationScope active(s.state,s.fault);
        if(!s.registration)s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_metadata_type_registration_arguments(s.state));
        const auto status=s.registration->resume(services?services->types:nullptr);
        if(s.fault)fail("Metadata registration continuation was invalidated.");
        if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();
        if(s.fault)fail("Metadata registration continuation was invalidated.");
        s.done=true;return {true,0,{}};
    }catch(...){s.fault=true;throw;}
}
struct IgbMetadataLookupInvocation::Impl {
    enum class P {read,pool_handle,pool,construct,publish,registration,reload,done};
    IgbMetadataLookup entry;
    std::shared_ptr<IgbMetadataRegistrationServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> state;
    std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::shared_ptr<IgbRuntimeMetaType> result;
    P phase=P::read;bool running{},fault{};
};
IgbMetadataLookupInvocation::IgbMetadataLookupInvocation(IgbMetadataLookup value):impl_(std::make_unique<Impl>()){impl_->entry=value;}
IgbMetadataLookupInvocation::~IgbMetadataLookupInvocation(){
    if(!completed() && impl_->owner && impl_->result)impl_->owner->unpublished_metadata_.push_back(impl_->result);
}
bool IgbMetadataLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbMetadataLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbMetadataLookupInvocation::resume(std::shared_ptr<IgbMetadataRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);const auto check=[&]{if(s.fault)fail("Metadata lookup continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:{
        if(!s.state){if(!services || !services->metadata)return pending("Metadata lookup requires canonical8056174C storage.");s.owner=services;s.state=services->metadata;}
        if(!s.state->metadata)return pending("Metadata lookup requires represented8056174C.");
        const auto metadata=*s.state->metadata;
        if(s.entry==IgbMetadataLookup::allocate_if_null)s.phase=metadata?P::reload:P::pool_handle;
        else if(metadata && (metadata->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8002a0d0);s.phase=P::registration;}
        break;
    }
    case P::pool_handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Lazy metadata allocation requires handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{
        const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();
        s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);s.phase=P::construct;break;
    }
    case P::construct:{
        const auto status=s.factory->resume(s.state);check();if(!status.completed)return status;
        s.result=s.factory->result();s.factory.reset();check();s.phase=P::publish;break;
    }
    case P::publish:
        //80029FA8 is a raw store of the captured factory result, not a second
        // cache check and not a reference assignment.
        if(s.state->metadata && *s.state->metadata && *s.state->metadata!=s.result)
            s.owner->unpublished_metadata_.push_back(*s.state->metadata);
        s.state->metadata=s.result;s.phase=P::reload;break;
    case P::registration:{
        const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;
    }
    case P::reload:
        if(!s.state->metadata)return pending("Metadata lookup requires reloaded8056174C.");
        s.result=*s.state->metadata;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_metadata_registration_function_factory(
    std::shared_ptr<IgbMetadataRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbMetadataRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x8002a0d0 && call.function!=0x8002a0a8))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata registration services expired before function selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8002a0d0){
            auto invocation=std::make_shared<IgbMetadataTypeRegistrationInvocation>();
            operation->resume=[invocation,selected,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else{
            auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8002a0d0);
            operation->resume=[invocation,selected]{return invocation->resume(selected->callbacks);};
        }return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_metadata_registration_type_factory(
    std::shared_ptr<IgbMetadataRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        const bool registration=call.kind==IgbTypeRegistrationServiceKind::register_parent && call.function==0x8002a0d0;
        if(!registration && !(call.kind==IgbTypeRegistrationServiceKind::get_parent && getter(call.function)))
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata registration services expired before type operation selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){
            auto invocation=std::make_shared<IgbMetadataTypeRegistrationInvocation>();
            operation->resume=[invocation,selected,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else{
            auto invocation=std::make_shared<IgbMetadataLookupInvocation>(entry(call.function));
            operation->resume=[invocation,selected,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->metadata_result=invocation->result();return status;};
        }return operation;
    };
}
IgbMetadataFactory make_native_igb_metadata_registration_factory(
    std::shared_ptr<IgbMetadataRegistrationServices> services,IgbMetadataFactory remaining){
    std::weak_ptr<IgbMetadataRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbMetadataFactoryCall& call){
        if(call.kind!=IgbMetadataFactoryServiceKind::register_type)return remaining?remaining(call):std::shared_ptr<IgbMetadataFactoryOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata registration services expired before factory selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8002a0d0);
        auto operation=std::make_shared<IgbMetadataFactoryOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected->callbacks);};return operation;
    };
}
} // namespace dw4
