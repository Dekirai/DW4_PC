#include "dw4/igb_platform_semaphore_registration.hpp"
#include "dw4/igb_field_values.hpp"

#include <array>
#include <iostream>

namespace {
using namespace dw4;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using FK=IgbFieldOperationKind;using SK=IgbFieldSchemaServiceKind;using Lookup=IgbPlatformSemaphoreLookup;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(unsigned size,std::uint8_t fill=0){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(size,fill);out->initialized.assign(size,1);return out;}
IgbBootstrapPointer allocation(std::uint32_t address,unsigned size){auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;
    storage->bytes=bytes(size+8,0xcc);return {storage,8};}
auto metadata(std::uint32_t flags=0){auto out=std::make_shared<IgbRuntimeMetaType>();out->class_flags=flags;
    out->fields=std::make_shared<IgbDecodeFieldRegistry>();out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);
    out->fields->backing_bytes=bytes(64);out->fields->backing_address=0x900000;return out;}
struct Fixture {
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services=std::make_shared<IgbPlatformSemaphoreRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    IgbBootstrapPointer physical=allocation(0x600008,0x64);
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> field_storage;
    std::vector<std::uint32_t> constructors;std::vector<IgbFieldOperationCall> setters;
    unsigned create_polls{},setter_polls{},names{},buffer_allocations{},struct_virtuals{},pool_queries{};
    bool fourth_ready=true,setter_ready=true;
    std::function<void()> after_setter,after_name;
    Fixture(){
        services->state=std::make_shared<IgbPlatformSemaphoreRegistrationState>();services->state->metadata=target;
        services->dependencies=std::make_shared<IgbThreadDependenciesServices>();services->dependencies->semaphore=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->types->runtime->singleton=core;core->vtable_offset=0;services->types->metadata_pool_handle=5;
        services->types->factory=[this](const IgbTypeRegistrationCall& call){check(call.kind==TK::pool_for_handle && call.handle==5,"Prepared registration endpoint received unexpected work.");
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[this,out=op.get()]{++pool_queries;out->object_result=pool;return MessengerCallResult{true,0,{}};};return op;};
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        services->callbacks->factory=make_native_igb_platform_semaphore_registration_function_factory(services);
        services->local_storage=[]{return allocation(0x700008,0x24);};
        physical.storage->bindings.push_back({physical.offset,{},target});services->physical_metadata.push_back(physical);
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;schema.types=services->types;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*schema.pools->memory_pools->fallback)->original_pool_access_bound=true;(*schema.pools->memory_pools->fallback)->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& call){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,call,out=op.get()]{if(call.kind==IgbDependenciesServiceKind::intern_name){++names;
                auto name=std::make_shared<IgbInternedString>();name->value=igb_dependency_name(call.name);name->container=pool;out->string_result=name;
                if(after_name)after_name();}else check(call.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared string operation.");
                return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();schema.fields->pointer_lists->values.push_back({0x600008,target,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& call){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,call,out=op.get()]{if(call.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else{check(call.kind==IgbPointerListServiceKind::allocation_size,"Prepared list storage unexpectedly required growth.");out->size_result=64;}
                return MessengerCallResult{true,0,{}};};return op;};
        values->fields=schema.fields;schema.fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        // The four field constructors and struct field virtuals are prepared
        // endpoints. Batch append, unsigned setters and schema execute natively.
        // This fixture is not evidence of a cold igStructMetaField implementation.
        const auto native_field_factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& call){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,call,out=op.get()]{std::uint32_t result{};
                if(call.kind==FK::object_pool || call.kind==FK::allocation_pool)out->pool_result=pool;
                else if(call.kind==FK::allocation_size)result=16;
                else{check(call.kind==FK::allocate_default,"Unexpected prepared field buffer operation.");++buffer_allocations;out->buffer_result={bytes(call.bytes),0};out->buffer_address=0xa00000+buffer_allocations*0x100;}
                return MessengerCallResult{true,result,{}};};return op;});
        const auto field_factory=[this,native_field_factory](const IgbFieldOperationCall& call){
            if(call.kind==FK::field_virtual && call.vtable_word==0x80470e00){++struct_virtuals;
                auto op=std::make_shared<IgbFieldOperation>();
                if(call.virtual_slot==0x5c)op->resume=[]{return MessengerCallResult{true,0,{}};};
                else if(call.virtual_slot==0x64)op->resume=[call]{return MessengerCallResult{true,call.field->size,{}};};
                else{check(call.virtual_slot==0x8c && !call.source,"Unexpected prepared struct virtual.");
                    auto invocation=std::make_shared<IgbFieldBufferInvocation>(call.field);op->resume=[this,invocation]{return invocation->resume(services->schema->fields);};}return op;
            }
            return native_field_factory(call);};
        schema.fields->factory=[this,field_factory](const IgbFieldOperationCall& call){const auto selected=field_factory(call);
            if(call.kind!=FK::field_virtual || call.virtual_slot!=0x8c || !call.source)return selected;
            setters.push_back(call);auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,selected,done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++setter_polls;if(!setter_ready)return MessengerCallResult{false,0x61,"prepared setter"};
                const auto status=selected->resume();if(status.completed){done=true;if(after_setter)after_setter();}return status;};return op;};
        schema.factory=[this](const IgbFieldSchemaCall& call){check(call.kind==SK::create_field,"Prepared field batch unexpectedly destroyed an object.");constructors.push_back(call.function);
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,call,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++create_polls;
                if(call.function==0x8002450c && !fourth_ready)return MessengerCallResult{false,0x62,"prepared struct factory"};
                auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=call.function==0x80022a90?0x80476e6cu:call.function==0x800385e0?0x80473e30u:0x80470e00u;
                field->size=4;field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                field->default_bytes=IgbRuntimeFieldDefaultSource{bytes(16,0xcc),0};field->default_bytes_address=0x800000+static_cast<std::uint32_t>(created.size())*0x100;
                auto storage=allocation(0x100008+static_cast<std::uint32_t>(created.size())*0x100,0x40);storage.storage->bindings.push_back({8,{},{},{},{},field});
                field->physical_storage=IgbFieldStorageBinding{storage.storage,8};igb_bootstrap_write_word(storage,0,*field->vtable_word);igb_bootstrap_write_word(storage,4,1);
                igb_bootstrap_write_halfword(storage,0x14,4);igb_bootstrap_write_word(storage,0x20,*field->default_bytes_address);
                created.push_back(field);field_storage.push_back(storage);out->field_result=field;out->field_address=igb_bootstrap_address(storage);done=true;return MessengerCallResult{true,0,{}};};return op;};
    }
};
void profile_and_prerequisites(){
    Fixture f;const auto args=igb_platform_semaphore_registration_arguments(f.services->state);
    check(args.metadata_slot.get()==&f.services->state->metadata && args.metadata_slot==igb_platform_semaphore_metadata_slot(f.services),"Semaphore profile copied its canonical metadata cell.");
    check(*args.name=="igGamecubeSemaphore" && args.flag==0 && args.native_size==0x24 && args.parent_registration==0x800260c0 &&
        args.parent_getter==0x8003b3b4 && args.field_callback==0x80026160 && args.layout_probe==0x8003b274 && args.defaults_callback==0x8003b3bc &&
        !args.trailing_callback && !args.extra_callbacks,"Concrete semaphore registration arguments differ from actual3B344.");
    check(igb_platform_semaphore_metadata_slot(f.services,Lookup::plain_parent)==f.services->dependencies->semaphore,"Semaphore parent getter does not alias existing805615EC.");
    constexpr std::array<std::uint32_t,4> factories={0x80022a90,0x80022a90,0x800385e0,0x8002450c},offsets={12,16,20,24};
    constexpr std::array names={"_initCount","_maxCount","_isActive","_sema"};
    for(unsigned i=0;i<4;++i)check(f.services->factories->at(i)==factories[i] && **f.services->names->at(i)==names[i] &&
        f.services->globals->at(i)->address==0x805620a4+4*i && f.services->offsets->at(i)==offsets[i],"Semaphore defaults table differs from primary data.");
    rejects([]{igb_platform_semaphore_registration_arguments({});},"Missing semaphore canonical state was fabricated.");
    rejects([]{IgbPlatformSemaphoreMetadataInvocation bad(static_cast<Lookup>(2));},"Unknown semaphore lookup mode was accepted.");
    f.services->callbacks->prerequisites->initialized.reset();f.services->state.reset();IgbPlatformSemaphoreRegistrationInvocation run;
    check(!run.resume(f.services).completed && f.pool_queries==0,"Semaphore body captured arguments before actual21B94.");
    auto captured=std::make_shared<IgbPlatformSemaphoreRegistrationState>();captured->metadata=metadata(4);f.services->state=captured;
    f.services->callbacks->prerequisites->initialized=1;f.services->types->metadata_pool_handle.reset();
    check(!run.resume(f.services).completed && f.pool_queries==0,"Semaphore body bypassed the registrar's pool-before-cache lookup.");
    f.services->state=std::make_shared<IgbPlatformSemaphoreRegistrationState>();f.services->types->metadata_pool_handle=5;
    check(run.resume(f.services).completed && f.pool_queries==1 && (*captured->metadata)->class_flags==4 && !f.services->state->metadata,
        "Pending semaphore registrar changed its captured canonical cell or ready flags.");
}
void getters_and_redirect(){
    Fixture unknown;unknown.services->state->metadata.reset();IgbPlatformSemaphoreMetadataInvocation wait;
    check(!wait.resume(unknown.services).completed,"Unknown concrete metadata became a null full-getter result.");
    *unknown.services->dependencies->semaphore=std::shared_ptr<IgbRuntimeMetaType>{};IgbPlatformSemaphoreMetadataInvocation parent(Lookup::plain_parent);
    check(parent.resume(unknown.services).completed && !parent.result() && unknown.pool_queries==0,"Plain semaphore parent getter registered a known-null pointer.");
    for(unsigned flags:{0u,2u,4u,8u}){Fixture f;f.target->class_flags=flags;bool ready{};unsigned selections{},polls{};
        const auto cell=f.services->state;auto replacement=metadata();
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){++selections;check(call.kind==CK::invoke_function && call.function==0x8003b344,"Full semaphore getter selected another wrapper body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(ready)cell->metadata=replacement;return MessengerCallResult{ready,0x63,"prepared registration"};};return op;};
        IgbPlatformSemaphoreMetadataInvocation getter;const auto status=getter.resume(f.services);
        if(flags==4){check(status.completed && getter.result()==f.target && selections==0,"Ready semaphore metadata unexpectedly registered.");continue;}
        check(!status.completed && status.value==0x63 && selections==1 && polls==1,"Full semaphore getter lost its pending registration.");
        f.services->state=std::make_shared<IgbPlatformSemaphoreRegistrationState>();f.services->state->metadata=metadata(4);cell->metadata=metadata(4);
        ready=true;f.services->callbacks->factory={};check(getter.resume(f.services).completed && getter.result()==replacement && selections==1 && polls==2,
            "Full semaphore getter skipped its retained body or used a replacement canonical cell.");
    }
    Fixture f;f.services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};bool ready{};unsigned calls{};auto concrete=metadata();
    f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){check(call.function==0x8003b344,"Semaphore redirect did not enter full concrete registration.");
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++calls;if(ready)f.services->state->metadata=concrete;return MessengerCallResult{ready,0x64,"prepared redirect"};};return op;};
    auto adapter=make_native_igb_platform_semaphore_redirect_factory(f.services);IgbFileFactoryCall call;call.kind=IgbFileFactoryServiceKind::redirect_type;call.redirect=0x80026160;
    auto operation=adapter(call);check(operation && !operation->resume().completed && !operation->metadata_result,"Semaphore redirect fabricated a ready class.");ready=true;
    check(operation->resume().completed && operation->metadata_result==concrete && !operation->allocation_result && !operation->object_result && calls==2,
        "Semaphore metadata redirect returned runtime storage or repeated its wrapper.");
    call.redirect=0x80023a10;check(!adapter(call),"Semaphore adapter claimed the manager redirect.");call.redirect=0x80026160;call.kind=IgbFileFactoryServiceKind::allocate;
    check(!adapter(call),"Semaphore redirect consumed a runtime allocation.");
}
void probe_store_order(){
    for(unsigned offset:{0u,4u,8u,12u,32u}){Fixture f;auto local=allocation(0x700008,0x24);f.core->vtable_offset=offset;
        for(unsigned at=12;at<36;at+=4)igb_bootstrap_write_word(local,at,0x12340000+at);
        IgbPlatformSemaphoreProbeInvocation run(local);const auto status=run.resume(f.services);
        const auto expected=offset==0?0x80470314u:offset==4?1u:offset==8?0u:0x12340000u+offset;
        check(status.completed && status.value==expected && run.value()==expected && igb_bootstrap_read_word(local,0)==0x8047650c &&
            igb_bootstrap_read_word(local,8)==0,"Semaphore probe failed its captured return or final named-object vtable restoration.");
        check(local.storage->bytes->bytes[7]==0xcc && igb_bootstrap_read_word(local,12)==0x1234000c,"Semaphore probe changed untouched stack members.");
        igb_bootstrap_write_word(local,0,0x11223344);check(run.resume().value==expected && igb_bootstrap_read_word(local,0)==0x11223344,"Completed semaphore probe replayed cleanup stores.");
    }
    Fixture missing;auto local=allocation(0x700008,0x24);missing.services->types->runtime->singleton.reset();IgbPlatformSemaphoreProbeInvocation core(local);
    check(!core.resume(missing.services).completed && igb_bootstrap_read_word(local,0)==0x8047650c && igb_bootstrap_read_word(local,8)==0xcccccccc,
        "Semaphore probe captured unknown ArkCore after abstract/name/derived stores.");
    missing.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};rejects([&]{core.resume(missing.services);},"Semaphore probe accepted null captured ArkCore.");
    check(core.faulted() && igb_bootstrap_read_word(local,0)==0x80470314 && igb_bootstrap_read_word(local,8)==0,"Null ArkCore fault lost its preceding semaphore stores.");
    Fixture captured;local=allocation(0x700008,0x24);captured.core->vtable_offset.reset();IgbPlatformSemaphoreProbeInvocation offset(local);
    check(!offset.resume(captured.services).completed && igb_bootstrap_read_word(local,0)==0x80470314 && igb_bootstrap_read_word(local,8)==0,"Unknown probe offset skipped prior name/derived stores.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;captured.services->types->runtime->singleton=replacement;captured.core->vtable_offset=8;
    igb_bootstrap_write_word(local,8,0xabcdef00);
    check(offset.resume(captured.services).completed && offset.value()==0xabcdef00 && igb_bootstrap_read_word(local,8)==0xabcdef00 && igb_bootstrap_read_word(local,0)==0x8047650c,
        "Probe reloaded ArkCore, cleared a late name or released a pointer despite the literal-zero cleanup guard.");
    for(unsigned position:{12u,32u}){Fixture f;local=allocation(0x700008,0x24);f.core->vtable_offset=position;local.storage->bytes->initialized[8+position]=0;IgbPlatformSemaphoreProbeInvocation bad(local);
        rejects([&]{bad.resume(f.services);},"Semaphore probe fabricated an unknown derived member.");
        check(bad.faulted() && igb_bootstrap_read_word(local,0)==0x80470314 && !local.storage->bytes->initialized[8+position],"Failed selected read ran cleanup or changed unknown storage.");}
    Fixture unreadable;local=allocation(0x700008,0x24);local.storage->bytes->initialized[12]=0;IgbPlatformSemaphoreProbeInvocation reset(local);
    rejects([&]{reset.resume(unreadable.services);},"Semaphore reset invented an unknown old reference word.");
    check(igb_bootstrap_read_word(local,0)==0x804717f0,"Failed common reset reached the named-object vtable store.");
    for(unsigned position:{1u,36u,0xffffffffu}){Fixture f;local=allocation(0x700008,0x24);f.core->vtable_offset=position;IgbPlatformSemaphoreProbeInvocation bad(local);
        rejects([&]{bad.resume(f.services);},"Semaphore probe escaped its bounded aligned36-byte local.");}
}
void defaults_batch_and_schema(){
    Fixture f;f.target->fields->count=2;f.fourth_ready=false;IgbPlatformSemaphoreDefaultsInvocation run;
    const auto pending=run.resume(f.services);
    check(!pending.completed && pending.value==0x62 && f.created.size()==3 && f.target->fields->count==5 && f.setters.empty() && f.names==0 && !f.target->word_40,
        "Semaphore defaults configured partial fields before the complete four-field batch.");
    check(f.constructors==std::vector<std::uint32_t>{0x80022a90,0x80022a90,0x800385e0,0x8002450c},"Semaphore field batch changed the struct dependency.");
    f.fourth_ready=true;f.setter_ready=false;const auto setter=run.resume(f.services);
    check(!setter.completed && setter.value==0x61 && f.created.size()==4 && f.target->fields->count==6 && f.setters.size()==1 && f.names==0 && f.created[3]->size==4,
        "Pending first semaphore setter changed later fields or started schema.");
    const auto captured=f.target;auto replacement=metadata();auto other=allocation(0x610008,0x64);other.storage->bindings.push_back({8,{},replacement});
    f.services->physical_metadata.push_back(other);f.services->state->metadata=replacement;f.setter_ready=true;
    check(run.resume(f.services).completed && f.setters.size()==2 && f.setter_polls==3 && f.names==4 && replacement->word_40==0x8002612c && !captured->word_40,
        "Semaphore defaults recaptured schema metadata or missed fresh final+40 publication.");
    check(igb_bootstrap_read_word(other,0x40)==0x8002612c && igb_bootstrap_read_word(other,0x3c)==0xcccccccc && !replacement->factory_redirect,
        "Semaphore defaults wrote a redirect or omitted the physical final word.");
    for(unsigned i=0;i<4;++i){const auto field=f.created[i];check(field->declaring_type->lock()==captured && field->offset==*f.services->offsets->at(i) &&
        field->name && *field->name==**f.services->names->at(i) && *f.services->globals->at(i)->slot==field &&
        igb_bootstrap_read_word(f.field_storage[i],0x1c)==0x805620a4+4*i && igb_bootstrap_read_word(f.field_storage[i],8)==*f.services->offsets->at(i) &&
        igb_bootstrap_read_word(f.field_storage[i],0x18)==0x600008,"Semaphore schema failed its captured declaring type or physical field configuration.");}
    for(unsigned i=0;i<2;++i)check(f.setters[i].field==f.created[i] && f.setters[i].source && !f.setters[i].source_address &&
        igb_buffer_word(f.setters[i].source->buffer,f.setters[i].source->offset)==1 && igb_buffer_word(f.created[i]->default_bytes->buffer,0)==1,
        "Semaphore count setter skipped its actual four-byte stack source and+8C operation.");
    check(f.created[3]->size==12 && igb_bootstrap_read_byte(f.field_storage[3],0x14)==0 &&
        igb_bootstrap_read_byte(f.field_storage[3],0x15)==12 && f.created[2]->size==1,
        "Semaphore struct size or boolean final size differs from the original rows.");
    const auto polls=f.create_polls;check(run.resume().completed && f.create_polls==polls && f.names==4,"Completed semaphore defaults repeated field work.");
}
void defaults_faults_and_live_registry(){
    Fixture rows;std::shared_ptr<IgbDecodeRuntimeField> alternative;
    rows.after_setter=[&]{if(rows.setters.size()==1){alternative=rows.created[0];rows.target->fields->backing->at(1)=alternative;}};
    IgbPlatformSemaphoreDefaultsInvocation live;check(live.resume(rows.services).completed && rows.setters.size()==2 && rows.setters[1].field==alternative &&
        igb_buffer_word(rows.created[1]->default_bytes->buffer,0)==0xcccccccc,"Second semaphore setter did not reload the live field registry.");
    Fixture schema;schema.services->names->at(0).reset();IgbPlatformSemaphoreDefaultsInvocation waiting;
    check(!waiting.resume(schema.services).completed && schema.setters.size()==2 && schema.created[3]->size==12 && schema.names==0 && !schema.target->word_40,
        "Missing schema name did not preserve all preceding semaphore defaults.");
    schema.services->names->at(0)=std::make_shared<const std::string>("_initCount");schema.after_name=[&]{if(schema.names==4)schema.services->state->metadata.reset();};
    check(!waiting.resume(schema.services).completed && schema.names==4 && !schema.target->word_40,"Unknown final semaphore cell became null or reused captured metadata.");
    schema.services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{waiting.resume(schema.services);},"Final semaphore+40 store accepted a known-null receiver.");
    check(waiting.faulted() && schema.names==4,"Final metadata fault repeated schema work.");
    Fixture truncated;truncated.physical.storage->bytes->bytes.resize(8+0x43);truncated.target->word_40=0x12345678;IgbPlatformSemaphoreDefaultsInvocation final_store;
    rejects([&]{final_store.resume(truncated.services);},"Truncated metadata accepted final semaphore+40 store.");
    check(final_store.faulted() && truncated.names==4 && truncated.target->word_40==0x12345678 && truncated.physical.storage->bytes->bytes[8+0x40]==0xcc,
        "Failed final physical store changed canonical state or lost completed schema.");
}
void exact_adapters(){
    Fixture f;f.target->class_flags=4;auto parent=metadata();*f.services->dependencies->semaphore=parent;unsigned delegated{};
    auto adapter=make_native_igb_platform_semaphore_registration_type_factory(f.services,[&](const IgbTypeRegistrationCall&){++delegated;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall call;call.kind=TK::get_parent;
    for(auto function:{0x8003b238u,0x8003b3b4u}){call.function=function;auto op=adapter(call);check(op && op->resume().completed &&
        op->metadata_result==(function==0x8003b238?f.target:parent),"Exact semaphore metadata adapter returned another canonical class.");}
    call.kind=TK::layout_probe;call.function=0x8003b274;f.services->local_storage={};auto probe=adapter(call);
    check(probe && !probe->resume().completed,"Semaphore probe adapter fabricated local storage.");auto local=allocation(0x700008,36);unsigned selections{};
    f.services->local_storage=[&]{++selections;return local;};f.core->vtable_offset.reset();check(!probe->resume().completed && selections==1,"Probe did not capture one local allocation.");
    f.services->local_storage={};f.core->vtable_offset=0;check(probe->resume().value==0x80470314 && selections==1 && igb_bootstrap_read_word(local,0)==0x8047650c,
        "Resumed semaphore probe adapter lost its selected local or cleanup order.");
    for(auto function:{0x80093530u,0x80077434u,0x80077484u,0x8003ae84u}){call.function=function;check(!adapter(call),"Registration adapter claimed runtime semaphore or manager behavior.");}
    check(delegated==4,"Unknown semaphore type operations did not delegate.");
    auto functions=make_native_igb_platform_semaphore_registration_function_factory(f.services);
    for(auto function:{0x8003b31cu,0x8003b344u}){IgbRegistrationCallbacksCall request;request.kind=CK::invoke_function;request.function=function;
        auto op=functions(request);check(op && op->resume().completed,"Exact semaphore registration wrapper/body was not composed.");}
    auto temporary=std::make_shared<IgbPlatformSemaphoreRegistrationServices>();auto expired=make_native_igb_platform_semaphore_registration_type_factory(temporary);temporary.reset();
    call.kind=TK::get_parent;call.function=0x8003b238;rejects([&]{expired(call);},"Expired semaphore type adapter selected new services.");
}
}
int main(){try{profile_and_prerequisites();getters_and_redirect();probe_store_order();defaults_batch_and_schema();defaults_faults_and_live_registry();exact_adapters();
    std::cout<<"igb_platform_semaphore_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Platform semaphore registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
