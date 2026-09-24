#include "dw4/igb_string_field_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <array>
#include <iostream>
#include <utility>

namespace {
using namespace dw4;using FK=IgbFieldOperationKind;using LK=IgbListDefaultsServiceKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& fn,const char* message){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
std::shared_ptr<IgbScratchBuffer> bytes(std::size_t size,std::uint8_t value=0){auto b=std::make_shared<IgbScratchBuffer>();
    b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t address){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(8);m->fields->backing_bytes=bytes(32);m->fields->backing_address=address;return m;}
std::shared_ptr<IgbDecodeRuntimeField> field(std::uint32_t table){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=table;f->size=1;
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(1),0};f->default_bytes_address=0x700000;
    f->name_string=std::shared_ptr<IgbInternedString>{};f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
    f->flag_26=0xab;f->flag_27=std::uint8_t{0xac};return f;}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& f,std::uint32_t address){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-12;storage->bytes=bytes(0x40,0xcc);
    storage->bindings.push_back({12,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{storage,12};
    IgbBootstrapPointer p{storage,12};igb_bootstrap_write_word(p,0,*f->vtable_word);igb_bootstrap_write_word(p,4,f->reference_word);
    igb_bootstrap_write_word(p,0x20,*f->default_bytes_address);return p;
}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& f){return {f->physical_storage->storage.lock(),f->physical_storage->offset};}
struct Fixture {
    std::shared_ptr<IgbStringFieldDefaultsServices> services=std::make_shared<IgbStringFieldDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(0x900000);
    std::array<std::shared_ptr<IgbDecodeRuntimeField>,2> inherited={field(0x80473f24),field(0x80473e30)},clones;
    std::shared_ptr<IgbDecodeRuntimeField> local;
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<IgbListDefaultsCall> list_calls;
    std::vector<IgbFieldOperationCall> value_calls;
    std::function<void(const IgbListDefaultsCall&)> list_action;
    std::function<void(const IgbFieldOperationCall&)> value_action;
    std::array<bool,2> clone_ready={true,true};
    std::array<unsigned,2> clone_polls{};
    bool scalar_ready=true,destroy_ready=true,with_physical{},omit_clone_address{},null_clone{};
    unsigned scalar_polls{},destroy_polls{},constructors{},names{};
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::string)]=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->lists=std::make_shared<IgbListDefaultsServices>();services->lists->schema=std::make_shared<IgbFieldSchemaServices>();
        auto& s=*services->lists->schema;s.types=std::make_shared<IgbTypeRegistrationServices>();s.types->metadata_pool_handle=5;
        s.pools=std::make_shared<IgbFilePoolsServices>();s.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        s.pools->memory_pools->selector=0;s.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        s.dependencies=std::make_shared<IgbDependenciesServices>();s.dependencies->globals=std::make_shared<SystemStageServices>();s.dependencies->globals->string_pool=pool;
        s.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++names;auto value=std::make_shared<IgbInternedString>();value->value=igb_dependency_name(c.name);value->container=pool;out->string_result=value;}
            else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared string endpoint.");return MessengerCallResult{true,0,{}};};return op;};
        s.fields=std::make_shared<IgbFieldOperationsServices>();s.fields->dependencies=s.dependencies;s.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        s.fields->pointer_lists->values.push_back({0x600000,target,{}});
        s.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool || c.kind==PK::object_pool)out->pool_result=pool;
            else{check(c.kind==PK::allocation_size,"Unexpected prepared list allocation.");out->size_result=32;}
            return MessengerCallResult{true,0,{}};};return op;};
        services->lists->values=std::make_shared<IgbFieldValuesServices>();auto& v=*services->lists->values;v.fields=s.fields;
        v.boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};v.signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        const auto native=make_native_igb_field_values_factory(services->lists->values,[this](const IgbFieldOperationCall& c){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==FK::allocation_pool || c.kind==FK::object_pool)out->pool_result=pool;
                else{check(c.kind==FK::allocation_size,"Unexpected prepared default allocation.");return MessengerCallResult{true,1,{}};}
                return MessengerCallResult{true,0,{}};};return op;});
        s.fields->factory=[this,native](const IgbFieldOperationCall& c){auto op=native(c);
            if(c.kind==FK::field_virtual && c.virtual_slot==0x8c && c.source){value_calls.push_back(c);const auto run=op->resume;
                op->resume=[this,c,run]{++scalar_polls;if(!scalar_ready)return MessengerCallResult{false,0,"pending native scalar default"};
                    if(value_action)value_action(c);return run();};}
            return op;};
        // Only constructor/clone bodies and allocation/string endpoints are
        // prepared; native batch, scalar values, schema and replacement execute.
        s.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field && c.function==0x800385e0 && c.pool==pool,
            "String defaults did not request the original Boolean constructor.");auto op=std::make_shared<IgbFieldSchemaOperation>();
            op->resume=[this,out=op.get()]{++constructors;local=field(0x80473e30);out->field_result=local;out->field_address=0x100000;
                if(with_physical)allocations.push_back(physical(local,0x100000));return MessengerCallResult{true,0,{}};};return op;};
        services->lists->factory=[this](const IgbListDefaultsCall& c){list_calls.push_back(c);auto op=std::make_shared<IgbListDefaultsOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==LK::clone_field){const unsigned i=c.field==inherited[0]?0u:1u;
                    check(c.field==inherited[i] && c.virtual_slot==0x54 && c.argument==1,"Inherited clone receiver or arguments differ.");
                    ++clone_polls[i];if(!clone_ready[i])return MessengerCallResult{false,0,"prepared inherited clone"};
                    if(!null_clone){clones[i]=std::make_shared<IgbDecodeRuntimeField>(*c.field);clones[i]->physical_storage.reset();clones[i]->reference_word=1;
                        clones[i]->vtable_word=c.vtable_word;clones[i]->default_bytes=IgbRuntimeFieldDefaultSource{bytes(1),0};out->field_result=clones[i];
                        const auto address=0x180000u+i*0x100u;if(!omit_clone_address)out->field_address=address;
                        if(with_physical)allocations.push_back(physical(clones[i],address));}
                }else{check(c.kind==LK::destroy_field && c.function==0x80066e1c,"Unexpected inherited descriptor destruction.");
                    ++destroy_polls;if(!destroy_ready)return MessengerCallResult{false,0,"prepared destructor"};}
                if(list_action)list_action(c);return MessengerCallResult{true,0,{}};};return op;};
        auto name=field(0x8047693c);name->offset=0xc;services->lists->name_descriptor=name;
        target->fields->count=2;
        for(unsigned i=0;i<2;++i){inherited[i]->name=std::make_shared<const std::string>(i?"_destruct":"_shallowCopyMethod");
            inherited[i]->name_string.reset();inherited[i]->reference_word=2;target->fields->backing->at(i)=inherited[i];}
    }
};
void complete_defaults(){Fixture f;f.with_physical=true;IgbStringFieldDefaultsInvocation call;
    check(call.resume(f.services).completed && f.constructors==1 && f.names==1 && f.target->fields->count==3,"String defaults did not finish its one-field schema.");
    for(unsigned i=0;i<2;++i){check(f.target->fields->backing->at(i)==f.clones[i] && f.clones[i]->reference_word==1 && f.inherited[i]->reference_word==1,
            "String inherited replacement lost its identities or original references.");
        check(f.clones[i]->default_bytes->buffer->bytes[0]==(i?1:2) && f.inherited[i]->default_bytes->buffer->bytes[0]==0,
            "Inherited default was not applied only to the clone.");
        check(igb_bootstrap_read_word(physical(f.clones[i]),0x1c)==(i?0x805615c0u:0x805615bcu) &&
            igb_bootstrap_read_word(physical(f.clones[i]),4)==1 && igb_buffer_word(f.target->fields->backing_bytes,i*4)==0x180000+i*0x100,
            "String replacement physical pointer/global/reference stores differ.");}
    check(*f.services->shallow_copy==f.clones[0] && *f.services->destruct==f.clones[1] && *f.services->globals->at(0)->slot==f.local,
        "String descriptors were not published to their three distinct canonical cells.");
    check(f.local->default_bytes->buffer->bytes[0]==1 && f.local->offset==0x38 && f.local->name && *f.local->name=="_refCounted" &&
        f.local->declaring_type->lock()==f.target && f.local->global_slot_address==0x805615c4 && f.local->flag_26==0xab && f.local->flag_27==0xac,
        "Local Boolean schema or unrelated field bytes differ.");
    const auto local_storage=physical(f.local);
    check(igb_bootstrap_read_word(local_storage,8)==0x38 && igb_bootstrap_read_word(local_storage,0x18)==0x600000 &&
        igb_bootstrap_read_word(local_storage,0x1c)==0x805615c4,"String local schema did not publish its physical descriptor words.");
    check(f.value_calls.size()==3 && f.value_calls[0].field==f.clones[0] && f.value_calls[1].field==f.clones[1] && f.value_calls[2].field==f.local,
        "Original scalar call order differs.");
    check(call.resume().completed && f.constructors==1 && f.list_calls.size()==2,"Completed string defaults replayed work.");
}
void retained_callbacks(){Fixture f;f.clone_ready[0]=false;IgbStringFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.constructors==1 && f.list_calls.size()==1 && f.names==0,"String clone boundary lost the completed batch.");
    f.inherited[0]->vtable_word=0;auto other=metadata(0x920000);
    *igb_field_type_metadata_slot(f.services->registration,IgbFieldTypeKind::string)=other;
    f.clone_ready[0]=true;f.scalar_ready=false;
    check(!call.resume(f.services).completed && f.clone_polls[0]==2 && f.value_calls.size()==1 && !*f.services->shallow_copy,
        "Pending clone re-read its selected vtable or skipped native scalar suspension.");
    check(!call.resume(f.services).completed && f.value_calls.size()==1 && f.scalar_polls==2,"Pending scalar operation was selected again.");
    f.scalar_ready=true;check(call.resume(f.services).completed && f.target->fields->count==3 && other->fields->count==0 && f.constructors==1,
        "Resumed defaults replaced the captured metadata receiver or replayed the batch.");
}
void fresh_index_registries(){for(unsigned method=0;method<2;++method){Fixture f;
    const auto hide=[&]{f.inherited[method]->name.reset();f.inherited[method]->name_string.reset();};
    if(method==0)hide();else f.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field && c.field==f.inherited[0])hide();};
    IgbStringFieldDefaultsInvocation call;check(!call.resume(f.services).completed && f.constructors==1 && f.list_calls.size()==method,
        "String inherited name lookup did not pause at the requested method.");
    const auto old=f.target->fields;const auto replacement=metadata(0x930000)->fields;
    *replacement->backing=*old->backing;replacement->count=old->count;std::swap(replacement->backing->at(0),replacement->backing->at(1));
    f.target->fields=replacement;f.inherited[method]->name=std::make_shared<const std::string>(method?"_destruct":"_shallowCopyMethod");
    check(call.resume(f.services).completed && replacement->backing->at(0)==f.clones[1] && replacement->backing->at(1)==f.clones[0],
        "String identity index used a registry captured before the name search completed.");
    check(old->backing->at(method)==f.inherited[method],"Replacement wrote the pre-search registry instead of reloading the live one.");
}}
void explicit_boundaries_and_binding(){Fixture missing;const auto factory=missing.services->lists->factory;missing.services->lists->factory={};IgbStringFieldDefaultsInvocation call;
    check(!call.resume(missing.services).completed && missing.constructors==1 && missing.names==0 && !*missing.services->shallow_copy,
        "Missing clone body was treated as a successful prepared descriptor.");
    missing.services->lists->factory=factory;check(call.resume(missing.services).completed && missing.constructors==1,"Late clone implementation did not resume the retained caller.");
    Fixture address;address.omit_clone_address=true;IgbStringFieldDefaultsInvocation mapping;
    check(!mapping.resume(address.services).completed && address.clones[0]->reference_word==2 && *address.services->shallow_copy==address.clones[0] &&
        address.target->fields->backing->at(0)==address.inherited[0],"Missing clone address skipped original finalization/reference prefix.");
    address.services->lists->schema->fields->pointer_lists->values.push_back({0x180000,{},address.clones[0]});address.omit_clone_address=false;
    check(mapping.resume(address.services).completed && address.clones[0]->reference_word==1 && address.inherited[0]->reference_word==1,
        "Late physical pointer binding repeated replacement reference effects.");
}
void physical_header_faults(){for(const bool clone:{false,true})for(const bool released:{false,true}){Fixture f;f.with_physical=true;
    const auto damage=[&](const std::shared_ptr<IgbDecodeRuntimeField>& descriptor){const auto p=physical(descriptor);
        if(released)p.storage->bytes->released=true;else p.storage->bytes->initialized[p.offset]=0;};
    if(clone){f.allocations.push_back(physical(f.inherited[0],0x200000));damage(f.inherited[0]);}
    else f.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field)damage(f.clones[0]);};
    IgbStringFieldDefaultsInvocation call;rejects([&]{call.resume(f.services);},"Unavailable physical vtable did not fail at original call selection.");
    check(call.faulted() && f.constructors==1 && f.list_calls.size()==(clone?0u:1u) && f.value_calls.empty() && f.names==0 && !*f.services->shallow_copy,
        "Physical vtable failure reached a later callback or descriptor publication.");
}}
void physical_rebinding_and_null(){Fixture f;f.with_physical=true;f.scalar_ready=false;IgbStringFieldDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.clones[0] && f.value_calls.size()==1,"Pending allocation test did not reach scalar service.");
    const auto pointer=physical(f.clones[0]);const auto before=pointer.storage->bytes->bytes;f.clones[0]->physical_storage.reset();
    rejects([&]{call.resume(f.services);},"Pending cloned descriptor lost its physical identity without failing.");
    check(pointer.storage->bytes->bytes==before && !*f.services->shallow_copy,"Rebound clone received additional physical/global stores.");
    Fixture null;null.null_clone=true;IgbStringFieldDefaultsInvocation empty;
    rejects([&]{empty.resume(null.services);},"Null clone result bypassed the original immediate scalar dereference.");
    check(null.constructors==1 && null.value_calls.empty() && null.names==0,"Null clone executed unreached callbacks.");
}
void packed_creator_release(){Fixture f;f.destroy_ready=false;
    f.list_action=[&](const IgbListDefaultsCall& c){if(c.kind==LK::clone_field && c.field==f.inherited[0])f.clones[0]->reference_word=0;};
    IgbStringFieldDefaultsInvocation call;check(!call.resume(f.services).completed && f.clones[0]->reference_word==0 && f.destroy_polls==1 &&
        f.target->fields->backing->at(0)==f.clones[0] && f.value_calls.size()==1,"Low23-zero creator release did not follow actual replacement publication.");
    check(!call.resume(f.services).completed && f.clones[0]->reference_word==0 && f.destroy_polls==2,"Pending destructor repeated the creator decrement.");
    f.destroy_ready=true;check(call.resume(f.services).completed && f.destroy_polls==3 && f.clones[0]->reference_word==0,"Creator destructor continuation lost the original reference prefix.");
}
void adapters_and_recursion(){Fixture f;auto adapter=make_native_igb_string_field_defaults_factory(f.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x8002549c;request.metadata=metadata(0x940000);
    check(adapter(request)->resume().completed && request.metadata->fields->count==0,"String defaults adapter replaced the canonical receiver.");
    request.function=0x8002af40;check(!adapter(request),"String defaults adapter consumed the memory caller.");
    Fixture recursive;IgbStringFieldDefaultsInvocation call;recursive.list_action=[&](const auto&){try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Caught recursive failure did not invalidate string defaults.");
    check(call.faulted() && !*recursive.services->shallow_copy && recursive.value_calls.empty(),"Recursive clone callback reached unrecovered stores.");
}
}
int main(){try{complete_defaults();retained_callbacks();fresh_index_registries();explicit_boundaries_and_binding();physical_header_faults();
    physical_rebinding_and_null();packed_creator_release();adapters_and_recursion();
    std::cout<<"igb_string_field_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"String-field defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
