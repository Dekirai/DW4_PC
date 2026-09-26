#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_field_reference.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldSchemaError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        check();
        if(!factory)factory=fallback;
        if(!factory)return pending("Field schema requires its reached native operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Field schema reached an unbound native operation.");
        if(!callback && !checked_callback){
            if constexpr(requires{operation->resume_checked;})checked_callback=operation->resume_checked;
            if(!checked_callback)callback=operation->resume;
        }
        if(!callback && !checked_callback)return pending("Field schema requires its retained operation continuation.");
        const auto status=checked_callback?checked_callback(check):callback();check();return status;
    }
};
using SchemaCall=Call<IgbFieldSchemaCall,IgbFieldSchemaFactory,IgbFieldSchemaOperation>;
using StringCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
template<class T>const std::optional<T>& table_entry(const std::shared_ptr<std::vector<std::optional<T>>>& table,std::uint32_t offset){
    const auto index=offset>>2u;
    if(!table || index>=table->size())fail("Field schema reads an unavailable physical table entry.");
    return (*table)[index];
}
std::optional<std::uint32_t> declaring_type_address(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbFieldSchemaServices>& services){
    if(!metadata)return 0;
    const auto pointers=services && services->fields?services->fields->pointer_lists:nullptr;
    if(!pointers)return {};
    std::optional<std::uint32_t> result;
    for(const auto& value:pointers->values)if(value.metadata==metadata){
        if(value.field || !value.address || (result && *result!=value.address))
            fail("Field configuration has conflicting declaring-type arithmetic identities.");
        result=value.address;
    }
    if(result)for(const auto& value:pointers->values)if(value.address==*result && (value.field || value.metadata!=metadata))
        fail("Field configuration declaring-type address has conflicting canonical owners.");
    return result;
}
}

struct IgbFieldBatchInvocation::Impl {
    enum class P{test,handle,pool,factory,after_factory,append_capture,append,release,release_test,after_destroy,next,call,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbFieldReferenceAccess> reference;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbFieldSchemaServices> owner;
    std::shared_ptr<ModelSceneObject> pool;
    std::optional<std::uint32_t> address;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::shared_ptr<IgbMetadataOperation> append;
    std::function<MessengerCallResult()> append_callback;
    std::function<MessengerCallResult(const std::function<void()>&)> append_checked_callback;
    SchemaCall call;P phase=P::test,after=P::done;
    std::uint32_t count{},index{},byte_offset{};bool running{},fault{};
};
IgbFieldBatchInvocation::IgbFieldBatchInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,
    std::shared_ptr<IgbFieldFactoryTable> factories,std::uint32_t count):impl_(std::make_unique<Impl>()){
    impl_->metadata=std::move(metadata);impl_->factories=std::move(factories);impl_->count=count;
}
IgbFieldBatchInvocation::~IgbFieldBatchInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.pool)s.owner->unfinished.push_back(s.pool);
    if(s.reference)s.owner->unfinished.push_back(s.reference);
    if(s.call.request.kind==IgbFieldSchemaServiceKind::create_field && s.call.operation &&
        s.call.operation->field_result && s.call.operation->field_result!=s.field)
        s.owner->unfinished.push_back(s.call.operation->field_result);
}}
bool IgbFieldBatchInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldBatchInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldBatchInvocation::resume(std::shared_ptr<IgbFieldSchemaServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbFieldSchemaServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field batch requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field batch continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field batch was invalidated by its enclosing callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto bind=[&]{
        if(!s.address || !s.field || !services || !services->fields || !services->fields->pointer_lists)return;
        auto& values=services->fields->pointer_lists->values;
        for(const auto& value:values)if(value.address==*s.address && value.field==s.field && !value.metadata)return;
        values.push_back({*s.address,{},s.field});
    };
    try{for(;;){check();switch(s.phase){
    case P::test:s.phase=std::bit_cast<std::int32_t>(s.index)<std::bit_cast<std::int32_t>(s.count)?P::handle:P::done;break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Field batch requires metadata-pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::factory;break;}
    case P::factory:{const auto& function=table_entry(s.factories,s.byte_offset);
        if(!function)return pending("Field batch requires its live constructor function identity.");
        if(*function==0)fail("Field batch calls a null constructor function.");
        clear();s.call.request.kind=K::create_field;s.call.request.function=*function;s.call.request.pool=s.pool;
        if(services)s.call.factory=services->factory;s.phase=P::call;s.after=P::after_factory;break;}
    case P::after_factory:s.field=s.call.operation->field_result;s.address=s.call.operation->field_address;
        s.reference=std::make_shared<IgbFieldReferenceAccess>(s.field);clear();
        bind();s.pool.reset();s.phase=P::append_capture;break;
    case P::append_capture:
        if(!s.metadata)fail("Field batch append dereferences null metadata.");
        s.fields=s.metadata->fields;if(s.field)s.reference->write(s.reference->read()+1u);s.phase=P::append;break;
    case P::append:
        bind();if(!s.append){if(!services || !services->fields || !services->fields->pointer_lists)return pending("Field batch requires native raw pointer-list append services.");
            IgbMetadataOperationCall request;request.kind=IgbMetadataOperationKind::append_field;request.fields=s.fields;request.field=s.field;
            s.append=make_native_igb_pointer_list_metadata_factory(services->fields->pointer_lists)(request);check();}
        if(!s.append_callback && !s.append_checked_callback){
            s.append_checked_callback=s.append->resume_checked;if(!s.append_checked_callback)s.append_callback=s.append->resume;
        }
        if(!s.append_callback && !s.append_checked_callback)return pending("Field batch requires its retained raw append continuation.");
        {const auto status=s.append_checked_callback?s.append_checked_callback(check):s.append_callback();check();if(!status.completed)return status;}
        s.append_callback={};s.append_checked_callback={};s.append.reset();check();s.phase=P::release;break;
    case P::release:
        if(!s.field)fail("Field batch releases a null constructor result after appending it.");
        s.reference->write(s.reference->read()-1u);s.phase=P::release_test;break;
    case P::release_test:
        if((s.reference->read()&0x7fffffu)==0){clear();s.call.request.kind=K::destroy_field;s.call.request.function=0x80066e1c;s.call.request.field=s.field;
            if(services)s.call.factory=services->factory;s.phase=P::call;s.after=P::after_destroy;}
        else s.phase=P::next;break;
    case P::after_destroy:clear();s.phase=P::next;break;
    case P::next:s.reference.reset();s.field.reset();s.address.reset();s.fields.reset();++s.index;s.byte_offset+=4;s.phase=P::test;break;
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbFieldSchemaFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldConfigureInvocation::Impl {
    enum class P{entry,string_pool,after_allocate,after_initialize,publish_pool,intern,after_intern,name,
        temporary,after_temporary,global,offset,declaring_type,buffer,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    SystemStageString name;
    IgbFieldGlobalBinding global;
    std::uint32_t offset{};
    std::shared_ptr<IgbRuntimeMetaType> declaring_type;
    std::optional<IgbBootstrapPointer> physical;
    std::shared_ptr<IgbScratchBuffer> physical_bytes;
    std::uint32_t physical_address{};
    std::shared_ptr<IgbFieldSchemaServices> owner;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbFieldNameSlot> temporary;
    std::shared_ptr<IgbInternedString> released;
    std::unique_ptr<IgbFieldNameInvocation> name_call;
    std::unique_ptr<IgbFieldBufferInvocation> buffer_call;
    StringCall call;P phase=P::entry,after=P::done;bool running{},fault{},stores_started{};
};
IgbFieldConfigureInvocation::IgbFieldConfigureInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,SystemStageString name,
    IgbFieldGlobalBinding global,std::uint32_t offset,std::shared_ptr<IgbRuntimeMetaType> declaring_type):impl_(std::make_unique<Impl>()){
    impl_->field=std::move(field);impl_->name=std::move(name);impl_->global=std::move(global);impl_->offset=offset;impl_->declaring_type=std::move(declaring_type);
}
IgbFieldConfigureInvocation::~IgbFieldConfigureInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.temporary && *s.temporary && **s.temporary)s.owner->unfinished.push_back(**s.temporary);
    if(s.released)s.owner->unfinished.push_back(s.released);if(s.pool)s.owner->unfinished.push_back(s.pool);
    if(s.physical && s.physical->storage)s.owner->unfinished.push_back(s.physical->storage);
    if(s.physical_bytes)s.owner->unfinished.push_back(s.physical_bytes);
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.declaring_type)s.owner->unfinished.push_back(s.declaring_type);
}}
bool IgbFieldConfigureInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldConfigureInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldConfigureInvocation::resume(std::shared_ptr<IgbFieldSchemaServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field configuration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check_local=[&]{if(s.fault)fail("Field configuration continuation was invalidated.");
        if(s.physical){
            if(!s.field || !s.field->physical_storage)fail("Field configuration lost its captured allocation binding.");
            const auto& binding=*s.field->physical_storage;
            if(binding.storage.lock()!=s.physical->storage || binding.offset!=s.physical->offset ||
                s.physical->storage->bytes!=s.physical_bytes ||
                igb_bootstrap_address(*s.physical)!=s.physical_address || s.physical->storage->faulted() ||
                igb_bootstrap_field(*s.physical)!=s.field)
                fail("Field configuration changed its captured descriptor allocation.");
        }else if(s.stores_started && s.field && s.field->physical_storage)
            fail("Field configuration acquired a physical binding after canonical-only stores.");
    };
    const auto check=[&]{check_local();if(enclosing_check)enclosing_check();check_local();};
    const auto capture_physical=[&]{
        if(!s.physical && s.field && s.field->physical_storage){
            const auto& binding=*s.field->physical_storage;
            auto storage=binding.storage.lock();if(!storage)fail("Field configuration reached expired descriptor storage.");
            s.physical=IgbBootstrapPointer{std::move(storage),binding.offset};s.physical_address=igb_bootstrap_address(*s.physical);
            s.physical_bytes=s.physical->storage->bytes;
        }check();
    };
    const auto clear=[&]{s.call.clear();check();};
    const auto dependencies=[&]{return services?services->dependencies:std::shared_ptr<IgbDependenciesServices>{};};
    const auto globals=[&]{const auto dep=dependencies();return dep?dep->globals:std::shared_ptr<SystemStageServices>{};};
    const auto select=[&](K kind,P after)->IgbDependenciesCall&{clear();s.call.request.kind=kind;
        if(const auto dep=dependencies())s.call.factory=dep->factory;s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        capture_physical();
        if(s.name)s.phase=P::string_pool;
        else{s.temporary=std::make_shared<IgbFieldNameSlot>(std::shared_ptr<IgbInternedString>{});s.phase=P::name;}break;
    case P::string_pool:{const auto global=globals();if(!global)return pending("Field configuration requires string-pool global80562140.");
        if(global->string_pool)s.phase=P::intern;else select(K::create_string_pool,P::after_allocate).bytes=16;break;}
    case P::after_allocate:s.pool=s.call.operation->pool_result;clear();
        if(s.pool)select(K::initialize_string_pool,P::after_initialize).pool=s.pool;else s.phase=P::publish_pool;break;
    case P::after_initialize:s.pool=s.call.operation->pool_result;clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{const auto global=globals();if(!global)return pending("Field configuration requires string-pool publication storage.");
        if(global->string_pool && global->string_pool!=s.pool && s.owner)s.owner->unfinished.push_back(global->string_pool);
        global->string_pool=s.pool;s.pool.reset();s.phase=P::intern;break;}
    case P::intern:{const auto global=globals();if(!global)return pending("Field configuration requires its live string-pool global.");
        const auto pool=global->string_pool;auto& call=select(K::intern_name,P::after_intern);call.pool=pool;call.name=s.name;break;}
    case P::after_intern:s.temporary=std::make_shared<IgbFieldNameSlot>(s.call.operation->string_result);clear();s.phase=P::name;break;
    case P::name:
        if(!s.name_call)s.name_call=std::make_unique<IgbFieldNameInvocation>(s.field,s.temporary);
        {const auto status=s.name_call->resume(services?services->fields:nullptr,check);check();if(!status.completed)return status;}
        s.name_call.reset();check();s.phase=P::temporary;break;
    case P::temporary:
        if(!*s.temporary)return pending("Field configuration requires its live temporary string slot.");
        s.released=**s.temporary;if(!s.released){s.phase=P::after_temporary;break;}
        if(s.released->released)fail("Field configuration releases an unavailable temporary string header.");
        --s.released->references;
        if(s.released->references==0){const auto old=s.released;auto& call=select(K::release_string,P::after_temporary);call.string=old;call.pool=old->container;}
        else s.phase=P::after_temporary;break;
    case P::after_temporary:clear();s.released.reset();s.temporary.reset();s.phase=P::global;break;
    case P::global:
        if(!s.field)fail("Field configuration stores through a null descriptor.");
        if((s.global.address==0)!=(!s.global.slot))fail("Field configuration requires consistent global-cell address and owner bindings.");
        capture_physical();s.stores_started=true;
        if(s.physical)igb_bootstrap_write_word(*s.physical,0x1c,s.global.address);
        s.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>(s.global.slot);s.field->global_slot_address=s.global.address;s.phase=P::offset;break;
    case P::offset:
        if(s.physical)igb_bootstrap_write_word(*s.physical,0x08,s.offset);
        s.field->offset=s.offset;s.phase=P::declaring_type;break;
    case P::declaring_type:
        if(s.physical){const auto address=declaring_type_address(s.declaring_type,services);
            if(!address)return pending("Field configuration requires its declaring-type arithmetic identity before the physical+18 store.");
            igb_bootstrap_write_word(*s.physical,0x18,*address);}
        s.field->declaring_type=std::weak_ptr<IgbRuntimeMetaType>(s.declaring_type);
        s.buffer_call=std::make_unique<IgbFieldBufferInvocation>(s.field);s.phase=P::buffer;break;
    case P::buffer:{const auto status=s.buffer_call->resume(services?services->fields:nullptr,check);check();if(!status.completed)return status;
        s.buffer_call.reset();check();s.phase=P::done;break;}
    case P::call:{const auto dep=dependencies();const auto status=s.call.poll(dep?dep->factory:IgbDependenciesFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldSchemaInvocation::Impl {
    enum class P{count,test,list,name,backing,global,field,offset,configure,finalize,next,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbDecodeFieldRegistry::Storage> backing;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    SystemStageString name;IgbFieldGlobalBinding global;
    std::unique_ptr<IgbFieldConfigureInvocation> configure;
    std::unique_ptr<IgbFieldFinalizeInvocation> finalize;
    std::uint32_t index{},field_byte_offset{},table_byte_offset{};std::int32_t count{};
    P phase=P::count;bool running{},fault{};
};
IgbFieldSchemaInvocation::IgbFieldSchemaInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,
    std::shared_ptr<IgbFieldNameTable> names,std::shared_ptr<IgbFieldGlobalTable> globals,
    std::shared_ptr<IgbFieldOffsetTable> offsets,std::uint32_t first):impl_(std::make_unique<Impl>()){
    impl_->metadata=std::move(metadata);impl_->names=std::move(names);impl_->globals=std::move(globals);
    impl_->offsets=std::move(offsets);impl_->index=first;impl_->field_byte_offset=first<<2u;
}
IgbFieldSchemaInvocation::~IgbFieldSchemaInvocation()=default;
bool IgbFieldSchemaInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldSchemaInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldSchemaInvocation::resume(std::shared_ptr<IgbFieldSchemaServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field schema loop requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);
    const auto check=[&]{if(s.fault)fail("Field schema loop continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field schema loop was invalidated by its enclosing callback.");};
    try{for(;;){check();switch(s.phase){
    case P::count:
        if(!s.metadata)fail("Field schema count reads null metadata.");
        if(!s.metadata->fields)fail("Field schema count reads a null field list.");
        s.count=s.metadata->fields->count;s.phase=P::test;break;
    case P::test:s.phase=std::bit_cast<std::int32_t>(s.index)<s.count?P::list:P::done;break;
    case P::list:s.fields=s.metadata->fields;s.phase=P::name;break;
    case P::name:{const auto& name=table_entry(s.names,s.table_byte_offset);if(!name)return pending("Field schema requires its live name-table entry.");
        s.name=*name;s.phase=P::backing;break;}
    case P::backing:
        if(!s.fields)fail("Field schema reads a null live field list.");s.backing=s.fields->backing;s.phase=P::global;break;
    case P::global:{const auto& global=table_entry(s.globals,s.table_byte_offset);if(!global)return pending("Field schema requires its live global-cell table entry.");
        s.global=*global;s.phase=P::field;break;}
    case P::field:{const auto index=s.field_byte_offset>>2u;
        if(!s.backing || index>=s.backing->size())fail("Field schema reads an unavailable physical field slot.");
        s.field=(*s.backing)[index];s.phase=P::offset;break;}
    case P::offset:{const auto& offset=table_entry(s.offsets,s.table_byte_offset);if(!offset)return pending("Field schema requires its live offset-table entry.");
        s.configure=std::make_unique<IgbFieldConfigureInvocation>(s.field,s.name,s.global,*offset,s.metadata);s.phase=P::configure;break;}
    case P::configure:{const auto status=s.configure->resume(services,check);check();if(!status.completed)return status;
        s.configure.reset();check();s.finalize=std::make_unique<IgbFieldFinalizeInvocation>(s.field);s.phase=P::finalize;break;}
    case P::finalize:{const auto status=s.finalize->resume(services?services->fields:nullptr,check);check();if(!status.completed)return status;
        s.finalize.reset();check();s.phase=P::next;break;}
    case P::next:s.field.reset();s.fields.reset();s.backing.reset();s.name.reset();s.global={};++s.index;s.field_byte_offset+=4;s.table_byte_offset+=4;s.phase=P::test;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbFieldSchemaFactory make_native_igb_field_schema_factory(std::shared_ptr<IgbFieldSchemaServices> services,IgbFieldSchemaFactory remaining){
    std::weak_ptr<IgbFieldSchemaServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbFieldSchemaCall& call){
        const auto kind=igb_meta_field_kind_for_factory(call.function);
        if(call.kind!=IgbFieldSchemaServiceKind::create_field || !kind)
            return remaining?remaining(call):std::shared_ptr<IgbFieldSchemaOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field constructor services expired before selection.");
        auto invocation=std::make_shared<IgbMetaFieldFactoryInvocation>(call.pool,*kind);auto operation=std::make_shared<IgbFieldSchemaOperation>();
        operation->resume=[invocation,selected,out=operation.get()]{const auto status=invocation->resume(selected->meta_fields);
            if(status.completed){out->field_result=invocation->result();out->field_address=igb_bootstrap_address(invocation->allocation());}return status;};
        return operation;
    };
}
} // namespace dw4
