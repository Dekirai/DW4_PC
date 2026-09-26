#include "dw4/igb_object_defaults.hpp"
#include "dw4/igb_field_reference.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbObjectDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("igObject defaults require their reached native operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("igObject defaults reached an unbound native operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("igObject defaults require their retained operation continuation.");
        const auto status=callback();check();return status;
    }
};
using StringCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
using DestroyCall=Call<IgbObjectDefaultsCall,IgbObjectDefaultsFactory,IgbObjectDefaultsOperation>;
}
struct IgbObjectDefaultsInvocation::Impl {
    enum class P{handle,pool,create_field,offset,global_slot,string_pool,after_pool_allocate,after_pool_initialize,
        publish_pool,intern,after_intern,name,temporary,after_temporary,flags,metadata,append,release_field,release_test,
        after_destroy,next,string_call,destroy_call,done};
    std::shared_ptr<IgbObjectDefaultsServices> owner;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbFieldReferenceAccess> reference;
    IgbBootstrapPointer field_allocation;
    std::optional<IgbBootstrapPointer> field_binding;
    std::shared_ptr<IgbScratchBuffer> field_bytes;
    std::uint32_t field_address{};
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbFieldNameSlot> temporary;
    std::shared_ptr<IgbInternedString> released_string;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetaFieldFactoryInvocation> factory_call;
    std::unique_ptr<IgbFieldNameInvocation> name_call;
    std::unique_ptr<IgbFieldAppendInvocation> append_call;
    StringCall string;DestroyCall destroy;
    P phase=P::handle,after=P::done;unsigned index{};bool running{},fault{};
};
IgbObjectDefaultsInvocation::IgbObjectDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbObjectDefaultsInvocation::~IgbObjectDefaultsInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.owner){
        if(s.field)s.owner->unfinished.push_back(s.field);
        if(s.reference)s.owner->unfinished.push_back(s.reference);
        if(s.field_allocation.storage)s.owner->unfinished.push_back(s.field_allocation.storage);
        if(s.field_bytes)s.owner->unfinished.push_back(s.field_bytes);
        if(s.temporary && *s.temporary && **s.temporary)s.owner->unfinished.push_back(**s.temporary);
        if(s.released_string)s.owner->unfinished.push_back(s.released_string);
        if(s.pool)s.owner->unfinished.push_back(s.pool);
    }
}
bool IgbObjectDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbObjectDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbObjectDefaultsInvocation::resume(std::shared_ptr<IgbObjectDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault){s.fault=true;fail("igObject defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("igObject defaults continuation was invalidated.");};
    const auto clear_string=[&]{s.string.clear();check();};
    const auto clear_destroy=[&]{s.destroy.clear();check();};
    const auto dependencies=[&]{return services?services->dependencies:std::shared_ptr<IgbDependenciesServices>{};};
    const auto globals=[&]{const auto dep=dependencies();return dep?dep->globals:std::shared_ptr<SystemStageServices>{};};
    const auto bind_pointer=[&]{
        if(!s.field || !services || !services->fields || !services->fields->pointer_lists)return;
        const auto address=igb_bootstrap_address(s.field_allocation);auto& values=services->fields->pointer_lists->values;
        for(const auto& value:values)if(value.field==s.field && value.address==address && !value.metadata)return;
        values.push_back({address,{},s.field});
    };
    const auto select_string=[&](K kind,P after)->IgbDependenciesCall&{
        clear_string();s.string.request.kind=kind;if(const auto dep=dependencies())s.string.factory=dep->factory;
        s.after=after;s.phase=P::string_call;return s.string.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("igObject defaults require metadata-pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{
        const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();
        s.factory_call=std::make_unique<IgbMetaFieldFactoryInvocation>(s.pool);s.phase=P::create_field;break;
    }
    case P::create_field:{
        const auto status=s.factory_call->resume(services?services->meta_fields:nullptr);check();if(!status.completed)return status;
        s.field=s.factory_call->result();s.field_allocation=s.factory_call->allocation();
        s.field_binding.reset();s.field_bytes.reset();
        if(s.field && s.field->physical_storage){const auto& binding=*s.field->physical_storage;
            s.field_binding=IgbBootstrapPointer{binding.storage.lock(),binding.offset};
            if(s.field_binding->storage){s.field_bytes=s.field_binding->storage->bytes;
                s.field_address=s.field_binding->storage->original_address;}
        }
        s.reference=std::make_shared<IgbFieldReferenceAccess>(s.field);s.factory_call.reset();check();
        bind_pointer();s.pool.reset();s.phase=P::offset;break;
    }
    case P::offset:
        if(!s.field)fail("igObject defaults store offset+08 through a null field factory result.");
        s.field->offset=4;s.phase=P::global_slot;break;
    case P::global_slot:{
        const auto slot=services?(s.index==0?services->ref_count:services->pool_index):std::shared_ptr<IgbRuntimeFieldSlot>{};
        if(!slot)return pending("igObject defaults require the reached canonical descriptor-global cell.");
        const auto address=s.index==0?0x805622a8u:0x805622acu;
        //66BB0/66C90 publish the original pointer before name interning.
        if(s.field_binding){const auto& p=*s.field_binding;
            if(!p.storage || p.storage->faulted() || !s.field->physical_storage || s.field->construction_fault_ ||
                s.field->physical_storage->storage.lock()!=p.storage || s.field->physical_storage->offset!=p.offset ||
                p.storage->original_address!=s.field_address || p.storage->bytes!=s.field_bytes || !s.field_bytes || s.field_bytes->released)
                fail("igObject global pointer store lost its captured descriptor allocation.");
            IgbObjectInstance object;object.storage=p.storage;object.offset=p.offset;object.field=s.field;
            igb_object_validate_instance(object);igb_bootstrap_write_word(p,0x1c,address);
        }else if(s.field->physical_storage)fail("igObject global pointer store acquired a different storage profile.");
        s.field->global_slot=slot;s.field->global_slot_address=address;
        s.phase=P::string_pool;break;
    }
    case P::string_pool:{
        const auto global=globals();if(!global)return pending("igObject defaults require string-pool global80562140.");
        if(global->string_pool)s.phase=P::intern;else select_string(K::create_string_pool,P::after_pool_allocate).bytes=16;break;
    }
    case P::after_pool_allocate:
        s.pool=std::move(s.string.operation->pool_result);clear_string();
        if(s.pool)select_string(K::initialize_string_pool,P::after_pool_initialize).pool=s.pool;else s.phase=P::publish_pool;break;
    case P::after_pool_initialize:s.pool=std::move(s.string.operation->pool_result);clear_string();s.phase=P::publish_pool;break;
    case P::publish_pool:{
        const auto global=globals();if(!global)return pending("igObject defaults require string-pool publication storage.");
        if(global->string_pool && global->string_pool!=s.pool && s.owner)s.owner->unfinished.push_back(global->string_pool);
        global->string_pool=s.pool;s.pool.reset();s.phase=P::intern;break;
    }
    case P::intern:{
        const auto global=globals();if(!global)return pending("igObject defaults require the reloaded string-pool global.");
        const auto pool=global->string_pool;auto& request=select_string(K::intern_name,P::after_intern);request.pool=pool;
        request.name=std::make_shared<const std::string>(s.index==0?"_refCount":"_memPoolIndex");break;
    }
    case P::after_intern:
        s.temporary=std::make_shared<IgbFieldNameSlot>(std::move(s.string.operation->string_result));clear_string();
        s.name_call=std::make_unique<IgbFieldNameInvocation>(s.field,s.temporary);s.phase=P::name;break;
    case P::name:{
        const auto status=s.name_call->resume(services?services->fields:nullptr);check();if(!status.completed)return status;
        s.name_call.reset();check();s.phase=P::temporary;break;
    }
    case P::temporary:
        if(!*s.temporary)return pending("igObject defaults require their live temporary string slot.");
        s.released_string=**s.temporary;
        if(!s.released_string){s.phase=P::after_temporary;break;}
        if(s.released_string->released)fail("igObject defaults read a released temporary string header.");
        --s.released_string->references;
        if(s.released_string->references==0){const auto old=s.released_string;auto& request=select_string(K::release_string,P::after_temporary);request.string=old;request.pool=old->container;}
        else s.phase=P::after_temporary;break;
    case P::after_temporary:
        clear_string();s.temporary.reset();s.released_string.reset();s.phase=P::flags;break;
    case P::flags:s.field->flag_26=0;s.field->default_mode=std::uint8_t{3};s.phase=P::metadata;break;
    case P::metadata:
        if(!services || !services->object_metadata || !*services->object_metadata)return pending("igObject defaults require canonical metadata805622A4.");
        s.append_call=std::make_unique<IgbFieldAppendInvocation>(**services->object_metadata,s.field);s.phase=P::append;break;
    case P::append:{
        bind_pointer();
        const auto status=s.append_call->resume(services?services->fields:nullptr);check();if(!status.completed)return status;
        s.append_call.reset();check();s.phase=P::release_field;break;
    }
    case P::release_field:
        s.reference->write(s.reference->read()-1u);s.phase=P::release_test;break;
    case P::release_test:
        if((s.reference->read()&0x7fffffu)==0){clear_destroy();s.destroy.request.field=s.field;
            if(services)s.destroy.factory=services->factory;s.phase=P::destroy_call;}
        else s.phase=P::next;break;
    case P::destroy_call:{
        const auto status=s.destroy.poll(services?services->factory:IgbObjectDefaultsFactory{},check);if(!status.completed)return status;
        s.phase=P::after_destroy;break;
    }
    case P::after_destroy:clear_destroy();s.phase=P::next;break;
    case P::next:s.reference.reset();s.field.reset();s.field_allocation={};++s.index;s.phase=s.index==2?P::done:P::handle;break;
    case P::string_call:{
        const auto dep=dependencies();const auto status=s.string.poll(dep?dep->factory:IgbDependenciesFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_object_defaults_factory(std::shared_ptr<IgbObjectDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbObjectDefaultsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& request){
        if(request.kind!=IgbTypeRegistrationServiceKind::defaults || request.function!=0x80066b7c)
            return remaining?remaining(request):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("igObject defaults services expired before selection.");
        auto invocation=std::make_shared<IgbObjectDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
