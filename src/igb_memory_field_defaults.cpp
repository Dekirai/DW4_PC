#include "dw4/igb_memory_field_defaults.hpp"
#include "dw4/igb_stream.hpp"
#include "dw4/igb_string_pool.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMemoryFieldDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("Memory-field defaults require their reached service factory.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Memory-field defaults reached an unbound constructor, clone or field virtual.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Memory-field defaults require their retained service continuation.");
        const auto status=callback();check();return status;
    }
};
std::shared_ptr<IgbDecodeRuntimeField> physical(const std::shared_ptr<IgbDecodeFieldRegistry::Storage>& backing,std::uint32_t index){
    const auto slot=(index<<2u)>>2u;
    if(slot>=262144 || !backing || slot>=backing->size())fail("Memory-field defaults reach an unavailable physical field slot.");
    return (*backing)[slot];
}
std::int32_t index_of(const std::shared_ptr<IgbDecodeFieldRegistry>& list,const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!list)fail("Memory-field identity lookup reads a null registry.");
    const auto count=list->count;const auto backing=list->backing;
    for(std::int32_t i=0;i<count;++i)if(physical(backing,static_cast<std::uint32_t>(i))==field)return i;
    return -1;
}
std::optional<IgbBootstrapPointer> capture_binding(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field || !field->physical_storage)return {};
    return IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Memory-field defaults dereference a null descriptor.");
    const auto pointer=capture_binding(field);
    if(pointer && (!pointer->storage || pointer->storage->faulted() || igb_bootstrap_field(*pointer)!=field))
        fail("Memory-field descriptor lost its canonical physical identity.");
    return pointer;
}
std::optional<std::uint32_t> field_vtable(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    // A represented allocation must satisfy the original lwz at the call
    // selection point; its canonical view cannot hide unavailable bytes.
    if(const auto pointer=physical_field(field))return igb_bootstrap_read_word(*pointer,0);
    return field->vtable_word;
}
IgbRuntimeFieldDefaultSource scalar(std::uint32_t value,unsigned width){
    auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.resize(width);bytes->initialized.assign(width,1);
    for(unsigned i=0;i<width;++i)bytes->bytes[i]=static_cast<std::uint8_t>(value>>((width-1-i)*8));
    return {bytes,0};
}
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbRuntimeMetaType>& type,
    const std::shared_ptr<IgbMemoryFieldDefaultsServices>& services){
    if(!type)return 0;
    std::optional<std::uint32_t> result;
    const auto accept=[&](std::uint32_t address){
        if(!address || (result && *result!=address))fail("Memory-field type has conflicting arithmetic identities.");result=address;};
    if(services)for(const auto& pointer:services->physical_metadata)if(igb_bootstrap_metadata(pointer)==type){
        if(pointer.storage->faulted())fail("Memory-field type has faulted physical storage.");accept(igb_bootstrap_address(pointer));}
    const auto pointers=services && services->lists && services->lists->schema && services->lists->schema->fields?
        services->lists->schema->fields->pointer_lists:nullptr;
    if(pointers)for(const auto& value:pointers->values)if(value.metadata==type){
        if(value.field)fail("Memory-field type address aliases a descriptor binding.");accept(value.address);}
    return result;
}
}
IgbMemoryFieldDefaultsServices::IgbMemoryFieldDefaultsServices(){
    shallow_copy=std::make_shared<IgbRuntimeFieldSlot>();reset_method=std::make_shared<IgbRuntimeFieldSlot>();
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002e1acu,0x8002879cu,0x800385e0u,0x800385e0u,
        0x800385e0u,0x8002e1acu,0x8002879cu,0x800251ecu});
    names=std::make_shared<IgbFieldNameTable>();globals=std::make_shared<IgbFieldGlobalTable>();
    constexpr std::array<const char*,8> labels={"_memSize","_memType","_releaseOnCopy","_releaseOnReset","_refCounted","_alignmentType","_memTypeRef","_poolName"};
    constexpr std::array<std::uint32_t,8> addresses={0x80561824,0x80561828,0x8056182c,0x80561830,0x80561838,0x8056183c,0x80561840,0x80561844};
    for(std::size_t i=0;i<labels.size();++i){names->push_back(std::make_shared<const std::string>(labels[i]));
        globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),addresses[i]});}
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x38u,0x3cu,0x40u,0x41u,0x42u,0x44u,0x48u,0x4cu});
    reset_default_source=scalar(3,4);
}
struct IgbMemoryFieldDefaultsInvocation::Impl {
    enum class P {capture,batch,find_setup,find_descriptor,find_test,find_field,find_name,find_compare,index,clone,after_clone,
        scalar_setup,scalar_call,global,replace,release,destroy,after_release,local_zero,local_type,lookup,type_store,type_flag,construct_flag,
        local_two,local_three,reset_search,pool_setup,pool,create,after_create,configure_setup,configure,reset_scalar,method,
        local_four,local_five,local_six,schema_setup,schema,done};
    P phase=P::capture,after_scalar=P::done;
    std::shared_ptr<IgbMemoryFieldDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,related;
    std::shared_ptr<IgbDecodeRuntimeField> field,found,name_descriptor;
    std::shared_ptr<IgbDecodeFieldRegistry> find_fields,index_fields;
    std::shared_ptr<ModelSceneObject> pool;
    IgbDependencyName find_name;SystemStageString search_name;
    std::optional<IgbBootstrapPointer> binding;
    std::optional<std::uint32_t> address,source_address;
    std::optional<IgbRuntimeFieldDefaultSource> source;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbFieldReplaceInvocation> replace;
    std::unique_ptr<IgbFieldConfigureInvocation> configure;
    std::unique_ptr<IgbFieldTypeLookupInvocation> lookup;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    Call<IgbListDefaultsCall,IgbListDefaultsFactory,IgbListDefaultsOperation> list_call;
    Call<IgbFieldOperationCall,IgbFieldOperationsFactory,IgbFieldOperation> value_call;
    Call<IgbFieldSchemaCall,IgbFieldSchemaFactory,IgbFieldSchemaOperation> factory_call;
    std::uint32_t first{},find_index{},index{},local_index{};
    bool running{},fault{},reset{},validate_binding{};
};
IgbMemoryFieldDefaultsInvocation::IgbMemoryFieldDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbMemoryFieldDefaultsInvocation::~IgbMemoryFieldDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.field)s.owner->retained.push_back(s.field);
    if(s.found)s.owner->retained.push_back(s.found);if(s.related)s.owner->retained.push_back(s.related);
    if(s.pool)s.owner->retained.push_back(s.pool);if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
    if(s.source && s.source->buffer)s.owner->retained.push_back(s.source->buffer);
}}
bool IgbMemoryFieldDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMemoryFieldDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMemoryFieldDefaultsInvocation::resume(std::shared_ptr<IgbMemoryFieldDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbListDefaultsServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Memory-field defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto lists=[&]{return services?services->lists:std::shared_ptr<IgbListDefaultsServices>{};};
    const auto schema=[&]{const auto list=lists();return list?list->schema:std::shared_ptr<IgbFieldSchemaServices>{};};
    const auto fields=[&]{const auto cfg=schema();return cfg?cfg->fields:std::shared_ptr<IgbFieldOperationsServices>{};};
    const auto check=[&]{if(s.fault)fail("Memory-field defaults were invalidated by a callback.");
        if(s.validate_binding && s.binding){const auto current=physical_field(s.field);
            if(!current || current->storage!=s.binding->storage || current->offset!=s.binding->offset)
                fail("Memory-field defaults lost their captured descriptor allocation.");}};
    const auto pointer=[&]{s.validate_binding=true;const auto result=physical_field(s.field);
        if(result && !s.binding)s.binding=result;check();return result;};
    const auto word=[&](std::uint32_t offset,std::uint32_t value){if(const auto p=pointer())igb_bootstrap_write_word(*p,offset,value);};
    const auto byte=[&](std::uint32_t offset,std::uint8_t value){if(const auto p=pointer())igb_bootstrap_write_byte(*p,offset,value);};
    const auto select_field=[&](std::shared_ptr<IgbDecodeRuntimeField> field){s.field=std::move(field);s.binding=capture_binding(s.field);s.validate_binding=false;s.address.reset();};
    const auto local=[&](std::uint32_t index){if(!s.metadata || !s.metadata->fields)fail("Memory-field defaults reload a null field registry.");
        s.local_index=index;select_field(physical(s.metadata->fields->backing,s.first+index));};
    const auto value=[&](std::uint32_t v,unsigned width,P after){s.source=scalar(v,width);s.source_address.reset();s.after_scalar=after;s.phase=P::scalar_setup;};
    const auto bind=[&]{const auto f=fields();if(!s.field || !s.address || !f || !f->pointer_lists)return;
        auto& values=f->pointer_lists->values;
        for(const auto& v:values)if(v.address==*s.address && v.field==s.field && !v.metadata)return;
        values.push_back({*s.address,{},s.field});};
    try{for(;;){check();switch(s.phase){
    case P::capture:{const auto cell=igb_field_type_metadata_slot(services?services->registration:nullptr,IgbFieldTypeKind::memory_reference);
        if(!cell || !*cell)return pending("Memory-field defaults require represented canonical metadata80561818.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,8);s.phase=P::batch;break;}
    case P::batch:{const auto status=s.batch->resume(schema());check();if(!status.completed)return status;s.batch.reset();check();s.phase=P::find_setup;break;}
    case P::find_setup:
        if(!s.metadata)fail("Memory-field name search reads null metadata.");
        s.find_fields=s.metadata->fields;s.find_index=0;s.search_name=std::make_shared<const std::string>(s.reset?"_resetMethod":"_shallowCopyMethod");
        s.phase=P::find_descriptor;break;
    case P::find_descriptor:
        if(!lists() || !lists()->name_descriptor)return pending("Memory-field name search requires descriptor805617C4.");
        s.name_descriptor=*lists()->name_descriptor;s.phase=P::find_test;break;
    case P::find_test:
        if(!s.find_fields)fail("Memory-field name search tests a null registry.");
        if(std::bit_cast<std::int32_t>(s.find_index)<s.find_fields->count)s.phase=P::find_field;
        else{s.found.reset();s.phase=P::index;}break;
    case P::find_field:
        if(!s.name_descriptor)fail("Memory-field name search dereferences a null name descriptor.");
        s.found=physical(s.find_fields->backing,s.find_index);
        if(!s.found)fail("Memory-field name search dereferences a null field.");
        if(s.name_descriptor->offset!=0x0c)fail("Memory-field name search reaches an unrepresented string-member offset.");
        s.phase=P::find_name;break;
    case P::find_name:
        if(s.found->name_string)s.find_name=igb_interned_string_name(*s.found->name_string);
        else if(s.found->name)s.find_name=s.found->name;
        else return pending("Memory-field name search requires its captured descriptor's string pointer.");
        s.phase=P::find_compare;break;
    case P::find_compare:
        if(igb_compare_dependency_names(s.find_name,s.search_name)==0){s.found=physical(s.find_fields->backing,s.find_index);s.phase=P::index;}
        else{++s.find_index;s.phase=P::find_test;}break;
    case P::index:
        if(!s.reset)s.index_fields=s.metadata->fields;
        s.index=std::bit_cast<std::uint32_t>(index_of(s.index_fields,s.found));s.phase=s.reset?P::pool_setup:P::clone;break;
    case P::clone:
        if(!s.list_call.request.field){
            if(!s.found)fail("Memory-field defaults clone a null inherited shallow-copy descriptor.");
            const auto table=field_vtable(s.found);
            if(!table)return pending("Memory-field clone requires its captured source's vtable.");
            if(!*table)fail("Memory-field defaults call a null clone vtable.");
            s.list_call.request={K::clone_field,s.found,0,*table,0x54,1};if(lists())s.list_call.factory=lists()->factory;
        }
        {const auto status=s.list_call.poll(lists()?lists()->factory:IgbListDefaultsFactory{},check);if(!status.completed)return status;}
        s.phase=P::after_clone;break;
    case P::after_clone:{const auto result=s.list_call.operation->field_result;const auto address=s.list_call.operation->field_address;
        s.list_call.clear();check();select_field(result);s.address=address;bind();value(2,1,P::global);break;}
    case P::scalar_setup:{
        // The one-byte/four-byte stack input exists before the fresh vtable read.
        pointer();const auto table=field_vtable(s.field);
        if(!table)return pending("Memory-field scalar default requires its descriptor's fresh vtable.");
        if(!*table)fail("Memory-field scalar default calls a null vtable.");
        s.value_call.clear();check();s.value_call.request.kind=IgbFieldOperationKind::field_virtual;s.value_call.request.field=s.field;
        s.value_call.request.vtable_word=*table;s.value_call.request.virtual_slot=0x8c;
        s.value_call.request.source=s.source;s.value_call.request.source_address=s.source_address;
        if(fields())s.value_call.factory=fields()->factory;s.phase=P::scalar_call;break;}
    case P::scalar_call:{const auto status=s.value_call.poll(fields()?fields()->factory:IgbFieldOperationsFactory{},check);if(!status.completed)return status;
        s.value_call.clear();check();s.source.reset();s.source_address.reset();s.phase=s.after_scalar;break;}
    case P::global:{const auto cell=services?(s.reset?services->reset_method:services->shallow_copy):nullptr;
        if(!cell)return pending("Memory-field defaults require their canonical method descriptor cell.");
        const std::uint32_t address=s.reset?0x80561834u:0x80561820u;word(0x1c,address);
        s.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>(cell);s.field->global_slot_address=address;
        s.replace=std::make_unique<IgbFieldReplaceInvocation>(s.metadata,s.index,s.field);s.phase=P::replace;break;}
    case P::replace:{bind();const auto status=s.replace->resume(lists());check();if(!status.completed)return status;
        s.replace.reset();check();s.phase=P::release;break;}
    case P::release:{const auto reference=s.field->reference_word-1u;word(4,reference);s.field->reference_word=reference;
        if((reference&0x7fffffu)==0){s.list_call.clear();check();s.list_call.request.kind=K::destroy_field;
            s.list_call.request.field=s.field;s.list_call.request.function=0x80066e1c;if(lists())s.list_call.factory=lists()->factory;s.phase=P::destroy;}
        else s.phase=P::after_release;break;}
    case P::destroy:{const auto status=s.list_call.poll(lists()?lists()->factory:IgbListDefaultsFactory{},check);if(!status.completed)return status;
        s.list_call.clear();check();s.phase=P::after_release;break;}
    case P::after_release:select_field({});s.phase=s.reset?P::local_four:P::local_zero;break;
    case P::local_zero:local(0);byte(0x26,0);s.field->flag_26=0;s.phase=P::local_type;break;
    case P::local_type:local(1);s.lookup=std::make_unique<IgbFieldTypeLookupInvocation>(IgbFieldTypeKind::base,IgbFieldTypeLookup::allocate_if_null);s.phase=P::lookup;break;
    case P::lookup:{const auto status=s.lookup->resume(services?services->registration:nullptr);check();if(!status.completed)return status;
        s.related=s.lookup->result();s.lookup.reset();check();s.phase=P::type_store;break;}
    case P::type_store:
        if(const auto p=pointer()){const auto address=metadata_address(s.related,services);
            if(!address)return pending("Memory-field type store requires its captured metadata arithmetic address.");igb_bootstrap_write_word(*p,0x38,*address);}
        s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.related);s.phase=s.local_index==1?P::type_flag:P::construct_flag;break;
    case P::type_flag:byte(0x3c,0);s.field->flag_3c=std::uint8_t{0};s.phase=P::construct_flag;break;
    case P::construct_flag:byte(0x28,1);s.field->flag_28=std::uint8_t{1};s.related.reset();s.phase=s.local_index==1?P::local_two:P::schema_setup;break;
    case P::local_two:local(2);value(1,1,P::local_three);break;
    case P::local_three:local(3);value(1,1,P::reset_search);break;
    case P::reset_search:
        if(!s.metadata)fail("Memory-field reset lookup reads null metadata.");
        select_field({});s.reset=true;s.index_fields=s.metadata->fields;s.phase=P::find_setup;break;
    case P::pool_setup:{const auto cfg=schema();if(!cfg || !cfg->types || !cfg->types->metadata_pool_handle)
            return pending("Memory-field reset factory requires fresh pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*cfg->types->metadata_pool_handle);s.phase=P::pool;break;}
    case P::pool:{const auto cfg=schema();const auto status=s.pool_call->resume(cfg?cfg->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::create;break;}
    case P::create:
        if(!s.factory_call.request.function){s.factory_call.request.kind=IgbFieldSchemaServiceKind::create_field;
            s.factory_call.request.function=0x80031378;s.factory_call.request.pool=s.pool;if(schema())s.factory_call.factory=schema()->factory;}
        {const auto status=s.factory_call.poll(schema()?schema()->factory:IgbFieldSchemaFactory{},check);if(!status.completed)return status;}
        s.phase=P::after_create;break;
    case P::after_create:{const auto result=s.factory_call.operation->field_result;const auto address=s.factory_call.operation->field_address;
        s.factory_call.clear();check();select_field(result);s.address=address;bind();s.phase=P::configure_setup;break;}
    case P::configure_setup:
        if(!services || !services->reset_method)return pending("Memory-field reset configuration requires descriptor cell80561834.");
        s.configure=std::make_unique<IgbFieldConfigureInvocation>(s.field,std::make_shared<const std::string>("_resetMethod"),
            IgbFieldGlobalBinding{services->reset_method,0x80561834},0x24,s.metadata);s.phase=P::configure;break;
    case P::configure:{const auto status=s.configure->resume(schema());check();if(!status.completed)return status;
        s.configure.reset();check();s.phase=P::reset_scalar;break;}
    case P::reset_scalar:
        if(!services || !services->reset_default_source)return pending("Memory-field reset default requires original static source8055D2A8.");
        s.source=services->reset_default_source;s.source_address=0x8055d2a8;s.after_scalar=P::method;s.phase=P::scalar_setup;break;
    case P::method:word(0x34,0x8002a5ec);s.field->method_function=0x8002a5ec;s.phase=P::global;break;
    case P::local_four:local(4);value(1,1,P::local_five);break;
    case P::local_five:local(5);value(0xffffffff,4,P::local_six);break;
    case P::local_six:local(6);s.lookup=std::make_unique<IgbFieldTypeLookupInvocation>(IgbFieldTypeKind::base,IgbFieldTypeLookup::allocate_if_null);s.phase=P::lookup;break;
    case P::schema_setup:
        select_field({});if(!services)return pending("Memory-field defaults require their final schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(schema());check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_memory_field_defaults_factory(std::shared_ptr<IgbMemoryFieldDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || call.function!=0x8002af40)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Memory-field defaults services expired before selection.");
        auto invocation=std::make_shared<IgbMemoryFieldDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
