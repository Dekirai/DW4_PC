#include "dw4/igb_object_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbObjectRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
bool getter(std::uint32_t address){return address==0x800237d0 || address==0x80066b74 || address==0x80042dec;}
}
IgbTypeRegistrationArguments igb_object_type_registration_arguments(const std::shared_ptr<IgbRegistrationMetaSlot>& slot){
    if(!slot)fail("igObject registration requires canonical805622A4 storage.");
    IgbTypeRegistrationArguments args;args.metadata_slot=slot;args.name=std::make_shared<const std::string>("igObject");
    args.flag=0;args.native_size=8;args.parent_registration=0x80066a60;args.parent_getter=0x80066aa4;
    args.field_callback=0x80066b74;args.layout_probe=0x80066aac;args.defaults_callback=0x80066b7c;
    return args;
}
struct IgbObjectTypeRegistrationInvocation::Impl {
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    bool done{},running{},fault{};
};
IgbObjectTypeRegistrationInvocation::IgbObjectTypeRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbObjectTypeRegistrationInvocation::~IgbObjectTypeRegistrationInvocation()=default;
bool IgbObjectTypeRegistrationInvocation::completed()const noexcept{return impl_->done;}
bool IgbObjectTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbObjectTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete igObject registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbObjectTypeRegistrationInvocation::resume(std::shared_ptr<IgbObjectRegistrationServices> services){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("igObject registration requires an unfaulted nonrecursive invocation.");}
    if(s.done)return {true,0,{}};Running running(s.running);
    try{
        if(!s.registration){if(!services || !services->metadata)return pending("igObject registration requires canonical805622A4 storage.");
            s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_object_type_registration_arguments(services->metadata));}
        const auto status=s.registration->resume(services?services->types:nullptr);
        if(s.fault)fail("igObject registration continuation was invalidated.");if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();if(s.fault)fail("igObject registration continuation was invalidated.");
        s.done=true;return {true,0,{}};
    }catch(...){s.fault=true;throw;}
}
struct IgbObjectRootRegistrationInvocation::Impl {
    enum class P{handle,pool,allocate,call,initialize,done};
    std::shared_ptr<IgbObjectRegistrationServices> owner;
    std::unique_ptr<IgbPoolByHandleInvocation> pool;
    IgbTypeRegistrationCall request;
    IgbTypeRegistrationFactory factory;
    std::shared_ptr<IgbTypeRegistrationOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    P phase=P::handle;bool running{},fault{};
};
IgbObjectRootRegistrationInvocation::IgbObjectRootRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbObjectRootRegistrationInvocation::~IgbObjectRootRegistrationInvocation(){
    if(!completed() && impl_->owner && impl_->result)impl_->owner->unfinished_.push_back(impl_->result);
}
bool IgbObjectRootRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbObjectRootRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbObjectRootRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete igObject root registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbObjectRootRegistrationInvocation::resume(std::shared_ptr<IgbObjectRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("igObject root registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("igObject root registration continuation was invalidated.");};
    const auto clear=[&]{s.callback={};s.factory={};s.operation.reset();check();};
    try{for(;;){check();switch(s.phase){
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Root registration requires metadata-pool handle805621F4.");
        s.pool=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{
        const auto status=s.pool->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.request.pool=s.pool->result();s.pool.reset();check();s.phase=P::allocate;break;
    }
    case P::allocate:
        s.request.kind=IgbTypeRegistrationServiceKind::create_callbacks;s.request.bytes=12;
        if(services && services->types)s.factory=services->types->factory;s.phase=P::call;break;
    case P::call:
        if(!s.factory && services && services->types)s.factory=services->types->factory;
        if(!s.factory)return pending("Root registration requires callback-list allocation80056138.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Root registration reached unbound callback-list allocation80056138.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Root registration requires its retained allocator continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        s.result=std::move(s.operation->callbacks_result);clear();s.phase=P::initialize;break;
    case P::initialize:
        if(s.result)igb_initialize_registration_callbacks(s.result);s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbObjectMetadataInvocation::IgbObjectMetadataInvocation(std::shared_ptr<IgbRegistrationMetaSlot> slot):slot_(std::move(slot)){}
bool IgbObjectMetadataInvocation::completed()const noexcept{return done_;}
bool IgbObjectMetadataInvocation::faulted()const noexcept{return fault_;}
std::shared_ptr<IgbRuntimeMetaType> IgbObjectMetadataInvocation::result()const{
    if(!done_ || fault_)fail("Incomplete igObject global read has no result.");return result_;
}
MessengerCallResult IgbObjectMetadataInvocation::resume(){
    if(fault_)fail("Faulted igObject global read cannot restart.");if(done_)return {true,0,{}};
    if(!slot_){fault_=true;fail("igObject global read has no canonical805622A4 cell.");}
    if(!*slot_)return pending("igObject global read requires represented805622A4.");
    result_=**slot_;done_=true;return {true,0,{}};
}
bool IgbObjectLayoutProbeInvocation::completed()const noexcept{return done_;}
bool IgbObjectLayoutProbeInvocation::faulted()const noexcept{return fault_;}
std::uint32_t IgbObjectLayoutProbeInvocation::value()const{
    if(!done_ || fault_)fail("Incomplete igObject layout probe has no result.");return value_;
}
MessengerCallResult IgbObjectLayoutProbeInvocation::resume(std::shared_ptr<IgbRuntimeTypesServices> services){
    if(fault_)fail("Faulted igObject layout probe cannot restart.");if(done_)return {true,value_,{}};
    try{
        //6665C has no callback: its packed-word operations collectively write1
        // regardless of the previous stack word. Both local words are fully
        // determined before the original singleton and offset reads.
        if(!captured_){if(!services || !services->singleton)return pending("igObject layout probe requires ArkCore80562100.");core_=*services->singleton;captured_=true;}
        if(!core_)fail("igObject layout probe dereferences null ArkCore at+394.");
        if(!core_->vtable_offset)return pending("igObject layout probe requires captured ArkCore offset+394.");
        const auto offset=*core_->vtable_offset;
        if(offset>4 || (offset&3u)!=0)fail("igObject layout probe exceeds its aligned8-byte local-object profile.");
        value_=offset==0?0x804717f0u:1u;done_=true;return {true,value_,{}};
    }catch(...){fault_=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_object_registration_function_factory(
    std::shared_ptr<IgbObjectRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbObjectRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x80066b08 && call.function!=0x80066ae0 && call.function!=0x80066a60))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("igObject registration services expired before selection.");
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x80066ae0){auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x80066b08);
            op->resume=[invocation,selected]{return invocation->resume(selected->callbacks);};}
        else if(call.function==0x80066a60){auto invocation=std::make_shared<IgbObjectRootRegistrationInvocation>();
            op->resume=[invocation,selected,out=op.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbObjectTypeRegistrationInvocation>();
            op->resume=[invocation,selected,out=op.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        return op;
    };
}
IgbTypeRegistrationFactory make_native_igb_object_registration_type_factory(
    std::shared_ptr<IgbObjectRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbObjectRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && (call.function==0x80066b08 || call.function==0x80066a60);
        const bool read=call.kind==K::get_parent && getter(call.function);
        const bool null_parent=call.kind==K::get_parent && call.function==0x80066aa4;
        const bool probe=call.kind==K::layout_probe && call.function==0x80066aac;
        if(!registration && !read && !null_parent && !probe)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto op=std::make_shared<IgbTypeRegistrationOperation>();
        if(null_parent){op->resume=[]{return MessengerCallResult{true,0,{}};};return op;}
        const auto selected=weak.lock();if(!selected)fail("igObject registration services expired before type operation selection.");
        if(read){auto invocation=std::make_shared<IgbObjectMetadataInvocation>(selected->metadata);
            op->resume=[invocation,selected,out=op.get()]{const auto status=invocation->resume();if(status.completed)out->metadata_result=invocation->result();return status;};}
        else if(probe){auto invocation=std::make_shared<IgbObjectLayoutProbeInvocation>();
            op->resume=[invocation,selected]{return invocation->resume(selected->types?selected->types->runtime:nullptr);};}
        else if(call.function==0x80066a60){auto invocation=std::make_shared<IgbObjectRootRegistrationInvocation>();
            op->resume=[invocation,selected,out=op.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbObjectTypeRegistrationInvocation>();
            op->resume=[invocation,selected,out=op.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        return op;
    };
}
} // namespace dw4
