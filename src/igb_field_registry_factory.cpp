#include "dw4/igb_field_registry_factory.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldRegistryFactoryError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& f):flag(f){flag=true;}~Running(){flag=false;}};
using K=IgbFieldRegistryServiceKind;
struct Call {
    IgbFieldRegistryCall request;
    IgbFieldRegistryFactory factory;
    std::shared_ptr<IgbFieldRegistryOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbFieldRegistryServices>& services,Check&& check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Internal field registry requires its reached native engine operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Internal field registry reached an unbound engine operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Internal field registry requires its retained continuation.");
        const auto result=callback();check();return result;
    }
};
}
struct IgbFieldRegistryConstructorInvocation::Impl {
    enum class P {bind,first_base,intermediate,reset,clear_backing,clear_count,pool,after_pool,allocate,after_allocate,finish,call,done};
    IgbBootstrapPointer object;
    IgbFieldRegistryConstructor entry{};
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbFieldRegistryServices> owner;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<IgbBootstrapBaseInvocation> base;
    IgbFieldRegistryBacking incoming;
    Call call;P phase=P::bind,after=P::done;
    bool running{},fault{},owns{};
};
IgbFieldRegistryConstructorInvocation::IgbFieldRegistryConstructorInvocation(IgbBootstrapPointer object,IgbFieldRegistryConstructor entry)
    :impl_(std::make_unique<Impl>()){impl_->object=std::move(object);impl_->entry=entry;}
IgbFieldRegistryConstructorInvocation::~IgbFieldRegistryConstructorInvocation(){
    auto& s=*impl_;if(s.owns && !completed()){
        s.fields->construction_fault_=true;
        if(s.owner){if(s.object.storage)s.owner->unfinished_.push_back(s.object.storage);
            if(s.incoming.allocation.storage)s.owner->unfinished_.push_back(s.incoming.allocation.storage);
        }else if(s.object.storage && s.object.storage->bytes)s.fields->retained_backings_.push_back(s.object.storage->bytes);
    }
}
bool IgbFieldRegistryConstructorInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldRegistryConstructorInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeFieldRegistry> IgbFieldRegistryConstructorInvocation::result() const{
    if(!completed() || faulted())fail("Incomplete internal-list construction has no result.");return impl_->fields;
}
MessengerCallResult IgbFieldRegistryConstructorInvocation::resume(std::shared_ptr<IgbFieldRegistryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.fields->construction_fault_=true;fail("Internal-list constructor requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.owns && s.fields->construction_fault_))fail("Internal-list construction was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbFieldRegistryCall&{
        clear();s.call.request.kind=kind;s.call.request.object=s.object;s.call.factory=services?services->factory:IgbFieldRegistryFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto retain_backing=[&]{
        if(s.fields->backing)s.fields->retained_backings_.push_back(s.fields->backing);
        if(s.fields->backing_bytes)s.fields->retained_backings_.push_back(s.fields->backing_bytes);
    };
    try{for(;;){check();switch(s.phase){
    case P::bind:
        // Numeric null reaches the original constructor's first store.
        if(igb_bootstrap_address(s.object)==0)igb_bootstrap_write_word(s.object,0,0x804717f0);
        s.fields=igb_bootstrap_fields(s.object);
        if(!s.fields)return pending("Internal-list construction requires its explicit typed allocation binding.");
        if(s.fields->construction_fault_ || !s.fields->construction_active_.expired())fail("Internal-list allocation is faulted or already being constructed.");
        s.fields->construction_active_=s.token;s.owns=true;
        s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,s.entry!=IgbFieldRegistryConstructor::reset);
        s.phase=s.entry==IgbFieldRegistryConstructor::reset?P::reset:P::first_base;break;
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
        igb_bootstrap_write_word(s.object,8,0);retain_backing();s.fields->backing.reset();s.fields->backing_bytes.reset();s.fields->backing_address=0;
        s.phase=P::clear_count;break;
    case P::clear_count:
        igb_bootstrap_write_word(s.object,12,0);s.fields->count=0;
        s.phase=s.entry==IgbFieldRegistryConstructor::reset?P::finish:P::pool;break;
    case P::pool:{const auto handle=igb_bootstrap_read_byte(s.object,4);select(K::object_pool,P::after_pool).handle=handle;break;}
    case P::after_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::allocate;break;
    case P::allocate:
        if(!s.pool)fail("Internal-list backing allocation dereferences a null object pool.");
        {auto& call=select(K::allocate_backing,P::after_allocate);call.pool=s.pool;call.bytes=16;}break;
    case P::after_allocate:{
        s.incoming=std::move(s.call.operation->backing_result);const auto address=igb_bootstrap_address(s.incoming.allocation);
        clear();igb_bootstrap_write_word(s.object,8,address);retain_backing();
        s.fields->backing_address=address;
        s.fields->backing=address?s.incoming.fields:nullptr;
        s.fields->backing_bytes=address && s.incoming.allocation.storage?s.incoming.allocation.storage->bytes:nullptr;
        s.incoming={};s.phase=P::finish;break;
    }
    case P::finish:
        clear();if(s.entry==IgbFieldRegistryConstructor::derived)igb_bootstrap_write_word(s.object,0,0x80475cb0);
        s.fields->construction_active_.reset();s.owns=false;s.phase=P::done;return {true,0,{}};
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;if(s.owns)s.fields->construction_fault_=true;throw;}
}
struct IgbFieldRegistryFactoryInvocation::Impl {
    enum class P {gate,register_type,metadata,reflected,after_reflected,allocate,construct,tail_2c,tail_24,typed_result,call,done};
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbFieldRegistryServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeFieldRegistry> result;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbRawAllocationInvocation> allocator;
    std::unique_ptr<IgbFieldRegistryConstructorInvocation> constructor;
    Call call;P phase=P::gate,after=P::done;bool running{},fault{};
};
IgbFieldRegistryFactoryInvocation::IgbFieldRegistryFactoryInvocation(std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){impl_->pool=std::move(pool);}
IgbFieldRegistryFactoryInvocation::~IgbFieldRegistryFactoryInvocation(){auto& s=*impl_;if(!completed() && s.owner && s.allocation.storage)s.owner->unfinished_.push_back(s.allocation.storage);}
bool IgbFieldRegistryFactoryInvocation::completed() const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldRegistryFactoryInvocation::faulted() const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeFieldRegistry> IgbFieldRegistryFactoryInvocation::result() const{if(!completed() || faulted())fail("Incomplete internal-list factory has no result.");return impl_->result;}
IgbBootstrapPointer IgbFieldRegistryFactoryInvocation::allocation() const{if(!completed() || faulted())fail("Incomplete internal-list factory has no allocation.");return impl_->allocation;}
MessengerCallResult IgbFieldRegistryFactoryInvocation::resume(std::shared_ptr<IgbFieldRegistryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Internal-list factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Internal-list factory continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbFieldRegistryCall&{
        clear();s.call.request.kind=kind;s.call.request.object=s.allocation;s.call.factory=services?services->factory:IgbFieldRegistryFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto tail=[&](std::uint32_t slot,P after){
        clear();const auto word=igb_bootstrap_read_word(s.allocation,0);
        if(word==0)fail("Internal-list factory dereferences a null tail vtable.");
        if(word==0x80475cb0u || word==0x80475c54u || word==0x804717f0u){s.phase=after;return;}
        auto& call=select(K::object_virtual,after);call.vtable_word=word;call.virtual_slot=slot;call.argument=0;
    };
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!services || !services->bootstrap || !services->bootstrap->runtime || !services->bootstrap->runtime->singleton)
            return pending("Internal-list factory requires represented ArkCore80562100.");
        if(!*services->bootstrap->runtime->singleton)fail("Internal-list factory dereferences a null ArkCore.");
        if((*services->bootstrap->runtime->singleton)->enabled)s.phase=P::register_type;
        else{s.allocator=std::make_unique<IgbRawAllocationInvocation>(16,0,s.pool);s.phase=P::allocate;}break;
    case P::register_type:select(K::register_type,P::metadata);break;
    case P::metadata:
        clear();if(!services || !services->metadata)return pending("Internal-list factory requires live805619CC metadata after registration.");
        s.metadata=*services->metadata;s.phase=P::reflected;break;
    case P::reflected:{auto& call=select(K::reflected_factory,P::after_reflected);call.metadata=s.metadata;call.pool=s.pool;break;}
    case P::after_reflected:s.allocation=std::move(s.call.operation->allocation_result);clear();s.phase=P::typed_result;break;
    case P::allocate:{
        const auto result=s.allocator->resume(services?services->bootstrap:nullptr);check();if(!result.completed)return result;
        s.allocation=s.allocator->result();s.allocator.reset();check();
        if(igb_bootstrap_address(s.allocation)==0)s.phase=P::tail_2c;
        else{s.constructor=std::make_unique<IgbFieldRegistryConstructorInvocation>(s.allocation);s.phase=P::construct;}break;
    }
    case P::construct:{
        const auto result=s.constructor->resume(services);check();if(!result.completed)return result;
        s.result=s.constructor->result();s.constructor.reset();check();s.phase=P::tail_2c;break;
    }
    case P::tail_2c:tail(0x2c,P::tail_24);break;
    case P::tail_24:tail(0x24,P::typed_result);break;
    case P::typed_result:
        clear();if(igb_bootstrap_address(s.allocation)!=0){s.result=igb_bootstrap_fields(s.allocation);if(!s.result)return pending("Internal-list factory requires an explicit typed result binding.");}
        else s.result.reset();s.phase=P::done;return {true,0,{}};
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
