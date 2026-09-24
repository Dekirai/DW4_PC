#include "dw4/igb_struct_field_defaults.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;using FK=IgbFieldOperationKind;using LK=IgbListDefaultsServiceKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(unsigned count,std::uint8_t fill=0){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(count,fill);out->initialized.assign(count,1);return out;}
auto metadata(std::uint32_t backing){auto out=std::make_shared<IgbRuntimeMetaType>();out->fields=std::make_shared<IgbDecodeFieldRegistry>();
    out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);out->fields->backing_bytes=bytes(64);out->fields->backing_address=backing;return out;}
auto field(std::uint32_t table,unsigned size=4){auto out=std::make_shared<IgbDecodeRuntimeField>();out->vtable_word=table;out->size=static_cast<std::uint16_t>(size);
    out->flag_26=1;out->default_bytes=IgbRuntimeFieldDefaultSource{bytes(8,0xcc),0};out->default_bytes_address=0x800000;
    out->name_string=std::shared_ptr<IgbInternedString>{};out->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};out->global_slot_address=0;return out;}
IgbBootstrapPointer physical(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t address){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address-8;storage->bytes=bytes(8+0x40,0xcc);storage->bindings.push_back({8,{},{},{},{},field});
    field->physical_storage=IgbFieldStorageBinding{storage,8};IgbBootstrapPointer out{storage,8};
    igb_bootstrap_write_word(out,0,*field->vtable_word);igb_bootstrap_write_word(out,4,field->reference_word);
    igb_bootstrap_write_halfword(out,0x14,field->size);igb_bootstrap_write_word(out,0x20,*field->default_bytes_address);igb_bootstrap_write_byte(out,0x26,field->flag_26);return out;}
struct Fixture {
    std::shared_ptr<IgbStructFieldDefaultsServices> services=std::make_shared<IgbStructFieldDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(0x900000),base=metadata(0x910000);
    std::shared_ptr<IgbDecodeRuntimeField> inherited=field(0x80477054,2),other=field(0x80475b60),clone;
    std::shared_ptr<IgbRuntimeFieldSlot> base_size=std::make_shared<IgbRuntimeFieldSlot>(inherited);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;std::vector<IgbBootstrapPointer> allocations;
    IgbBootstrapPointer inherited_storage,clone_storage;
    std::vector<std::uint32_t> factories;std::vector<IgbListDefaultsCall> list_calls;std::vector<IgbFieldOperationCall> setters;
    std::function<void(const IgbListDefaultsCall&)> list_action;
    std::function<void(const IgbFieldOperationCall&)> scalar_action;
    std::function<void()> constructor_action;
    bool batch_ready=true,clone_ready=true,scalar_ready=true,destroy_ready=true,clone_null{};
    unsigned factory_polls{},clone_polls{},scalar_polls{},destroy_polls{},names{};
    std::uint32_t clone_references=1;
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::structure)]=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->lists=std::make_shared<IgbListDefaultsServices>();services->lists->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->lists->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*schema.pools->memory_pools->fallback)->original_pool_access_bound=true;(*schema.pools->memory_pools->fallback)->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& call){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,call,out=op.get()]{
            if(call.kind==IgbDependenciesServiceKind::intern_name){++names;auto name=std::make_shared<IgbInternedString>();name->value=igb_dependency_name(call.name);name->container=pool;out->string_result=name;}
            else check(call.kind==IgbDependenciesServiceKind::release_string,"Unexpected struct schema string endpoint.");return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();schema.fields->pointer_lists->values.push_back({0x600008,target,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& call){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,call,out=op.get()]{
            if(call.kind==PK::allocation_pool || call.kind==PK::object_pool)out->pool_result=pool;
            else{check(call.kind==PK::allocation_size,"Prepared struct descriptor registry unexpectedly grew.");out->size_result=64;}return MessengerCallResult{true,0,{}};};return op;};
        services->lists->values=std::make_shared<IgbFieldValuesServices>();auto& values=*services->lists->values;values.fields=schema.fields;
        values.signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values.pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values.unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        const auto native=make_native_igb_field_values_factory(services->lists->values,[this](const IgbFieldOperationCall& call){auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,call,out=op.get()]{if(call.kind==FK::allocation_pool || call.kind==FK::object_pool)out->pool_result=pool;
                else{check(call.kind==FK::allocation_size,"Unexpected struct scalar allocation endpoint.");return MessengerCallResult{true,8,{}};}return MessengerCallResult{true,0,{}};};return op;});
        schema.fields->factory=[this,native](const IgbFieldOperationCall& call){const auto selected=native(call);if(!call.source)return selected;
            setters.push_back(call);auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,call,selected,done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++scalar_polls;if(!scalar_ready)return MessengerCallResult{false,0x72,"prepared struct scalar"};
                check(selected && bool(selected->resume),"Struct setter did not select an actual scalar value implementation.");const auto status=selected->resume();
                if(status.completed){done=true;if(scalar_action)scalar_action(call);}return status;};return op;};
        // Descriptor construction and the unsigned-short+54 clone are prepared
        // endpoints. Actual batch, values,6588C replacement and schema execute.
        // This does not establish cold unsigned-short registration or cloning.
        schema.factory=[this](const IgbFieldSchemaCall& call){check(call.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected struct batch destructor.");factories.push_back(call.function);
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,call,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++factory_polls;if(!batch_ready)return MessengerCallResult{false,0x70,"prepared struct field factory"};
                check(call.pool==pool && (call.function==0x8002e1ac || call.function==0x800274ec),"Struct local field factory or pool differs.");
                auto local=field(call.function==0x8002e1ac?0x80475b60:0x80476630);local->default_bytes_address=0x810000+static_cast<std::uint32_t>(created.size())*0x100;
                const auto storage=physical(local,0x100008+static_cast<std::uint32_t>(created.size())*0x100);created.push_back(local);allocations.push_back(storage);
                out->field_result=local;out->field_address=igb_bootstrap_address(storage);done=true;if(constructor_action)constructor_action();return MessengerCallResult{true,0,{}};};return op;};
        services->lists->factory=[this](const IgbListDefaultsCall& call){list_calls.push_back(call);auto op=std::make_shared<IgbListDefaultsOperation>();
            op->resume=[this,call,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(call.kind==LK::clone_field){++clone_polls;if(!clone_ready)return MessengerCallResult{false,0x71,"prepared inherited-size clone"};
                    check(call.field==inherited && call.vtable_word==0x80477054 && call.virtual_slot==0x54 && call.argument==1,"Struct inherited clone changed its original selection.");
                    if(!clone_null){clone=field(0x80477054,2);clone->reference_word=clone_references;clone->name=inherited->name;clone->name_string.reset();clone->offset=20;
                        clone->default_bytes_address=0x820000;clone_storage=physical(clone,0x300008);out->field_result=clone;out->field_address=0x300008;}
                }else{check(call.kind==LK::destroy_field && call.function==0x80066e1c,"Unexpected struct defaults list operation.");++destroy_polls;
                    if(!destroy_ready)return MessengerCallResult{false,0x73,"prepared struct descriptor destruction"};}
                done=true;if(list_action)list_action(call);return MessengerCallResult{true,0,{}};};return op;};
        auto descriptor=field(0x8047693c);descriptor->offset=12;services->lists->name_descriptor=descriptor;
        other->name=std::make_shared<const std::string>("_offset");other->name_string.reset();
        inherited->name=std::make_shared<const std::string>("_size");inherited->name_string.reset();inherited->offset=20;inherited->reference_word=2;
        inherited->global_slot=std::weak_ptr(base_size);inherited->global_slot_address=0x805617d0;inherited_storage=physical(inherited,0x200008);
        for(const auto& owner:{target,base}){owner->fields->count=2;owner->fields->backing->at(0)=other;owner->fields->backing->at(1)=inherited;}
    }
    auto cell(){return igb_field_type_metadata_slot(services->registration,IgbFieldTypeKind::structure);}
};
void complete_specialization(){
    Fixture f;constexpr std::array<std::uint32_t,3> factories={0x8002e1ac,0x800274ec,0x800274ec},offsets={0x34,0x38,0x3c};
    constexpr std::array names={"_alignment","_cppConstructor","_cppDestructor"};
    for(unsigned i=0;i<3;++i)check(f.services->factories->at(i)==factories[i] && **f.services->names->at(i)==names[i] &&
        f.services->globals->at(i)->address==0x8056156c+4*i && f.services->offsets->at(i)==offsets[i],"Struct defaults literal tables differ.");
    IgbStructFieldDefaultsInvocation run;check(run.resume(f.services).completed && f.created.size()==3 && f.target->fields->count==5 && f.names==3 && f.setters.size()==2,
        "Struct defaults did not execute its inherited specialization and three local fields.");
    check(f.target->fields->backing->at(1)==f.clone && f.base->fields->backing->at(1)==f.inherited && *f.base_size==f.inherited &&
        *f.services->size_descriptor==f.clone,"Struct defaults modified the shared base _size or missed the specialized global.");
    check(f.inherited->reference_word==1 && f.clone->reference_word==1 && igb_bootstrap_read_word(f.inherited_storage,4)==1 &&
        igb_bootstrap_read_word(f.clone_storage,4)==1 && igb_buffer_word(f.target->fields->backing_bytes,4)==0x300008,
        "Struct replacement lost packed reference or physical registry publication.");
    check(f.clone->flag_26==0 && igb_bootstrap_read_byte(f.clone_storage,0x26)==0 && f.clone->global_slot_address==0x80561568 &&
        igb_bootstrap_read_word(f.clone_storage,0x1c)==0x80561568 && f.clone->default_bytes->buffer->bytes[0]==0 &&
        f.clone->default_bytes->buffer->bytes[1]==1 && f.clone->default_bytes->buffer->bytes[2]==0xcc && f.inherited->default_bytes->buffer->bytes[1]==0xcc,
        "Inherited size specialization changed the source descriptor or widened its halfword default.");
    check(f.setters[0].field==f.clone && f.setters[0].source->buffer->bytes==std::vector<std::uint8_t>{0,1} && !f.setters[0].source_address &&
        f.setters[1].field==f.created[0] && f.setters[1].source->buffer->bytes==std::vector<std::uint8_t>{0,0,0,1} && !f.setters[1].source_address,
        "Struct scalar sources lost original75E8C/53650 widths or order.");
    for(unsigned i=0;i<3;++i)check(f.created[i]->declaring_type->lock()==f.target && *f.created[i]->name==names[i] && f.created[i]->offset==offsets[i] &&
        *f.services->globals->at(i)->slot==f.created[i] && igb_bootstrap_read_word(f.allocations[i],0x18)==0x600008 &&
        igb_bootstrap_read_word(f.allocations[i],0x1c)==0x8056156c+4*i,"Struct local schema did not retain its declaring metadata or physical globals.");
    check(f.created[0]->flag_26==0 && igb_bootstrap_read_byte(f.allocations[0],0x26)==0 && f.created[1]->flag_26==1 && f.created[2]->flag_26==1 &&
        igb_buffer_word(f.created[0]->default_bytes->buffer,0)==1 && !f.target->factory_redirect && !f.target->word_40,"Struct local defaults changed unrelated persistent flags or metadata callbacks.");
    check(run.resume().completed && f.clone_polls==1 && f.names==3,"Completed struct defaults replayed original operations.");
}
void pending_batch_clone_and_setter(){
    Fixture f;f.batch_ready=false;IgbStructFieldDefaultsInvocation run;
    check(!run.resume(f.services).completed && f.factories==std::vector<std::uint32_t>{0x8002e1ac} && f.list_calls.empty(),"Struct defaults searched or cloned before batch completion.");
    auto replacement=metadata(0x920000);*f.cell()=replacement;f.batch_ready=true;f.clone_ready=false;
    const auto pending=run.resume(f.services);check(!pending.completed && pending.value==0x71 && f.created.size()==3 && f.target->fields->count==5 &&
        replacement->fields->count==0 && f.setters.empty() && f.names==0,"Pending struct clone lost its captured metadata or allowed later default work.");
    const auto selected=f.list_calls.size();f.clone_ready=true;f.scalar_ready=false;const auto scalar=run.resume(f.services);
    check(!scalar.completed && scalar.value==0x72 && f.clone_polls==2 && f.list_calls.size()==selected && f.setters.size()==1 && f.clone->flag_26==1 &&
        f.target->fields->backing->at(1)==f.inherited && !*f.services->size_descriptor,"Pending size setter published flags, global or replacement early.");
    f.scalar_ready=true;check(run.resume(f.services).completed && f.scalar_polls==3 && f.clone_polls==2 && replacement->fields->count==0,
        "Struct defaults replayed pending selections or switched canonical receivers.");
}
void fresh_registry_index_and_alignment(){
    Fixture f;f.inherited->name.reset();IgbStructFieldDefaultsInvocation run;
    check(!run.resume(f.services).completed && f.created.size()==3 && f.list_calls.empty(),"Unknown inherited name did not pause after all locals were appended.");
    auto original=f.target->fields;auto replacement=metadata(0x920000)->fields;replacement->count=original->count;*replacement->backing=*original->backing;
    std::swap(replacement->backing->at(0),replacement->backing->at(1));f.target->fields=replacement;f.inherited->name=std::make_shared<const std::string>("_size");
    check(run.resume(f.services).completed && original->backing->at(1)==f.inherited && replacement->backing->at(0)==f.clone && replacement->backing->at(1)==f.other,
        "Struct53998 reused the name-search registry instead of its fresh metadata registry.");
    Fixture fixed;fixed.clone_ready=false;IgbStructFieldDefaultsInvocation captured;
    check(!captured.resume(fixed.services).completed && fixed.list_calls.size()==1,"Captured index fixture did not pause at clone.");
    std::swap(fixed.target->fields->backing->at(0),fixed.target->fields->backing->at(1));fixed.clone_ready=true;
    check(captured.resume(fixed.services).completed && fixed.target->fields->backing->at(1)==fixed.clone && fixed.target->fields->backing->at(0)==fixed.inherited,
        "Pending clone recomputed the previously captured replacement index.");
    Fixture local;local.clone_references=0xa5800000;local.destroy_ready=false;IgbStructFieldDefaultsInvocation alignment;
    check(!alignment.resume(local.services).completed && local.clone->reference_word==0xa5800000 && local.target->fields->backing->at(1)==local.clone &&
        local.created[0]->flag_26==1 && local.setters.size()==1,"Temporary clone release skipped its original low23 destructor gate or ran alignment early.");
    std::swap(local.target->fields->backing->at(2),local.target->fields->backing->at(3));local.destroy_ready=true;local.services->lists->factory={};
    check(alignment.resume(local.services).completed && local.destroy_polls==2 && local.clone->reference_word==0xa5800000 && local.setters[1].field==local.created[1] &&
        local.created[1]->flag_26==0 && local.created[0]->flag_26==1,"Alignment reused an old local descriptor or repeated temporary release after pending destruction.");
    Fixture destroyed;destroyed.clone_references=0;destroyed.list_action=[&](const IgbListDefaultsCall& call){if(call.kind==LK::destroy_field && call.field==destroyed.clone){
        destroyed.clone_storage.storage->bytes->released=true;destroyed.clone->physical_storage.reset();}};
    IgbStructFieldDefaultsInvocation released;check(released.resume(destroyed.services).completed && destroyed.destroy_polls==1 && destroyed.names==3 &&
        destroyed.clone_storage.storage->bytes->released && destroyed.created[0]->flag_26==0,"Struct caller dereferenced its destroyed temporary after66E1C returned.");
}
void physical_failure_prefixes(){
    Fixture short_flag;short_flag.scalar_action=[&](const IgbFieldOperationCall& call){if(call.field==short_flag.clone)short_flag.clone_storage.storage->bytes->bytes.resize(8+0x26);};
    IgbStructFieldDefaultsInvocation flag;rejects([&]{flag.resume(short_flag.services);},"Short clone accepted the persistent-byte store.");
    check(flag.faulted() && short_flag.clone->flag_26==1 && short_flag.clone->default_bytes->buffer->bytes[1]==1 &&
        short_flag.clone->global_slot_address==0 && short_flag.target->fields->backing->at(1)==short_flag.inherited && short_flag.names==0,
        "Failed persistent-byte store lost the completed scalar prefix or committed the global/replacement.");
    Fixture binding;binding.scalar_ready=false;IgbStructFieldDefaultsInvocation retained;
    check(!retained.resume(binding.services).completed && binding.setters.size()==1,"Physical identity fixture did not pause at scalar callback.");
    const auto prefix=binding.clone_storage.storage->bytes->bytes;binding.clone->physical_storage.reset();binding.scalar_ready=true;
    rejects([&]{retained.resume(binding.services);},"Pending struct scalar accepted a removed physical binding.");
    check(retained.faulted() && binding.scalar_polls==1 && binding.clone_storage.storage->bytes->bytes==prefix && binding.clone->flag_26==1,
        "Changed clone identity executed the pending setter or later stores.");
    Fixture replacement;replacement.inherited->reference_word=1;igb_bootstrap_write_word(replacement.inherited_storage,4,1);replacement.destroy_ready=false;
    IgbStructFieldDefaultsInvocation store;check(!store.resume(replacement.services).completed && replacement.clone->reference_word==2 &&
        replacement.inherited->reference_word==0 && replacement.target->fields->backing->at(1)==replacement.inherited && *replacement.services->size_descriptor==replacement.clone,
        "Replacement destructor pause lost finalization, retain/release or registry-store ordering.");
    replacement.target->fields->backing_bytes->bytes.resize(7);replacement.destroy_ready=true;
    rejects([&]{store.resume(replacement.services);},"Truncated replacement backing accepted its four-byte pointer store.");
    check(store.faulted() && replacement.clone->reference_word==2 && replacement.target->fields->backing->at(1)==replacement.inherited &&
        replacement.setters.size()==1 && replacement.created[0]->flag_26==1,"Replacement storage fault ran temporary release or alignment defaults.");
}
void malformed_inputs_and_adapters(){
    Fixture missing;missing.inherited->name=std::make_shared<const std::string>("absent");IgbStructFieldDefaultsInvocation find;
    rejects([&]{find.resume(missing.services);},"Missing inherited_size produced a prepared clone.");
    check(find.faulted() && missing.created.size()==3 && missing.list_calls.empty() && missing.names==0,"Failed inherited search skipped the local batch or reached schema.");
    Fixture null;null.clone_null=true;IgbStructFieldDefaultsInvocation no_clone;rejects([&]{no_clone.resume(null.services);},"Null returned clone was accepted by the size setter.");
    check(no_clone.faulted() && null.created.size()==3 && null.setters.empty() && null.names==0,"Null clone reached later specialization or schema.");
    Fixture unknown;unknown.cell()->reset();IgbStructFieldDefaultsInvocation absent;
    check(!absent.resume(unknown.services).completed && unknown.factories.empty(),"Unknown struct class became a prepared receiver.");
    *unknown.cell()=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{absent.resume(unknown.services);},"Known-null struct metadata bypassed the original count access.");
    Fixture recursive;IgbStructFieldDefaultsInvocation run;recursive.list_action=[&](const IgbListDefaultsCall& call){if(call.kind==LK::clone_field)
        rejects([&]{run.resume(recursive.services);},"Recursive struct defaults invocation was accepted.");};
    rejects([&]{run.resume(recursive.services);},"Swallowed recursion did not invalidate the outer struct defaults.");
    check(run.faulted() && recursive.setters.empty() && recursive.names==0 && !*recursive.services->size_descriptor,"Recursive clone failure reached later scalar or global publication.");
    Fixture adapted;unsigned delegated{};auto adapter=make_native_igb_struct_field_defaults_factory(adapted.services,[&](const IgbTypeRegistrationCall&){++delegated;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::defaults;call.function=0x800246c8;call.metadata=metadata(0x940000);
    auto selected=adapter(call);check(selected && selected->resume().completed && call.metadata->fields->count==0 && adapted.target->fields->count==5,
        "Struct defaults adapter replaced its canonical class with generic callback metadata.");
    call.function=0x8002af40;check(!adapter(call) && delegated==1,"Struct adapter consumed memory-field defaults.");call.function=0x800246c8;call.kind=IgbTypeRegistrationServiceKind::get_parent;
    check(!adapter(call) && delegated==2,"Struct defaults adapter consumed another operation kind.");
    auto owner=std::make_shared<IgbStructFieldDefaultsServices>();auto expired=make_native_igb_struct_field_defaults_factory(owner);owner.reset();call.kind=IgbTypeRegistrationServiceKind::defaults;
    rejects([&]{expired(call);},"Expired struct defaults adapter fabricated services.");
}
}
int main(){try{complete_specialization();pending_batch_clone_and_setter();fresh_registry_index_and_alignment();physical_failure_prefixes();malformed_inputs_and_adapters();
    std::cout<<"igb_struct_field_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Struct field defaults failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
