#include "dw4/igb_memory_pool_registration.hpp"
#include "dw4/igb_field_values.hpp"
#include <array>
#include <cstdint>
#include <string_view>

// Synthetic test expectations transcribed from the independently sealed
// abstract-class contract, not runtime substitutes for original asset tables.
namespace memory_pool_test {
struct Row {
    std::uint32_t factory;
    std::string_view name;
    std::uint32_t global;
    std::uint32_t offset;
};
inline constexpr std::array<Row,14> schema{{
    {0x8002cea8,"_localAllocated",0x805618b8,0x08},
    {0x8002cea8,"_localFreed",0x805618bc,0x10},
    {0x8002cea8,"_localNetAllocated",0x805618c0,0x18},
    {0x8002cea8,"_localNetAllocatedHigh",0x805618c4,0x20},
    {0x8002cea8,"_localAllocatedWithOverhead",0x805618c8,0x28},
    {0x8002cea8,"_localFreedWithOverhead",0x805618cc,0x30},
    {0x8002cea8,"_localNetAllocatedWithOverhead",0x805618d0,0x38},
    {0x8002cea8,"_localNetAllocatedWithOverheadHigh",0x805618d4,0x40},
    {0x8002cea8,"_localAllocationCount",0x805618d8,0x48},
    {0x8002cea8,"_localFreeCount",0x805618dc,0x50},
    {0x8002cea8,"_localNetAllocationCount",0x805618e0,0x58},
    {0x8002cea8,"_localNetAllocationCountHigh",0x805618e4,0x60},
    {0x800385e0,"_updateStatistics",0x805618e8,0x68},
    {0x800385e0,"_updateGlobalStatistics",0x805618ec,0x69}
}};
inline constexpr std::uint32_t pool_global=0x805618b4;
inline constexpr std::uint32_t deferred_redirect=0x8002c0a0;
inline constexpr std::uint32_t signed_long_metadata=0x8056198c;
inline constexpr std::uint32_t signed_long_prototype=0x80561990;
inline constexpr std::uint32_t signed_long_table=0x80472100;
} // namespace memory_pool_test

#include <algorithm>
#include <iostream>

// Synthetic checks for original metadata registration and defaults ordering.
namespace {
using namespace dw4;
using C=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& operation,const char* why){bool failed{};try{operation();}catch(const std::runtime_error&){failed=true;}check(failed,why);}
auto metadata(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;return result;}
auto pool_services(){auto result=std::make_shared<IgbMemoryPoolRegistrationServices>();
    result->metadata_slot=std::make_shared<IgbRegistrationMetaSlot>();
    result->malloc_metadata_slot=std::make_shared<IgbRegistrationMetaSlot>();return result;}

void exact_abstract_contract(){
    auto services=pool_services();
    const auto arguments=igb_memory_pool_registration_arguments(services);
    check(arguments.metadata_slot==services->metadata_slot && arguments.name && *arguments.name=="igMemoryPool" &&
        arguments.flag==1 && arguments.native_size==0x70,"Abstract memory-pool registrar identity changed.");
    check(arguments.parent_registration==0x80066b08 && arguments.parent_getter==0x800237d0 &&
        arguments.field_callback==0x8002bfd8 && arguments.defaults_callback==0x8002bff8 &&
        !arguments.layout_probe && !arguments.trailing_callback && !arguments.extra_callbacks,
        "Abstract memory-pool profile added a probe, callback or different dependency.");
    check(services->factories && services->names && services->globals && services->offsets &&
        services->factories->size()==14 && services->names->size()==14 && services->globals->size()==14 &&
        services->offsets->size()==14,"Abstract memory-pool schema table lengths changed.");
    for(std::size_t i=0;i<memory_pool_test::schema.size();++i){const auto& expected=memory_pool_test::schema[i];
        check(services->factories->at(i)==expected.factory && services->names->at(i) &&
            **services->names->at(i)==expected.name && services->globals->at(i) &&
            services->globals->at(i)->address==expected.global && services->offsets->at(i)==expected.offset,
            "Abstract memory-pool table differs from its original field row.");
    }
    const auto malloc=igb_memory_pool_registration_arguments(services,IgbMemoryPoolKind::malloc_pool);
    check(malloc.metadata_slot==services->malloc_metadata_slot && malloc.metadata_slot!=services->metadata_slot &&
        malloc.name && *malloc.name=="igMallocMemoryPool" && malloc.flag==0 && malloc.native_size==0xa0 &&
        malloc.parent_registration==0x8002bf70 && malloc.parent_getter==0x80038f58 && malloc.field_callback==0x80039e08 &&
        malloc.layout_probe==0x80039cc0 && malloc.defaults_callback==0x80039e28 && !malloc.trailing_callback &&
        malloc.extra_callbacks && *malloc.extra_callbacks==std::vector<std::uint32_t>({0x80030d50,0x80026098,0}),
        "Enclosing malloc registration changed its explicit parent or deferred dependencies.");
    auto factory=make_native_igb_memory_pool_registration_type_factory(services);
    IgbTypeRegistrationCall next;next.kind=IgbTypeRegistrationServiceKind::layout_probe;next.function=0x80039cc0;
    auto probe=factory(next);
    check(probe && !probe->resume().completed,"Malloc probe adapter fabricated its missing local storage.");
    next.kind=IgbTypeRegistrationServiceKind::defaults;next.function=0x80039e28;
    check(!factory(next),"Parent metadata recovery invented malloc default behavior.");
}

void distinct_lookup_modes(){
    for(const auto mode:{IgbMemoryPoolLookup::plain,IgbMemoryPoolLookup::allocate_if_null,IgbMemoryPoolLookup::fully_registered}){
        auto services=pool_services();
        IgbMemoryPoolMetadataLookupInvocation invocation(mode);
        check(!invocation.resume(services).completed && !invocation.faulted(),"Unknown pool metadata was treated as known-null.");
        auto existing=metadata(mode==IgbMemoryPoolLookup::fully_registered?4:0);
        *services->metadata_slot=existing;
        check(invocation.resume(services).completed && invocation.result()==existing,
            "Existing metadata lookup required an irrelevant lower service.");
        check(invocation.resume().completed && invocation.result()==existing,"Completed metadata lookup recaptured its result.");
    }
    auto services=pool_services();
    *services->metadata_slot=std::shared_ptr<IgbRuntimeMetaType>{};
    IgbMemoryPoolMetadataLookupInvocation plain(IgbMemoryPoolLookup::plain);
    check(plain.resume(services).completed && !plain.result(),"Plain metadata getter attempted allocation or registration.");
    IgbMemoryPoolMetadataLookupInvocation lazy(IgbMemoryPoolLookup::allocate_if_null);
    check(!lazy.resume(services).completed,"Lazy null getter bypassed its original metadata-pool dependency.");
}

void full_getter_captures_cell_and_continuation(){
    for(const bool fresh_null:{false,true}){
        auto services=pool_services();auto captured=services->metadata_slot;
        *captured=metadata(0);services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
        unsigned selections{},polls{};bool ready{};auto fresh=metadata(4);
        services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){
            check(call.kind==C::invoke_function && call.function==0x8002bf70,"Full pool getter selected another registration body.");
            ++selections;auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();operation->resume=[&]{
                ++polls;if(!ready)return MessengerCallResult{false,0,"Prepared abstract registration body."};
                *captured=fresh_null?std::shared_ptr<IgbRuntimeMetaType>{}:fresh;return MessengerCallResult{true,0,{}};
            };return operation;
        };
        IgbMemoryPoolMetadataLookupInvocation invocation;
        check(!invocation.resume(services).completed && selections==1 && polls==1,"Full getter skipped the unready registration branch.");
        services->metadata_slot=std::make_shared<IgbRegistrationMetaSlot>(metadata(4));services->callbacks->factory={};ready=true;
        check(invocation.resume(services).completed && invocation.result()==(fresh_null?std::shared_ptr<IgbRuntimeMetaType>{}:fresh) &&
            selections==1 && polls==2,"Full getter lost its canonical cell or retained callback across suspension.");
    }
}

void direct_abstract_body_does_not_run_prerequisites(){
    auto services=pool_services();*services->metadata_slot=metadata(4);
    services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
    unsigned lookups{};services->types->factory=[&](const IgbTypeRegistrationCall& call){
        check(call.kind==IgbTypeRegistrationServiceKind::pool_for_handle && call.handle==5,
            "Ready abstract registrar selected an unrelated lower operation.");
        ++lookups;auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;
    };
    // Actual 66204 first looks up the metadata pool even when its class is ready.
    // The long-field body has a separate earlier 21B94 prerequisite.
    IgbMemoryPoolRegistrationInvocation invocation;
    check(invocation.resume(services).completed && !invocation.result() && lookups==1,
        "Abstract body invented the long-field prerequisite or layout probe.");
}

IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
// Actual fourteen-field batch and registry insertion, with named constructors
// and a no-receiver-read virtual endpoint isolating the 3EC68 caller contract.
struct BooleanCallFixture {
    std::shared_ptr<IgbMemoryPoolRegistrationServices> services=pool_services();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbBootstrapPointer> physical;
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbFieldOperationCall> selected;
    std::function<void(unsigned,const std::shared_ptr<IgbDecodeRuntimeField>&,const IgbBootstrapPointer&)> prepare_field;
    std::function<void(unsigned)> after_field;
    bool first_ready{};unsigned first_polls{},second_polls{};
    BooleanCallFixture(){
        *services->metadata_slot=target;target->fields=std::make_shared<IgbDecodeFieldRegistry>();
        target->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);
        target->fields->backing_bytes=raw(0x900000,128).storage->bytes;target->fields->backing_address=0x900000;
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;
        auto manager=make_original_igb_memory_pool_registry();manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->pools->memory_pools->fallback=manager;
        services->schema=std::make_shared<IgbFieldSchemaServices>();services->schema->types=services->types;services->schema->pools=services->pools;
        services->schema->fields=std::make_shared<IgbFieldOperationsServices>();
        auto lists=std::make_shared<IgbPointerListServices>();services->schema->fields->pointer_lists=lists;
        lists->factory=[this](const IgbPointerListCall& call){auto operation=std::make_shared<IgbPointerListOperation>();
            operation->resume=[this,call,out=operation.get()]{
                if(call.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else{check(call.kind==IgbPointerListServiceKind::allocation_size,"Prepared batch unexpectedly requested registry growth.");out->size_result=128;}
                return MessengerCallResult{true,0,{}};
            };return operation;
        };
        services->schema->factory=[this](const IgbFieldSchemaCall& call){
            check(call.kind==IgbFieldSchemaServiceKind::create_field && created.size()<14 &&
                call.function==memory_pool_test::schema[created.size()].factory,"Pool batch changed its original constructor order.");
            auto operation=std::make_shared<IgbFieldSchemaOperation>();
            operation->resume=[this,call,out=operation.get()]{
                auto field=std::make_shared<IgbDecodeRuntimeField>();auto storage=raw(0x100000+static_cast<std::uint32_t>(created.size())*0x100,0x34);
                field->physical_storage=IgbFieldStorageBinding{storage.storage,0};storage.storage->bindings.push_back({0,{},{},{},{},field});
                igb_bootstrap_write_word(storage,0,call.function==0x8002cea8?0x80472100:0x80473e30);
                igb_bootstrap_write_word(storage,4,1);igb_bootstrap_write_word(storage,0x1c,0);
                const auto index=static_cast<unsigned>(created.size());if(prepare_field)prepare_field(index,field,storage);
                created.push_back(field);physical.push_back(storage);out->field_result=field;out->field_address=igb_bootstrap_address(storage);
                if(after_field)after_field(index);
                return MessengerCallResult{true,0,{}};
            };return operation;
        };
        services->schema->fields->factory=[this](const IgbFieldOperationCall& call){
            check(call.kind==IgbFieldOperationKind::field_virtual && call.virtual_slot==0x8c && call.vtable_word==0x80473e30 &&
                call.source && call.source->buffer && call.source->buffer->bytes.size()==1 && call.source->offset==0 &&
                call.source->buffer->bytes[0]==1 && call.source->buffer->initialized[0]==1,
                "Boolean setter changed its captured VT or exact one-byte local source.");
            selected.push_back(call);const auto index=selected.size();auto operation=std::make_shared<IgbFieldOperation>();
            operation->resume=[this,index]{if(index==1){++first_polls;return MessengerCallResult{first_ready,0,"Prepared no-read Boolean virtual."};}
                ++second_polls;return MessengerCallResult{true,0,{}};};return operation;
        };
        services->names->at(0).reset(); // Deliberate next boundary after both setters.
    }
};

void selected_boolean_virtual_has_no_extra_receiver_reads(){
    for(const bool replace_binding:{false,true}){
        BooleanCallFixture f;IgbMemoryPoolDefaultsInvocation invocation;
        check(!invocation.resume(f.services).completed && f.created.size()==14 && f.target->fields->count==14 &&
            f.selected.size()==1 && f.selected[0].field==f.created[12],"First Boolean callback did not follow the completed real batch.");
        if(replace_binding){f.created[12]->physical_storage.reset();}
        else{for(unsigned i=0;i<4;++i)f.physical[12].storage->bytes->initialized[i]=0;}
        auto next=std::make_shared<IgbDecodeRuntimeField>(*f.created[13]);next->physical_storage.reset();next->vtable_word=0x80473e30;
        f.target->fields->backing->at(13)=next;
        check(!invocation.resume(f.services).completed && f.first_polls==2 && f.selected.size()==1 && !invocation.faulted(),
            "Pending no-read Boolean callback caused an extra receiver identity or VT read.");
        f.first_ready=true;const auto status=invocation.resume(f.services);
        check(!status.completed && !invocation.faulted() && f.first_polls==3 && f.second_polls==1 && f.selected.size()==2 &&
            f.selected[0].field==f.created[12] && f.selected[1].field==next,
            "Boolean completion reread its receiver or missed the later fresh indexed field.");
        check(!f.target->factory_redirect,"Paused schema published the deferred factory redirect.");
    }
}

// Actual batch, Boolean value/buffer operations, schema and finalizer. Only
// field constructors and the named pool-size/string services are prepared.
struct DefaultsFixture : BooleanCallFixture {
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    IgbBootstrapPointer metadata_bytes=raw(0x600000,0x64);
    unsigned interned{};
    std::function<void()> after_intern;
    DefaultsFixture(){
        services->names->at(0)=std::make_shared<const std::string>(memory_pool_test::schema[0].name);
        metadata_bytes.storage->bindings.push_back({0,{},target});services->physical_metadata.push_back(metadata_bytes);
        target->factory_redirect=0; // Match the explicitly prepared physical null below.
        igb_bootstrap_write_word(metadata_bytes,0x3c,0);igb_bootstrap_write_word(metadata_bytes,0x40,0x12345678);
        services->schema->fields->pointer_lists->values.push_back({0x600000,target,{}});
        auto dependencies=std::make_shared<IgbDependenciesServices>();services->schema->dependencies=dependencies;
        services->schema->fields->dependencies=dependencies;dependencies->globals=std::make_shared<SystemStageServices>();
        dependencies->globals->string_pool=pool;
        dependencies->factory=[this](const IgbDependenciesCall& call){
            check(call.kind==IgbDependenciesServiceKind::intern_name,"Prepared pool schema requested an unrelated string service.");
            auto operation=std::make_shared<IgbDependenciesOperation>();operation->resume=[this,call,out=operation.get()]{
                ++interned;auto name=std::make_shared<IgbInternedString>();name->container=pool;name->value=igb_dependency_name(call.name);
                out->string_result=name;if(after_intern)after_intern();return MessengerCallResult{true,0,{}};
            };return operation;
        };
        values->fields=services->schema->fields;values->signed_long_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->schema->fields->factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& call){
            auto operation=std::make_shared<IgbFieldOperation>();operation->resume=[this,call,out=operation.get()]{
                if(call.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=pool;
                else check(call.kind==IgbFieldOperationKind::allocation_size,"Prepared pool descriptor unexpectedly needed allocation/free.");
                return MessengerCallResult{true,call.kind==IgbFieldOperationKind::allocation_size?8u:0u,{}};
            };return operation;
        });
        prepare_field=[this](unsigned index,const auto& field,const IgbBootstrapPointer& storage){
            field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
            field->default_bytes=IgbRuntimeFieldDefaultSource{raw(0x800000+index*0x100,8).storage->bytes,0};
            field->default_bytes_address=0x800000+index*0x100;
            igb_bootstrap_write_halfword(storage,0x14,index<12?8:1);
            igb_bootstrap_write_word(storage,0x20,*field->default_bytes_address);
        };
    }
};

void real_defaults_preserve_eight_byte_unknown_payloads(){
    DefaultsFixture f;auto inherited=std::make_shared<IgbDecodeRuntimeField>();
    f.target->fields->count=2;f.target->fields->backing->at(0)=inherited;
    IgbMemoryPoolDefaultsInvocation invocation;
    check(invocation.resume(f.services).completed && f.target->fields->count==16 &&
        f.target->fields->backing->at(0)==inherited && f.created.size()==14 && f.interned==14,
        "Pool defaults changed inherited fields/count or omitted actual batch/schema work.");
    for(unsigned index=0;index<14;++index){const auto& expected=memory_pool_test::schema[index];const auto& field=f.created[index];
        check(field->offset==expected.offset && field->declaring_type && field->declaring_type->lock()==f.target &&
            *f.services->globals->at(index)->slot==field && igb_bootstrap_read_word(f.physical[index],0x1c)==expected.global,
            "Pool schema omitted exact offset, declaring owner or physical global pointer.");
        check(field->size==(index<12?8:1),"Signed-long descriptor size was truncated or Boolean size widened.");
        const auto& payload=*field->default_bytes->buffer;
        if(index<12)check(std::none_of(payload.initialized.begin(),payload.initialized.end(),[](auto value){return value!=0;}),
            "Null long-field finalization invented initialized default payload bytes.");
        else check(payload.bytes[0]==1 && payload.initialized[0]==1 &&
            std::none_of(payload.initialized.begin()+1,payload.initialized.end(),[](auto value){return value!=0;}),
            "Boolean defaults changed more than their one-byte source.");
    }
    check(f.target->factory_redirect==memory_pool_test::deferred_redirect &&
        igb_bootstrap_read_word(f.metadata_bytes,0x3c)==memory_pool_test::deferred_redirect &&
        igb_bootstrap_read_word(f.metadata_bytes,0x40)==0x12345678,
        "Pool defaults failed the physical factory-redirect store or wrote metadata +40 instead.");
    check(invocation.resume().completed && f.interned==14 && f.created.size()==14,"Completed defaults repeated schema effects.");
}

void final_redirect_uses_fresh_metadata_after_schema(){
    DefaultsFixture f;auto fresh=metadata();auto fresh_bytes=raw(0x610000,0x64);
    fresh_bytes.storage->bindings.push_back({0,{},fresh});igb_bootstrap_write_word(fresh_bytes,0x3c,0);
    f.services->physical_metadata.push_back(fresh_bytes);
    f.after_field=[&](unsigned index){if(index==13)*f.services->metadata_slot=fresh;};
    IgbMemoryPoolDefaultsInvocation invocation;
    check(invocation.resume(f.services).completed && f.target->fields->count==14 && f.interned==14 &&
        f.target->factory_redirect==0 && fresh->factory_redirect==memory_pool_test::deferred_redirect &&
        igb_bootstrap_read_word(f.metadata_bytes,0x3c)==0 &&
        igb_bootstrap_read_word(fresh_bytes,0x3c)==memory_pool_test::deferred_redirect,
        "Defaults recaptured its initial metadata or failed the distinct final canonical reload.");

    DefaultsFixture unknown;unknown.after_intern=[&]{if(unknown.interned==14)unknown.services->metadata_slot->reset();};
    IgbMemoryPoolDefaultsInvocation waiting;
    check(!waiting.resume(unknown.services).completed && unknown.interned==14 && unknown.created[13]->offset==0x69 &&
        igb_bootstrap_read_word(unknown.metadata_bytes,0x3c)==0,"Unknown final metadata lost or bypassed the complete schema prefix.");
    *unknown.services->metadata_slot=unknown.target;
    check(waiting.resume(unknown.services).completed && unknown.interned==14,"Final metadata continuation replayed schema work.");

    DefaultsFixture truncated;truncated.after_intern=[&]{if(truncated.interned==14){
        truncated.metadata_bytes.storage->bytes->bytes.resize(0x3f);truncated.metadata_bytes.storage->bytes->initialized.resize(0x3f);}};
    IgbMemoryPoolDefaultsInvocation failed;
    rejects([&]{failed.resume(truncated.services);},"Short final metadata accepted a partial redirect word store.");
    check(failed.faulted() && truncated.interned==14 && truncated.created[13]->offset==0x69 && truncated.target->factory_redirect==0 &&
        truncated.metadata_bytes.storage->bytes->bytes[0x3c]==0 && truncated.metadata_bytes.storage->bytes->bytes[0x3d]==0 &&
        truncated.metadata_bytes.storage->bytes->bytes[0x3e]==0,"Failed redirect write changed bytes/cache or hid its earlier complete schema.");
}

void lazy_allocation_only_retains_captured_pool_and_cell(){
    for(const bool returned_null:{false,true}){
        BooleanCallFixture f;auto captured=f.services->metadata_slot;*captured=std::shared_ptr<IgbRuntimeMetaType>{};
        auto fresh=metadata();auto physical=raw(0x620000,0x64);physical.storage->bindings.push_back({0,{},fresh});
        auto allocator=std::make_shared<IgbMetadataFactoryServices>();f.services->metadata=allocator;
        allocator->metadata=metadata(4);allocator->bootstrap=std::make_shared<IgbBootstrapServices>();
        allocator->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=1;allocator->bootstrap->runtime->singleton=core;
        unsigned registrations{},allocations{},polls{};bool ready{};
        allocator->factory=[&](const IgbMetadataFactoryCall& call){auto operation=std::make_shared<IgbMetadataFactoryOperation>();
            if(call.kind==IgbMetadataFactoryServiceKind::register_type){++registrations;operation->resume=[]{return MessengerCallResult{true,0,{}};};}
            else{check(call.kind==IgbMetadataFactoryServiceKind::reflected_factory && call.pool==f.pool,
                    "Lazy metadata allocation lost its captured metadata pool.");++allocations;
                operation->resume=[&,out=operation.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"Prepared metadata reflection."};
                    out->allocation_result=returned_null?IgbBootstrapPointer{}:physical;return MessengerCallResult{true,0,{}};};}
            return operation;
        };
        IgbMemoryPoolMetadataLookupInvocation invocation(IgbMemoryPoolLookup::allocate_if_null);
        check(!invocation.resume(f.services).completed && allocations==1 && registrations==1 && !captured->value(),
            "Lazy pool getter registered the pool class or published an incomplete allocation.");
        auto replacement=metadata(4);f.services->metadata_slot=std::make_shared<IgbRegistrationMetaSlot>(replacement);
        f.services->metadata=std::make_shared<IgbMetadataFactoryServices>();allocator->factory={};ready=true;
        check(invocation.resume(f.services).completed && invocation.result()==(returned_null?std::shared_ptr<IgbRuntimeMetaType>{}:fresh) &&
            captured->value()==invocation.result() && f.services->metadata_slot->value()==replacement &&
            allocations==1 && polls==2 && fresh->class_flags==0,
            "Lazy continuation lost its selected allocator/cell, fabricated null behavior, or added pool registration.");
    }
}

void caught_outer_reentry_stops_native_boolean_publication(){
    DefaultsFixture f;const auto prepare=f.prepare_field;
    f.prepare_field=[&](unsigned index,const auto& field,const IgbBootstrapPointer& storage){
        prepare(index,field,storage);if(index==12){field->default_bytes=IgbRuntimeFieldDefaultSource{};
            field->default_bytes_address=0;igb_bootstrap_write_word(storage,0x20,0);}
    };
    auto payload=raw(0x820000,1).storage->bytes;unsigned allocations{};
    IgbMemoryPoolDefaultsInvocation invocation;
    f.services->schema->fields->factory=make_native_igb_field_values_factory(f.values,[&](const IgbFieldOperationCall& call){
        auto operation=std::make_shared<IgbFieldOperation>();operation->resume=[&,call,out=operation.get()]{
            if(call.kind==IgbFieldOperationKind::object_pool || call.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=f.pool;
            else if(call.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,8,{}};
            else{check(call.kind==IgbFieldOperationKind::allocate_default && call.bytes==1,"Boolean guard fixture reached another allocation.");
                ++allocations;rejects([&]{invocation.resume(f.services);},"Recursive pool defaults did not fault.");
                out->buffer_result={payload,0};out->buffer_address=0x820000;}
            return MessengerCallResult{true,0,{}};
        };return operation;
    });
    rejects([&]{invocation.resume(f.services);},"Caught parent recursion allowed nested Boolean construction to complete.");
    check(invocation.faulted() && allocations==1 && f.created.size()==14 && f.target->fields->count==14 &&
        f.created[12]->size==1 && !f.created[12]->default_bytes->buffer && igb_bootstrap_read_word(f.physical[12],0x20)==0 &&
        payload->initialized[0]==0 && f.interned==0 && f.target->factory_redirect==0,
        "Nested Boolean allocation published or copied after the parent fault instead of preserving its reached prefix.");
    rejects([&]{invocation.resume(f.services);},"Faulted parent resumed schema or another allocation.");
}
} // namespace
int main(){try{exact_abstract_contract();distinct_lookup_modes();full_getter_captures_cell_and_continuation();
    direct_abstract_body_does_not_run_prerequisites();selected_boolean_virtual_has_no_extra_receiver_reads();
    real_defaults_preserve_eight_byte_unknown_payloads();final_redirect_uses_fresh_metadata_after_schema();
    lazy_allocation_only_retains_captured_pool_and_cell();
    caught_outer_reentry_stops_native_boolean_publication();
    std::cout<<"Memory-pool registration checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Memory-pool registration failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
