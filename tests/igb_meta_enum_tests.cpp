#include "dw4/igb_meta_enum.hpp"

#include <iostream>

namespace {
using namespace dw4;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned size,std::uint8_t value=0xcc){auto result=std::make_shared<IgbScratchBuffer>();
    result->bytes.assign(size,value);result->initialized.assign(size,1);return result;}
IgbBootstrapPointer storage(std::uint32_t address,unsigned size,unsigned offset=0){
    auto result=std::make_shared<IgbObjectStorage>();result->original_address=address;result->bytes=bytes(size);return {result,offset};}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;
    result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);
    result->fields->backing_bytes=bytes(64,0);result->fields->backing_address=0x900000;return result;}
struct Fixture {
    std::shared_ptr<IgbMetaEnumServices> services=std::make_shared<IgbMetaEnumServices>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(4);
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbTypeRegistrationFactory remaining;IgbRegistrationCallbacksFactory remaining_function;
    std::vector<IgbTypeRegistrationCall> calls;std::vector<IgbRegistrationCallbacksCall> functions;
    Fixture(){
        services->metadata=std::make_shared<IgbRegistrationMetaSlot>(meta);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->vtable_offset=0;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->started=1;services->callbacks->prerequisites->initialized=1;
        services->callbacks->factory=make_native_igb_meta_enum_function_factory(services,[this](const IgbRegistrationCallbacksCall& c){
            functions.push_back(c);return remaining_function?remaining_function(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};});
        services->types->factory=make_native_igb_meta_enum_type_factory(services,[this](const IgbTypeRegistrationCall& c){
            calls.push_back(c);if(c.kind!=TK::pool_for_handle)return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[this,out=op.get()]{out->object_result=pool;return MessengerCallResult{true,0,{}};};return op;});
        services->local_storage=[]{return storage(0x10000,0x24,8);};
    }
};
struct DefaultsFixture:Fixture {
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> names_type=metadata(),values_type=metadata();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::uint32_t> factories;std::vector<IgbMetaEnumCall> getters;
    unsigned field_polls{},getter_polls{},interns{};bool fields_ready=true,getters_ready=true,addresses=true;
    std::function<void(const IgbMetaEnumCall&,IgbMetaEnumOperation&)> action;
    DefaultsFixture(){
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;schema.types=services->types;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();
        schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==IgbDependenciesServiceKind::intern_name){++interns;auto header=std::make_shared<IgbInternedString>();
                header->value=igb_dependency_name(c.name);header->container=pool;header->references=1;out->string_result=header;
            }else if(c.kind!=IgbDependenciesServiceKind::release_string)throw std::runtime_error("Unexpected enum string endpoint");
                return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else if(c.kind==IgbPointerListServiceKind::allocation_size)out->size_result=64;
                else throw std::runtime_error("Prepared enum field list unexpectedly grew");return MessengerCallResult{true,0,{}};};return op;};
        schema.fields->factory=[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbFieldOperationKind::allocation_pool){out->pool_result=pool;return MessengerCallResult{true,0,{}};}
                if(c.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,4,{}};
                check(c.kind==IgbFieldOperationKind::field_virtual,"Unexpected enum field operation");
                if(c.virtual_slot==0x64)return MessengerCallResult{true,4,{}};
                check(c.virtual_slot==0x5c || c.virtual_slot==0x8c,"Unexpected enum field virtual");return MessengerCallResult{true,0,{}};
            };return op;};
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected enum field destruction");
            factories.push_back(c.function);auto op=std::make_shared<IgbFieldSchemaOperation>();
            op->resume=[this,c,out=op.get()]{++field_polls;if(!fields_ready)return MessengerCallResult{false,0,"prepared enum field factory"};
                check(c.pool==pool,"Enum field factory changed the captured pool");auto f=std::make_shared<IgbDecodeRuntimeField>();
                f->vtable_word=0x81000000u+static_cast<std::uint32_t>(created.size())*0x100;f->flag_26=1;f->flag_34=7;f->flag_35=8;f->flag_3c=9;
                f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;f->name_string=std::shared_ptr<IgbInternedString>{};f->default_mode=0;
                f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0),0};
                auto allocation=storage(0x100000u+static_cast<std::uint32_t>(created.size())*0x100,0x44);
                allocation.storage->bindings.push_back({0,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{allocation.storage,0};
                f->default_bytes_address=igb_bootstrap_address(allocation)+0x80;
                created.push_back(f);allocations.push_back(allocation);out->field_result=f;out->field_address=igb_bootstrap_address(allocation);
                return MessengerCallResult{true,0,{}};};return op;};
        services->factory=[this](const IgbMetaEnumCall& c){getters.push_back(c);auto op=std::make_shared<IgbMetaEnumOperation>();
            op->resume=[this,c,out=op.get()]{++getter_polls;if(!getters_ready)return MessengerCallResult{false,0,"prepared enum object-type getter"};
                check(c.function==0x80024b94 || c.function==0x800365b4,"Enum defaults changed original getter identity");
                out->metadata_result=c.function==0x80024b94?names_type:values_type;
                if(addresses)out->metadata_address=c.function==0x80024b94?0x700000:0x710000;
                if(action)action(c,*out);return MessengerCallResult{true,0,{}};};return op;};
        meta->fields->count=2;for(unsigned i=0;i<2;++i)meta->fields->backing->at(i)=std::make_shared<IgbDecodeRuntimeField>();
    }
};
void arguments_and_registration(){
    Fixture f;const auto args=igb_meta_enum_registration_arguments(f.services->metadata);
    check(args.metadata_slot==f.services->metadata && *args.name=="igMetaEnum" && args.flag==0 && args.native_size==0x14,"Enum registration name/slot/size differs.");
    check(args.parent_registration==0x80066b08 && args.parent_getter==0x800237d0 && args.field_callback==0x8002ab74 &&
        args.layout_probe==0x8002a9d4 && args.defaults_callback==0x8002ab94 && args.trailing_callback==0,"Enum registration callback arguments differ.");
    check(*args.extra_callbacks==std::vector<std::uint32_t>({0x80024c64,0x80036684,0}),"Enum extra callbacks or terminator differ.");
    rejects([]{igb_meta_enum_registration_arguments({});},"Enum registration accepted missing canonical storage.");
    f.services->callbacks->prerequisites->initialized.reset();IgbMetaEnumRegistrationInvocation paused;
    check(!paused.resume(f.services).completed && f.calls.empty(),"Enum registration bypassed unknown prerequisite state.");
    f.services->callbacks->prerequisites->initialized=1;
    check(paused.resume(f.services).completed && !paused.result() && f.calls.size()==1 && f.calls[0].kind==TK::pool_for_handle &&
        f.calls[0].handle==5 && f.meta->class_flags==4,"Ready enum bypassed pool lookup or repeated prerequisite work.");
    check(paused.resume().completed && f.calls.size()==1,"Completed enum registration replayed.");
    Fixture cold;*cold.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetaEnumRegistrationInvocation missing;
    check(!missing.resume(cold.services).completed && cold.calls.size()==2 && cold.calls.back().kind==TK::create_metadata &&
        *cold.calls.back().allocation_label=="igMetaObject: igMetaEnum","Cold enum did not reach the actual metadata constructor.");
    check(cold.services->metadata->has_value() && !**cold.services->metadata && !missing.completed() && !missing.faulted(),
        "Missing metadata construction was reported as completed registration.");
}
void lookups_and_adapters(){
    Fixture f;f.services->metadata->reset();IgbMetaEnumLookupInvocation unknown;
    check(!unknown.resume(f.services).completed && f.calls.empty(),"Unknown enum cell became null or registered.");
    *f.services->metadata=f.meta;check(unknown.resume(f.services).completed && unknown.result()==f.meta,"Ready enum lookup did not use canonical metadata.");
    *f.services->metadata=metadata();check(unknown.resume().completed && unknown.result()==f.meta,"Completed lookup reread the global.");
    for(const auto flags:{0u,1u,2u,8u}){
        Fixture called;called.meta->class_flags=flags;auto replacement=metadata(4);bool ready{};unsigned selections{},polls{};
        called.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;
            check(c.kind==CK::invoke_function && c.function==0x8002aafc,"Enum getter selected another registration body");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(!ready)return MessengerCallResult{false,0,"prepared registration"};
                *called.services->metadata=replacement;return MessengerCallResult{true,0,{}};};return op;};
        IgbMetaEnumLookupInvocation call;check(!call.resume(called.services).completed && polls==1,"Non-ready enum skipped original registration.");
        called.meta->class_flags=4;ready=true;check(call.resume(called.services).completed && call.result()==replacement && selections==1 && polls==2,
            "Pending enum getter canceled registration or failed to reload its canonical slot.");
    }
    Fixture aliases;for(const auto address:{0x8002a998u,0x8002ab74u}){
        IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=address;const auto op=aliases.services->types->factory(c);
        check(op && op->resume().completed && op->metadata_result==aliases.meta,"Enum getter adapter changed result type.");
    }
    IgbRegistrationCallbacksCall wrapper;wrapper.kind=CK::invoke_function;wrapper.function=0x8002aad4;
    check(aliases.services->callbacks->factory(wrapper)->resume().completed,"Enum wrapper did not run actual66188 body.");
    unsigned delegated{};auto adapter=make_native_igb_meta_enum_type_factory(aliases.services,[&](const IgbTypeRegistrationCall&){++delegated;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall unrelated;unrelated.kind=TK::layout_probe;unrelated.function=0x8002a998;
    check(!adapter(unrelated) && delegated==1,"Enum getter was incorrectly treated as a constructor/probe.");
    auto expired=std::make_shared<IgbMetaEnumServices>();auto dead=make_native_igb_meta_enum_type_factory(expired);std::weak_ptr<IgbMetaEnumServices> weak=expired;
    expired.reset();check(weak.expired(),"Enum adapter created a permanent services cycle.");unrelated.function=0x8002a9d4;
    rejects([&]{dead(unrelated);},"Expired enum services were recreated.");
}
void probe_storage_and_gates(){
    for(const auto offset:{0u,4u,8u,12u,16u}){
        Fixture f;f.core->vtable_offset=offset;const auto local=storage(0x10000,0x24,8);IgbMetaEnumLayoutProbeInvocation call(local);
        const auto expected=offset==0?0x8047617cu:offset==4?1u:0u;
        check(call.resume(f.services).completed && call.value()==expected,"Enum probe selected another initialized local word.");
        check(igb_bootstrap_read_word(local,0)==0x8047617c && igb_bootstrap_read_word(local,4)==1 &&
            igb_bootstrap_read_word(local,8)==0 && igb_bootstrap_read_word(local,12)==0 && igb_bootstrap_read_word(local,16)==0,
            "Enum probe did not execute actual reset and inline member stores.");
        check(local.storage->bytes->bytes.front()==0xcc && local.storage->bytes->bytes.back()==0xcc,"Enum probe overwrote outside its adjusted local.");
        f.core->vtable_offset=20;check(call.resume().value==expected,"Completed enum probe reread the offset.");
    }
    Fixture pending;pending.services->types->runtime->singleton.reset();const auto local=storage(0x10000,0x14);IgbMetaEnumLayoutProbeInvocation call(local);
    check(!call.resume(pending.services).completed && igb_bootstrap_read_word(local,0)==0x804717f0 && igb_bootstrap_read_word(local,4)==1 &&
        igb_bootstrap_read_word(local,8)==0xcccccccc,"Unknown ArkCore lost the base-reset prefix or wrote derived members early.");
    pending.core->vtable_offset.reset();pending.services->types->runtime->singleton=pending.core;
    check(!call.resume(pending.services).completed && igb_bootstrap_read_word(local,0)==0x8047617c && igb_bootstrap_read_word(local,16)==0,
        "Unknown offset skipped inline construction.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;pending.services->types->runtime->singleton=replacement;pending.core->vtable_offset=4;
    check(call.resume(pending.services).completed && call.value()==1,"Enum probe lost captured ArkCore identity.");
    for(const auto offset:{1u,3u,17u,20u,0xfffffffcu}){Fixture f;f.core->vtable_offset=offset;IgbMetaEnumLayoutProbeInvocation bad(storage(0x10000,0x14));
        rejects([&]{bad.resume(f.services);},"Enum probe accepted unaligned or unavailable stack words.");check(bad.faulted(),"Invalid enum probe lost fault state.");}
    Fixture null;null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};const auto prefix=storage(0x10000,0x14);IgbMetaEnumLayoutProbeInvocation invalid(prefix);
    rejects([&]{invalid.resume(null.services);},"Null ArkCore did not fault at reached offset read.");
    check(igb_bootstrap_read_word(prefix,0)==0x8047617c && igb_bootstrap_read_word(prefix,16)==0,"Null-core fault skipped original member-store prefix.");
    Fixture unavailable;const auto raw=storage(0x10000,0x14);raw.storage->bytes->initialized[5]=0;IgbMetaEnumLayoutProbeInvocation uninitialized(raw);
    rejects([&]{uninitialized.resume(unavailable.services);},"Enum probe fabricated an initialized old header word.");
    check(igb_bootstrap_read_word(raw,0)==0x804717f0,"Unknown old reference lost the completed base-vtable store.");
}
void defaults_and_suspension(){
    DefaultsFixture f;IgbMetaEnumDefaultsInvocation call;
    check(call.resume(f.services).completed && f.created.size()==3 && f.meta->fields->count==5 && f.interns==3,"Complete enum defaults did not run native batch/schema.");
    check(f.factories==std::vector<std::uint32_t>({0x800251ec,0x8002879c,0x8002879c}) && f.getters.size()==2 &&
        f.getters[0].function==0x80024b94 && f.getters[1].function==0x800365b4,"Enum defaults changed factories or getter order.");
    check(!f.created[0]->object_type && f.created[1]->object_type->lock()==f.names_type && f.created[2]->object_type->lock()==f.values_type &&
        igb_bootstrap_read_word(f.allocations[1],0x38)==0x700000 && igb_bootstrap_read_word(f.allocations[2],0x38)==0x710000,"Enum object-type stores lost canonical or numeric identity.");
    for(unsigned i=0;i<3;++i){const auto field=f.created[i];const auto global=*f.services->globals->at(i);
        check(field->name==*f.services->names->at(i) && field->offset==8+i*4 && field->declaring_type->lock()==f.meta &&
            global.address==0x8056180cu+i*4 && *global.slot==field,"Enum schema changed name/offset/global publication.");
        check(field->flag_26==1 && field->flag_34==7 && field->flag_35==8 && field->flag_3c==9,"Enum defaults fabricated extra reference flags.");}
    check(call.resume().completed && f.getter_polls==2 && f.field_polls==3,"Completed enum defaults replayed ownership work.");
    DefaultsFixture pending;pending.fields_ready=false;IgbMetaEnumDefaultsInvocation paused;
    check(!paused.resume(pending.services).completed && pending.factories.size()==1 && pending.getters.empty(),"Enum defaults bypassed the pending first factory.");
    pending.fields_ready=true;pending.getters_ready=false;
    check(!paused.resume(pending.services).completed && pending.created.size()==3 && pending.getters.size()==1 && pending.interns==0 && !pending.created[1]->object_type,
        "Pending first type getter published later defaults or schema.");
    const auto old=pending.meta;*pending.services->metadata=metadata();pending.getters_ready=true;
    check(paused.resume(pending.services).completed && pending.getters.size()==2 && pending.getter_polls==3 && old->fields->count==5 &&
        (**pending.services->metadata)->fields->count==0,"Enum defaults lost captured receiver or replayed a selected getter.");
}
void defaults_bindings_and_faults(){
    // The original getter result remains captured independently of subsequent globals.
    DefaultsFixture mapping;mapping.addresses=false;const auto old_type=mapping.names_type;IgbMetaEnumDefaultsInvocation late;
    check(!late.resume(mapping.services).completed && mapping.getters.size()==1 && !mapping.created[1]->object_type,
        "Missing type address was fabricated or published early.");mapping.names_type=metadata();
    mapping.services->schema->fields->pointer_lists->values.push_back({0x700000,old_type,{}});
    mapping.services->schema->fields->pointer_lists->values.push_back({0x710000,mapping.values_type,{}});
    check(late.resume(mapping.services).completed && mapping.created[1]->object_type->lock()==old_type && mapping.getter_polls==2,
        "Late mapping reread a replacement getter result or repeated the callback.");
    DefaultsFixture null_type;null_type.action=[](const IgbMetaEnumCall&,IgbMetaEnumOperation& op){op.metadata_result.reset();op.metadata_address.reset();};
    IgbMetaEnumDefaultsInvocation null;check(null.resume(null_type.services).completed &&
        igb_field_declaring_type_is_null(*null_type.created[1]->object_type) && igb_bootstrap_read_word(null_type.allocations[1],0x38)==0,
        "Known null type result was not stored as original null.");
    DefaultsFixture recursive;IgbMetaEnumDefaultsInvocation reentry;recursive.action=[&](const IgbMetaEnumCall&,IgbMetaEnumOperation&){try{reentry.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{reentry.resume(recursive.services);},"Caught enum callback recursion allowed later stores.");
    check(reentry.faulted() && !recursive.created[1]->object_type && recursive.interns==0,"Recursive enum defaults committed unreached work.");
    DefaultsFixture bad;*bad.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetaEnumDefaultsInvocation null_receiver;
    rejects([&]{null_receiver.resume(bad.services);},"Enum defaults accepted a null captured receiver.");check(bad.factories.empty(),"Null receiver called field factories.");
}
}
int main(){try{arguments_and_registration();lookups_and_adapters();probe_storage_and_gates();defaults_and_suspension();defaults_bindings_and_faults();
    std::cout<<"igb_meta_enum_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Meta enum failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
