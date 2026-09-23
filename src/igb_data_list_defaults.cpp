#include "dw4/igb_data_list_defaults.hpp"
#include "dw4/igb_string_pool.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbDataListDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
struct Call {
    IgbListDefaultsCall request;IgbListDefaultsFactory factory;
    std::shared_ptr<IgbListDefaultsOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbListDefaultsServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Data-list defaults require their captured clone or destructor service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Data-list defaults reached an unbound clone or destructor operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Data-list defaults require their retained operation continuation.");
        const auto status=callback();check();return status;
    }
};
std::shared_ptr<IgbDecodeRuntimeField> physical(const std::shared_ptr<IgbDecodeFieldRegistry::Storage>& backing,std::uint32_t index){
    // Original658E4 scales the index with a wrapping32-bit SLWI before
    // accessing backing. Bound the resulting physical slot, not high bits
    // that the original multiplication discards.
    const auto slot=(index<<2u)>>2u;
    if(slot>=262144 || !backing || slot>=backing->size())fail("Data-list defaults reach an unavailable physical field slot.");
    return (*backing)[slot];
}
std::int32_t index_of(const std::shared_ptr<IgbDecodeFieldRegistry>& list,const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!list)fail("Data-list identity lookup reads a null field registry.");
    const auto count=list->count;const auto backing=list->backing;
    for(std::int32_t i=0;i<count;++i)if(physical(backing,static_cast<std::uint32_t>(i))==field)return i;
    return -1;
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Data-list defaults store through a null descriptor.");
    if(!field->physical_storage)return {};
    IgbBootstrapPointer pointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
    if(!pointer.storage || pointer.storage->faulted() || igb_bootstrap_field(pointer)!=field)
        fail("Data-list descriptor lost its canonical physical identity.");
    return pointer;
}
std::optional<IgbBootstrapPointer> capture_field_binding(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    // The caller captures the descriptor before invoking the prototype getter,
    // but does not dereference its destination storage until the later store.
    if(!field || !field->physical_storage)return {};
    return IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
}
std::optional<std::uint32_t> field_address(const std::shared_ptr<IgbDecodeRuntimeField>& field,
    const std::shared_ptr<IgbListDefaultsServices>& services){
    if(!field)return 0;
    std::optional<std::uint32_t> result;
    if(const auto pointer=physical_field(field))result=igb_bootstrap_address(*pointer);
    const auto pointers=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    if(pointers)for(const auto& value:pointers->values)if(value.field==field){
        if(value.metadata || !value.address || (result && *result!=value.address))fail("Data-list prototype has conflicting arithmetic identities.");
        result=value.address;
    }return result;
}
}
IgbDataListDefaultsServices::IgbDataListDefaultsServices(){
    unsigned_char_prototype=std::make_shared<IgbRuntimeFieldSlot>();nonref_data=std::make_shared<IgbRuntimeFieldSlot>();
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002e1acu,0x8002e1acu,0x8002ac34u});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_count"),
        std::make_shared<const std::string>("_capacity"),std::make_shared<const std::string>("_data")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561d40},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561d44},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561d48}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u,12u,16u});
}
struct IgbDataListPrototypeInvocation::Impl {
    enum class P {read,registration,reload,done};P phase=P::read;
    IgbDataListPrototypeKind kind{};std::shared_ptr<IgbRuntimeFieldSlot> slot;
    std::shared_ptr<IgbDecodeRuntimeField> result;std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    bool running{},fault{};
};
IgbDataListPrototypeInvocation::IgbDataListPrototypeInvocation(IgbDataListPrototypeKind kind):impl_(std::make_unique<Impl>()){
    if(kind!=IgbDataListPrototypeKind::unsigned_char && kind!=IgbDataListPrototypeKind::object)fail("Unknown data-list element prototype getter.");
    impl_->kind=kind;
}
IgbDataListPrototypeInvocation::~IgbDataListPrototypeInvocation()=default;
bool IgbDataListPrototypeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListPrototypeInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbDataListPrototypeInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete data-list prototype getter has no result.");return impl_->result;
}
MessengerCallResult IgbDataListPrototypeInvocation::resume(std::shared_ptr<IgbDataListDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Data-list prototype getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Data-list prototype getter was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot && services){
            if(s.kind==IgbDataListPrototypeKind::unsigned_char)s.slot=services->unsigned_char_prototype;
            else if(services->lists && services->lists->values)
                s.slot=std::shared_ptr<IgbRuntimeFieldSlot>(services->lists->values,&services->lists->values->object_prototype);
        }
        if(!s.slot || !*s.slot)return pending(s.kind==IgbDataListPrototypeKind::unsigned_char?
            "Data-list prototype getter requires represented805614D4.":"Data-list prototype getter requires represented805616EC.");
        if(**s.slot)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(s.kind==IgbDataListPrototypeKind::unsigned_char?0x80023234u:0x80028a20u);s.phase=P::registration;}
        break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Data-list prototype getter requires its reloaded canonical cell.");
        s.result=**s.slot;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbDataListDefaultsInvocation::Impl {
    enum class P {capture,batch,field,prototype,element,size,flag35,flag40,flag41,flag42,global,schema_setup,schema,
        find_setup,find_descriptor,find_test,find_field,find_name,find_compare,index,clone,after_clone,replace,release,destroy,call,done};
    P phase=P::capture,after=P::done;IgbDataListKind kind{};
    std::shared_ptr<IgbDataListDefaultsServices> owner;std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field,element,found,name_descriptor;
    std::shared_ptr<IgbDecodeFieldRegistry> find_fields;IgbDependencyName find_name;
    std::optional<IgbBootstrapPointer> field_binding;std::optional<std::uint32_t> address;
    std::unique_ptr<IgbFieldBatchInvocation> batch;std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbDataListPrototypeInvocation> prototype;std::unique_ptr<IgbFieldReplaceInvocation> replace;
    Call call;std::uint32_t first{},find_index{},index{};bool running{},fault{},validate_binding{};
};
IgbDataListDefaultsInvocation::IgbDataListDefaultsInvocation(IgbDataListKind kind):impl_(std::make_unique<Impl>()){
    if(kind!=IgbDataListKind::data_list && kind!=IgbDataListKind::nonref_object)fail("This data-list class has no defaults body.");impl_->kind=kind;
}
IgbDataListDefaultsInvocation::~IgbDataListDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.field)s.owner->retained.push_back(s.field);
    if(s.found)s.owner->retained.push_back(s.found);if(s.field_binding)s.owner->retained.push_back(s.field_binding->storage);
}}
bool IgbDataListDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbDataListDefaultsInvocation::resume(std::shared_ptr<IgbDataListDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbListDefaultsServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Data-list defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto lists=[&]{return services?services->lists:std::shared_ptr<IgbListDefaultsServices>{};};
    const auto schema=[&]{const auto list=lists();return list?list->schema:std::shared_ptr<IgbFieldSchemaServices>{};};
    const auto check=[&]{if(s.fault)fail("Data-list defaults were invalidated by a callback.");
        if(s.validate_binding && s.field_binding){const auto current=physical_field(s.field);
            if(!current || current->storage!=s.field_binding->storage || current->offset!=s.field_binding->offset)
                fail("Data-list defaults lost their captured descriptor allocation.");}};
    const auto clear=[&]{s.call.clear();check();};
    const auto pointer=[&]{const auto result=physical_field(s.field);
        if(result && !s.field_binding)s.field_binding=result;check();return result;};
    const auto word=[&](std::uint32_t offset,std::uint32_t value){if(const auto p=pointer())igb_bootstrap_write_word(*p,offset,value);};
    const auto byte=[&](std::uint32_t offset,std::uint8_t value){if(const auto p=pointer())igb_bootstrap_write_byte(*p,offset,value);};
    const auto bind=[&]{const auto cfg=schema();if(!s.field || !s.address || !cfg || !cfg->fields || !cfg->fields->pointer_lists)return;
        auto& values=cfg->fields->pointer_lists->values;for(const auto& value:values)if(value.field==s.field && value.address==*s.address && !value.metadata)return;
        values.push_back({*s.address,{},s.field});};
    const auto select=[&](K kind,P after)->IgbListDefaultsCall&{clear();s.call.request.kind=kind;
        if(const auto list=lists())s.call.factory=list->factory;s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::capture:{const auto slot=igb_data_list_metadata_slot(services?services->registration:nullptr,s.kind);
        if(!slot || !*slot)return pending("Data-list defaults require their represented canonical metadata cell.");s.metadata=**slot;
        if(s.kind==IgbDataListKind::data_list){s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
            s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,3);s.phase=P::batch;}
        else s.phase=P::find_setup;break;}
    case P::batch:{const auto status=s.batch->resume(schema());check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;}
    case P::field:
        if(!s.metadata || !s.metadata->fields)fail("Data-list defaults reload a null field registry.");
        s.field=physical(s.metadata->fields->backing,s.first+2u);s.field_binding=capture_field_binding(s.field);
        s.prototype=std::make_unique<IgbDataListPrototypeInvocation>(IgbDataListPrototypeKind::unsigned_char);s.phase=P::prototype;break;
    case P::prototype:{const auto status=s.prototype->resume(services);check();if(!status.completed)return status;
        s.element=s.prototype->result();s.prototype.reset();check();s.phase=P::element;break;}
    case P::element:
        s.validate_binding=true;
        if(const auto p=pointer()){const auto address=field_address(s.element,lists());
            if(!address)return pending("Data-list element store requires its captured prototype arithmetic address.");igb_bootstrap_write_word(*p,0x3c,*address);}
        s.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>(s.element);
        s.phase=s.kind==IgbDataListKind::data_list?P::flag35:P::size;break;
    case P::size:word(0x38,0);s.field->word_38=0;s.phase=P::flag42;break;
    case P::flag35:byte(0x35,1);s.field->flag_35=std::uint8_t{1};s.phase=P::flag40;break;
    case P::flag40:byte(0x40,1);s.field->flag_40=std::uint8_t{1};s.phase=P::flag41;break;
    case P::flag41:byte(0x41,1);s.field->flag_41=std::uint8_t{1};s.phase=P::schema_setup;break;
    case P::flag42:byte(0x42,0);s.field->flag_42=std::uint8_t{0};s.phase=P::global;break;
    case P::global:
        if(!services || !services->nonref_data)return pending("Data-list specialization requires canonical descriptor cell80561734.");
        word(0x1c,0x80561734);s.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>(services->nonref_data);s.field->global_slot_address=0x80561734;
        s.replace=std::make_unique<IgbFieldReplaceInvocation>(s.metadata,s.index,s.field);s.phase=P::replace;break;
    case P::schema_setup:
        if(!services)return pending("Data-list defaults require their original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(schema());check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::find_setup:
        if(!s.metadata)fail("Data-list field lookup dereferences null metadata.");s.find_fields=s.metadata->fields;s.phase=P::find_descriptor;break;
    case P::find_descriptor:
        if(!lists() || !lists()->name_descriptor)return pending("Data-list field lookup requires name descriptor805617C4.");
        s.name_descriptor=*lists()->name_descriptor;s.phase=P::find_test;break;
    case P::find_test:
        if(!s.find_fields)fail("Data-list name search tests a null registry.");
        if(std::bit_cast<std::int32_t>(s.find_index)<s.find_fields->count)s.phase=P::find_field;
        else{s.found.reset();s.phase=P::index;}break;
    case P::find_field:
        if(!s.name_descriptor)fail("Data-list name search dereferences a null name descriptor.");
        s.found=physical(s.find_fields->backing,s.find_index);
        if(!s.found)fail("Data-list name search dereferences a null field.");
        if(s.name_descriptor->offset!=0x0c)fail("Data-list name search reaches an unrepresented string-member offset.");s.phase=P::find_name;break;
    case P::find_name:
        if(s.found->name_string)s.find_name=igb_interned_string_name(*s.found->name_string);
        else if(s.found->name)s.find_name=s.found->name;
        else return pending("Data-list name search requires its captured field's string pointer.");s.phase=P::find_compare;break;
    case P::find_compare:
        if(igb_compare_dependency_names(s.find_name,SystemStageString{std::make_shared<const std::string>("_data")})==0){
            s.found=physical(s.find_fields->backing,s.find_index);s.phase=P::index;
        }else{++s.find_index;s.phase=P::find_test;}break;
    case P::index:s.index=std::bit_cast<std::uint32_t>(index_of(s.metadata->fields,s.found));s.phase=P::clone;break;
    case P::clone:
        if(!s.found)fail("Data-list specialization calls clone through a null inherited field.");
        if(!s.found->vtable_word)return pending("Data-list specialization requires its captured source's clone virtual.");
        if(!*s.found->vtable_word)fail("Data-list specialization dereferences a null clone vtable.");
        {const auto table=*s.found->vtable_word;auto& request=select(K::clone_field,P::after_clone);
            request.field=s.found;request.vtable_word=table;request.virtual_slot=0x54;request.argument=1;}break;
    case P::after_clone:
        s.field=s.call.operation->field_result;s.address=s.call.operation->field_address;clear();bind();
        s.field_binding=capture_field_binding(s.field);
        s.prototype=std::make_unique<IgbDataListPrototypeInvocation>(IgbDataListPrototypeKind::object);s.phase=P::prototype;break;
    case P::replace:{bind();const auto status=s.replace->resume(lists());check();if(!status.completed)return status;
        s.replace.reset();check();s.phase=P::release;break;}
    case P::release:{const auto reference=s.field->reference_word-1u;word(4,reference);s.field->reference_word=reference;
        if((s.field->reference_word&0x7fffffu)==0){auto& request=select(K::destroy_field,P::destroy);request.field=s.field;request.function=0x80066e1c;}
        else s.phase=P::done;break;}
    case P::destroy:clear();s.phase=P::done;break;
    case P::call:{const auto status=s.call.poll(lists(),check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_data_list_defaults_factory(std::shared_ptr<IgbDataListDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || (call.function!=0x80033aa4 && call.function!=0x80029724))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Data-list defaults services expired before selection.");
        auto invocation=std::make_shared<IgbDataListDefaultsInvocation>(call.function==0x80033aa4?IgbDataListKind::data_list:IgbDataListKind::nonref_object);
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
