#include "dw4/igb_string_field_defaults.hpp"
#include "dw4/igb_stream.hpp"
#include "dw4/igb_string_pool.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbStringFieldDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("String-field defaults require their reached clone or scalar service factory.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("String-field defaults reached an unbound clone or field virtual.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("String-field defaults require their retained service continuation.");
        const auto status=callback();check();return status;
    }
};
std::shared_ptr<IgbDecodeRuntimeField> physical(const std::shared_ptr<IgbDecodeFieldRegistry::Storage>& backing,std::uint32_t index){
    const auto slot=(index<<2u)>>2u;
    if(slot>=262144 || !backing || slot>=backing->size())fail("String-field defaults reach an unavailable physical field slot.");
    return (*backing)[slot];
}
std::int32_t index_of(const std::shared_ptr<IgbDecodeFieldRegistry>& list,const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!list)fail("String-field identity lookup reads a null registry.");
    const auto count=list->count;const auto backing=list->backing;
    for(std::int32_t i=0;i<count;++i)if(physical(backing,static_cast<std::uint32_t>(i))==field)return i;
    return -1;
}
std::optional<IgbBootstrapPointer> capture_binding(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field || !field->physical_storage)return {};
    return IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("String-field defaults dereference a null descriptor.");
    const auto pointer=capture_binding(field);
    if(pointer && (!pointer->storage || pointer->storage->faulted() || igb_bootstrap_field(*pointer)!=field))
        fail("String-field descriptor lost its canonical physical identity.");
    return pointer;
}
std::optional<std::uint32_t> field_vtable(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    // Physical +00 is read at the original call-selection point. Canonical
    // state cannot conceal a released allocation or an uninitialized header.
    if(const auto pointer=physical_field(field))return igb_bootstrap_read_word(*pointer,0);
    return field->vtable_word;
}
IgbRuntimeFieldDefaultSource scalar(std::uint8_t value){
    auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes={value};bytes->initialized={1};return {bytes,0};
}
}
IgbStringFieldDefaultsServices::IgbStringFieldDefaultsServices(){
    shallow_copy=std::make_shared<IgbRuntimeFieldSlot>();destruct=std::make_shared<IgbRuntimeFieldSlot>();
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x800385e0u});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_refCounted")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805615c4}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x38u});
}
struct IgbStringFieldDefaultsInvocation::Impl {
    enum class P {capture,batch,find_setup,find_descriptor,find_test,find_field,find_name,find_compare,index,clone,after_clone,
        scalar_setup,scalar_call,global,replace,release,destroy,after_release,local,schema_setup,schema,done};
    P phase=P::capture,after_scalar=P::done;
    std::shared_ptr<IgbStringFieldDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field,found,name_descriptor;
    std::shared_ptr<IgbDecodeFieldRegistry> find_fields;
    IgbDependencyName find_name;SystemStageString search_name;
    std::optional<IgbBootstrapPointer> binding;
    std::optional<std::uint32_t> address;
    std::optional<IgbRuntimeFieldDefaultSource> source;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbFieldReplaceInvocation> replace;
    Call<IgbListDefaultsCall,IgbListDefaultsFactory,IgbListDefaultsOperation> list_call;
    Call<IgbFieldOperationCall,IgbFieldOperationsFactory,IgbFieldOperation> value_call;
    std::uint32_t first{},find_index{},index{};
    bool running{},fault{},destruct{},validate_binding{};
};
IgbStringFieldDefaultsInvocation::IgbStringFieldDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbStringFieldDefaultsInvocation::~IgbStringFieldDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.field)s.owner->retained.push_back(s.field);
    if(s.found)s.owner->retained.push_back(s.found);if(s.find_fields)s.owner->retained.push_back(s.find_fields);
    if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
    if(s.source && s.source->buffer)s.owner->retained.push_back(s.source->buffer);
}}
bool IgbStringFieldDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbStringFieldDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbStringFieldDefaultsInvocation::resume(std::shared_ptr<IgbStringFieldDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbListDefaultsServiceKind;
    if(s.running || s.fault){s.fault=true;fail("String-field defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto lists=[&]{return services?services->lists:std::shared_ptr<IgbListDefaultsServices>{};};
    const auto schema=[&]{const auto list=lists();return list?list->schema:std::shared_ptr<IgbFieldSchemaServices>{};};
    const auto fields=[&]{const auto cfg=schema();return cfg?cfg->fields:std::shared_ptr<IgbFieldOperationsServices>{};};
    const auto check=[&]{if(s.fault)fail("String-field defaults were invalidated by a callback.");
        if(s.validate_binding && s.binding){const auto current=physical_field(s.field);
            if(!current || current->storage!=s.binding->storage || current->offset!=s.binding->offset)
                fail("String-field defaults lost their captured descriptor allocation.");}};
    const auto pointer=[&]{s.validate_binding=true;const auto result=physical_field(s.field);
        if(result && !s.binding)s.binding=result;check();return result;};
    const auto word=[&](std::uint32_t offset,std::uint32_t value){if(const auto p=pointer())igb_bootstrap_write_word(*p,offset,value);};
    const auto select_field=[&](std::shared_ptr<IgbDecodeRuntimeField> field){s.field=std::move(field);s.binding=capture_binding(s.field);s.validate_binding=false;s.address.reset();};
    const auto value=[&](std::uint8_t v,P after){s.source=scalar(v);s.after_scalar=after;s.phase=P::scalar_setup;};
    const auto bind=[&]{const auto f=fields();if(!s.field || !s.address || !f || !f->pointer_lists)return;
        auto& values=f->pointer_lists->values;
        for(const auto& v:values)if(v.address==*s.address && v.field==s.field && !v.metadata)return;
        values.push_back({*s.address,{},s.field});};
    try{for(;;){check();switch(s.phase){
    case P::capture:{const auto cell=igb_field_type_metadata_slot(services?services->registration:nullptr,IgbFieldTypeKind::string);
        if(!cell || !*cell)return pending("String-field defaults require represented canonical metadata805615B4.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,1);s.phase=P::batch;break;}
    case P::batch:{const auto status=s.batch->resume(schema());check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::find_setup;break;}
    case P::find_setup:
        if(!s.metadata)fail("String-field name search reads null metadata.");
        s.find_fields=s.metadata->fields;s.find_index=0;s.search_name=std::make_shared<const std::string>(s.destruct?"_destruct":"_shallowCopyMethod");
        s.phase=P::find_descriptor;break;
    case P::find_descriptor:
        if(!lists() || !lists()->name_descriptor)return pending("String-field name search requires descriptor805617C4.");
        s.name_descriptor=*lists()->name_descriptor;s.phase=P::find_test;break;
    case P::find_test:
        if(!s.find_fields)fail("String-field name search tests a null registry.");
        if(std::bit_cast<std::int32_t>(s.find_index)<s.find_fields->count)s.phase=P::find_field;
        else{s.found.reset();s.phase=P::index;}break;
    case P::find_field:
        if(!s.name_descriptor)fail("String-field name search dereferences a null name descriptor.");
        s.found=physical(s.find_fields->backing,s.find_index);
        if(!s.found)fail("String-field name search dereferences a null field.");
        if(s.name_descriptor->offset!=0x0c)fail("String-field name search reaches an unrepresented string-member offset.");
        s.phase=P::find_name;break;
    case P::find_name:
        if(s.found->name_string)s.find_name=igb_interned_string_name(*s.found->name_string);
        else if(s.found->name)s.find_name=s.found->name;
        else return pending("String-field name search requires its captured descriptor's string pointer.");
        s.phase=P::find_compare;break;
    case P::find_compare:
        if(igb_compare_dependency_names(s.find_name,s.search_name)==0){s.found=physical(s.find_fields->backing,s.find_index);s.phase=P::index;}
        else{++s.find_index;s.phase=P::find_test;}break;
    case P::index:
        // Both254E8 and25570 reload metadata.fields after the name search.
        s.index=std::bit_cast<std::uint32_t>(index_of(s.metadata->fields,s.found));s.phase=P::clone;break;
    case P::clone:
        if(!s.list_call.request.field){
            const auto table=field_vtable(s.found);
            if(!table)return pending("String-field clone requires its captured source's vtable.");
            if(!*table)fail("String-field defaults call a null clone vtable.");
            s.list_call.request={K::clone_field,s.found,0,*table,0x54,1};if(lists())s.list_call.factory=lists()->factory;
        }
        {const auto status=s.list_call.poll(lists()?lists()->factory:IgbListDefaultsFactory{},check);if(!status.completed)return status;}
        s.phase=P::after_clone;break;
    case P::after_clone:{const auto result=s.list_call.operation->field_result;const auto address=s.list_call.operation->field_address;
        s.list_call.clear();check();select_field(result);s.address=address;bind();value(s.destruct?std::uint8_t{1}:std::uint8_t{2},P::global);break;}
    case P::scalar_setup:{
        pointer();const auto table=field_vtable(s.field);
        if(!table)return pending("String-field scalar default requires its descriptor's fresh vtable.");
        if(!*table)fail("String-field scalar default calls a null vtable.");
        s.value_call.clear();check();s.value_call.request.kind=IgbFieldOperationKind::field_virtual;s.value_call.request.field=s.field;
        s.value_call.request.vtable_word=*table;s.value_call.request.virtual_slot=0x8c;s.value_call.request.source=s.source;
        if(fields())s.value_call.factory=fields()->factory;s.phase=P::scalar_call;break;}
    case P::scalar_call:{const auto status=s.value_call.poll(fields()?fields()->factory:IgbFieldOperationsFactory{},check);if(!status.completed)return status;
        s.value_call.clear();check();s.source.reset();s.phase=s.after_scalar;break;}
    case P::global:{const auto cell=services?(s.destruct?services->destruct:services->shallow_copy):nullptr;
        if(!cell)return pending("String-field defaults require their canonical inherited descriptor cell.");
        const std::uint32_t address=s.destruct?0x805615c0u:0x805615bcu;word(0x1c,address);
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
    case P::after_release:select_field({});if(s.destruct)s.phase=P::local;else{s.destruct=true;s.phase=P::find_setup;}break;
    case P::local:
        if(!s.metadata || !s.metadata->fields)fail("String-field defaults reload a null local-field registry.");
        select_field(physical(s.metadata->fields->backing,s.first));value(std::uint8_t{1},P::schema_setup);break;
    case P::schema_setup:
        select_field({});if(!services)return pending("String-field defaults require their final schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(schema());check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_string_field_defaults_factory(std::shared_ptr<IgbStringFieldDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || call.function!=0x8002549c)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("String-field defaults services expired before selection.");
        auto invocation=std::make_shared<IgbStringFieldDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
