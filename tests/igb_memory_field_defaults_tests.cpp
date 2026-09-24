#include "dw4/igb_memory_field_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace dw4;using FK=IgbFieldOperationKind;using LK=IgbListDefaultsServiceKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& fn,const char* message){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
std::shared_ptr<IgbScratchBuffer> bytes(std::size_t size,std::uint8_t value=0){auto b=std::make_shared<IgbScratchBuffer>();
    b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t address){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);m->fields->backing_bytes=bytes(64);m->fields->backing_address=address;return m;}
std::shared_ptr<IgbDecodeRuntimeField> field(std::uint32_t table,std::uint16_t size=4){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=table;f->size=size;
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(size),0};f->default_bytes_address=0x700000;
    f->name_string=std::shared_ptr<IgbInternedString>{};f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
    f->flag_26=1;f->flag_3c=std::uint8_t{0xaa};f->flag_27=std::uint8_t{0xab};return f;}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& f,std::uint32_t address,std::uint32_t size=0x50){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-12;storage->bytes=bytes(size+12,0xcc);
    storage->bindings.push_back({12,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{storage,12};
    IgbBootstrapPointer p{storage,12};igb_bootstrap_write_word(p,0,*f->vtable_word);igb_bootstrap_write_word(p,4,f->reference_word);
    igb_bootstrap_write_word(p,0x20,*f->default_bytes_address);return p;
}
struct Fixture {
    static constexpr std::uint32_t target_address=0x650000;
    std::shared_ptr<IgbMemoryFieldDefaultsServices> services=std::make_shared<IgbMemoryFieldDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(0x900000),base=metadata(0x910000);
    std::shared_ptr<IgbDecodeRuntimeField> inherited_copy=field(0x80473f24,1),inherited_reset=field(0x80473f24,1),clone,method;
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<IgbListDefaultsCall> list_calls;
    std::vector<IgbFieldOperationCall> method_calls;
    std::function<void(const IgbListDefaultsCall&)> list_action;
    std::function<void(const IgbFieldOperationCall&)> value_action;
    std::function<void(std::size_t)> constructor_action;
    bool clone_ready=true,method_ready=true,factory_ready=true,with_physical{};
    unsigned clone_polls{},method_polls{},factory_polls{},names{},method_factories{};
    std::uint32_t physical_size=0x50;
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=std::make_shared<IgbRegistrationMetaSlot>();
        *igb_field_type_metadata_slot(services->registration,IgbFieldTypeKind::memory_reference)=target;
        services->registration->base_metadata=std::make_shared<IgbRegistrationMetaSlot>(base);
        services->lists=std::make_shared<IgbListDefaultsServices>();services->lists->schema=std::make_shared<IgbFieldSchemaServices>();
        auto& s=*services->lists->schema;s.types=std::make_shared<IgbTypeRegistrationServices>();s.types->metadata_pool_handle=5;
        s.pools=std::make_shared<IgbFilePoolsServices>();s.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        s.pools->memory_pools->selector=0;s.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->registration->types=s.types;services->registration->pools=s.pools;
        s.dependencies=std::make_shared<IgbDependenciesServices>();s.dependencies->globals=std::make_shared<SystemStageServices>();s.dependencies->globals->string_pool=pool;
        s.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++names;auto value=std::make_shared<IgbInternedString>();value->value=igb_dependency_name(c.name);value->container=pool;out->string_result=value;}
            else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared name endpoint.");return MessengerCallResult{true,0,{}};};return op;};
        s.fields=std::make_shared<IgbFieldOperationsServices>();s.fields->dependencies=s.dependencies;s.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        // This is the declaring metadata's identity, not its pointer-array address.
        s.fields->pointer_lists->values.push_back({target_address,target,{}});
        s.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool || c.kind==PK::object_pool)out->pool_result=pool;
            else{check(c.kind==PK::allocation_size,"Unexpected prepared list allocation.");out->size_result=64;}
            return MessengerCallResult{true,0,{}};};return op;};
        services->lists->values=std::make_shared<IgbFieldValuesServices>();auto& v=*services->lists->values;v.fields=s.fields;
        v.signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};v.boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        v.object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};v.string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        v.signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        const auto prepared_fields=[this](const IgbFieldOperationCall& c){
            auto op=std::make_shared<IgbFieldOperation>();
            if(c.kind==FK::field_virtual){check(c.vtable_word==0x804758e4,"Unexpected prepared field vtable.");
                if(c.virtual_slot==0x8c && c.source)method_calls.push_back(c);
                op->resume=[this,c]{
                    if(c.virtual_slot==0x64)return MessengerCallResult{true,4,{}};
                    if(c.virtual_slot==0x5c)return MessengerCallResult{true,0,{}};
                    if(c.virtual_slot==0x8c && !c.source)return MessengerCallResult{true,0,{}};
                    check(c.virtual_slot==0x8c && c.source && c.source_address==0x8055d2a8,"Reset method setter lost its static source.");
                    ++method_polls;if(!method_ready)return MessengerCallResult{false,0,"prepared method virtual"};
                    if(value_action)value_action(c);
                    const auto source=igb_buffer_bytes(c.source->buffer,c.source->offset,4);
                    for(unsigned i=0;i<4;++i)c.field->default_bytes->buffer->bytes[c.field->default_bytes->offset+i]=source[i];
                    return MessengerCallResult{true,0,{}};
                };
            }else op->resume=[this,c,out=op.get()]{
                if(c.kind==FK::allocation_pool || c.kind==FK::object_pool)out->pool_result=pool;
                else{check(c.kind==FK::allocation_size,"Unexpected prepared field allocation.");return MessengerCallResult{true,4,{}};}
                return MessengerCallResult{true,0,{}};};return op;
        };
        const auto native_fields=make_native_igb_field_values_factory(services->lists->values,prepared_fields);
        // This caller fixture deliberately prepares enum-field virtuals so its
        // suspension/mutation cases isolate2AF40. Keep that boundary explicit
        // now that the production value adapter also recognizes this vtable.
        s.fields->factory=[prepared_fields,native_fields](const IgbFieldOperationCall& c){
            if(c.kind==FK::field_virtual && c.vtable_word==0x804758e4)return prepared_fields(c);
            return native_fields(c);
        };
        // Only factory/clone/method bodies and allocation/string endpoints are
        // prepared. The defaults caller, batch, values, schema and replacement run natively.
        s.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected batch destroy.");
            const bool reset=c.function==0x80031378;const auto index=created.size();
            if(reset)++method_factories;else check(index<8 && c.function==*services->factories->at(index),"Memory constructor table differs.");
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,index,reset,c,out=op.get()]{
                if(reset){++factory_polls;if(!factory_ready)return MessengerCallResult{false,0,"prepared method constructor"};check(c.pool==pool,"Method factory lost the fresh pool.");}
                const auto table=reset?0x804758e4u:index==1 || index==6?0x804762fcu:index==2 || index==3 || index==4?0x80473e30u:index==7?0x8047693cu:0x80475b60u;
                auto f=field(table,(index==2 || index==3 || index==4) && !reset?1:4);
                if(reset)method=f;else created.push_back(f);
                out->field_result=f;out->field_address=reset?0x190000u:0x100000u+static_cast<std::uint32_t>(index)*0x100u;
                if(with_physical)allocations.push_back(physical(f,*out->field_address,physical_size));
                if(constructor_action)constructor_action(reset?8:index);return MessengerCallResult{true,0,{}};};return op;};
        services->lists->factory=[this](const IgbListDefaultsCall& c){list_calls.push_back(c);auto op=std::make_shared<IgbListDefaultsOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==LK::clone_field){++clone_polls;if(!clone_ready)return MessengerCallResult{false,0,"prepared shallow-copy clone"};
                    check(c.field==inherited_copy && c.virtual_slot==0x54 && c.argument==1,"Inherited clone selection differs.");
                    clone=std::make_shared<IgbDecodeRuntimeField>(*c.field);clone->physical_storage.reset();clone->reference_word=1;
                    clone->default_bytes=IgbRuntimeFieldDefaultSource{bytes(1),0};out->field_result=clone;out->field_address=0x180000;
                    if(with_physical)allocations.push_back(physical(clone,*out->field_address,physical_size));
                }else check(c.kind==LK::destroy_field && c.function==0x80066e1c,"Unexpected replacement destructor.");
                if(list_action)list_action(c);return MessengerCallResult{true,0,{}};};return op;};
        auto name=field(0x8047693c);name->offset=0xc;services->lists->name_descriptor=name;
        inherited_copy->name=std::make_shared<const std::string>("_shallowCopyMethod");inherited_copy->name_string.reset();inherited_copy->reference_word=2;
        inherited_reset->name=std::make_shared<const std::string>("_resetMethod");inherited_reset->name_string.reset();inherited_reset->reference_word=2;
        target->fields->count=2;target->fields->backing->at(0)=inherited_copy;target->fields->backing->at(1)=inherited_reset;
        s.fields->pointer_lists->values.push_back({0x660000,base,{}});
    }
};
void complete_defaults(){Fixture f;f.with_physical=true;IgbMemoryFieldDefaultsInvocation call;
    check(call.resume(f.services).completed && f.created.size()==8 && f.names==9 && f.target->fields->count==10,"Memory defaults did not finish both inherited replacements and eight fields.");
    check(f.target->fields->backing->at(0)==f.clone && f.target->fields->backing->at(1)==f.method &&
        *f.services->shallow_copy==f.clone && *f.services->reset_method==f.method,"Method replacement did not publish actual canonical identities.");
    check(f.inherited_copy->reference_word==1 && f.inherited_reset->reference_word==1 && f.clone->reference_word==1 && f.method->reference_word==1,
        "Method replacement ownership differs.");
    check(f.clone->default_bytes->buffer->bytes[0]==2 && f.inherited_copy->default_bytes->buffer->bytes[0]==0 &&
        igb_buffer_word(f.method->default_bytes->buffer,0)==3 && f.method->method_function==0x8002a5ec,
        "Method defaults, original source constant or method function differ.");
    check(f.created[0]->flag_26==0 && f.created[1]->object_type->lock()==f.base && f.created[1]->flag_3c==0 && f.created[1]->flag_28==1 &&
        f.created[1]->flag_27==0xab && f.created[6]->object_type->lock()==f.base && f.created[6]->flag_28==1 && f.created[6]->flag_3c==0xaa,
        "Memory descriptor type stores touched unrelated flags or skipped the second getter.");
    for(const auto i:{2u,3u,4u})check(f.created[i]->default_bytes->buffer->bytes[0]==1,"Boolean memory default is not true.");
    check(igb_buffer_word(f.created[5]->default_bytes->buffer,0)==0xffffffff,"Alignment default is not signed minus one.");
    for(std::size_t i=0;i<8;++i)check(*f.services->globals->at(i)->slot==f.created[i] && f.created[i]->offset==*f.services->offsets->at(i) &&
        *f.created[i]->name==**f.services->names->at(i) && f.created[i]->declaring_type->lock()==f.target &&
        igb_bootstrap_read_word(f.allocations[i],0x18)==Fixture::target_address &&
        igb_bootstrap_read_word(f.allocations[i],8)==*f.services->offsets->at(i) &&
        igb_bootstrap_read_word(f.allocations[i],0x1c)==f.services->globals->at(i)->address,
        "Eight-field schema changed tables, declaring owner or physical descriptor words.");
    check(igb_bootstrap_read_word(f.allocations[1],0x38)==0x660000 && igb_bootstrap_read_byte(f.allocations[1],0x3c)==0 &&
        igb_bootstrap_read_byte(f.allocations[1],0x28)==1 && igb_bootstrap_read_word(f.allocations[6],0x38)==0x660000 &&
        igb_bootstrap_read_word(f.allocations.back(),0x34)==0x8002a5ec && igb_bootstrap_read_word(f.allocations.back(),0x1c)==0x80561834 &&
        igb_bootstrap_read_word(f.allocations.back(),8)==0x24 && igb_bootstrap_read_word(f.allocations.back(),0x18)==Fixture::target_address &&
        igb_bootstrap_read_word(f.allocations.back(),4)==1,"Memory default direct physical stores do not match canonical state.");
    check(call.resume().completed && f.created.size()==8 && f.method_factories==1,"Completed defaults replayed constructors.");
}
void retained_operations(){Fixture f;f.clone_ready=false;IgbMemoryFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.created.size()==8 && f.list_calls.size()==1,"Clone suspension did not retain the completed batch.");
    f.inherited_copy->vtable_word=0;f.clone_ready=true;f.factory_ready=false;
    // The clone leaf returns a valid descriptor even though its captured source
    // vtable changes while pending; the original call target was already selected.
    f.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field)f.clone->vtable_word=0x80473f24;};
    check(!call.resume(f.services).completed && f.method_factories==1 && f.factory_polls==1 && f.created[3]->default_bytes->buffer->bytes[0]==1,
        "Captured clone vtable was re-read or reset factory was skipped.");
    f.services->lists->schema->factory={};f.factory_ready=true;f.method_ready=false;
    check(!call.resume(f.services).completed && f.factory_polls==2 && f.method_calls.size()==1 && f.names==1 && !f.method->method_function,
        "Method factory continuation was reselected or pointer published before setter completion.");
    f.method_ready=true;check(call.resume(f.services).completed && f.method_polls==2 && f.method_calls.size()==1 && f.names==9,
        "Pending method setter replayed completed configuration or lost its selected operation.");
}
void getter_capture_and_fault_order(){Fixture f;f.with_physical=true;
    f.services->registration->base_metadata->reset();IgbMemoryFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.clone && f.created[0]->flag_26==0 && !f.created[1]->object_type,
        "Unknown lazy type getter skipped the committed shallow replacement/local-zero prefix.");
    const auto captured=f.created[1];auto other=metadata(0x950000);
    f.services->lists->schema->fields->pointer_lists->values.push_back({0x640000,other,{}});
    *igb_field_type_metadata_slot(f.services->registration,IgbFieldTypeKind::memory_reference)=other;
    *f.services->registration->base_metadata=f.base;
    check(call.resume(f.services).completed && captured->object_type->lock()==f.base && other->fields->count==0 && f.created.size()==8,
        "Getter suspension changed captured metadata or replayed fields.");
    Fixture malformed;malformed.with_physical=true;malformed.services->registration->base_metadata->reset();
    malformed.constructor_action=[&](std::size_t i){if(i==1)malformed.created[1]->physical_storage=IgbFieldStorageBinding{{},12};};
    IgbMemoryFieldDefaultsInvocation delayed;check(!delayed.resume(malformed.services).completed && !delayed.faulted(),
        "Malformed future type-store destination failed before the original getter.");
    *malformed.services->registration->base_metadata=malformed.base;
    rejects([&]{delayed.resume(malformed.services);},"Malformed type destination accepted its reached store.");
    check(delayed.faulted() && !malformed.created[1]->object_type && !malformed.created[1]->flag_28,
        "Faulting type store published later canonical fields.");
}
void physical_and_null_prefixes(){Fixture f;f.with_physical=true;f.method_ready=false;IgbMemoryFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.method_calls.size()==1,"Physical binding fixture did not reach pending method setter.");
    const auto p=f.allocations.back();const auto before=p.storage->bytes->bytes;f.method->physical_storage.reset();
    rejects([&]{call.resume(f.services);},"Pending method descriptor lost its physical allocation without failing.");
    check(call.faulted() && p.storage->bytes->bytes==before && !f.method->method_function,"Invalid method allocation received later stores.");
    Fixture missing;missing.inherited_copy->name=std::make_shared<const std::string>("absent");IgbMemoryFieldDefaultsInvocation absent;
    rejects([&]{absent.resume(missing.services);},"Missing inherited shallow-copy field produced a clone.");
    check(missing.created.size()==8 && missing.list_calls.empty() && missing.names==0,"Missing inherited method lost batch prefix or reached schema.");
}
void reset_registry_capture(){Fixture f;f.factory_ready=false;
    // Pause name search after8002B068 has captured the registry for53998.
    f.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field){f.inherited_reset->name.reset();f.inherited_reset->name_string.reset();}};
    IgbMemoryFieldDefaultsInvocation call;check(!call.resume(f.services).completed && f.created[3]->default_bytes->buffer->bytes[0]==1 && f.method_factories==0,
        "Reset name-search suspension was not reached after the first two true defaults.");
    const auto original=f.target->fields;auto replacement=metadata(0x960000)->fields;
    *replacement->backing=*original->backing;replacement->count=original->count;
    std::swap(replacement->backing->at(0),replacement->backing->at(1));f.target->fields=replacement;
    f.inherited_reset->name=std::make_shared<const std::string>("_resetMethod");
    check(!call.resume(f.services).completed && f.method_factories==1,"Resumed reset lookup did not enter its real factory.");
    f.factory_ready=true;check(call.resume(f.services).completed && replacement->backing->at(1)==f.method && replacement->backing->at(0)==f.inherited_reset,
        "Reset identity search used the new registry instead of the captured pre-search registry.");
}
void physical_vtable_fault_order(){
    for(const bool released:{false,true}){
        Fixture source;source.with_physical=true;
        const auto storage=physical(source.inherited_copy,0x980000);
        if(released)storage.storage->bytes->released=true;
        else storage.storage->bytes->initialized[storage.offset]=0;
        IgbMemoryFieldDefaultsInvocation clone;
        rejects([&]{clone.resume(source.services);},"Clone dispatch ignored its unavailable physical vtable word.");
        check(clone.faulted() && source.created.size()==8 && source.list_calls.empty() && !source.clone && source.names==0,
            "Invalid clone header lost the completed batch or invoked a later callback.");
        Fixture setter;setter.with_physical=true;
        setter.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field){
            const auto p=setter.allocations.back();
            if(released)p.storage->bytes->released=true;
            else p.storage->bytes->initialized[p.offset]=0;
        }};
        IgbMemoryFieldDefaultsInvocation scalar;
        rejects([&]{scalar.resume(setter.services);},"Scalar dispatch ignored its unavailable physical vtable word.");
        check(scalar.faulted() && setter.list_calls.size()==1 && setter.clone->default_bytes->buffer->bytes[0]==0 &&
            !*setter.services->shallow_copy && setter.created[0]->flag_26==1 && setter.names==0,
            "Invalid scalar header allowed the setter or subsequent stores before failing.");
    }
}
void adapters_and_recursion(){Fixture f;auto adapter=make_native_igb_memory_field_defaults_factory(f.services);IgbTypeRegistrationCall c;
    c.kind=IgbTypeRegistrationServiceKind::defaults;c.function=0x8002af40;c.metadata=metadata(0x970000);
    check(adapter(c)->resume().completed && c.metadata->fields->count==0 && f.target->fields->count==10,"Memory defaults adapter replaced the canonical receiver.");
    c.function=0x800273ec;check(!adapter(c),"Memory adapter consumed another defaults body.");
    Fixture recursive;IgbMemoryFieldDefaultsInvocation call;recursive.list_action=[&](const auto&){try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Caught recursive defaults failure did not invalidate outer work.");
    check(call.faulted() && !*recursive.services->shallow_copy && recursive.names==0,"Recursive defaults failure reached later stores.");
}
}
int main(){try{complete_defaults();retained_operations();getter_capture_and_fault_order();physical_and_null_prefixes();reset_registry_capture();physical_vtable_fault_order();adapters_and_recursion();
    std::cout<<"igb_memory_field_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Memory-field defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
