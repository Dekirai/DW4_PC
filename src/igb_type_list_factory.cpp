#include "dw4/igb_type_list_factory.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbTypeListFactoryError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& f):flag(f){flag=true;}~Running(){flag=false;}};
using K=IgbTypeListServiceKind;
struct Call {
    IgbTypeListCall request;
    IgbTypeListFactory factory;
    std::shared_ptr<IgbTypeListOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbTypeListServices>& services,Check&& check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Metadata type list requires its reached native engine operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Metadata type list reached an unbound engine operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Metadata type list requires its retained continuation.");
        const auto result=callback();check();return result;
    }
};
}
struct IgbTypeListConstructorInvocation::Impl {
    enum class P {bind,first_base,intermediate,reset,clear_backing,clear_count,pool,after_pool,allocate,after_allocate,finish,call,done};
    IgbBootstrapPointer object;
    IgbTypeListConstructor entry{};
    std::shared_ptr<IgbRuntimeTypeList> types;
    std::shared_ptr<IgbTypeListServices> owner;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<IgbBootstrapBaseInvocation> base;
    IgbTypeListBacking incoming;
    Call call;P phase=P::bind,after=P::done;
    bool running{},fault{},owns{};
};
IgbTypeListConstructorInvocation::IgbTypeListConstructorInvocation(IgbBootstrapPointer object,IgbTypeListConstructor entry)
    :impl_(std::make_unique<Impl>()){impl_->object=std::move(object);impl_->entry=entry;}
IgbTypeListConstructorInvocation::~IgbTypeListConstructorInvocation(){
    auto& s=*impl_;if(s.owns && !completed()){
        s.types->construction_fault_=true;
        if(s.owner){if(s.object.storage)s.owner->unfinished_.push_back(s.object.storage);
            if(s.incoming.allocation.storage)s.owner->unfinished_.push_back(s.incoming.allocation.storage);
        }else if(s.object.storage && s.object.storage->bytes)s.types->retained_backings_.push_back(s.object.storage->bytes);
    }
}
bool IgbTypeListConstructorInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbTypeListConstructorInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeTypeList> IgbTypeListConstructorInvocation::result() const{
    if(!completed() || faulted())fail("Incomplete metadata-list construction has no result.");return impl_->types;
}
MessengerCallResult IgbTypeListConstructorInvocation::resume(std::shared_ptr<IgbTypeListServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.types->construction_fault_=true;fail("Metadata-list constructor requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.owns && s.types->construction_fault_))fail("Metadata-list construction was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbTypeListCall&{
        clear();s.call.request.kind=kind;s.call.request.object=s.object;s.call.factory=services?services->factory:IgbTypeListFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto retain_backing=[&]{
        if(s.types->backing)s.types->retained_backings_.push_back(s.types->backing);
        if(s.types->backing_bytes)s.types->retained_backings_.push_back(s.types->backing_bytes);
    };
    try{for(;;){check();switch(s.phase){
    case P::bind:
        // Numeric null reaches the original constructor's first store.
        if(igb_bootstrap_address(s.object)==0)igb_bootstrap_write_word(s.object,0,0x804717f0);
        s.types=igb_bootstrap_types(s.object);
        if(!s.types)return pending("Metadata-list construction requires its explicit typed allocation binding.");
        if(s.types->construction_fault_ || !s.types->construction_active_.expired())fail("Metadata-list allocation is faulted or already being constructed.");
        s.types->construction_active_=s.token;s.owns=true;
        s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,s.entry!=IgbTypeListConstructor::reset);
        s.phase=s.entry==IgbTypeListConstructor::reset?P::reset:P::first_base;break;
    case P::first_base:{
        const auto result=s.base->resume(services?services->bootstrap:nullptr);check();if(!result.completed)return result;
        s.base.reset();check();s.phase=P::intermediate;break;
    }
    case P::intermediate:
        igb_bootstrap_write_word(s.object,0,0x80475c54);
        s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,false);s.phase=P::reset;break;
    case P::reset:{
        const auto result=s.base->resume(services?services->bootstrap:nullptr);check();if(!result.completed)return result;
        s.base.reset();check();igb_bootstrap_write_word(s.object,0,0x80475c54);s.phase=P::clear_backing;break;
    }
    case P::clear_backing:
        igb_bootstrap_write_word(s.object,8,0);retain_backing();s.types->backing.reset();s.types->backing_bytes.reset();s.types->backing_address=0;s.types->backing_offset=0;
        s.phase=P::clear_count;break;
    case P::clear_count:
        igb_bootstrap_write_word(s.object,12,0);s.types->count=0;
        s.phase=s.entry==IgbTypeListConstructor::reset?P::finish:P::pool;break;
    case P::pool:{const auto handle=igb_bootstrap_read_byte(s.object,4);select(K::object_pool,P::after_pool).handle=handle;break;}
    case P::after_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::allocate;break;
    case P::allocate:
        if(!s.pool)fail("Metadata-list backing allocation dereferences a null object pool.");
        {auto& call=select(K::allocate_backing,P::after_allocate);call.pool=s.pool;call.bytes=16;}break;
    case P::after_allocate:{
        s.incoming=std::move(s.call.operation->backing_result);const auto address=igb_bootstrap_address(s.incoming.allocation);
        clear();igb_bootstrap_write_word(s.object,8,address);retain_backing();
        s.types->backing_address=address;
        s.types->backing_offset=address?s.incoming.allocation.offset:0;
        s.types->backing=address?s.incoming.types:nullptr;
        s.types->backing_bytes=address && s.incoming.allocation.storage?s.incoming.allocation.storage->bytes:nullptr;
        s.incoming={};s.phase=P::finish;break;
    }
    case P::finish:
        clear();if(s.entry==IgbTypeListConstructor::derived)igb_bootstrap_write_word(s.object,0,0x80475cb0);
        s.types->construction_active_.reset();s.owns=false;s.phase=P::done;return {true,0,{}};
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;if(s.owns)s.types->construction_fault_=true;throw;}
}
struct IgbTypeListFactoryInvocation::Impl {
    enum class P {gate,register_type,metadata,reflected,after_reflected,allocate,construct,tail_2c,tail_24,typed_result,call,done};
    std::shared_ptr<ModelSceneObject> pool;
    IgbTypeListFactoryEntry entry{};
    std::shared_ptr<IgbTypeListServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbRuntimeTypeList> result;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbRawAllocationInvocation> allocator;
    std::unique_ptr<IgbTypeListConstructorInvocation> constructor;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    Call call;P phase=P::gate,after=P::done;bool running{},fault{};
};
IgbTypeListFactoryInvocation::IgbTypeListFactoryInvocation(std::shared_ptr<ModelSceneObject> pool,IgbTypeListFactoryEntry entry):impl_(std::make_unique<Impl>()){impl_->pool=std::move(pool);impl_->entry=entry;}
IgbTypeListFactoryInvocation::~IgbTypeListFactoryInvocation(){auto& s=*impl_;if(!completed() && s.owner && s.allocation.storage)s.owner->unfinished_.push_back(s.allocation.storage);}
bool IgbTypeListFactoryInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbTypeListFactoryInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeTypeList> IgbTypeListFactoryInvocation::result() const{if(!completed() || faulted())fail("Incomplete metadata-list factory has no result.");return impl_->result;}
IgbBootstrapPointer IgbTypeListFactoryInvocation::allocation() const{if(!completed() || faulted())fail("Incomplete metadata-list factory has no allocation.");return impl_->allocation;}
MessengerCallResult IgbTypeListFactoryInvocation::resume(std::shared_ptr<IgbTypeListServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata-list factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Metadata-list factory continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbTypeListCall&{
        clear();s.call.request.kind=kind;s.call.request.entry=s.entry;s.call.request.object=s.allocation;s.call.factory=services?services->factory:IgbTypeListFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto tail=[&](std::uint32_t slot,P after){
        clear();const auto word=igb_bootstrap_read_word(s.allocation,0);
        if(word==0)fail("Metadata-list factory dereferences a null tail vtable.");
        if(word==0x80475cb0u || word==0x80475c54u || word==0x804717f0u){s.phase=after;return;}
        auto& call=select(K::object_virtual,after);call.vtable_word=word;call.virtual_slot=slot;call.argument=0;
    };
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!services || !services->bootstrap || !services->bootstrap->runtime || !services->bootstrap->runtime->singleton)
            return pending("Metadata-list factory requires represented ArkCore80562100.");
        if(!*services->bootstrap->runtime->singleton)fail("Metadata-list factory dereferences a null ArkCore.");
        if((*services->bootstrap->runtime->singleton)->enabled)s.phase=P::register_type;
        else{s.allocator=std::make_unique<IgbRawAllocationInvocation>(16,0,s.pool);s.phase=P::allocate;}break;
    case P::register_type:
        if(s.registration || (services && services->callbacks)){
            if(!s.registration)s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(s.entry==IgbTypeListFactoryEntry::children?0x8002e07c:0x8002dd40);
            const auto progress=s.registration->resume(services?services->callbacks:nullptr);check();if(!progress.completed)return progress;
            s.registration.reset();check();s.phase=P::metadata;
        }else select(K::register_type,P::metadata).function=s.entry==IgbTypeListFactoryEntry::children?0x8002e054:0x8002dd18;
        break;
    case P::metadata:{
        clear();const auto cell=services?(s.entry==IgbTypeListFactoryEntry::children?services->children_metadata:services->registry_metadata):nullptr;
        if(!cell || !cell->has_value())return pending("Metadata-list factory requires its live canonical metadata cell after registration.");
        s.metadata=**cell;s.phase=P::reflected;break;
    }
    case P::reflected:{auto& call=select(K::reflected_factory,P::after_reflected);call.metadata=s.metadata;call.pool=s.pool;break;}
    case P::after_reflected:s.allocation=std::move(s.call.operation->allocation_result);clear();s.phase=P::typed_result;break;
    case P::allocate:{
        const auto result=s.allocator->resume(services?services->bootstrap:nullptr);check();if(!result.completed)return result;
        s.allocation=s.allocator->result();s.allocator.reset();check();
        if(igb_bootstrap_address(s.allocation)==0)s.phase=P::tail_2c;
        else{s.constructor=std::make_unique<IgbTypeListConstructorInvocation>(s.allocation,
            s.entry==IgbTypeListFactoryEntry::children?IgbTypeListConstructor::base:IgbTypeListConstructor::derived);s.phase=P::construct;}break;
    }
    case P::construct:{
        const auto result=s.constructor->resume(services);check();if(!result.completed)return result;
        s.result=s.constructor->result();s.constructor.reset();check();s.phase=P::tail_2c;break;
    }
    case P::tail_2c:tail(0x2c,P::tail_24);break;
    case P::tail_24:tail(0x24,P::typed_result);break;
    case P::typed_result:
        clear();if(igb_bootstrap_address(s.allocation)!=0){s.result=igb_bootstrap_types(s.allocation);
            if(s.owner)s.owner->completed_allocations.push_back(s.allocation);}
        else s.result.reset();s.phase=P::done;return {true,0,{}};
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbMetadataOperationsFactory make_native_igb_type_list_metadata_factory(
    std::shared_ptr<IgbTypeListServices> services,IgbMetadataOperationsFactory remaining){
    std::weak_ptr<IgbTypeListServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbMetadataOperationCall& call){
        if(call.kind!=IgbMetadataOperationKind::create_children)
            return remaining?remaining(call):std::shared_ptr<IgbMetadataOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata child-list factory services expired before selection.");
        auto invocation=std::make_shared<IgbTypeListFactoryInvocation>(call.pool,IgbTypeListFactoryEntry::children);
        auto operation=std::make_shared<IgbMetadataOperation>();operation->resume=[invocation,selected,out=operation.get()]{
            const auto progress=invocation->resume(selected);if(progress.completed)out->children_result=invocation->result();return progress;};
        return operation;
    };
}
IgbArkCoreFactory make_native_igb_type_list_bootstrap_factory(
    std::shared_ptr<IgbTypeListServices> services,IgbArkCoreFactory remaining){
    std::weak_ptr<IgbTypeListServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbArkCoreCall& call){
        if(call.kind!=IgbArkCoreServiceKind::create_type_list)
            return remaining?remaining(call):std::shared_ptr<IgbArkCoreOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Bootstrap type-list factory services expired before selection.");
        auto invocation=std::make_shared<IgbTypeListFactoryInvocation>(call.pool,IgbTypeListFactoryEntry::registry);
        auto operation=std::make_shared<IgbArkCoreOperation>();operation->resume=[invocation,selected,out=operation.get()]{
            const auto progress=invocation->resume(selected);if(progress.completed){out->types_result=invocation->result();out->allocation_result=invocation->allocation();}return progress;};
        return operation;
    };
}
} // namespace dw4
