#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_string_pool.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataFactoryError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation> struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check> MessengerCallResult poll(const Factory& fallback,Check&& check){
        if(!factory)factory=fallback;
        if(!factory)return pending("Metadata construction requires its reached operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Metadata construction reached an unbound operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Metadata construction requires its retained continuation.");
        const auto result=callback();check();return result;
    }
};
using MetaCall=Call<IgbMetadataFactoryCall,IgbMetadataFactory,IgbMetadataFactoryOperation>;
using StringCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
using K=IgbMetadataFactoryServiceKind;
using S=IgbDependenciesServiceKind;
void retain_name(const std::shared_ptr<IgbRuntimeMetaType>& metadata){
    if(metadata->default_pool_string && *metadata->default_pool_string)
        metadata->overwritten_constructor_owners_.push_back(*metadata->default_pool_string);
    if(metadata->default_pool_name && *metadata->default_pool_name)
        metadata->overwritten_constructor_strings_.push_back(*metadata->default_pool_name);
}
void store_word(const IgbBootstrapPointer& object,const std::shared_ptr<IgbRuntimeMetaType>& m,
                std::uint32_t offset,std::uint32_t value){
    // These are raw constructor stores, not reference assignments.
    if(offset==0x1c && m->name)m->overwritten_constructor_strings_.push_back(m->name);
    if(offset==0x1c && m->name_string && *m->name_string)m->overwritten_constructor_owners_.push_back(*m->name_string);
    if(offset==0x60)retain_name(m);
    if(offset==0x28 && m->fields)m->overwritten_constructor_owners_.push_back(m->fields);
    if(offset==0x30 && m->instances && *m->instances)m->overwritten_constructor_owners_.push_back(*m->instances);
    if(offset==0x34 && m->children && *m->children)m->overwritten_constructor_owners_.push_back(*m->children);
    if(offset==0x38 && m->parent && *m->parent)m->overwritten_constructor_owners_.push_back(*m->parent);
    igb_bootstrap_write_word(object,offset,value);
    switch(offset){
    case 0x08:m->word_08=value;break;case 0x0c:m->word_0c=value;break;
    case 0x10:m->major=value;break;case 0x14:m->minor=value;break;
    case 0x1c:m->name.reset();m->name_string=std::shared_ptr<IgbInternedString>{};break;case 0x20:m->allocation_prefix=value;break;
    case 0x24:m->class_flags=value;break;case 0x28:m->fields.reset();break;
    case 0x2c:m->instance_count=value;break;case 0x30:m->instances=std::shared_ptr<IgbObjectInstanceList>{};break;
    case 0x34:m->word_34=value;m->children=std::shared_ptr<IgbRuntimeTypeList>{};break;case 0x38:m->parent=std::shared_ptr<IgbRuntimeMetaType>{};break;
    case 0x3c:m->factory_redirect=value;break;case 0x40:m->word_40=value;break;
    case 0x44:m->word_44=value;break;case 0x48:m->native_size=value;break;
    case 0x4c:m->post_layout_callback=value;break;case 0x50:m->word_50=value;break;
    case 0x54:m->field_callback=value;break;case 0x5c:m->type_word=value;break;
    case 0x60:m->default_pool_name=SystemStageString{};m->default_pool_string=std::shared_ptr<IgbInternedString>{};
        m->default_pool_string_address=0;break;
    default:break;
    }
}
IgbMetadataFactoryOperation* native_fields(const IgbMetadataFactoryCall& request,
    const std::shared_ptr<IgbMetadataFactoryServices>& services,std::shared_ptr<IgbMetadataFactoryOperation>& operation){
    if(request.kind!=K::create_fields || !services || !services->field_registries)return nullptr;
    auto invocation=std::make_shared<IgbFieldRegistryFactoryInvocation>();
    operation=std::make_shared<IgbMetadataFactoryOperation>();auto* result=operation.get();
    operation->resume=[invocation,fields=services->field_registries,result]{
        const auto status=invocation->resume(fields);if(status.completed){result->fields_result=invocation->result();result->allocation_result=invocation->allocation();}return status;
    };return result;
}
}
void igb_metadata_set_pool_name(const IgbBootstrapPointer& object,
    const std::shared_ptr<IgbInternedString>& owner,std::uint32_t address){
    if((address==0)!=(!owner))fail("Metadata string address and canonical owner disagree.");
    auto metadata=igb_bootstrap_metadata(object);
    if(!metadata->default_pool_string || *metadata->default_pool_string!=owner)retain_name(metadata);
    igb_bootstrap_write_word(object,0x60,address);
    metadata->default_pool_string=owner;metadata->default_pool_string_address=address;
    metadata->default_pool_name=owner?owner->value:SystemStageString{};
}

struct IgbMetadataConstructorInvocation::Impl {
    enum class P {base,initial,reset_base,stores,string_global,after_allocate,after_initialize,
        publish_pool,intern_null,old_name,release_name,publish_name,fields,after_fields,call,string_call,done};
    IgbBootstrapPointer object;IgbMetadataConstructor entry;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbMetadataFactoryServices> owner;
    std::unique_ptr<IgbBootstrapBaseInvocation> base;
    std::shared_ptr<ModelSceneObject> string_pool;
    std::shared_ptr<IgbInternedString> old_name;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    MetaCall call;StringCall string_call;P phase=P::base,after=P::done;
    bool running{},fault{},owns{};
};
IgbMetadataConstructorInvocation::IgbMetadataConstructorInvocation(IgbBootstrapPointer object,IgbMetadataConstructor entry)
    :impl_(std::make_unique<Impl>()){impl_->object=std::move(object);impl_->entry=entry;}
IgbMetadataConstructorInvocation::~IgbMetadataConstructorInvocation(){
    auto& s=*impl_;if(s.owns && !completed())s.metadata->construction_fault_=true;
    if(!completed() && s.owner){
        if(s.object.storage)s.owner->unfinished_.push_back(s.object.storage);
        if(s.old_name)s.owner->unfinished_.push_back(s.old_name);
        if(s.string_pool)s.owner->unfinished_.push_back(s.string_pool);
    }
}
bool IgbMetadataConstructorInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataConstructorInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbMetadataConstructorInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete metadata constructor has no result.");return impl_->metadata;
}
MessengerCallResult IgbMetadataConstructorInvocation::resume(std::shared_ptr<IgbMetadataFactoryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.metadata->construction_fault_=true;fail("Metadata constructor requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.owns && s.metadata->construction_fault_))fail("Metadata constructor continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();s.string_call.clear();check();};
    const auto select_string=[&](S kind,P after)->IgbDependenciesCall&{
        clear();s.string_call.request.kind=kind;
        if(services && services->dependencies)s.string_call.factory=services->dependencies->factory;
        s.after=after;s.phase=P::string_call;return s.string_call.request;
    };
    const auto globals=[&]()->std::shared_ptr<SystemStageServices>{
        return services && services->dependencies?services->dependencies->globals:nullptr;
    };
    try{for(;;){check();switch(s.phase){
    case P::base:
        if(!s.owns && igb_bootstrap_address(s.object)){
            s.metadata=igb_bootstrap_metadata(s.object);
            if(s.metadata->construction_fault_ || !s.metadata->construction_active_.expired())fail("Metadata is faulted or already constructing.");
            s.metadata->construction_active_=s.token;s.owns=true;
        }
        if(!s.base)s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,s.entry==IgbMetadataConstructor::construct);
        {const auto status=s.base->resume(services?services->bootstrap:nullptr);check();if(!status.completed)return status;}
        s.base.reset();check();s.metadata=igb_bootstrap_metadata(s.object);
        s.phase=s.entry==IgbMetadataConstructor::construct?P::initial:P::stores;break;
    case P::initial:
        store_word(s.object,s.metadata,0,0x804761d8);store_word(s.object,s.metadata,0x1c,0);store_word(s.object,s.metadata,0x60,0);
        s.phase=P::reset_base;break;
    case P::reset_base:
        if(!s.base)s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,false);
        {const auto status=s.base->resume(services?services->bootstrap:nullptr);check();if(!status.completed)return status;}
        s.base.reset();check();s.phase=P::stores;break;
    case P::stores:
        store_word(s.object,s.metadata,0,0x804761d8);
        for(const auto offset:std::array<std::uint32_t,9>{0x1c,0x60,0x28,0x30,0x34,0x38,0x3c,0x40,0x44})store_word(s.object,s.metadata,offset,0);
        igb_bootstrap_write_byte(s.object,0x18,0);s.metadata->flag_18=std::uint8_t{0};
        igb_bootstrap_write_byte(s.object,0x19,1);s.metadata->flag_19=std::uint8_t{1};
        igb_bootstrap_write_byte(s.object,0x1a,0);s.metadata->flag_1a=0;
        store_word(s.object,s.metadata,0x10,1);
        for(const auto offset:std::array<std::uint32_t,7>{0x14,0x0c,0x08,0x28,0x20,0x2c,0x24})store_word(s.object,s.metadata,offset,0);
        store_word(s.object,s.metadata,0x48,0xffffffffu);
        for(const auto offset:std::array<std::uint32_t,3>{0x4c,0x50,0x54})store_word(s.object,s.metadata,offset,0);
        igb_bootstrap_write_halfword(s.object,0x58,4);s.metadata->native_alignment=4;
        store_word(s.object,s.metadata,0x5c,0);s.phase=P::string_global;break;
    case P::string_global:{
        const auto owner=globals();if(!owner)return pending("Metadata reset requires canonical string-pool global80562140.");
        if(owner->string_pool){s.phase=P::intern_null;break;}
        select_string(S::create_string_pool,P::after_allocate).bytes=16;break;
    }
    case P::after_allocate:
        s.string_pool=std::move(s.string_call.operation->pool_result);clear();
        if(s.string_pool)select_string(S::initialize_string_pool,P::after_initialize).pool=s.string_pool;
        else s.phase=P::publish_pool;break;
    case P::after_initialize:s.string_pool=std::move(s.string_call.operation->pool_result);clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{
        const auto owner=globals();if(!owner)return pending("Metadata reset requires string-pool global publication storage.");
        if(owner->string_pool && owner->string_pool!=s.string_pool)s.metadata->overwritten_constructor_owners_.push_back(owner->string_pool);
        owner->string_pool=s.string_pool;s.phase=P::intern_null;break;
    }
    case P::intern_null:{
        const auto owner=globals();if(!owner)return pending("Metadata reset requires the reloaded string-pool global.");
        IgbStringInternInvocation call(owner->string_pool,std::monostate{});
        const auto status=call.resume();if(!status.completed || call.result())fail("Null-name string leaf did not produce its original null result.");
        s.phase=P::old_name;break;
    }
    case P::old_name:{
        const auto address=igb_bootstrap_read_word(s.object,0x60);
        if(address==0){s.phase=P::publish_name;break;}
        if(!s.metadata->default_pool_string || !s.metadata->default_pool_string_address || *s.metadata->default_pool_string_address!=address)
            return pending("Metadata reset requires a canonical owner for its live default-pool string pointer.");
        s.old_name=*s.metadata->default_pool_string;
        if(!s.old_name)fail("Nonnull metadata string pointer has a null canonical header.");
        if(s.old_name->released)fail("Metadata reset reads a released string header.");
        --s.old_name->references;s.phase=s.old_name->references==0?P::release_name:P::publish_name;break;
    }
    case P::release_name:{auto& request=select_string(S::release_string,P::publish_name);request.pool=s.old_name->container;request.string=s.old_name;break;}
    case P::publish_name:
        clear();
        // The release callback may replace +60. The following raw store does
        // not release that new owner or change its guest reference count.
        igb_metadata_set_pool_name(s.object,nullptr,0);
        s.phase=s.entry==IgbMetadataConstructor::construct?P::fields:P::done;break;
    case P::fields:
        clear();s.call.request.kind=K::create_fields;s.call.request.object=s.object;s.call.request.metadata=s.metadata;
        if(!native_fields(s.call.request,services,s.call.operation) && services)s.call.factory=services->factory;
        s.after=P::after_fields;s.phase=P::call;break;
    case P::after_fields:{
        const auto fields=s.call.operation->fields_result;const auto allocation=s.call.operation->allocation_result;
        const auto address=igb_bootstrap_address(allocation);clear();
        if((address==0)!=(!fields) || (address && igb_bootstrap_fields(allocation)!=fields))
            fail("Field-list factory returned inconsistent physical and typed identities.");
        if(s.metadata->fields)s.metadata->overwritten_constructor_owners_.push_back(s.metadata->fields);
        igb_bootstrap_write_word(s.object,0x28,address);s.metadata->fields=fields;s.phase=P::done;break;
    }
    case P::call:{
        // A composed native field-list operation needs no external factory.
        MessengerCallResult status;
        if(s.call.operation){if(!s.call.callback)s.call.callback=s.call.operation->resume;if(!s.call.callback)return pending("Field-list construction requires its retained continuation.");status=s.call.callback();check();}
        else status=s.call.poll(services?services->factory:IgbMetadataFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;
    }
    case P::string_call:{const auto status=s.string_call.poll(services && services->dependencies?services->dependencies->factory:IgbDependenciesFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:if(s.owns){s.metadata->construction_active_.reset();s.owns=false;}return {true,0,{}};
    }}}catch(...){s.fault=true;if(s.owns)s.metadata->construction_fault_=true;throw;}
}

struct IgbMetadataFactoryInvocation::Impl {
    enum class P {entry,after_register,reflected,after_reflected,allocate,construct,post_2c,post_24,call,finish,done};
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbMetadataFactoryServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    IgbBootstrapPointer object;
    std::unique_ptr<IgbRawAllocationInvocation> allocator;
    std::unique_ptr<IgbMetadataConstructorInvocation> constructor;
    MetaCall call;P phase=P::entry,after=P::done;bool running{},fault{};
};
IgbMetadataFactoryInvocation::IgbMetadataFactoryInvocation(std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){impl_->pool=std::move(pool);}
IgbMetadataFactoryInvocation::~IgbMetadataFactoryInvocation(){auto& s=*impl_;if(!completed() && s.owner && s.object.storage)s.owner->unfinished_.push_back(s.object.storage);}
bool IgbMetadataFactoryInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataFactoryInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbMetadataFactoryInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete metadata factory has no result.");
    return impl_->metadata;
}
IgbBootstrapPointer IgbMetadataFactoryInvocation::allocation()const{if(!completed() || faulted())fail("Incomplete metadata factory has no allocation.");return impl_->object;}
MessengerCallResult IgbMetadataFactoryInvocation::resume(std::shared_ptr<IgbMetadataFactoryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Metadata factory continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbMetadataFactoryCall&{
        clear();s.call.request.kind=kind;s.call.request.object=s.object;s.call.request.metadata=s.metadata;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;return s.call.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:{
        const auto runtime=services && services->bootstrap?services->bootstrap->runtime:nullptr;
        if(!runtime || !runtime->singleton)return pending("Metadata factory requires represented ArkCore80562100.");
        const auto core=*runtime->singleton;if(!core)fail("Metadata factory dereferences null ArkCore.");
        if(core->enabled)select(K::register_type,P::after_register);else s.phase=P::allocate;break;
    }
    case P::after_register:clear();s.phase=P::reflected;break;
    case P::reflected:
        if(!services || !services->metadata)return pending("Metadata factory requires reloaded type global8056174C.");
        s.metadata=*services->metadata;select(K::reflected_factory,P::after_reflected).pool=s.pool;break;
    case P::after_reflected:s.object=std::move(s.call.operation->allocation_result);clear();s.phase=P::finish;break;
    case P::allocate:
        if(!s.allocator)s.allocator=std::make_unique<IgbRawAllocationInvocation>(0x64,0,s.pool);
        {const auto status=s.allocator->resume(services?services->bootstrap:nullptr);check();if(!status.completed)return status;}
        s.object=s.allocator->result();s.allocator.reset();check();
        s.phase=igb_bootstrap_address(s.object)?P::construct:P::post_2c;break;
    case P::construct:
        if(!s.constructor)s.constructor=std::make_unique<IgbMetadataConstructorInvocation>(s.object);
        {const auto status=s.constructor->resume(services);check();if(!status.completed)return status;}
        s.constructor.reset();check();s.phase=P::post_2c;break;
    case P::post_2c:case P::post_24:{
        clear();const auto slot=s.phase==P::post_2c?0x2cu:0x24u;const auto next=s.phase==P::post_2c?P::post_24:P::finish;
        const auto vtable=igb_bootstrap_read_word(s.object,0);
        if(vtable==0x804761d8){s.phase=next;break;}
        if(vtable==0)fail("Metadata factory dereferences a null virtual table.");
        auto& request=select(K::object_virtual,next);request.vtable_word=vtable;request.virtual_slot=slot;request.argument=0;break;
    }
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbMetadataFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::finish:clear();s.metadata=igb_bootstrap_address(s.object)?igb_bootstrap_metadata(s.object):nullptr;s.phase=P::done;break;
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_metadata_factory(std::shared_ptr<IgbMetadataFactoryServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataFactoryServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::create_metadata)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto services=weak.lock();if(!services)fail("Metadata factory services expired before selection.");
        auto invocation=std::make_shared<IgbMetadataFactoryInvocation>(call.pool);
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();auto* result=operation.get();
        operation->resume=[invocation,services,result]{const auto status=invocation->resume(services);if(status.completed)result->metadata_result=invocation->result();return status;};return operation;
    };
}
} // namespace dw4
