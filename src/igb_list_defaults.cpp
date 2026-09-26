#include "dw4/igb_list_defaults.hpp"
#include "dw4/igb_string_pool.hpp"
#include "dw4/igb_stream.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbListDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& v):flag(v){flag=true;}~Running(){flag=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;if(!factory)return pending("List defaults require their reached operation factory.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("List defaults reached an unbound original operation.");
        if(!callback)callback=operation->resume;if(!callback)return pending("List defaults require their retained continuation.");
        const auto status=callback();check();return status;
    }
};
using DefaultsCall=Call<IgbListDefaultsCall,IgbListDefaultsFactory,IgbListDefaultsOperation>;
using PointerCall=Call<IgbPointerListCall,IgbPointerListFactory,IgbPointerListOperation>;
std::shared_ptr<IgbDecodeRuntimeField> physical(const std::shared_ptr<IgbDecodeFieldRegistry::Storage>& backing,std::uint32_t byte_offset){
    const auto index=byte_offset>>2u;if(!backing || index>=backing->size())fail("List defaults read an unavailable physical field slot.");return (*backing)[index];
}
IgbPointerListBacking backing(const std::shared_ptr<IgbDecodeFieldRegistry>& list){
    if(!list)fail("Field replacement reads a null field registry.");
    return {list->backing_bytes,list->backing_address,list->backing_offset,{},list->backing};
}
std::size_t physical_store(const IgbPointerListBacking& b,std::uint32_t relative){
    const auto target=b.address+relative,at=static_cast<std::uint32_t>(target-(b.address-b.offset));
    if(!target || !b.bytes || b.bytes->released || at>b.bytes->bytes.size() || 4>b.bytes->bytes.size()-at ||
        at>b.bytes->initialized.size() || 4>b.bytes->initialized.size()-at)fail("Field replacement writes unavailable physical backing.");return at;
}
std::int32_t field_index(const std::shared_ptr<IgbDecodeFieldRegistry>& list,const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!list)fail("Field index search reads a null registry.");const auto count=list->count;const auto slots=list->backing;
    for(std::int32_t i=0;i<count;++i)if(physical(slots,static_cast<std::uint32_t>(i)<<2u)==field)return i;
    return -1;
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("List field store dereferences a null descriptor.");if(!field->physical_storage)return {};
    const auto& binding=*field->physical_storage;IgbBootstrapPointer pointer{binding.storage.lock(),binding.offset};
    if(!pointer.storage || igb_bootstrap_field(pointer)!=field)fail("List field store requires its live canonical physical binding.");return pointer;
}
void field_word(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t offset,std::uint32_t value){
    if(const auto pointer=physical_field(field))igb_bootstrap_write_word(*pointer,offset,value);
}
void field_byte(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t offset,std::uint8_t value){
    if(const auto pointer=physical_field(field))igb_bootstrap_write_byte(*pointer,offset,value);
}
void field_reference(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t value){field_word(field,4,value);field->reference_word=value;}
std::optional<std::uint32_t> field_address(const std::shared_ptr<IgbDecodeRuntimeField>& field,const std::shared_ptr<IgbListDefaultsServices>& services){
    if(!field)return 0;std::optional<std::uint32_t> result;
    if(const auto pointer=physical_field(field))result=igb_bootstrap_address(*pointer);
    const auto pointers=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    if(pointers)for(const auto& binding:pointers->values)if(binding.field==field){
        if(binding.metadata || !binding.address || (result && *result!=binding.address))fail("List element prototype has conflicting arithmetic identities.");result=binding.address;
    }return result;
}
}
IgbListDefaultsServices::IgbListDefaultsServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002ac34u,0x8002e1acu});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_data"),std::make_shared<const std::string>("_count")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805619d8},IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805619dc}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u,12u});
}
struct IgbFieldReplaceInvocation::Impl {
    enum class P {finalize,list,old,release_old,capacity,pool,size,after_size,reserve,store,bind,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field,old;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbListDefaultsServices> owner;
    std::unique_ptr<IgbFieldFinalizeInvocation> finalize;
    std::unique_ptr<IgbPointerListInvocation> reserve;
    std::shared_ptr<ModelSceneObject> pool;
    IgbPointerListBacking captured,store;
    DefaultsCall destroy;PointerCall call;
    std::uint32_t index{},capacity{},address{};P phase=P::finalize;bool running{},fault{};
};
IgbFieldReplaceInvocation::IgbFieldReplaceInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,std::uint32_t index,std::shared_ptr<IgbDecodeRuntimeField> field):impl_(std::make_unique<Impl>()){
    impl_->metadata=std::move(metadata);impl_->index=index;impl_->field=std::move(field);impl_->finalize=std::make_unique<IgbFieldFinalizeInvocation>(impl_->field);
}
IgbFieldReplaceInvocation::~IgbFieldReplaceInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.old)s.owner->unfinished.push_back(s.old);if(s.fields)s.owner->unfinished.push_back(s.fields);
    if(s.captured.bytes)s.owner->unfinished.push_back(s.captured.bytes);if(s.store.bytes)s.owner->unfinished.push_back(s.store.bytes);
}}
bool IgbFieldReplaceInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldReplaceInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldReplaceInvocation::resume(std::shared_ptr<IgbListDefaultsServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbPointerListServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field replacement requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field replacement continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field replacement was invalidated by its enclosing callback.");};
    const auto pointers=[&]{return services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:std::shared_ptr<IgbPointerListServices>{};};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind){clear();s.call.request.kind=kind;s.call.request.backing=s.captured;s.call.request.fields=s.fields;
        if(const auto p=pointers())s.call.factory=p->factory;};
    try{for(;;){check();switch(s.phase){
    case P::finalize:{const auto status=s.finalize->resume(services && services->schema?services->schema->fields:nullptr,check);check();if(!status.completed)return status;
        s.finalize.reset();check();s.phase=P::list;break;}
    case P::list:
        if(!s.metadata)fail("Finalized field replacement reads a null metadata receiver.");s.fields=s.metadata->fields;
        if(s.field)field_reference(s.field,s.field->reference_word+1u);s.phase=P::old;break;
    case P::old:
        if(!s.fields)fail("Field replacement reads a null registry after retaining its replacement.");
        s.old=physical(s.fields->backing,s.index<<2u);
        if(s.old){field_reference(s.old,s.old->reference_word-1u);if((s.old->reference_word&0x7fffffu)==0){
            s.destroy.request.kind=IgbListDefaultsServiceKind::destroy_field;s.destroy.request.function=0x80066e1c;s.destroy.request.field=s.old;
            if(services)s.destroy.factory=services->factory;s.phase=P::release_old;break;}}
        s.phase=P::capacity;break;
    case P::release_old:{const auto status=s.destroy.poll(services?services->factory:IgbListDefaultsFactory{},check);if(!status.completed)return status;
        s.destroy.clear();check();s.phase=P::capacity;break;}
    case P::capacity:
        s.captured=backing(s.fields);
        if(s.captured.address==0){if(s.captured.bytes || s.captured.fields)fail("Field replacement has a null arithmetic backing with nonnull storage.");s.capacity=0;s.phase=P::after_size;}
        else{select(K::allocation_pool);s.phase=P::pool;}break;
    case P::pool:{const auto p=pointers();const auto status=s.call.poll(p?p->factory:IgbPointerListFactory{},check);if(!status.completed)return status;
        s.pool=s.call.operation->pool_result;clear();if(!s.pool)fail("Field replacement size query dereferences a null pool.");
        select(K::allocation_size);s.call.request.pool=s.pool;s.phase=P::size;break;}
    case P::size:{const auto p=pointers();const auto status=s.call.poll(p?p->factory:IgbPointerListFactory{},check);if(!status.completed)return status;
        s.capacity=s.call.operation->size_result>>2u;clear();s.phase=P::after_size;break;}
    case P::after_size:
        if(std::bit_cast<std::int32_t>(s.index)<std::bit_cast<std::int32_t>(s.capacity))s.phase=P::store;
        else{s.reserve=std::make_unique<IgbPointerListInvocation>(s.fields,std::bit_cast<std::int32_t>(s.index));s.phase=P::reserve;}break;
    case P::reserve:{const auto status=s.reserve->resume(pointers(),check);check();if(!status.completed)return status;s.reserve.reset();check();s.phase=P::store;break;}
    case P::store:s.store=backing(s.fields);s.phase=P::bind;break;
    case P::bind:{
        const auto relative=s.index<<2u,slot=relative>>2u;const auto at=physical_store(s.store,relative);
        const auto live=backing(s.fields);if(!s.store.fields && live.bytes==s.store.bytes && live.address==s.store.address && live.offset==s.store.offset)s.store.fields=live.fields;
        if(!s.store.fields || slot>=s.store.fields->size())return pending("Field replacement requires its captured physical owner mapping.");
        std::optional<std::uint32_t> address;if(!s.field)address=0;
        else if(const auto p=pointers())for(const auto& value:p->values)if(value.field==s.field){
            if(value.metadata || value.address==0 || (address && *address!=value.address))fail("Replacement field has inconsistent arithmetic pointer bindings.");address=value.address;}
        if(!address)return pending("Field replacement requires the replacement's arithmetic pointer identity.");
        for(unsigned i=0;i<4;++i){s.store.bytes->bytes[at+i]=static_cast<std::uint8_t>(*address>>((3-i)*8));s.store.bytes->initialized[at+i]=1;}
        if((*s.store.fields)[slot] && (*s.store.fields)[slot]!=s.field && s.owner)s.owner->unfinished.push_back((*s.store.fields)[slot]);
        (*s.store.fields)[slot]=s.field;s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbListDefaultsInvocation::Impl {
    enum class P {capture,batch,first,prototype,register_prototype,reload_prototype,array,schema_setup,schema,find_setup,find_descriptor,find_test,
        find_field,find_name,find_compare,index,clone,after_clone,flag,global,replace,release,after_destroy,call,done};
    IgbListDefaultsEntry entry;
    std::shared_ptr<IgbListDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field,descriptor,found,element;
    std::shared_ptr<IgbDecodeFieldRegistry> find_fields;
    IgbDependencyName find_name;
    std::uint32_t first{},find_index{},index{};
    std::optional<std::uint32_t> address;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbFieldReplaceInvocation> replace;
    DefaultsCall call;P phase=P::capture,after=P::done;bool running{},fault{};
};
IgbListDefaultsInvocation::IgbListDefaultsInvocation(IgbListDefaultsEntry entry):impl_(std::make_unique<Impl>()){impl_->entry=entry;}
IgbListDefaultsInvocation::~IgbListDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->unfinished.push_back(s.metadata);if(s.field)s.owner->unfinished.push_back(s.field);if(s.found)s.owner->unfinished.push_back(s.found);
}}
bool IgbListDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbListDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbListDefaultsInvocation::resume(std::shared_ptr<IgbListDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbListDefaultsServiceKind;
    if(s.running || s.fault){s.fault=true;fail("List defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("List defaults continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbListDefaultsCall&{clear();s.call.request.kind=kind;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;return s.call.request;};
    const auto bind=[&]{if(!s.field || !s.address || !services || !services->schema || !services->schema->fields || !services->schema->fields->pointer_lists)return;
        auto& values=services->schema->fields->pointer_lists->values;for(const auto& v:values)if(v.field==s.field && v.address==*s.address && !v.metadata)return;
        values.push_back({*s.address,{},s.field});};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        if(!services || !services->lists)return pending("List defaults require their canonical metadata globals.");
        const auto slot=s.entry==IgbListDefaultsEntry::children?services->lists->children_metadata:services->lists->registry_metadata;
        if(!slot || !*slot)return pending("List defaults require their represented canonical metadata cell.");s.metadata=**slot;
        if(s.entry==IgbListDefaultsEntry::children){s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
            s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,2);s.phase=P::batch;}
        else s.phase=P::find_setup;break;
    }
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;s.batch.reset();check();s.phase=P::first;break;}
    case P::first:
        if(!s.metadata || !s.metadata->fields)fail("List defaults reload a null field registry.");
        s.field=physical(s.metadata->fields->backing,s.first<<2u);s.phase=P::prototype;break;
    case P::prototype:
        if(!services || !services->values || !services->values->object_prototype)return pending("Array-field default requires canonical prototype805616EC.");
        if(*services->values->object_prototype)s.phase=P::reload_prototype;
        else select(K::register_object_field,P::register_prototype).function=0x800289f8;break;
    case P::register_prototype:clear();s.phase=P::reload_prototype;break;
    case P::reload_prototype:
        if(!services || !services->values || !services->values->object_prototype)return pending("Array-field default requires the getter's reloaded805616EC.");
        s.element=*services->values->object_prototype;s.phase=P::array;break;
    case P::array:{
        if(!s.field)fail("List defaults configure a null first local field.");
        if(const auto pointer=physical_field(s.field)){const auto address=field_address(s.element,services);
            if(!address)return pending("Array field requires its captured element prototype's arithmetic address.");
            igb_bootstrap_write_word(*pointer,0x3c,*address);}
        s.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>(s.element);
        field_word(s.field,0x38,0x40);s.field->word_38=std::uint32_t{0x40};
        field_byte(s.field,0x34,1);s.field->flag_34=std::uint8_t{1};field_byte(s.field,0x35,1);s.field->flag_35=std::uint8_t{1};
        field_byte(s.field,0x40,1);s.field->flag_40=std::uint8_t{1};field_byte(s.field,0x41,1);s.field->flag_41=std::uint8_t{1};
        field_byte(s.field,0x42,0);s.field->flag_42=std::uint8_t{0};s.phase=P::schema_setup;break;
    }
    case P::schema_setup:
        if(!services)return pending("List defaults require their original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;s.schema.reset();check();s.phase=P::done;break;}
    case P::find_setup:
        if(!s.metadata)fail("Internal list defaults find a field through null metadata.");s.find_fields=s.metadata->fields;s.phase=P::find_descriptor;break;
    case P::find_descriptor:
        if(!services || !services->name_descriptor)return pending("Internal field lookup requires name descriptor805617C4.");
        s.descriptor=*services->name_descriptor;s.phase=P::find_test;break;
    case P::find_test:
        if(!s.find_fields)fail("Internal field lookup tests a null field registry.");
        if(std::bit_cast<std::int32_t>(s.find_index)<s.find_fields->count)s.phase=P::find_field;
        else{s.found.reset();s.phase=P::index;}break;
    case P::find_field:{
        const auto slots=s.find_fields->backing;if(!s.descriptor)fail("Internal field lookup reads a null name descriptor.");
        const auto offset=s.descriptor->offset;s.found=physical(slots,s.find_index<<2u);
        if(!s.found)fail("Internal field lookup dereferences a null field.");
        if(offset!=0x0c)fail("Internal field lookup reaches an unrepresented string-member offset.");s.phase=P::find_name;break;
    }
    case P::find_name:
        if(s.found->name_string)s.find_name=igb_interned_string_name(*s.found->name_string);
        else if(s.found->name)s.find_name=s.found->name;
        else return pending("Internal field lookup requires the selected field's string pointer.");
        s.phase=P::find_compare;break;
    case P::find_compare:
        if(igb_compare_dependency_names(s.find_name,SystemStageString{std::make_shared<const std::string>("_data")})==0){
            s.found=physical(s.find_fields->backing,s.find_index<<2u);s.phase=P::index;
        }else{++s.find_index;s.phase=P::find_test;}break;
    case P::index:
        if(!s.metadata)fail("Internal defaults reload a null metadata receiver.");s.index=std::bit_cast<std::uint32_t>(field_index(s.metadata->fields,s.found));s.phase=P::clone;break;
    case P::clone:
        if(!s.found)fail("Internal defaults call clone through a null found field.");
        if(!s.found->vtable_word)return pending("Internal defaults require the selected field's live clone virtual.");
        {const auto field=s.found;const auto table=*field->vtable_word;auto& call=select(K::clone_field,P::after_clone);
            call.field=field;call.vtable_word=table;call.virtual_slot=0x54;call.argument=1;}break;
    case P::after_clone:s.field=s.call.operation->field_result;s.address=s.call.operation->field_address;clear();bind();s.phase=P::flag;break;
    case P::flag:
        if(!s.field)fail("Internal defaults write through a null cloned field.");field_byte(s.field,0x42,1);s.field->flag_42=std::uint8_t{1};s.phase=P::global;break;
    case P::global:
        if(!services || !services->internal_data)return pending("Internal defaults require canonical descriptor-global805619D0.");
        field_word(s.field,0x1c,0x805619d0);
        s.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>(services->internal_data);s.field->global_slot_address=0x805619d0;
        s.replace=std::make_unique<IgbFieldReplaceInvocation>(s.metadata,s.index,s.field);s.phase=P::replace;break;
    case P::replace:{bind();const auto status=s.replace->resume(services,check);check();if(!status.completed)return status;s.replace.reset();check();s.phase=P::release;break;}
    case P::release:
        field_reference(s.field,s.field->reference_word-1u);if((s.field->reference_word&0x7fffffu)==0){const auto field=s.field;auto& call=select(K::destroy_field,P::after_destroy);
            call.field=field;call.function=0x80066e1c;}else s.phase=P::done;break;
    case P::after_destroy:clear();s.phase=P::done;break;
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbListDefaultsFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_list_defaults_factory(std::shared_ptr<IgbListDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbListDefaultsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || (call.function!=0x8002e108 && call.function!=0x8002ddd4))
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("List defaults services expired before selection.");
        auto invocation=std::make_shared<IgbListDefaultsInvocation>(call.function==0x8002e108?IgbListDefaultsEntry::children:IgbListDefaultsEntry::registry);
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
