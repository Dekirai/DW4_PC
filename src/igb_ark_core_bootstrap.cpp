#include "dw4/igb_ark_core_bootstrap.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbArkCoreBootstrapError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
template<class T>void retain(const std::shared_ptr<IgbRuntimeTypeRegistry>& core,const std::shared_ptr<T>& value){
    if(value)core->overwritten_startup_owners_.push_back(value);
}
template<class T>void retain(const std::shared_ptr<IgbRuntimeTypeRegistry>& core,const std::optional<std::shared_ptr<T>>& value){
    if(value)retain(core,*value);
}
constexpr std::uint32_t registration_bodies[]{
    0x8002aafc,0x80039d94,0x80039720,0x80038ec4,0x80039254,0x8002cbe4,
    0x80036abc,0x800368fc,0x800366ac,0x800364ec,0x80035da8,0x80035be8,
    0x80024a1c,0x8002435c,0x800384a0,0x8003826c,0x8003be00,0x8002d5ac,
    0x80030d78,0x8002faf4,0x8002cfd0,0x80023234};
struct Call {
    IgbArkCoreCall request;
    IgbArkCoreFactory factory;
    std::shared_ptr<IgbArkCoreOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbArkCoreBootstrapServices>& services,Check&& check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("ArkCore bootstrap requires its reached native engine operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("ArkCore bootstrap reached an unbound engine operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("ArkCore bootstrap requires its captured continuation.");
        const auto result=callback();check();return result;
    }
};
}
void igb_construct_ark_core(const IgbArkCoreObject& object){
    if(!object.core)fail("ArkCore construction requires its canonical native owner.");
    auto& c=*object.core;
    if(c.bootstrap_fault_ || !c.bootstrap_active_.expired())fail("ArkCore construction cannot overwrite a faulted or active bootstrap.");
    const auto word=[&](std::uint32_t off,std::uint32_t value){igb_bootstrap_write_word(object.allocation,off,value);};
    const auto byte=[&](std::uint32_t off){igb_bootstrap_write_byte(object.allocation,off,0);};
    try{
        word(0x398,0);word(0x39c,0);byte(0x14);c.field_initialization_enabled=std::uint8_t{0};byte(0x15);byte(0x16);byte(0);c.enabled=0;
        word(0x18,0);retain(object.core,c.types);c.types.reset();word(0x30,0);word(0x20,0);
        word(0x24,0);retain(object.core,c.secondary_types);c.secondary_types=std::shared_ptr<IgbRuntimeTypeList>{};
        word(0x28,0);retain(object.core,c.bootstrap_callbacks);c.bootstrap_callbacks=std::shared_ptr<IgbRegistrationCallbackList>{};
        word(0x2c,0);retain(object.core,c.registration_callbacks);c.registration_callbacks=std::shared_ptr<IgbRegistrationCallbackList>{};
        word(0x38,0);word(0x44,0);word(0x48,0);
        for(std::uint32_t off=0x74;off<=0x7a;++off)byte(off);
        word(0x7c,0x80000);for(std::uint32_t off=0x80;off<=0x8c;off+=4)word(off,0xffffffff);
        for(const auto off:{0x90u,0x110u,0x190u,0x210u,0x310u})byte(off);
        word(0x50,0);word(0x54,0);word(0x34,0);retain(object.core,c.registration_callback_pool);c.registration_callback_pool=std::shared_ptr<ModelSceneObject>{};
        byte(0x5c);word(0x60,0);retain(object.core,c.registration_filter_pool);c.registration_filter_pool=std::shared_ptr<ModelSceneObject>{};
        word(0x58,0);word(0x64,0);c.registration_filter_enabled=0;
        word(0x68,0);retain(object.core,c.registration_filter_names);c.registration_filter_names=std::shared_ptr<ModelSceneObject>{};
        word(0x3c,0);word(0x394,0);c.vtable_offset=0;word(0x4c,0);word(0x6c,0);word(0x70,0);
    }catch(...){c.bootstrap_fault_=true;throw;}
}

struct IgbArkCoreBootstrapInvocation::Impl {
    enum class P {entry,platform,after_platform,manager,after_manager_allocation,after_manager_constructor,
        publish_manager,manager_begin,pool,lookup,list,after_list,capacity,after_size,reserve,after_reserve,
        callbacks,after_callbacks,register_meta,enable,counts,enum_count,registrations,final_metadata,
        after_final_metadata,publish_final_metadata,manager_end,call,done};
    IgbArkCoreObject object;
    std::shared_ptr<IgbArkCoreBootstrapServices> owner;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<IgbMemoryPoolRegistry> manager;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbRuntimeTypeList> types;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbPoolByHandleInvocation> lookup;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    Call call;P phase=P::entry,after=P::done;
    std::uint32_t returned{};unsigned list_index{},callback_index{},registration_index{};
    bool running{},fault{},owns{};
};
IgbArkCoreBootstrapInvocation::IgbArkCoreBootstrapInvocation(IgbArkCoreObject object):impl_(std::make_unique<Impl>()){impl_->object=std::move(object);}
IgbArkCoreBootstrapInvocation::~IgbArkCoreBootstrapInvocation(){auto& s=*impl_;if(s.owns && !completed()){
    s.object.core->bootstrap_fault_=true;
    if(s.owner){if(s.object.allocation.storage)s.owner->unfinished_.push_back(s.object.allocation.storage);
        if(s.allocation.storage)s.owner->unfinished_.push_back(s.allocation.storage);}
    else if(s.object.allocation.storage && s.object.allocation.storage->bytes)retain(s.object.core,s.object.allocation.storage->bytes);
}}
bool IgbArkCoreBootstrapInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbArkCoreBootstrapInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbArkCoreBootstrapInvocation::resume(std::shared_ptr<IgbArkCoreBootstrapServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbArkCoreServiceKind;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.object.core->bootstrap_fault_=true;fail("ArkCore bootstrap requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.owns && s.object.core->bootstrap_fault_))fail("ArkCore bootstrap continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbArkCoreCall&{clear();s.call.request.kind=kind;s.call.request.object=s.object;
        s.call.factory=services?services->factory:IgbArkCoreFactory{};s.after=after;s.phase=P::call;return s.call.request;};
    const auto memory_pools=[&](){return services && services->pools?services->pools->memory_pools:nullptr;};
    const auto word=[&](std::uint32_t offset,std::uint32_t value){igb_bootstrap_write_word(s.object.allocation,offset,value);};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        if(!s.object.core)fail("ArkCore bootstrap dereferences a null canonical core.");
        if(s.object.core->bootstrap_fault_ || !s.object.core->bootstrap_active_.expired())fail("ArkCore bootstrap owner is faulted or already active.");
        s.object.core->bootstrap_active_=s.token;s.owns=true;
        igb_bootstrap_write_byte(s.object.allocation,0x14,1);s.object.core->field_initialization_enabled=std::uint8_t{1};
        s.phase=P::platform;break;
    case P::platform:select(K::platform_initialize,P::after_platform);break;
    case P::after_platform:clear();word(0x394,0);s.object.core->vtable_offset=0;s.phase=P::manager;break;
    case P::manager:{
        const auto pools=memory_pools();if(!pools || !pools->fallback)return pending("ArkCore bootstrap requires fallback manager global805621C8.");
        if(*pools->fallback){s.phase=P::manager_begin;break;}
        select(K::allocate_manager,P::after_manager_allocation).bytes=0x19f4;break;
    }
    case P::after_manager_allocation:
        s.allocation=std::move(s.call.operation->allocation_result);s.manager=std::move(s.call.operation->manager_result);clear();
        if(igb_bootstrap_address(s.allocation)!=0){auto& call=select(K::construct_manager,P::after_manager_constructor);call.allocation=s.allocation;call.manager=s.manager;}
        else{s.manager.reset();s.phase=P::publish_manager;}break;
    case P::after_manager_constructor:
        s.allocation=std::move(s.call.operation->allocation_result);s.manager=std::move(s.call.operation->manager_result);clear();
        if(igb_bootstrap_address(s.allocation)==0)s.manager.reset();
        else if(!s.manager)fail("Constructed manager requires its explicit canonical owner.");
        s.phase=P::publish_manager;break;
    case P::publish_manager:{
        const auto pools=memory_pools();if(!pools)return pending("ArkCore bootstrap requires canonical manager global storage.");
        retain(s.object.core,pools->fallback);pools->fallback=s.manager;s.phase=P::manager_begin;break;
    }
    case P::manager_begin:case P::manager_end:{
        const bool ending=s.phase==P::manager_end;const auto pools=memory_pools();
        if(!pools || !pools->fallback)return pending("ArkCore bootstrap requires its freshly reloaded fallback manager.");
        const auto manager=*pools->fallback;if(!manager)fail("ArkCore bootstrap dereferences a null fallback manager.");
        auto& call=select(K::manager_virtual,ending?P::done:P::pool);call.manager=manager;call.virtual_slot=ending?0xacu:0xa8u;break;
    }
    case P::pool:
        clear();if(!services || !services->bootstrap || !services->bootstrap->default_pool_handle)return pending("ArkCore type-list allocation requires current default handle805621E0.");
        s.lookup=std::make_unique<IgbPoolByHandleInvocation>(*services->bootstrap->default_pool_handle);s.phase=P::lookup;break;
    case P::lookup:{const auto r=s.lookup->resume(services?services->pools:nullptr);check();if(!r.completed)return r;
        s.pool=s.lookup->result();s.lookup.reset();check();s.phase=P::list;break;}
    case P::list:select(K::create_type_list,P::after_list).pool=s.pool;break;
    case P::after_list:{
        const auto pointer=std::move(s.call.operation->allocation_result);const auto types=std::move(s.call.operation->types_result);
        const auto address=igb_bootstrap_address(pointer);clear();if(address && !types)fail("Type-list allocation has no matching canonical metadata-list owner.");
        if(pointer.storage && pointer.storage->bytes)retain(s.object.core,pointer.storage->bytes);
        const auto incoming=address?types:nullptr;
        if(s.list_index==0){word(0x18,address);retain(s.object.core,s.object.core->types);s.object.core->types=incoming;s.phase=P::capacity;}
        else{word(0x24,address);retain(s.object.core,s.object.core->secondary_types);s.object.core->secondary_types=incoming;s.phase=P::callbacks;}
        break;
    }
    case P::capacity:
        s.types=s.object.core->types;if(!s.types)fail("ArkCore bootstrap dereferences a null main type list.");
        if(const auto backing=s.types->backing){auto& call=select(K::backing_size,P::after_size);call.types=s.types;call.type_backing=backing;}
        else{s.returned=0;s.phase=P::after_size;}break;
    case P::after_size:
        clear();s.phase=(s.returned>>2u)>1024u?P::after_reserve:P::reserve;break;
    case P::reserve:{auto& call=select(K::reserve_types,P::after_reserve);call.types=s.types;call.argument=1024;break;}
    case P::after_reserve:clear();++s.list_index;s.phase=P::pool;break;
    case P::callbacks:select(K::allocate_callbacks,P::after_callbacks).bytes=12;break;
    case P::after_callbacks:{
        const auto pointer=std::move(s.call.operation->allocation_result);auto callbacks=std::move(s.call.operation->callbacks_result);
        const auto address=igb_bootstrap_address(pointer);clear();
        if(pointer.storage && pointer.storage->bytes)retain(s.object.core,pointer.storage->bytes);
        if(address){if(!callbacks)fail("Callback-list allocation has no matching canonical owner.");igb_initialize_registration_callbacks(callbacks);}
        else callbacks.reset();
        if(s.callback_index==0){word(0x28,address);retain(s.object.core,s.object.core->bootstrap_callbacks);s.object.core->bootstrap_callbacks=callbacks;}
        else{word(0x2c,address);retain(s.object.core,s.object.core->registration_callbacks);s.object.core->registration_callbacks=callbacks;}
        s.phase=++s.callback_index==2?P::register_meta:P::callbacks;break;
    }
    case P::register_meta:
        if(!s.registration)s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8002a0d0);
        {const auto r=s.registration->resume(services?services->callbacks:nullptr);check();if(!r.completed)return r;}
        s.registration.reset();check();s.phase=P::enable;break;
    case P::enable:igb_bootstrap_write_byte(s.object.allocation,0,1);s.object.core->enabled=1;s.phase=P::counts;break;
    case P::counts:
        if(!s.object.core->types)fail("ArkCore bootstrap count snapshot dereferences a null main type list.");
        word(4,std::bit_cast<std::uint32_t>(s.object.core->types->count));s.phase=P::enum_count;break;
    case P::enum_count:
        if(!services || !services->enum_types)return pending("ArkCore bootstrap requires enum type-list global8056229C.");
        if(!*services->enum_types)fail("ArkCore bootstrap dereferences a null enum type list.");
        word(8,std::bit_cast<std::uint32_t>((*services->enum_types)->count));s.phase=P::registrations;break;
    case P::registrations:
        if(!s.registration)s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(registration_bodies[s.registration_index]);
        {const auto r=s.registration->resume(services?services->callbacks:nullptr);check();if(!r.completed)return r;}
        s.registration.reset();check();if(++s.registration_index==std::size(registration_bodies))s.phase=P::final_metadata;break;
    case P::final_metadata:select(K::final_metadata,P::after_final_metadata);break;
    case P::after_final_metadata:
        s.metadata=std::move(s.call.operation->metadata_result);clear();s.phase=P::publish_final_metadata;break;
    case P::publish_final_metadata:{
        if(!services)return pending("ArkCore bootstrap requires final metadata global80561688 storage.");
        retain(s.object.core,services->final_metadata);services->final_metadata=s.metadata;s.phase=P::manager_end;break;
    }
    case P::call:{const auto r=s.call.poll(services,check);if(!r.completed)return r;s.returned=r.value;s.phase=s.after;break;}
    case P::done:
        clear();s.object.core->bootstrap_active_.reset();s.owns=false;return {true,0,{}};
    }}}catch(...){s.fault=true;if(s.owns)s.object.core->bootstrap_fault_=true;throw;}
}
} // namespace dw4
