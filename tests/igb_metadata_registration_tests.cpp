#include "dw4/igb_metadata_registration.hpp"
#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_thread_startup.hpp"
#include "dw4/igb_gamecube_thread_adopt.hpp"
#include "dw4/igb_thread_manager_insert.hpp"
#include "dw4/igb_data_list_growth.hpp"
#include "dw4/igb_os_thread_wake.hpp"
#include "dw4/igb_thread_runtime_init.hpp"
#include "dw4/igb_semaphore_runtime_init.hpp"
#include "dw4/igb_struct_member.hpp"
#include "dw4/igb_platform_thread_registration.hpp"
#include "dw4/igb_platform_semaphore_registration.hpp"
#include "dw4/igb_gamecube_thread_registration.hpp"
#include "dw4/igb_gamecube_thread_init.hpp"
#include "dw4/igb_struct_field_defaults.hpp"
#include "dw4/igb_thread_defaults.hpp"
#include "dw4/igb_named_type_defaults.hpp"
#include "dw4/igb_data_list_defaults.hpp"
#include "dw4/igb_ark_core_bootstrap.hpp"
#include "dw4/igb_object_registration.hpp"
#include "dw4/igb_metadata_operations.hpp"
#include "dw4/igb_string_pool.hpp"
#include "dw4/igb_object_defaults.hpp"
#include "dw4/igb_type_list_factory.hpp"
#include "dw4/igb_type_publication.hpp"
#include "dw4/igb_metadata_layout.hpp"
#include "dw4/igb_metadata_probe.hpp"
#include "dw4/igb_metadata_defaults.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_list_registration.hpp"
#include "dw4/igb_list_defaults.hpp"
#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_type_defaults.hpp"
#include "dw4/igb_field_probe.hpp"
#include "dw4/igb_enum_field_defaults.hpp"
#include "dw4/igb_field_prototype.hpp"
#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_clone.hpp"
#include "dw4/igb_reflected_clone.hpp"
#include "dw4/igb_meta_enum.hpp"
#include "dw4/igb_metadata_init.hpp"
#include "dw4/igb_object_field_construct.hpp"
#include "dw4/igb_memory_field_construct.hpp"
#include "dw4/igb_memory_field_defaults.hpp"
#include "dw4/igb_string_field_defaults.hpp"
#include "dw4/igb_memory_member_copy.hpp"
#include "dw4/igb_memory_member_defaults.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using MK=IgbMetadataFactoryServiceKind;using BK=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbRuntimeMetaType> meta(std::uint32_t flags){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;return result;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t bytes){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(bytes,0xcc);
    storage->bytes->initialized.assign(bytes,1);return {storage,0};}
struct Fixture {
    std::shared_ptr<IgbMetadataRegistrationServices> services=std::make_shared<IgbMetadataRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta(0);
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer object=allocation(0x120000,0x64),list=allocation(0x180000,16);
    std::vector<IgbTypeRegistrationCall> calls;std::vector<IgbMetadataFactoryCall> factory_calls;
    unsigned allocations{};
    IgbTypeRegistrationFactory remaining;IgbMetadataFactory factory_remaining;
    Fixture(){
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->pools=std::make_shared<IgbFilePoolsServices>();
        services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();services->pools->memory_pools->selector=0;
        services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->types->metadata_pool_handle=5;services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();services->metadata->bootstrap->runtime=services->types->runtime;
        services->metadata->dependencies=std::make_shared<IgbDependenciesServices>();services->metadata->dependencies->globals=std::make_shared<SystemStageServices>();
        services->metadata->dependencies->globals->string_pool=pool;
        object.storage->bindings.push_back({0,{},constructed,{}});list.storage->bindings.push_back({0,{},{},fields});
        services->metadata->bootstrap->factory=[this](const IgbBootstrapCall& call){
            if(call.kind!=BK::pool_allocate)return std::shared_ptr<IgbBootstrapOperation>{};
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,call,out=op.get()]{
                check(call.pool==pool && call.bytes==0x64,"Actual metadata allocation lost its pool or size.");
                ++allocations;out->allocation_result=object;return MessengerCallResult{true,0,{}};};return op;
        };
        // Prepared field-list output isolates the actual metadata constructor;
        // these tests do not claim to register or allocate the original list.
        services->metadata->factory=make_native_igb_metadata_registration_factory(services,[this](const IgbMetadataFactoryCall& call){
            factory_calls.push_back(call);if(factory_remaining)return factory_remaining(call);
            if(call.kind!=MK::create_fields)return std::shared_ptr<IgbMetadataFactoryOperation>{};
            auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[this,out=op.get()]{out->allocation_result=list;out->fields_result=fields;return MessengerCallResult{true,0,{}};};return op;
        });
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,
            make_native_igb_metadata_factory(services->metadata,[this](const IgbTypeRegistrationCall& call){
                calls.push_back(call);return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_metadata_registration_function_factory(services);
    }
};
void exact_arguments(){
    auto state=std::make_shared<IgbMetadataFactoryServices>();const auto args=igb_metadata_type_registration_arguments(state);
    check(args.metadata_slot.get()==&state->metadata && !*args.metadata_slot,"Metadata arguments do not alias canonical factory state.");
    check(*args.name=="igMetaObject" && args.flag==0 && args.native_size==0x64,"Concrete metadata name/flag/size differ.");
    check(args.parent_registration==0x80066b08 && args.parent_getter==0x800237d0 && args.field_callback==0x8002a144,"Metadata parent/getter identities differ.");
    check(args.layout_probe==0x80029ffc && args.defaults_callback==0x8002a164 && args.trailing_callback==0,"Real layout/default functions were replaced.");
    check(*args.extra_callbacks==std::vector<std::uint32_t>({0x8002dd18,0x8002e054,0x80066ae0,0}),"Metadata extension table differs.");
    const auto owner=meta(0);state->metadata=owner;check(args.metadata_slot->value()==owner,"Canonical later publication was copied instead of aliased.");
    std::weak_ptr<IgbMetadataFactoryServices> weak=state;state.reset();check(!weak.expired(),"Argument alias dropped canonical cell owner.");
    rejects([]{igb_metadata_type_registration_arguments({});},"Missing canonical cell was accepted.");
}
void cold_body_and_ready(){
    Fixture f;f.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetadataTypeRegistrationInvocation call;
    check(!call.resume(f.services).completed,"Unimplemented parent should remain pending.");
    check(!f.services->callbacks->prerequisites && f.allocations==1,"Metadata body invented prerequisite guard or skipped real construction.");
    check(f.services->metadata->metadata==f.constructed && f.constructed->class_flags==8 && f.constructed->native_size==0xffffffffu,"Constructed canonical metadata/busy prefix was not preserved.");
    check(f.constructed->fields==f.fields && igb_bootstrap_read_word(f.object,4)==1,"Cold metadata construction did not execute physical defaults.");
    check(f.calls.size()==1 && f.calls[0].kind==TK::register_parent && f.calls[0].function==0x80066b08,"Wrong first parent boundary.");
    check(!call.resume(f.services).completed && f.allocations==1,"Pending parent replayed metadata allocation.");
    for(const auto flags:{4u,8u,12u,0xffffffffu}){
        Fixture ready;ready.services->metadata->metadata=meta(flags);IgbMetadataTypeRegistrationInvocation body;
        check(body.resume(ready.services).completed && !body.result() && ready.calls.empty() && ready.allocations==0,"Ready or busy branch did not use original registrar gates.");
        check((*ready.services->metadata->metadata)->class_flags==flags,"Ready/busy short return changed original flags.");
        check(body.resume().completed,"Completed registration replayed.");
    }
    Fixture pool_first;pool_first.services->metadata->metadata=meta(4);pool_first.manager->pools[5].reset();
    IgbMetadataTypeRegistrationInvocation body;check(!body.resume(pool_first.services).completed,"Ready state bypassed earlier physical pool lookup.");
}
void layout_and_defaults_boundaries(){
    Fixture f;const auto owner=meta(1);f.services->metadata->metadata=owner;
    bool probe_ready{};unsigned probe_selections{},probe_polls{};
    f.remaining=[&](const IgbTypeRegistrationCall& call){
        if(call.kind==TK::set_name){auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,call]{
            IgbMetadataTypeRegistrationInvocation nested;
            check(nested.resume(f.services).completed && !nested.result() && owner->class_flags==9,"Legitimate published busy recursion was rejected.");
            call.metadata->name=call.name;return MessengerCallResult{true,0,{}};};return op;}
        if(call.kind==TK::layout_probe){++probe_selections;check(call.function==0x80029ffc,"Wrong metadata stack-layout probe.");
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&]{++probe_polls;return MessengerCallResult{probe_ready,0x804761d8,"prepared probe"};};return op;}
        return std::shared_ptr<IgbTypeRegistrationOperation>{};
    };
    IgbMetadataTypeRegistrationInvocation call;check(!call.resume(f.services).completed && owner->class_flags==9 && owner->native_size==0x64 && owner->flag_1a==0,"Layout pause lost preceding schema stores.");
    check(owner->field_callback==0x8002a144 && !owner->type_word,"Uncompleted probe published a type word.");
    probe_ready=true;f.remaining={};check(!call.resume(f.services).completed,"Unknown metadata fields were treated as completed.");
    check(probe_selections==1 && probe_polls==2 && owner->type_word==0x804761d8,"Pending probe was replaced or replayed.");
    check(f.calls.back().kind==TK::defaults && f.calls.back().function==0x8002a164 && owner->class_flags==9,"Exact defaults boundary or prior flags differ.");
    rejects([&]{call.result();},"Incomplete schema exposed completed result.");
}
void full_getter(){
    Fixture f;IgbMetadataLookupInvocation call;check(!call.resume(f.services).completed,"Unknown metadata cell did not pause.");
    auto ready=meta(4);f.services->metadata->metadata=ready;check(call.resume(f.services).completed && call.result()==ready,"Ready getter failed without prerequisites.");
    f.services->metadata->metadata=meta(0);check(call.resume().completed && call.result()==ready,"Completed getter reread live cache.");
    for(const auto flags:{0u,1u,2u,8u}){
        Fixture item;item.services->metadata->metadata=meta(flags);item.services->callbacks.reset();IgbMetadataLookupInvocation getter;
        check(!getter.resume(item.services).completed,"Full getter skipped required wrapper for missing ready bit.");
    }
    Fixture pending;pending.services->metadata->metadata=meta(0);unsigned selections{},polls{};auto replacement=meta(4);
    pending.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
        check(c.kind==CK::invoke_function && c.function==0x8002a0d0,"Full getter selected wrong wrapper body.");++selections;
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{if(++polls==1)return MessengerCallResult{false,0,"prepared registration"};
            pending.services->metadata->metadata=replacement;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbMetadataLookupInvocation getter;check(!getter.resume(pending.services).completed,"Prepared callback did not pause.");
    pending.services->metadata->metadata=meta(4);pending.services->callbacks->factory={};
    check(getter.resume(pending.services).completed && getter.result()==replacement && selections==1 && polls==2,"Full getter failed to retain call then reload canonical result.");
    Fixture reload;reload.services->metadata->metadata=meta(0);unsigned calls{};
    reload.services->callbacks->factory=[&](const auto&){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++calls;reload.services->metadata->metadata.reset();return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataLookupInvocation late;check(!late.resume(reload.services).completed && calls==1,"Unknown final cache failed to pause after registration.");
    reload.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(late.resume(reload.services).completed && !late.result() && calls==1,"Known null final cache triggered registration replay.");
}
void raw_getter(){
    Fixture f;f.services->metadata->metadata=meta(0);f.services->types.reset();f.services->pools.reset();
    IgbMetadataLookupInvocation existing(IgbMetadataLookup::allocate_if_null);check(existing.resume(f.services).completed && existing.result()==*f.services->metadata->metadata,"Raw getter incorrectly required ready flags or pool services.");
    Fixture fresh;fresh.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};fresh.services->types->metadata_pool_handle.reset();
    IgbMetadataLookupInvocation raw(IgbMetadataLookup::allocate_if_null);
    check(!raw.resume(fresh.services).completed && fresh.allocations==0,"Raw null lookup skipped handle boundary.");
    fresh.services->types->metadata_pool_handle=5;check(raw.resume(fresh.services).completed && raw.result()==fresh.constructed && fresh.services->metadata->metadata==fresh.constructed,"Raw getter failed real direct metadata construction/publication.");
    check(fresh.constructed->class_flags==0 && fresh.calls.empty(),"Raw getter invented registration flags or parent calls.");
    Fixture captured;captured.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};captured.manager->original_pool_access_bound=false;
    unsigned selections{},polls{};captured.services->pools->factory=[&](const IgbFilePoolsCall& c){
        ++selections;check(c.handle==5,"Raw getter did not capture original handle.");auto op=std::make_shared<IgbFilePoolsOperation>();
        op->resume=[&,out=op.get()]{if(++polls==1)return MessengerCallResult{false,0,"prepared pool"};out->object_result=captured.pool;return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataLookupInvocation held(IgbMetadataLookup::allocate_if_null);check(!held.resume(captured.services).completed,"Raw pool did not pause.");
    captured.services->types->metadata_pool_handle=7;captured.services->pools->factory={};
    check(held.resume(captured.services).completed && selections==1 && polls==2 && captured.allocations==1,"Raw getter replayed live handle or lost selected pool.");
    Fixture overwritten;overwritten.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};std::weak_ptr<IgbRuntimeMetaType> displaced;
    overwritten.factory_remaining=[&](const IgbMetadataFactoryCall& c){if(c.kind!=MK::create_fields)return std::shared_ptr<IgbMetadataFactoryOperation>{};
        auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[&,out=op.get()]{auto inserted=meta(0);inserted->reference_word=17;displaced=inserted;
            overwritten.services->metadata->metadata=inserted;out->fields_result=overwritten.fields;out->allocation_result=overwritten.list;return MessengerCallResult{true,0,{}};};return op;};
    {IgbMetadataLookupInvocation publish(IgbMetadataLookup::allocate_if_null);check(publish.resume(overwritten.services).completed && publish.result()==overwritten.constructed,"Factory result did not overwrite callback-replaced raw cache.");}
    check(!displaced.expired() && displaced.lock()->reference_word==17,"Raw cache publication released a displaced original owner.");
    // Prepared enabled-branch endpoints isolate the raw getter's null return
    // publication. They do not claim to construct a reflected metadata object.
    Fixture null_return;null_return.core->enabled=1;null_return.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    unsigned reflected_calls{};null_return.services->metadata->factory=[&](const IgbMetadataFactoryCall& c){
        auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[&,c]{
            if(c.kind==MK::register_type)null_return.services->metadata->metadata=meta(4);
            else if(c.kind==MK::reflected_factory){++reflected_calls;check(bool(c.metadata) && c.pool==null_return.pool,"Raw getter lost actual factory arguments.");}
            else throw std::runtime_error("Unexpected prepared reflected endpoint");return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataLookupInvocation null_value(IgbMetadataLookup::allocate_if_null);
    check(null_value.resume(null_return.services).completed && !null_value.result() && null_return.services->metadata->metadata && !*null_return.services->metadata->metadata && reflected_calls==1,"Raw getter did not publish a captured null factory result.");
}
void cycles_and_adapters(){
    Fixture cycle;cycle.core->enabled=1;cycle.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbMetadataTypeRegistrationInvocation call;
    rejects([&]{call.resume(cycle.services);},"Invalid enabled/null bootstrap recursed without a boundary.");
    check(call.faulted() && cycle.allocations==0 && cycle.services->metadata->metadata && !*cycle.services->metadata->metadata && cycle.calls.empty(),"Cycle boundary fabricated metadata or changed original schema flags.");
    rejects([&]{call.resume(cycle.services);},"Faulted recursive registration replayed.");
    Fixture ready;ready.services->metadata->metadata=meta(4);auto adapter=make_native_igb_metadata_registration_type_factory(ready.services);
    for(const auto address:{0x80029f84u,0x80029fc0u,0x8002a144u}){
        IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=address;auto op=adapter(c);
        check(op && op->resume().completed && op->metadata_result==*ready.services->metadata->metadata,"Exact getter adapter missed canonical result.");
    }
    IgbTypeRegistrationCall c;c.kind=TK::register_parent;c.function=0x8002a0d0;auto body=adapter(c);
    check(body && body->resume().completed,"Exact registration adapter missed ready path.");
    c.function=0x8002a164;check(!adapter(c),"Unknown defaults callback was completed by wrapper adapter.");
    IgbRegistrationCallbacksCall wrapper;wrapper.kind=CK::invoke_function;wrapper.function=0x8002a0a8;
    auto op=ready.services->callbacks->factory(wrapper);check(op && op->resume().completed,"Wrapper did not compose actual callback runner.");
    Fixture held;held.services->types->metadata_pool_handle.reset();held.services->metadata->metadata=meta(4);
    auto selected=held.services->callbacks->factory(IgbRegistrationCallbacksCall{CK::invoke_function,0x8002a0d0});
    check(!selected->resume().completed,"Selected registration did not pause at missing handle.");
    auto weak=std::weak_ptr<IgbMetadataRegistrationServices>(held.services);held.services.reset();
    check(!weak.expired(),"Pending selected operation lost services.");selected.reset();check(weak.expired(),"Native weak adapter created permanent service cycle.");
    auto expired_services=std::make_shared<IgbMetadataRegistrationServices>();auto expired=make_native_igb_metadata_registration_type_factory(expired_services);expired_services.reset();
    c.function=0x8002a0d0;rejects([&]{expired(c);},"Expired adapter selected nonexistent services.");
}
void actual_cold_bootstrap_prefix(){
    // Platform/memory-pool activation and physical allocators remain explicit
    // prepared endpoints. Initial type-list construction/reserve, registration
    // and concrete metadata/field constructors all execute natively.
    Fixture f;using AK=IgbArkCoreServiceKind;using FK=IgbFieldRegistryServiceKind;
    IgbArkCoreObject ark{allocation(0x300000,0x3a0),f.core};
    igb_construct_ark_core(ark);
    check(f.core->enabled==0 && igb_bootstrap_read_byte(ark.allocation,0)==0,"Actual ArkCore constructor did not establish the cold gate.");
    f.services->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    f.services->metadata->bootstrap->default_pool_handle=0;f.manager->pools[0]=f.pool;
    f.services->metadata->bootstrap->factory=make_native_igb_bootstrap_pool_factory(f.services->pools,[&](const IgbBootstrapCall& c){
        if(c.kind!=BK::pool_allocate)return std::shared_ptr<IgbBootstrapOperation>{};
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
            check(f.core->enabled==0 && c.pool==f.pool,"Cold object allocation used a changed gate or pool.");
            ++f.allocations;
            if(c.bytes==0x64)out->allocation_result=f.object;
            else if(c.bytes==16)out->allocation_result=f.list;
            else throw std::runtime_error("Unexpected cold object allocation size");
            return MessengerCallResult{true,0,{}};};return op;
    });
    auto fields_services=std::make_shared<IgbFieldRegistryServices>();fields_services->bootstrap=f.services->metadata->bootstrap;
    f.services->metadata->field_registries=fields_services;
    auto backing=allocation(0x400000,16);backing.storage->bytes->initialized.assign(16,0);
    bool backing_ready{};unsigned backing_selections{},backing_polls{},object_pool_calls{};
    fields_services->factory=[&](const IgbFieldRegistryCall& c){
        if(c.kind!=FK::object_pool && c.kind!=FK::allocate_backing)return std::shared_ptr<IgbFieldRegistryOperation>{};
        if(c.kind==FK::allocate_backing)++backing_selections;
        auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==FK::object_pool){++object_pool_calls;check(c.handle==0,"Actual list base reset did not supply object pool tag zero.");out->pool_result=f.pool;}
            else{++backing_polls;check(c.pool==f.pool && c.bytes==16,"Actual list backing request changed owner or size.");
                if(!backing_ready)return MessengerCallResult{false,0,"prepared physical backing allocator"};
                out->backing_result={backing,std::make_shared<IgbDecodeFieldRegistry::Storage>(4)};}
            return MessengerCallResult{true,0,{}};};return op;
    };
    auto bootstrap=std::make_shared<IgbArkCoreBootstrapServices>();bootstrap->bootstrap=f.services->metadata->bootstrap;
    bootstrap->pools=f.services->pools;bootstrap->callbacks=f.services->callbacks;
    f.services->callbacks->runtime=f.services->types->runtime;
    std::vector<IgbArkCoreServiceKind> prefix;
    std::vector<std::shared_ptr<IgbRuntimeTypeList>> initial_lists;
    std::vector<IgbBootstrapPointer> initial_allocations,initial_backings;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> initial_callbacks;
    auto native_lists=std::make_shared<IgbTypeListServices>();native_lists->bootstrap=std::make_shared<IgbBootstrapServices>();
    native_lists->bootstrap->runtime=f.services->types->runtime;
    native_lists->bootstrap->factory=[&](const IgbBootstrapCall& c){
        check(c.kind==BK::pool_allocate && c.bytes==16 && c.pool==f.pool,"Cold type-list object allocation changed its actual request.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,out=op.get()]{
            auto list=std::make_shared<IgbRuntimeTypeList>();list->count=23;list->capacity=29;
            auto physical=allocation(0x500000+static_cast<std::uint32_t>(initial_lists.size())*0x100,16);
            physical.storage->bindings.push_back({0,{},{},{},list});initial_lists.push_back(list);initial_allocations.push_back(physical);
            out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbTypeListFactory resumed_type_lists;
    std::vector<IgbTypeListCall> enabled_type_list_calls;
    native_lists->factory=[&](const IgbTypeListCall& c){
        if(c.kind==IgbTypeListServiceKind::reflected_factory){
            enabled_type_list_calls.push_back(c);
            return resumed_type_lists?resumed_type_lists(c):std::shared_ptr<IgbTypeListOperation>{};
        }
        auto op=std::make_shared<IgbTypeListOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbTypeListServiceKind::object_pool){check(c.handle==0,"Cold type-list constructor did not reset its pool tag.");out->pool_result=f.pool;}
            else if(c.kind==IgbTypeListServiceKind::allocate_backing){
                check(c.pool==f.pool && c.bytes==16,"Cold type-list backing allocation changed its pool/size.");
                auto physical=allocation(0x510000+static_cast<std::uint32_t>(initial_backings.size())*0x100,16);
                physical.storage->bytes->initialized.assign(16,0);initial_backings.push_back(physical);
                out->backing_result={physical,std::make_shared<IgbRuntimeTypeList::Storage>(4)};
            }else throw std::runtime_error("Cold type-list construction reached unexpected reflection or virtual work");
            return MessengerCallResult{true,0,{}};};return op;
    };
    auto type_pointers=std::make_shared<IgbPointerListServices>();
    auto grown_types=allocation(0x520000,1536*4);grown_types.storage->bytes->initialized.assign(1536*4,0);
    auto grown_type_mapping=std::make_shared<IgbRuntimeTypeList::Storage>(1536);unsigned type_size_queries{},type_growths{};
    type_pointers->factory=[&](const IgbPointerListCall& c){
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,out=op.get()]{
            switch(c.kind){
            case IgbPointerListServiceKind::allocation_pool:out->pool_result=f.pool;break;
            case IgbPointerListServiceKind::allocation_size:
                ++type_size_queries;check(c.pool==f.pool,"Native type-list query lost its pool.");
                if(c.backing.address==igb_bootstrap_address(grown_types))out->size_result=1536*4;
                else{bool found{};for(const auto& value:initial_backings)if(igb_bootstrap_address(value)==c.backing.address){found=true;out->size_result=16;}
                    check(found,"Native type-list query used unallocated backing.");}break;
            case IgbPointerListServiceKind::object_pool:check(c.handle==0,"Native list growth lost its live reset pool tag.");out->pool_result=f.pool;break;
            case IgbPointerListServiceKind::reallocate:
                check(c.bytes==1536*4 && c.backing.address==0x510000 && type_growths==0,"Native initial reserve changed its growth request.");++type_growths;
                out->backing_result={grown_types.storage->bytes,igb_bootstrap_address(grown_types),0,grown_type_mapping,{}};break;
            }return MessengerCallResult{true,0,{}};};return op;
    };
    bootstrap->factory=make_native_igb_type_list_bootstrap_factory(native_lists,
        make_native_igb_pointer_list_bootstrap_factory(type_pointers,[&](const IgbArkCoreCall& c){
        auto op=std::make_shared<IgbArkCoreOperation>();op->resume=[&,c,out=op.get()]{
            prefix.push_back(c.kind);check(f.core->enabled==0,"Prepared bootstrap prefix ran after an invented enable store.");
            switch(c.kind){
            case AK::platform_initialize:break;
            case AK::manager_virtual:check(c.manager==f.manager && c.virtual_slot==0xa8,"Cold bootstrap selected a different manager virtual.");break;
            case AK::allocate_callbacks:{
                check(c.bytes==12,"Bootstrap callback allocation changed size.");
                auto list=std::make_shared<IgbRegistrationCallbackList>();list->count=17;list->capacity=19;
                initial_callbacks.push_back(list);out->callbacks_result=list;
                out->allocation_result=allocation(0x600000+static_cast<std::uint32_t>(initial_callbacks.size())*16,12);break;
            }
            default:throw std::runtime_error("Cold integration reached an unexpected prepared bootstrap endpoint");
            }
            return MessengerCallResult{true,0,{}};};return op;
    }));
    // Retained callback selection still consults the same dynamic binding
    // registry when a reached function body becomes available later.
    IgbRegistrationCallbacksFactory additional_callbacks;
    bool unsigned_byte_registration_ready{},unsigned_int_registration_ready{},struct_prototype_ready{};
    const auto original_function_bindings=f.services->callbacks->factory;
    f.services->callbacks->factory=[&,original_function_bindings](const IgbRegistrationCallbacksCall& c){
        if(additional_callbacks && (c.function!=0x80023234 || unsigned_byte_registration_ready) &&
            (c.function!=0x80022cd8 || unsigned_int_registration_ready) &&
            (c.function!=0x800247e4 || struct_prototype_ready)){
            auto result=additional_callbacks(c);if(result)return result;}
        return original_function_bindings?original_function_bindings(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
    };
    IgbArkCoreBootstrapInvocation call(ark);
    check(!call.resume(bootstrap).completed && !call.faulted(),"Real cold chain did not suspend at physical field-list backing allocation.");
    check(f.services->metadata->metadata && !*f.services->metadata->metadata && f.allocations==2 && f.calls.empty(),"Pending nested list construction prematurely published metadata.");
    check(igb_bootstrap_read_word(f.object,0)==0x804761d8 && igb_bootstrap_read_word(f.object,0x28)==0 && igb_bootstrap_read_word(f.list,0)==0x80475c54,"Native constructor prefixes were replaced by prepared objects.");
    check(initial_lists.size()==2 && initial_callbacks.size()==2 && f.core->types==initial_lists.front() && *f.core->secondary_types==initial_lists.back(),"Bootstrap did not publish both distinct runtime lists.");
    check(type_growths==1 && type_size_queries==3 && native_lists->completed_allocations.size()==2 && f.core->types->count==0 &&
        f.core->types->backing_bytes==grown_types.storage->bytes && f.core->types->backing==grown_type_mapping,"Cold metadata chain skipped original initial type-list construction/reserve.");
    for(const auto& physical:initial_allocations)check(igb_bootstrap_read_word(physical,0)==0x80475cb0 && igb_bootstrap_read_word(physical,4)==1,"Cold type list did not execute its actual derived constructor.");
    for(const auto& callbacks:initial_callbacks)check(callbacks->count==0 && callbacks->capacity==0 && !callbacks->backing,"Actual callback-list initializer did not reset prepared callback header state.");
    const auto prefix_count=prefix.size();backing_ready=true;
    check(!call.resume(bootstrap).completed && !call.faulted(),"Missing igObject registration was treated as a completed cold bootstrap.");
    check(f.services->metadata->metadata==f.constructed && f.constructed->class_flags==8 && f.constructed->fields==f.fields,"Actual cold registrar did not publish its constructed canonical busy metadata.");
    check(f.core->enabled==0 && igb_bootstrap_read_byte(ark.allocation,0)==0 && f.core->field_initialization_enabled==1,"Pending registration enabled reflection too early.");
    check(f.calls.size()==1 && f.calls.front().kind==TK::register_parent && f.calls.front().function==0x80066b08,"Cold chain did not reach the actual missing igObject parent body.");
    check(f.factory_calls.empty() && f.allocations==2 && object_pool_calls==1 && backing_selections==1 && backing_polls==2 && prefix.size()==prefix_count,"Resumption replaced native construction or replayed earlier allocations.");
    check(igb_bootstrap_read_word(f.list,0)==0x80475cb0 && igb_bootstrap_read_word(f.list,8)==0x400000 && igb_bootstrap_read_word(f.list,12)==0 && f.fields->backing_bytes==backing.storage->bytes,"Concrete internal-list constructor failed to publish its real backing.");
    for(const auto initialized:backing.storage->bytes->initialized)check(initialized==0,"Empty native list fabricated initialized spare pointer words.");
    check(igb_bootstrap_read_word(ark.allocation,4)==0xcccccccc && igb_bootstrap_read_word(ark.allocation,8)==0xcccccccc,"Incomplete bootstrap published future type-count snapshots.");
    check(!call.resume(bootstrap).completed && f.allocations==2 && prefix.size()==prefix_count,"Repeated parent suspension replayed completed cold startup work.");

    // Continue the same suspended cold chain. The second metadata/list pair
    // receives distinct physical allocations; neither canonical global is
    // seeded with a ready type. Arena allocation stays explicitly prepared.
    auto object_services=std::make_shared<IgbObjectRegistrationServices>();
    object_services->metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_services->types=f.services->types;object_services->pools=f.services->pools;object_services->callbacks=f.services->callbacks;
    f.services->metadata->bootstrap->object_metadata=object_services->metadata;
    auto object_type=meta(0);auto object_fields=std::make_shared<IgbDecodeFieldRegistry>();
    auto object_allocation=allocation(0x700000,0x64);object_allocation.storage->bindings.push_back({0,{},object_type,{}});
    auto fields_allocation=allocation(0x710000,16);fields_allocation.storage->bindings.push_back({0,{},{},object_fields});
    auto fields_backing=allocation(0x720000,16);fields_backing.storage->bytes->initialized.assign(16,0);
    f.services->metadata->bootstrap->factory=make_native_igb_bootstrap_pool_factory(f.services->pools,[&](const IgbBootstrapCall& c){
        if(c.kind!=BK::pool_allocate)return std::shared_ptr<IgbBootstrapOperation>{};
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
            check(f.core->enabled==0 && c.pool==f.pool,"igObject physical allocation changed the cold gate or selected pool.");++f.allocations;
            if(c.bytes==0x64)out->allocation_result=object_allocation;
            else if(c.bytes==16)out->allocation_result=fields_allocation;
            else throw std::runtime_error("Unexpected igObject allocation size");return MessengerCallResult{true,0,{}};};return op;
    });
    fields_services->factory=[&](const IgbFieldRegistryCall& c){
        if(c.kind!=FK::object_pool && c.kind!=FK::allocate_backing)return std::shared_ptr<IgbFieldRegistryOperation>{};
        auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==FK::object_pool){++object_pool_calls;check(c.handle==0,"igObject field registry did not use reset pool tag zero.");out->pool_result=f.pool;}
            else{++backing_selections;++backing_polls;check(c.pool==f.pool && c.bytes==16,"igObject field backing allocation changed arguments.");out->backing_result={fields_backing,{}};}
            return MessengerCallResult{true,0,{}};};return op;
    };
    auto native_strings=std::make_shared<IgbStringPoolServices>();native_strings->mutex=std::shared_ptr<LoaderDirectoryMutex>{};
    auto string_state=std::make_shared<IgbStringPoolState>();string_state->table=std::make_shared<IgbStringTable>();
    string_state->table->capacity=4;string_state->table->storage=std::make_shared<IgbStringTable::Storage>(4);string_state->arena=f.pool;
    igb_bind_string_pool(native_strings,f.pool,string_state);unsigned string_allocations{};
    std::vector<IgbFieldStringBinding> allocated_names;
    std::shared_ptr<IgbFieldValuesServices> live_value_bindings;
    native_strings->factory=[&](const IgbStringPoolCall& c){
        check(c.kind==IgbStringPoolServiceKind::allocate_string && c.arena==f.pool,"Cold name interning reached an unexpected arena operation.");
        auto op=std::make_shared<IgbStringPoolOperation>();op->resume=[&,c,out=op.get()]{
            ++string_allocations;out->entry_result=std::make_shared<IgbStringEntry>();out->entry_result->owner=std::make_shared<IgbInternedString>();
            allocated_names.push_back({0x780000+string_allocations*0x100,out->entry_result->owner});
            if(live_value_bindings)igb_bind_field_string(live_value_bindings,allocated_names.back().address,allocated_names.back().owner);
            out->entry_result->bytes.buffer=std::make_shared<IgbScratchBuffer>();out->entry_result->bytes.buffer->bytes.resize(c.bytes+4);
            out->entry_result->bytes.buffer->initialized.assign(c.bytes+4,0);
            // Original strcmp reads aligned words beyond a terminating byte.
            // Represent the arena's accessible padding without initializing
            // the character payload that the actual interner still must copy.
            for(std::uint32_t i=c.bytes;i<c.bytes+4;++i){out->entry_result->bytes.buffer->bytes[i]=0xa5;out->entry_result->bytes.buffer->initialized[i]=1;}
            out->container_result=f.pool;
            return MessengerCallResult{true,0,{}};};return op;
    };
    f.services->metadata->dependencies->strings=native_strings->strings;
    f.services->metadata->dependencies->factory=make_native_igb_string_factory(native_strings);
    auto metadata_ops=std::make_shared<IgbMetadataOperationsServices>();metadata_ops->types=f.services->types;
    metadata_ops->dependencies=f.services->metadata->dependencies;
    std::vector<IgbTypeRegistrationCall> remaining_calls;
    std::shared_ptr<IgbRegistrationCallbackList> root_callbacks;
    f.remaining=make_native_igb_object_registration_type_factory(object_services,
        make_native_igb_metadata_operations_factory(metadata_ops,[&](const IgbTypeRegistrationCall& c){
            remaining_calls.push_back(c);if(c.kind!=TK::create_callbacks)return std::shared_ptr<IgbTypeRegistrationOperation>{};
            check(c.bytes==12 && c.pool==f.pool,"Real root parent changed its callback allocation arguments.");
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,out=op.get()]{
                root_callbacks=std::make_shared<IgbRegistrationCallbackList>();root_callbacks->count=31;root_callbacks->capacity=37;
                out->callbacks_result=root_callbacks;return MessengerCallResult{true,0,{}};};return op;
        }));
    const auto defaults=call.resume(bootstrap);
    check(!defaults.completed && !call.faulted(),"Unrecovered igObject descriptor defaults were reported as complete.");
    check(*object_services->metadata==object_type && object_type!=f.constructed && object_type->class_flags==9,"Real igObject registration did not construct and publish its distinct busy metadata.");
    check(f.constructed->class_flags==8 && f.services->metadata->metadata==f.constructed && f.core->enabled==0,"Parent suspension completed its child or enabled reflection early.");
    check(object_type->fields==object_fields && object_type->native_size==8 && object_type->field_callback==0x80066b74 && object_type->type_word==0x804717f0,"Actual parent registration/probe lost its canonical schema prefix.");
    check(object_type->name && *object_type->name=="igObject" && string_allocations==1 && string_state->table->count==1 &&
        native_strings->strings->owners.size()==1 && native_strings->strings->owners[0]->references==1,"Cold metadata naming did not execute real string interning with one retained header.");
    check(root_callbacks && root_callbacks->count==0 && root_callbacks->capacity==0 && !root_callbacks->backing,"Root parent callback list did not execute its actual constructor.");
    check(!remaining_calls.empty() && remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x80066b7c,"Cold composition stopped at a substituted igObject defaults endpoint.");
    check(f.allocations==4 && object_pool_calls==2 && backing_selections==2 && backing_polls==3 && prefix.size()==prefix_count,"Extending the native parent chain replayed completed allocations or bootstrap calls.");
    check(igb_bootstrap_read_word(object_allocation,0)==0x804761d8 && igb_bootstrap_read_word(fields_allocation,0)==0x80475cb0 &&
        igb_bootstrap_read_word(fields_allocation,8)==0x720000,"Second metadata and internal list were replaced by prepared completed objects.");
    check(!call.resume(bootstrap).completed && f.allocations==4 && string_allocations==1 && prefix.size()==prefix_count,"Pending descriptor defaults replayed earlier native construction or interning.");

    // Supply the recovered specialized descriptor factory and field operations.
    // Physical allocation and the original null BSS prototype are represented
    // explicitly; no completed descriptor or field virtual is substituted.
    auto object_defaults=std::make_shared<IgbObjectDefaultsServices>();object_defaults->object_metadata=object_services->metadata;
    object_defaults->types=f.services->types;object_defaults->pools=f.services->pools;object_defaults->dependencies=f.services->metadata->dependencies;
    object_defaults->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();object_defaults->meta_fields->dependencies=object_defaults->dependencies;
    object_defaults->meta_fields->bootstrap=std::make_shared<IgbBootstrapServices>();object_defaults->meta_fields->bootstrap->runtime=f.services->types->runtime;
    object_defaults->meta_fields->bootstrap->object_metadata=object_services->metadata;
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> descriptors;std::vector<IgbBootstrapPointer> descriptor_allocations;
    object_defaults->meta_fields->bootstrap->factory=[&](const IgbBootstrapCall& c){
        check(c.kind==BK::pool_allocate && c.bytes==0x34 && c.pool==f.pool,"Cold specialized field allocation changed its real request.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,out=op.get()]{
            auto descriptor=std::make_shared<IgbDecodeRuntimeField>();auto physical=allocation(0x730000+static_cast<std::uint32_t>(descriptors.size())*0x100,0x34);
            physical.storage->bindings.push_back({0,{},{},{},{},descriptor});descriptors.push_back(descriptor);descriptor_allocations.push_back(physical);
            out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;
    };
    object_defaults->fields=std::make_shared<IgbFieldOperationsServices>();object_defaults->fields->dependencies=object_defaults->dependencies;
    object_defaults->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    object_defaults->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
    object_fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);
    unsigned field_size_queries{},default_allocations{};
    object_defaults->fields->pointer_lists->factory=[&](const IgbPointerListCall& c){
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,out=op.get()]{
            check((c.backing.address==0x720000 && c.backing.bytes==fields_backing.storage->bytes) ||
                (c.backing.address==0x400000 && c.backing.bytes==backing.storage->bytes),"Descriptor append lost its actual field-list allocation.");
            if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=f.pool;
            else if(c.kind==IgbPointerListServiceKind::allocation_size){++field_size_queries;out->size_result=16;}
            else throw std::runtime_error("Cold descriptor registry unexpectedly grew");return MessengerCallResult{true,0,{}};};return op;
    };
    object_defaults->fields->factory=make_native_igb_unsigned_int_field_factory(object_defaults->fields,[&](const IgbFieldOperationCall& c){
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbFieldOperationKind::object_pool){check(c.handle==0,"Cold descriptor reset did not clear its pool tag.");out->pool_result=f.pool;}
            else if(c.kind==IgbFieldOperationKind::allocate_default){
                check(c.pool==f.pool && c.bytes==4 && c.alignment==1,"Actual unsigned descriptor default request differs.");++default_allocations;
                auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.assign(4,0);buffer->initialized.assign(4,1);
                out->buffer_result={buffer,0};out->buffer_address=0x740000+default_allocations*0x100;
            }else throw std::runtime_error("Prepared service replaced a recovered descriptor virtual");return MessengerCallResult{true,0,{}};};return op;
    });
    f.remaining=make_native_igb_object_defaults_factory(object_defaults,f.remaining);
    const auto published_fields=call.resume(bootstrap);
    check(!published_fields.completed && !call.faulted(),"Incomplete runtime type publication was reported as complete startup.");
    check(descriptors.size()==2 && default_allocations==2 && string_allocations==3 && field_size_queries==2,"Native cold descriptor construction/finalization did not run exactly twice.");
    check(object_type->class_flags==11 && f.constructed->class_flags==8 && f.core->enabled==0 && object_fields->count==2,"Completed defaults skipped schema flag2 or enabled reflection before publication.");
    check(*object_defaults->ref_count==descriptors[0] && *object_defaults->pool_index==descriptors[1],"Real field finalizers did not publish both canonical descriptor globals.");
    for(unsigned i=0;i<2;++i){const auto& descriptor=descriptors[i];
        check(descriptor->vtable_word==0x80476e6c && descriptor->offset==4 && descriptor->size==4 && descriptor->type_ordinal==0xffff &&
            descriptor->default_mode==3 && descriptor->flag_26==0 && descriptor->reference_word==1,"Cold specialized descriptor schema or ownership differs.");
        check(descriptor->name && *descriptor->name==(i==0?"_refCount":"_memPoolIndex") && descriptor->name_string && (*descriptor->name_string)->references==1,"Cold descriptor name temporary ownership differs.");
        check(object_fields->backing->at(i)==descriptor && igb_buffer_word(object_fields->backing_bytes,i*4)==igb_bootstrap_address(descriptor_allocations[i]),"Cold field append did not publish actual allocated identity.");
    }
    check(remaining_calls.back().kind==TK::publish_type && remaining_calls.back().metadata==object_type,"Native defaults did not reach the next actual runtime publication boundary.");
    check(!call.resume(bootstrap).completed && descriptors.size()==2 && default_allocations==2 && string_allocations==3,"Pending publication replayed recovered descriptor defaults.");

    auto publication=std::make_shared<IgbTypePublicationServices>();publication->runtime=f.services->types->runtime;
    publication->pointer_lists=type_pointers;type_pointers->values.push_back({igb_bootstrap_address(object_allocation),object_type,{}});
    type_pointers->values.push_back({igb_bootstrap_address(f.object),f.constructed,{}});
    object_defaults->fields->pointer_lists->values.push_back({igb_bootstrap_address(object_allocation),object_type,{}});
    object_defaults->fields->pointer_lists->values.push_back({igb_bootstrap_address(f.object),f.constructed,{}});
    f.remaining=make_native_igb_type_publication_factory(publication,f.remaining);
    check(!call.resume(bootstrap).completed && !call.faulted(),"Unbound original field layout was skipped.");
    check(object_type->word_0c==0 && object_type->reference_word==1 && f.core->types->count==1 &&
        f.core->types->backing->at(0)==object_type && igb_buffer_word(grown_types.storage->bytes,0)==0x700000,
        "Native class publication did not retain/store the canonical object type and index.");
    check(object_type->class_flags==11 && remaining_calls.back().kind==TK::compute_layout && remaining_calls.back().metadata==object_type,
        "Published object type did not reach its actual unimplemented layout boundary.");
    const auto queries_before_layout=type_size_queries;
    check(!call.resume(bootstrap).completed && type_size_queries==queries_before_layout && f.core->types->count==1,
        "Pending layout repeated type publication or its retained append.");

    // Connect the completed base layout to real inheritance: construct the
    // parent's child list, append the child and retain both inherited fields.
    const auto append_fields=make_native_igb_pointer_list_metadata_factory(object_defaults->fields->pointer_lists);
    const auto append_children=make_native_igb_pointer_list_metadata_factory(type_pointers);
    metadata_ops->factory=make_native_igb_metadata_pool_factory(f.services->pools,
        make_native_igb_type_list_metadata_factory(native_lists,[append_fields,append_children](const IgbMetadataOperationCall& c){
            return c.kind==IgbMetadataOperationKind::append_field?append_fields(c):append_children(c);
        }));
    auto layout=std::make_shared<IgbMetadataLayoutServices>();layout->runtime=f.services->types->runtime;
    layout->pointer_lists=object_defaults->fields->pointer_lists;
    f.remaining=make_native_igb_metadata_layout_factory(layout,f.remaining);
    const auto next=call.resume(bootstrap);
    check(!next.completed && !call.faulted(),"Missing metadata layout probe was treated as completed startup.");
    check(object_type->class_flags==7 && object_type->native_alignment==4 && object_type->allocation_prefix==0 && object_type->native_size==8,
        "Base type layout changed its native size or did not complete/clear the busy flag.");
    check(f.constructed->class_flags==9 && f.constructed->parent==object_type && f.constructed->name && *f.constructed->name=="igMetaObject" &&
        f.constructed->native_size==0x64 && f.constructed->field_callback==0x8002a144 && f.fields->count==2,
        "Completed base registration did not feed actual parent attachment and metadata naming.");
    check(initial_lists.size()==3 && initial_backings.size()==3 && *object_type->children==initial_lists[2] &&
        initial_lists[2]->count==1 && initial_lists[2]->backing->at(0)==f.constructed &&
        igb_bootstrap_read_word(initial_allocations[2],0)==0x80475c54 && igb_buffer_word(initial_backings[2].storage->bytes,0)==0x120000,
        "Native inherited child registration did not construct/store the exact child-list identity.");
    for(unsigned i=0;i<2;++i){const auto& descriptor=descriptors[i];
        check(descriptor->ordinal==i && descriptor->declaring_type && descriptor->declaring_type->lock()==object_type &&
            descriptor->reference_word==2 && f.fields->backing->at(i)==descriptor &&
            igb_buffer_word(backing.storage->bytes,i*4)==igb_bootstrap_address(descriptor_allocations[i]),
            "Native layout/inheritance lost declaring identity, field ordinal, retained count or physical pointer.");
    }
    check(string_allocations==4 && field_size_queries==4 && type_growths==1 && f.core->types->count==1 && f.core->enabled==0 &&
        remaining_calls.back().kind==TK::layout_probe && remaining_calls.back().function==0x80029ffc,
        "Extended cold chain did not stop at the actual metadata constructor probe.");
    const auto final_type_queries=type_size_queries;
    check(!call.resume(bootstrap).completed && type_size_queries==final_type_queries && string_allocations==4 && field_size_queries==4 &&
        initial_lists.size()==3 && f.core->types->count==1 && object_type->reference_word==1,
        "Pending metadata probe replayed base publication, layout, inheritance or naming.");

    auto probe=std::make_shared<IgbMetadataProbeServices>();probe->metadata=f.services->metadata;probe->runtime=f.services->types->runtime;
    auto probe_local=allocation(0x750000,0x64);auto probe_type=std::make_shared<IgbRuntimeMetaType>();
    probe_local.storage->bindings.push_back({0,{},probe_type,{}});unsigned probe_locals{};
    probe->local_storage=[&]{++probe_locals;return probe_local;};
    f.remaining=make_native_igb_metadata_probe_factory(probe,f.remaining);
    check(!call.resume(bootstrap).completed && !call.faulted() && probe_locals==1,
        "Native metadata constructor probe did not reach its next actual default callback.");
    check(f.constructed->type_word==0x804761d8 && probe_type->reference_word==1 && !probe_type->fields &&
        probe_type->native_size==0xffffffffu && igb_bootstrap_read_word(probe_local,0)==0x804761d8,
        "Actual reset-only stack probe did not publish its captured vtable identity.");
    check(remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x8002a164 &&
        f.constructed->class_flags==9 && f.fields->count==2 && f.core->enabled==0,
        "Metadata probe skipped incomplete defaults or prematurely enabled reflection.");

    auto metadata_defaults=std::make_shared<IgbMetadataDefaultsServices>();metadata_defaults->metadata=f.services->metadata;
    metadata_defaults->schema=std::make_shared<IgbFieldSchemaServices>();metadata_defaults->schema->types=f.services->types;
    metadata_defaults->schema->pools=f.services->pools;metadata_defaults->schema->dependencies=object_defaults->dependencies;
    metadata_defaults->schema->fields=object_defaults->fields;metadata_defaults->schema->meta_fields=object_defaults->meta_fields;
    metadata_defaults->object_metadata=object_services->metadata;std::vector<IgbFieldSchemaCall> schema_calls;
    bool schema_constructors_ready{},enumeration_constructor_ready{},structure_constructor_ready{};
    const auto schema_constructors=make_native_igb_field_schema_factory(metadata_defaults->schema);
    metadata_defaults->schema->factory=[&](const IgbFieldSchemaCall& c){
        if(schema_constructors_ready && (c.function!=0x80031378 || enumeration_constructor_ready) &&
            (c.function!=0x8002450c || structure_constructor_ready)){
            const auto operation=schema_constructors(c);if(operation)return operation;}
        schema_calls.push_back(c);return std::shared_ptr<IgbFieldSchemaOperation>{};
    };
    f.remaining=make_native_igb_metadata_defaults_factory(metadata_defaults,f.remaining);
    check(!call.resume(bootstrap).completed && !call.faulted() && schema_calls.size()==1 &&
        schema_calls.back().kind==IgbFieldSchemaServiceKind::create_field && schema_calls.back().function==0x8002e1ac &&
        schema_calls.back().pool==f.pool,"Actual metadata defaults did not stop at the first original signed-int field factory.");
    check(f.fields->count==2 && f.constructed->class_flags==9 && f.core->enabled==0 && probe_locals==1 && descriptors.size()==2 &&
        string_allocations==4 && default_allocations==2 && initial_lists.size()==3 && f.core->types->count==1,
        "Unimplemented field factory fabricated schema completion or replayed the native cold prefix.");
    check(!call.resume(bootstrap).completed && schema_calls.size()==2 && probe_locals==1 && f.fields->count==2 && descriptors.size()==2,
        "Pending signed-int factory replayed the probe or inserted unconstructed descriptors.");

    // Continue with actual specialized constructors. Only physical allocation,
    // resize and pool ownership are prepared; no completed descriptor is seeded.
    object_defaults->meta_fields->fields=object_defaults->fields;
    object_defaults->meta_fields->bootstrap->factory=[&](const IgbBootstrapCall& c){
        check(c.kind==BK::pool_allocate && c.pool==f.pool &&
            (c.bytes==0x34 || c.bytes==0x38 || c.bytes==0x3c || c.bytes==0x44 || c.bytes==0x50),"Specialized allocation changed its original profile.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
            auto descriptor=std::make_shared<IgbDecodeRuntimeField>();
            auto physical=allocation(0x730000+static_cast<std::uint32_t>(descriptors.size())*0x100,c.bytes);
            physical.storage->bindings.push_back({0,{},{},{},{},descriptor});descriptors.push_back(descriptor);descriptor_allocations.push_back(physical);
            out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;
    };
    std::vector<IgbPointerListBacking> field_backings{
        {backing.storage->bytes,0x400000,0,{},f.fields->backing},
        {fields_backing.storage->bytes,0x720000,0,{},object_fields->backing}};
    unsigned field_growths{};
    object_defaults->fields->pointer_lists->factory=[&](const IgbPointerListCall& c){
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,out=op.get()]{
            bool found{};for(const auto& old:field_backings)if(old.address==c.backing.address && old.bytes==c.backing.bytes)found=true;
            if(c.kind!=IgbPointerListServiceKind::object_pool)check(found,"Field-list growth used unallocated backing.");
            if(c.kind==IgbPointerListServiceKind::allocation_pool || c.kind==IgbPointerListServiceKind::object_pool)out->pool_result=f.pool;
            else if(c.kind==IgbPointerListServiceKind::allocation_size){++field_size_queries;out->size_result=static_cast<std::uint32_t>(c.backing.bytes->bytes.size());}
            else if(c.kind==IgbPointerListServiceKind::reallocate){
                check(c.pool==f.pool && c.bytes>c.backing.bytes->bytes.size() && c.bytes<=128,"Unexpected physical field-list growth request.");
                auto grown=allocation(0x760000+(++field_growths)*0x100,c.bytes);grown.storage->bytes->initialized.assign(c.bytes,0);
                std::copy(c.backing.bytes->bytes.begin(),c.backing.bytes->bytes.end(),grown.storage->bytes->bytes.begin());
                std::copy(c.backing.bytes->initialized.begin(),c.backing.bytes->initialized.end(),grown.storage->bytes->initialized.begin());
                auto mapping=std::make_shared<IgbDecodeFieldRegistry::Storage>(c.bytes/4);
                std::copy(c.backing.fields->begin(),c.backing.fields->end(),mapping->begin());
                out->backing_result={grown.storage->bytes,igb_bootstrap_address(grown),0,{},mapping};field_backings.push_back(out->backing_result);
            }return MessengerCallResult{true,0,{}};};return op;
    };
    // Existing cold arena allocations are explicitly initialized zero bytes.
    // This fixture does not establish the original pool allocator's body.
    object_defaults->fields->factory=[&](const IgbFieldOperationCall& c){
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbFieldOperationKind::object_pool || c.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=f.pool;
            else if(c.kind==IgbFieldOperationKind::allocation_size){check(c.buffer.buffer!=nullptr,"Default-size query lost captured allocation.");return MessengerCallResult{true,static_cast<std::uint32_t>(c.buffer.buffer->bytes.size()),{}};}
            else if(c.kind==IgbFieldOperationKind::allocate_default){
                check(c.pool==f.pool && (c.bytes==1 || c.bytes==2 || c.bytes==4) && c.alignment==1,"Specialized default allocation changed its byte width or pool.");
                ++default_allocations;auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(c.bytes,0);bytes->initialized.assign(c.bytes,1);
                out->buffer_result={bytes,0};out->buffer_address=0x740000+default_allocations*0x100;
            }else throw std::runtime_error("Missing recovered field value operation");return MessengerCallResult{true,0,{}};};return op;
    };
    const auto string_allocator=native_strings->factory;unsigned string_growths{};
    native_strings->factory=[&,string_allocator](const IgbStringPoolCall& c){
        if(c.kind!=IgbStringPoolServiceKind::resize_table)return string_allocator(c);
        auto op=std::make_shared<IgbStringPoolOperation>();op->resume=[&,c,out=op.get()]{
            check(c.storage && c.capacity==c.storage->size()*2 && c.bytes==c.capacity*4,"String table resize changed its actual request.");
            ++string_growths;out->storage_result=std::make_shared<IgbStringTable::Storage>(*c.storage);out->storage_result->resize(c.capacity);
            return MessengerCallResult{true,0,{}};};return op;
    };
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> lazy_metadata;
    std::vector<std::shared_ptr<IgbDecodeFieldRegistry>> lazy_fields;
    std::vector<IgbBootstrapPointer> lazy_allocations,lazy_backings;
    f.services->metadata->bootstrap->factory=make_native_igb_bootstrap_pool_factory(f.services->pools,[&](const IgbBootstrapCall& c){
        if(c.kind!=BK::pool_allocate)return std::shared_ptr<IgbBootstrapOperation>{};
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
            check(f.core->enabled==0 && c.pool==f.pool,"Lazy metadata constructor changed the cold allocation gate.");
            IgbBootstrapPointer physical;
            if(c.bytes==0x64){auto type=std::make_shared<IgbRuntimeMetaType>();physical=allocation(0x790000+static_cast<std::uint32_t>(lazy_metadata.size())*0x100,c.bytes);
                physical.storage->bindings.push_back({0,{},type,{}});lazy_metadata.push_back(type);
                object_defaults->fields->pointer_lists->values.push_back({igb_bootstrap_address(physical),type,{}});}
            else{check(c.bytes==16,"Lazy metadata requested an unexpected allocation size.");auto fields=std::make_shared<IgbDecodeFieldRegistry>();
                physical=allocation(0x7a0000+static_cast<std::uint32_t>(lazy_fields.size())*0x100,c.bytes);
                physical.storage->bindings.push_back({0,{},{},fields});lazy_fields.push_back(fields);}
            lazy_allocations.push_back(physical);out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;
    });
    fields_services->factory=[&](const IgbFieldRegistryCall& c){
        auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==FK::object_pool)out->pool_result=f.pool;
            else{check(c.kind==FK::allocate_backing && c.pool==f.pool && c.bytes==16,"Lazy metadata field-list backing changed its original request.");
                auto physical=allocation(0x7b0000+static_cast<std::uint32_t>(lazy_backings.size())*0x100,16);physical.storage->bytes->initialized.assign(16,0);
                lazy_backings.push_back(physical);out->backing_result={physical,std::make_shared<IgbDecodeFieldRegistry::Storage>(4)};}
            return MessengerCallResult{true,0,{}};};return op;
    };
    fields_services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    metadata_defaults->field_list_metadata=std::shared_ptr<IgbRegistrationMetaSlot>(fields_services,&fields_services->metadata);
    native_lists->registry_metadata=metadata_defaults->field_list_metadata;
    native_lists->children_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    metadata_defaults->metadata_list_metadata=native_lists->children_metadata;
    auto values=std::make_shared<IgbFieldValuesServices>();values->fields=object_defaults->fields;
    values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    values->string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    values->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    object_defaults->fields->factory=make_native_igb_field_values_factory(values,object_defaults->fields->factory);
    metadata_defaults->factory=make_native_igb_metadata_string_default_factory(values);
    layout->factory=make_native_igb_field_alignment_factory();schema_constructors_ready=true;
    const auto completed_schema=call.resume(bootstrap);
    check(!completed_schema.completed && !call.faulted(),"Remaining cold bootstrap was silently completed after metadata registration.");
    check(f.fields->count==27 && descriptors.size()==27 && f.constructed->class_flags==15 && f.core->types->count==2 &&
        remaining_calls.back().kind==TK::append_callback && remaining_calls.back().function==0x8002dd18,
        "Complete native field schema did not reach the original deferred-registration append.");
    unsigned callback_backings{};const auto original_callbacks=f.services->callbacks->factory;
    f.services->callbacks->factory=[&,original_callbacks](const IgbRegistrationCallbacksCall& c){
        if(c.kind!=CK::allocate_backing)return original_callbacks(c);
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&,c,out=op.get()]{
            check(c.bytes==16 && !c.pool,"First deferred callback backing changed original cold arguments.");
            ++callback_backings;out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);return MessengerCallResult{true,0,{}};};return op;
    };
    f.remaining=make_native_igb_registration_list_factory(f.services->callbacks,f.remaining);
    const auto registered=call.resume(bootstrap);
    check(!registered.completed && !call.faulted() && registered.diagnostic.find("0x8002DD18")!=std::string::npos &&
        f.constructed->class_flags==7 && f.constructed->reference_word==1 && f.constructed->word_0c==1 &&
        f.core->types->count==2 && f.core->types->backing->at(1)==f.constructed && igb_buffer_word(grown_types.storage->bytes,4)==0x120000,
        "Native metadata registration did not finish before the next actual deferred class registration.");
    check(callback_backings==1 && root_callbacks->count==3 && root_callbacks->capacity==4 &&
        root_callbacks->backing->at(0)==0x8002dd18 && root_callbacks->backing->at(1)==0x8002e054 && root_callbacks->backing->at(2)==0x80066ae0,
        "Actual callback append did not publish the original deferred registration order.");
    check(f.constructed->native_size==0x64 && f.constructed->native_alignment==4 && f.constructed->allocation_prefix==0 &&
        f.constructed->type_word==0x804761d8 && f.core->enabled==0 && igb_bootstrap_read_byte(ark.allocation,0)==0 &&
        igb_bootstrap_read_word(ark.allocation,4)==0xcccccccc && igb_bootstrap_read_word(ark.allocation,8)==0xcccccccc,
        "Completed metadata changed its native layout or prematurely enabled/snapshotted the cold core.");
    check(default_allocations==27 && string_allocations==29 && field_growths==3 && string_growths==3 &&
        lazy_metadata.size()==2 && lazy_fields.size()==2 && lazy_allocations.size()==4 && lazy_backings.size()==2,
        "Complete native field schema skipped actual allocations, growth or lazy metadata constructors.");
    for(std::size_t i=0;i<25;++i){const auto descriptor=descriptors[i+2];
        const auto kind=igb_meta_field_kind_for_factory(*metadata_defaults->factories->at(i));check(kind.has_value(),"Schema factory has no verified profile.");
        check(f.fields->backing->at(i+2)==descriptor && igb_buffer_word(f.fields->backing_bytes,static_cast<std::uint32_t>((i+2)*4))==igb_bootstrap_address(descriptor_allocations[i+2]) &&
            descriptor->vtable_word==igb_meta_field_profile(*kind).vtable && descriptor_allocations[i+2].storage->bytes->bytes.size()==igb_meta_field_profile(*kind).bytes,
            "Schema descriptor lost its actual constructor, allocation size or physical list publication.");
        check(descriptor->name && *descriptor->name==**metadata_defaults->names->at(i) && descriptor->offset==*metadata_defaults->offsets->at(i) &&
            descriptor->ordinal==i+2 && descriptor->type_ordinal==0xffff && descriptor->reference_word==1 &&
            descriptor->declaring_type && descriptor->declaring_type->lock()==f.constructed &&
            *metadata_defaults->globals->at(i)->slot==descriptor,"Schema naming, layout, ownership or canonical global publication differs.");
    }
    const auto word_default=[&](std::size_t index){const auto& source=*descriptors[index+2]->default_bytes;return igb_buffer_word(source.buffer,source.offset);};
    check(word_default(0)==0xffffffffu && word_default(1)==0xffffffffu && word_default(2)==1 &&
        word_default(8)==0xffffffffu && word_default(18)==0xffffffffu && word_default(7)==0,
        "Native metadata scalar/string setters did not preserve their original defaults.");
    check(descriptors[7]->default_bytes->buffer->bytes==std::vector<std::uint8_t>{1} &&
        descriptors[24]->default_bytes->buffer->bytes==std::vector<std::uint8_t>({0,4}),"Boolean/short defaults were copied with another field width.");
    check(descriptors[12]->object_type->lock()==lazy_metadata[0] && descriptors[14]->object_type->lock()==lazy_metadata[1] &&
        descriptors[15]->object_type->lock()==lazy_metadata[1] && descriptors[16]->object_type->lock()==f.constructed &&
        descriptors[19]->object_type->lock()==object_type && lazy_metadata[0]->fields==lazy_fields[0] && lazy_metadata[1]->fields==lazy_fields[1] &&
        lazy_fields[0]->count==0 && lazy_fields[1]->count==0 && lazy_metadata[0]->class_flags==0 && lazy_metadata[1]->class_flags==0,
        "Metadata reference defaults were seeded instead of using native lazy constructors and canonical globals.");
    const auto final_queries=field_size_queries;
    check(!call.resume(bootstrap).completed && f.fields->count==27 && f.constructed->class_flags==7 && f.core->types->count==2 &&
        callback_backings==1 && descriptors.size()==27 && default_allocations==27 && string_allocations==29 && field_size_queries==final_queries,
        "Pending deferred class registration repeated completed constructors, schema, publication or callbacks.");

    auto list_registration=std::make_shared<IgbListRegistrationServices>();
    list_registration->lists=native_lists;list_registration->field_registries=fields_services;
    list_registration->metadata=f.services->metadata;list_registration->callbacks=f.services->callbacks;
    list_registration->types=f.services->types;list_registration->pools=f.services->pools;
    unsigned list_probe_locals{};std::vector<IgbBootstrapPointer> list_probe_storage;
    list_registration->local_storage=[&](IgbTypeListFactoryEntry entry){
        auto local=allocation(0x7c0000+(++list_probe_locals)*0x100,16);
        if(entry==IgbTypeListFactoryEntry::children)local.storage->bindings.push_back({0,{},{},{},std::make_shared<IgbRuntimeTypeList>()});
        else local.storage->bindings.push_back({0,{},{},std::make_shared<IgbDecodeFieldRegistry>()});
        list_probe_storage.push_back(local);return local;
    };
    // Bind the previously allocated canonical owners to their physical pointer
    // words before real parent attachment or registry publication reaches them.
    for(std::size_t i=0;i<lazy_metadata.size();++i){
        type_pointers->values.push_back({0x790000+static_cast<std::uint32_t>(i)*0x100,lazy_metadata[i],{}});
        field_backings.push_back({lazy_backings[i].storage->bytes,igb_bootstrap_address(lazy_backings[i]),0,{},lazy_fields[i]->backing});
    }
    additional_callbacks=make_native_igb_list_registration_function_factory(list_registration,
        make_native_igb_object_registration_function_factory(object_services));
    f.remaining=make_native_igb_list_registration_type_factory(list_registration,f.remaining);
    const auto list_defaults_boundary=call.resume(bootstrap);
    check(!list_defaults_boundary.completed && !call.faulted() &&
        remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x8002e108,
        "Actual deferred list registration did not reach its parent's original schema callback.");
    check(lazy_metadata[0]->class_flags==8 && lazy_metadata[1]->class_flags==9 && lazy_metadata[1]->parent==object_type &&
        lazy_metadata[1]->fields->count==2 && lazy_metadata[1]->native_size==16 && lazy_metadata[1]->type_word==0x80475c54 &&
        lazy_metadata[1]->name && *lazy_metadata[1]->name=="__internalNonRefCountedObjectList" && list_probe_locals==1,
        "Deferred list registration skipped inheritance, naming or its actual reset-only probe.");
    check(list_probe_storage.size()==1 && igb_bootstrap_read_word(list_probe_storage.front(),0)==0x80475c54 &&
        igb_bootstrap_read_word(list_probe_storage.front(),4)==1 && igb_bootstrap_read_word(list_probe_storage.front(),8)==0 &&
        igb_bootstrap_read_word(list_probe_storage.front(),12)==0 && initial_lists.size()==3 && descriptors.size()==27,
        "List probe allocated backing or substituted a prepared layout value.");
    auto list_defaults=std::make_shared<IgbListDefaultsServices>();list_defaults->lists=native_lists;
    list_defaults->schema=metadata_defaults->schema;list_defaults->values=values;
    std::vector<IgbListDefaultsCall> list_defaults_calls;
    IgbListDefaultsFactory reached_list_operations;
    bool unsigned_short_clone_ready{};
    list_defaults->factory=[&](const IgbListDefaultsCall& c){
        list_defaults_calls.push_back(c);
        if(c.kind==IgbListDefaultsServiceKind::clone_field && c.vtable_word==0x80477054 && !unsigned_short_clone_ready)
            return std::shared_ptr<IgbListDefaultsOperation>{};
        if(reached_list_operations)return reached_list_operations(c);
        return std::shared_ptr<IgbListDefaultsOperation>{};};
    values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    f.remaining=make_native_igb_list_defaults_factory(list_defaults,f.remaining);
    const auto prototype_boundary=call.resume(bootstrap);
    check(!prototype_boundary.completed && !call.faulted() && !list_defaults_calls.empty() &&
        list_defaults_calls.back().kind==IgbListDefaultsServiceKind::register_object_field && list_defaults_calls.back().function==0x800289f8,
        "Native list defaults skipped the required original object-field prototype registration.");
    check(lazy_fields[1]->count==4 && descriptors.size()==29 && default_allocations==27 &&
        lazy_fields[1]->backing->at(2)==descriptors[27] && lazy_fields[1]->backing->at(3)==descriptors[28] &&
        descriptors[27]->vtable_word==0x80476088 && descriptors[28]->vtable_word==0x80475b60 &&
        descriptor_allocations[27].storage->bytes->bytes.size()==0x50 && descriptor_allocations[28].storage->bytes->bytes.size()==0x34,
        "List batch did not construct and publish its actual memory-reference and signed-integer descriptors.");
    check(descriptors[27]->word_38==0 && descriptors[27]->element_field && descriptors[27]->element_field->expired() &&
        descriptors[27]->flag_40==1 && descriptors[27]->flag_41==1 && descriptors[27]->flag_42==1 &&
        !descriptors[27]->name && !descriptors[28]->name && !values->object_prototype.value() &&
        f.core->types->count==2 && f.core->enabled==0 && f.constructed->class_flags==7,
        "Pending prototype lookup fabricated field configuration, publication or completed bootstrap.");
    const auto final_field_queries=field_size_queries,final_string_allocations=string_allocations;
    check(!call.resume(bootstrap).completed && list_defaults_calls.size()==2 && descriptors.size()==29 &&
        field_size_queries==final_field_queries && string_allocations==final_string_allocations && list_probe_locals==1,
        "Pending object-field registration replayed completed list construction, append, inheritance or probe.");


    // Continue through the actual object/reference/base field registrations.
    // Newly allocated physical metadata and backing receive identity bindings;
    // the existing native constructors still initialize every represented field.
    auto field_types=std::make_shared<IgbFieldTypeRegistrationServices>();
    field_types->meta_fields=object_defaults->meta_fields;field_types->metadata=f.services->metadata;
    field_types->callbacks=f.services->callbacks;field_types->types=f.services->types;field_types->pools=f.services->pools;
    field_types->base_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    field_types->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    field_types->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=
        std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    values->signed_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    values->signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    const auto allocate_metadata=f.services->metadata->bootstrap->factory;
    f.services->metadata->bootstrap->factory=[&,allocate_metadata](const IgbBootstrapCall& c){
        auto op=allocate_metadata(c);if(!op || c.kind!=BK::pool_allocate)return op;
        const auto run=op->resume;op->resume=[&,run,out=op.get(),bound=false]()mutable{
            const auto status=run();if(status.completed && !bound){
                const auto physical=out->allocation_result;
                if(physical.storage)for(const auto& binding:physical.storage->bindings)if(binding.metadata)
                    type_pointers->values.push_back({igb_bootstrap_address(physical)+binding.offset,binding.metadata,{}});
                bound=true;
            }return status;};return op;
    };
    const auto allocate_field_backing=fields_services->factory;
    fields_services->factory=[&,allocate_field_backing](const IgbFieldRegistryCall& c){
        auto op=allocate_field_backing(c);if(!op || c.kind!=FK::allocate_backing)return op;
        const auto run=op->resume;op->resume=[&,run,out=op.get(),bound=false]()mutable{
            const auto status=run();if(status.completed && !bound){const auto& b=out->backing_result;
                field_backings.push_back({b.allocation.storage->bytes,igb_bootstrap_address(b.allocation),b.allocation.offset,{},b.fields});bound=true;
            }return status;};return op;
    };
    additional_callbacks=make_native_igb_field_type_registration_function_factory(field_types,additional_callbacks);
    f.remaining=make_native_igb_field_type_registration_type_factory(field_types,f.remaining);
    auto field_defaults=std::make_shared<IgbFieldTypeDefaultsServices>();field_defaults->registration=field_types;
    field_defaults->schema=metadata_defaults->schema;field_defaults->metadata=f.services;
    // The class-default publication and later list-name lookup address the
    // same original805617C4 cell, not a copied or prepopulated descriptor.
    field_defaults->tables[0].globals->at(1)->slot=std::shared_ptr<IgbRuntimeFieldSlot>(list_defaults,&list_defaults->name_descriptor);
    f.remaining=make_native_igb_field_type_defaults_factory(field_defaults,f.remaining);
    auto field_probe=std::make_shared<IgbFieldProbeServices>();field_probe->fields=object_defaults->meta_fields;
    field_probe->runtime=f.services->types->runtime;auto object_field_local=allocation(0x7d0000,0x44);
    auto object_field_view=std::make_shared<IgbDecodeRuntimeField>();object_field_local.storage->bindings.push_back({0,{},{},{},{},object_field_view});
    unsigned field_probe_locals{};field_probe->local_storage=[&]{++field_probe_locals;return object_field_local;};
    f.remaining=make_native_igb_field_probe_factory(field_probe,f.remaining);
    reached_list_operations=[&](const IgbListDefaultsCall& c){
        if(c.kind!=IgbListDefaultsServiceKind::register_object_field || c.function!=0x800289f8)return std::shared_ptr<IgbListDefaultsOperation>{};
        auto wrapper=std::make_shared<IgbRegistrationCallbacksInvocation>(0x80028a20);
        auto op=std::make_shared<IgbListDefaultsOperation>();op->resume=[&,wrapper]{return wrapper->resume(f.services->callbacks);};return op;
    };
    const auto field_chain=call.resume(bootstrap);
    check(!field_chain.completed && !call.faulted() && field_chain.diagnostic.find("0x80027454")!=std::string::npos,
        "Original field registration chain did not reach its required reference prototype callback.");
    const auto base_field=**field_types->base_metadata,reference_field=**field_types->reference_metadata;
    const auto object_field=**field_types->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)];
    check(base_field && reference_field && object_field && base_field->parent==object_type && reference_field->parent==base_field && object_field->parent==reference_field,
        "Field class inheritance did not preserve its three actual canonical metadata owners.");
    check(base_field->class_flags==7 && reference_field->class_flags==7 && object_field->class_flags==7 &&
        base_field->fields->count==20 && reference_field->fields->count==22 && object_field->fields->count==25 && descriptors.size()==52,
        "Field registration skipped original local descriptors, inheritance or publication.");
    check(base_field->native_size==0x34 && reference_field->native_size==0x38 && object_field->native_size==0x44 &&
        base_field->flag_1a==1 && reference_field->flag_1a==1 && object_field->flag_1a==0 && object_field->type_word==0x804762fc &&
        field_probe_locals==1 && object_field_view->vtable_word==0x80471914,
        "Abstract field registration invented a probe or object field registration skipped actual reset/cleanup.");
    const std::array<std::shared_ptr<IgbRuntimeMetaType>,3> field_classes={base_field,reference_field,object_field};
    const std::array<std::size_t,3> descriptor_starts={29,47,49},inherited_counts={2,20,22};
    for(std::size_t k=0;k<field_classes.size();++k){const auto& t=field_defaults->tables[k];const auto& type=field_classes[k];
        for(std::size_t i=0;i<t.factories->size();++i){const auto d=descriptors[descriptor_starts[k]+i];
            const auto kind=igb_meta_field_kind_for_factory(*t.factories->at(i));check(kind.has_value(),"Field class uses an unverified descriptor factory.");
            check(type->fields->backing->at(inherited_counts[k]+i)==d && *t.globals->at(i)->slot==d &&
                d->name && *d->name==**t.names->at(i) && d->offset==*t.offsets->at(i) && d->ordinal==inherited_counts[k]+i &&
                d->vtable_word==igb_meta_field_profile(*kind).vtable && d->declaring_type && d->declaring_type->lock()==type,
                "Registered field schema lost its constructor, canonical global, owner, name or layout.");
        }
    }
    check(descriptors[31]->default_bytes->buffer->bytes==std::vector<std::uint8_t>({0xff,0xff}) &&
        descriptors[32]->default_bytes->buffer->bytes==std::vector<std::uint8_t>({0xff,0xff}) &&
        descriptors[45]->default_bytes->buffer->bytes==std::vector<std::uint8_t>({2}) &&
        descriptors[34]->object_type->lock()==f.constructed && descriptors[49]->object_type->lock()==f.constructed,
        "Field class signed defaults or parent metadata references differ from original callers.");
    check(f.core->types->count==5 && f.core->types->backing->at(2)==base_field && f.core->types->backing->at(3)==reference_field &&
        f.core->types->backing->at(4)==object_field && lazy_metadata.size()==5 && lazy_fields.size()==5 &&
        default_allocations==50 && f.core->enabled==0 && !values->object_prototype.value() &&
        lazy_metadata[0]->class_flags==8 && lazy_metadata[1]->class_flags==9 && !descriptors[27]->name,
        "Field class completion fabricated prototype creation, list completion or enabled native startup.");
    const auto final_descriptors=descriptors.size(),final_metadata=lazy_metadata.size();
    const auto final_defaults=default_allocations,final_names=string_allocations;
    check(!call.resume(bootstrap).completed && descriptors.size()==final_descriptors && lazy_metadata.size()==final_metadata &&
        default_allocations==final_defaults && string_allocations==final_names && field_probe_locals==1 && f.core->types->count==5,
        "Pending reference prototype replayed registered classes or completed original bootstrap.");


    // The abstract reference class's real reflected factory returns null.
    // Its callback must finish before the concrete object prototype starts.
    auto prototypes=std::make_shared<IgbFieldPrototypeServices>();prototypes->registration=field_types;
    prototypes->values=values;prototypes->registries=fields_services;prototypes->schema=metadata_defaults->schema;
    bootstrap->reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    bootstrap->field_prototypes=std::shared_ptr<IgbDecodeFieldRegistry>{};
    prototypes->reference_prototype=std::shared_ptr<IgbRuntimeFieldSlot>(bootstrap,&bootstrap->reference_prototype);
    prototypes->registry=std::shared_ptr<std::optional<std::shared_ptr<IgbDecodeFieldRegistry>>>(bootstrap,&bootstrap->field_prototypes);
    prototypes->reflected=std::make_shared<IgbFileFactoryServices>();
    prototypes->reflected->initializer=std::make_shared<IgbObjectInitServices>();
    prototypes->reflected->initializer->runtime=f.services->types->runtime;
    prototypes->reflected->initializer->object_metadata=object_type;
    auto prototype_storage=allocation(0x7e0000,0x44);auto prototype_field=std::make_shared<IgbDecodeRuntimeField>();
    prototype_storage.storage->bindings.push_back({0,{},{},{},{},prototype_field});unsigned prototype_allocations{};
    prototypes->reflected->factory=[&](const IgbFileFactoryCall& c){
        check(c.kind==IgbFileFactoryServiceKind::allocate && c.metadata==object_field && c.pool==f.pool && c.bytes==0x44,
            "Actual abstract prototype allocated or concrete reflected factory changed its metadata/pool/size.");
        auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{++prototype_allocations;
            out->allocation_result=prototype_storage.storage;return MessengerCallResult{true,0,{}};};return op;
    };
    additional_callbacks=make_native_igb_field_prototype_function_factory(prototypes,additional_callbacks);
    const auto concrete_prototype=call.resume(bootstrap);
    check(!concrete_prototype.completed && !call.faulted() && concrete_prototype.diagnostic.find("operation")!=std::string::npos && prototype_allocations==1,
        "Native prototype callbacks did not finish the abstract null branch and reach actual concrete667F4.");
    check(bootstrap->reference_prototype && !*bootstrap->reference_prototype && bootstrap->field_prototypes && !*bootstrap->field_prototypes &&
        !values->object_prototype.value() && f.core->types->count==5 && f.core->enabled==0 && descriptors.size()==52 &&
        igb_bootstrap_read_word(prototype_storage,0)==0x804762fc && prototype_field->vtable_word==0x804762fc &&
        igb_bootstrap_read_word(prototype_storage,4)==1 && prototype_field->reference_word==1 &&
        igb_bootstrap_read_word(prototype_storage,8)==0xcccccccc &&
        default_allocations==50 && field_probe_locals==1,
        "Pending typed initializer lost header writes or fabricated member defaults, prototype publication or core activation.");
    check(!call.resume(bootstrap).completed && prototype_allocations==1 && descriptors.size()==52 && default_allocations==50 &&
        f.core->types->count==5 && !values->object_prototype.value(),
        "Pending concrete prototype restarted reflected allocation or earlier field registrations.");

    auto field_init=std::make_shared<IgbFieldInitServices>();field_init->registration=field_types;field_init->values=values;
    unsigned prototype_pool_queries{};
    prototypes->reflected->initializer->factory=make_native_igb_scalar_field_factory(make_native_igb_field_init_factory(field_init,
        [&](const IgbObjectInitCall& c){
            if(c.kind!=IgbObjectInitServiceKind::allocation_pool && c.kind!=IgbObjectInitServiceKind::pool_index)
                return std::shared_ptr<IgbObjectInitOperation>{};
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[&,c,out=op.get()]{
                ++prototype_pool_queries;if(c.kind==IgbObjectInitServiceKind::allocation_pool){
                    check(c.address==0x7e0000,"Reflected field initialization queried a wrong allocation base.");out->pool_result=f.pool;
                }else check(c.pool==f.pool,"Reflected field initialization changed its captured pool owner.");
                return MessengerCallResult{true,0,{}};};return op;
        }));
    const auto initialized_prototype=call.resume(bootstrap);
    check(!initialized_prototype.completed && !call.faulted() && values->object_prototype && *values->object_prototype==prototype_field,
        "Actual reflected field initialization did not publish its concrete prototype.");
    check(prototype_allocations==1 && prototype_pool_queries==2 && prototype_field->reference_word==1 &&
        prototype_field->offset==0 && prototype_field->type_ordinal==0 && prototype_field->ordinal==0 && prototype_field->size==4 &&
        prototype_field->default_mode==0 && prototype_field->flag_26==1 && prototype_field->flag_2b==1 && prototype_field->flag_2c==2 &&
        prototype_field->flag_34==0 && prototype_field->flag_35==0 && prototype_field->flag_3c==1 && prototype_field->word_40==0,
        "Reflected field constructor/default/finalizer sequence left incorrect canonical members.");
    check(prototype_field->default_bytes && prototype_field->default_bytes->buffer && prototype_field->default_bytes_address &&
        igb_bootstrap_read_word(prototype_storage,0x20)==*prototype_field->default_bytes_address &&
        igb_bootstrap_read_word(prototype_storage,0x10)==0 && igb_bootstrap_read_word(prototype_storage,4)==1 &&
        prototype_storage.storage->bytes->bytes[0x16]==0xcc && prototype_storage.storage->bytes->bytes[0x3d]==0xcc,
        "Reflected prototype physical image, published ordinals or untouched padding differs.");
    check(bootstrap->field_prototypes && *bootstrap->field_prototypes && (*bootstrap->field_prototypes)->count==1 &&
        (*bootstrap->field_prototypes)->backing->at(0)==prototype_field && object_field->instance_count==1,
        "Concrete prototype skipped its native instance count or prototype registry publication.");
    check(initialized_prototype.diagnostic.find("free80056378")!=std::string::npos,
        "Completed concrete prototype did not return to its original callback-list cleanup.");
    const auto callback_functions=additional_callbacks;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> freed_callback_lists;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList::Storage>> freed_callback_backings;
    additional_callbacks=[&,callback_functions](const IgbRegistrationCallbacksCall& c){
        if(c.kind!=CK::free_backing && c.kind!=CK::free_list)return callback_functions(c);
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&,c]{
            check(c.callbacks && !c.callbacks->released,"Callback allocator reached a released or null list.");
            if(c.kind==CK::free_backing){check(c.backing==c.callbacks->backing,"Callback cleanup changed its captured backing.");freed_callback_backings.push_back(c.backing);}
            else freed_callback_lists.push_back(c.callbacks);
            return MessengerCallResult{true,0,{}};};return op;
    };
    const auto list_clone=call.resume(bootstrap);
    std::cout<<"Cold field initializer boundary: "<<list_clone.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<'\n';
    check(!list_clone.completed && !call.faulted() && f.core->enabled==0 && values->object_prototype.value()==prototype_field,
        "Callback cleanup skipped remaining list registration or replayed the field prototype.");
    check(!freed_callback_lists.empty() && freed_callback_lists.size()==freed_callback_backings.size() &&
        std::all_of(freed_callback_lists.begin(),freed_callback_lists.end(),[](const auto& list){return list->released;}),
        "Native callback cleanup did not release each completed backing/list pair.");
    check(!list_defaults_calls.empty() && list_defaults_calls.back().kind==IgbListDefaultsServiceKind::clone_field &&
        list_defaults_calls.back().field==descriptors[27] && list_defaults_calls.back().vtable_word==0x80476088 &&
        list_defaults_calls.back().virtual_slot==0x54 && list_defaults_calls.back().argument==1,
        "Original list name/index search did not reach its actual inherited memory-field clone.");
    check(list_defaults->name_descriptor==descriptors[30] && lazy_metadata[1]->class_flags==7 && lazy_metadata[0]->class_flags==9 &&
        f.core->types->count==6 && f.core->types->backing->at(5)==lazy_metadata[1] && default_allocations==53 &&
        *list_defaults->globals->at(0)->slot==descriptors[27] && *list_defaults->globals->at(1)->slot==descriptors[28],
        "Native parent-list defaults/publication or canonical name-descriptor linkage differs.");
    const auto freed_lists=freed_callback_lists.size();
    check(!call.resume(bootstrap).completed && descriptors.size()==52 && default_allocations==53 && prototype_allocations==1 &&
        f.core->types->count==6 && freed_callback_lists.size()==freed_lists && (*bootstrap->field_prototypes)->count==1,
        "Pending clone repeated prototype construction, parent registration or callback cleanup.");

    // Continue this same disabled bootstrap through actual2ACD8/6331C.
    // Only pool selection and physical D8 allocation are prepared endpoints.
    auto clones=std::make_shared<IgbFieldCloneServices>();clones->fields=object_defaults->meta_fields;clones->values=values;
    clones->copy=std::make_shared<IgbFieldCopyServices>();clones->copy->fields=clones->fields;clones->copy->values=values;
    for(const auto& binding:allocated_names)igb_bind_field_string(values,binding.address,binding.owner);
    for(const auto& binding:type_pointers->values)if(binding.metadata)values->fields->pointer_lists->values.push_back(binding);
    clones->fields->bootstrap->default_pool_handle=0;
    clones->fields->bootstrap->factory=make_native_igb_bootstrap_pool_factory(f.services->pools,clones->fields->bootstrap->factory);
    unsigned clone_pool_queries{},clone_default_allocations{};bool clone_allocation_ready=false;
    clones->copy->factory=[&](const IgbFieldCopyCall& c){
        auto op=std::make_shared<IgbFieldCopyOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbFieldCopyServiceKind::tagged_pool){
                ++clone_pool_queries;check(c.handle==0,"Disabled field copy used a nonzero pool tag.");out->pool_result=f.pool;
            }else{
                check(c.kind==IgbFieldCopyServiceKind::allocate_default && c.pool==f.pool && c.bytes==4,
                    "Field copy changed its original D8 allocation or entered an enabled registration.");
                if(!clone_allocation_ready)return MessengerCallResult{false,0,"prepared clone default allocation"};
                ++clone_default_allocations;++default_allocations;
                auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(c.bytes,0xcc);bytes->initialized.assign(c.bytes,0);
                out->buffer_result={bytes,0};out->buffer_address=0x740000+default_allocations*0x100;
            }return MessengerCallResult{true,0,{}};};return op;
    };
    reached_list_operations=make_native_igb_list_field_clone_factory(clones,reached_list_operations);
    const auto suspended_copy=call.resume(bootstrap);
    check(!suspended_copy.completed && !call.faulted() && suspended_copy.diagnostic=="prepared clone default allocation" &&
        descriptors.size()==53 && clone_pool_queries==1 && clone_default_allocations==0 && default_allocations==53,
        "Native clone failed to retain its allocated copy and captured default allocator.");
    const auto inherited=descriptors[27],copied=descriptors[52];const auto copied_storage=descriptor_allocations[52];
    check(copied->vtable_word==0x80471914 && copied->name_string==inherited->name_string &&
        (*copied->name_string)->references==2 && copied->offset==inherited->offset && copied->size==4 &&
        lazy_fields[0]->backing->at(2)==inherited && inherited->reference_word==2 && f.core->types->count==6,
        "Suspended base copy lost its exact publication, string retain or original list membership.");
    check(!call.resume(bootstrap).completed && descriptors.size()==53 && clone_pool_queries==1 &&
        (*copied->name_string)->references==2,"Pending default allocation repeated descriptor allocation or name retention.");
    clone_allocation_ready=true;
    const auto after_clone=call.resume(bootstrap);
    std::cout<<"Cold field clone boundary: "<<after_clone.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<'\n';
    check(!after_clone.completed && !call.faulted() && f.core->enabled==1 && clone_default_allocations==1 &&
        after_clone.diagnostic.find("0x8002AAFC")!=std::string::npos,
        "Field clone was fabricated or incomplete bootstrap was reported complete.");
    check(f.core->types->count==7 && lazy_metadata[0]->class_flags==7 && lazy_metadata[1]->class_flags==7 &&
        f.core->types->backing->at(6)==lazy_metadata[0] && igb_bootstrap_read_byte(ark.allocation,0)==1 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1 &&
        descriptors.size()==53 && default_allocations==54 && (*bootstrap->field_prototypes)->count==1,
        "Completed fundamental registration failed to enable reflection and publish its original count snapshots.");
    check(copied->vtable_word==0x80476088 && copied->word_38==0x40 && copied->element_field &&
        copied->element_field->lock()==prototype_field && copied->flag_34==1 && copied->flag_35==1 &&
        copied->flag_40==1 && copied->flag_41==1 && copied->flag_42==1 && copied->word_48==0 && copied->word_4c==0,
        "Actual memory copy constructor lost configured element identity, size or flags.");
    check(copied->default_bytes && copied->default_bytes->buffer && inherited->default_bytes &&
        copied->default_bytes->buffer!=inherited->default_bytes->buffer &&
        copied->default_bytes->buffer->bytes==inherited->default_bytes->buffer->bytes &&
        igb_bootstrap_read_word(copied_storage,0x20)==copied->default_bytes_address &&
        igb_bootstrap_read_word(copied_storage,0x38)==0x40 && igb_bootstrap_read_word(copied_storage,0x3c)==0x7e0000,
        "Native clone lost its separate initialized default buffer or physical member publications.");
    check(lazy_fields[0]->backing->at(2)==copied && lazy_fields[1]->backing->at(2)==inherited &&
        *list_defaults->internal_data==copied && copied->global_slot_address==0x805619d0 &&
        igb_bootstrap_read_word(copied_storage,0x1c)==0x805619d0 && copied->reference_word==1 && inherited->reference_word==1 &&
        igb_bootstrap_read_word(copied_storage,4)==1 && igb_bootstrap_read_byte(copied_storage,0x42)==1,
        "Native replacement failed to preserve parent/child ownership, global publication or physical stores.");
    const auto after_types=f.core->types->count;const auto after_names=string_allocations,after_defaults=default_allocations;
    const auto after_descriptors=descriptors.size(),after_frees=freed_callback_lists.size();
    check(!call.resume(bootstrap).completed && f.core->types->count==after_types && string_allocations==after_names &&
        default_allocations==after_defaults && descriptors.size()==after_descriptors && freed_callback_lists.size()==after_frees &&
        clone_default_allocations==1 && (*copied->name_string)->references==2,
        "Later suspension replayed native clone, replacement, name retention or completed registration.");

    // Continue the real first enabled registration. Its prerequisite guard
    // enters ThreadManager before constructing igMetaEnum itself. Represent
    // only the original null BSS globals and raw allocation identities here.
    auto meta_enum=std::make_shared<IgbMetaEnumServices>();meta_enum->callbacks=f.services->callbacks;
    meta_enum->types=f.services->types;meta_enum->schema=metadata_defaults->schema;
    meta_enum->metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    unsigned enum_probe_locals{};meta_enum->local_storage=[&]{++enum_probe_locals;return allocation(0x820000,0x14);};
    additional_callbacks=make_native_igb_meta_enum_function_factory(meta_enum,additional_callbacks);
    f.remaining=make_native_igb_meta_enum_type_factory(meta_enum,f.remaining);
    const auto missing_prerequisites=call.resume(bootstrap);
    check(!missing_prerequisites.completed && !call.faulted() &&
        missing_prerequisites.diagnostic.find("80561488/489")!=std::string::npos && meta_enum->metadata->has_value() && !**meta_enum->metadata,
        "Enabled enum registration skipped its original prerequisite storage boundary.");
    f.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    f.services->callbacks->prerequisites->started=0;f.services->callbacks->prerequisites->initialized=0;
    auto thread=std::make_shared<IgbThreadRegistrationState>();thread->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    additional_callbacks=make_native_igb_thread_registration_function_factory(thread,f.services->callbacks,f.services->types,additional_callbacks);
    f.remaining=make_native_igb_thread_type_factory(thread,f.services->callbacks,f.services->types,f.remaining);

    auto metadata_init=std::make_shared<IgbMetadataInitServices>();metadata_init->metadata=f.services->metadata;
    metadata_init->reflected=prototypes->reflected;metadata_init->field_lists=fields_services;
    auto member_construct=std::make_shared<IgbObjectFieldConstructServices>();member_construct->reflected=prototypes->reflected;
    member_construct->values=values;field_init->object_construct=member_construct;
    std::vector<IgbObjectFieldConstructCall> member_calls;
    member_construct->factory=[&](const IgbObjectFieldConstructCall& c){
        member_calls.push_back(c);
        if(c.kind!=IgbObjectFieldConstructServiceKind::tagged_pool)return std::shared_ptr<IgbObjectFieldConstructOperation>{};
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&,c,out=op.get()]{
            check(c.handle==0 && c.object.metadata && igb_bootstrap_address({c.object.storage,c.object.offset})==0x800000,
                "Metadata member construction changed its captured tag or target allocation.");
            out->pool_result=f.pool;return MessengerCallResult{true,0,{}};};return op;
    };
    auto reflected_metadata=std::make_shared<IgbRuntimeMetaType>();
    auto reflected_fields=std::make_shared<IgbDecodeFieldRegistry>();reflected_fields->count=37;
    const auto reflected_metadata_storage=allocation(0x800000,0x64),reflected_fields_storage=allocation(0x810000,16);
    reflected_metadata_storage.storage->bindings.push_back({0,{},reflected_metadata,{}});
    reflected_fields_storage.storage->bindings.push_back({0,{},{},reflected_fields});
    std::vector<IgbFileFactoryCall> reflected_allocations;
    const auto original_reflected_allocator=prototypes->reflected->factory;
    prototypes->reflected->factory=[&,original_reflected_allocator](const IgbFileFactoryCall& c){
        if(c.kind!=IgbFileFactoryServiceKind::allocate || (c.metadata!=f.constructed && c.metadata!=lazy_metadata[0]))
            return original_reflected_allocator(c);
        reflected_allocations.push_back(c);auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,c,out=op.get()]{
            check(f.core->enabled==1 && c.pool==f.pool,"Enabled metadata allocation lost its actual reflection gate or pool.");
            if(c.metadata==f.constructed){check(c.bytes==0x64,"Reflected metadata changed its native allocation size.");out->allocation_result=reflected_metadata_storage.storage;}
            else{check(c.bytes==16,"Reflected field registry changed its native allocation size.");out->allocation_result=reflected_fields_storage.storage;}
            return MessengerCallResult{true,0,{}};};return op;
    };
    std::vector<IgbObjectInitCall> reflected_init_calls;
    prototypes->reflected->initializer->factory=make_native_igb_metadata_init_factory(metadata_init,
        make_native_igb_scalar_field_factory(make_native_igb_field_init_factory(field_init,[&](const IgbObjectInitCall& c){
            reflected_init_calls.push_back(c);
            if(c.kind!=IgbObjectInitServiceKind::allocation_pool && c.kind!=IgbObjectInitServiceKind::pool_index)
                return std::shared_ptr<IgbObjectInitOperation>{};
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[&,c,out=op.get()]{
                if(c.kind==IgbObjectInitServiceKind::allocation_pool){
                    check(c.address==0x800000 || c.address==0x810000,"Enabled initializer queried another physical allocation.");out->pool_result=f.pool;
                }else check(c.pool==f.pool,"Enabled initializer changed the captured allocation owner.");
                return MessengerCallResult{true,0,{}};};return op;
        })));
    f.services->metadata->factory=make_native_igb_reflected_metadata_factory(metadata_init,f.services->metadata->factory);
    const auto reflected_member=call.resume(bootstrap);
    std::cout<<"Cold enabled metadata boundary: "<<reflected_member.diagnostic<<"; types="<<f.core->types->count<<
        ", reflected allocations="<<reflected_allocations.size()<<'\n';
    check(!reflected_member.completed && !call.faulted() &&
        reflected_member.diagnostic=="Memory member construction requires its reached field or allocator service.",
        "Enabled metadata construction did not reach its nested field-list memory allocation boundary.");
    check(f.services->callbacks->prerequisites->initialized==1 && f.services->callbacks->prerequisites->started==1 &&
        thread->metadata && !*thread->metadata && meta_enum->metadata->has_value() && !**meta_enum->metadata && enum_probe_locals==0,
        "Pending prerequisite metadata fabricated ThreadManager or enum publication.");
    check(reflected_allocations.size()==2 && reflected_allocations[0].metadata==f.constructed &&
        reflected_allocations[1].metadata==lazy_metadata[0] && member_calls.size()==1 && reflected_init_calls.size()==4,
        "Positive object-member construction skipped reflected allocation or original owner/tag lookup.");
    check(igb_bootstrap_read_word(reflected_metadata_storage,0)==0x804761d8 &&
        igb_bootstrap_read_word(reflected_metadata_storage,4)==1 && igb_bootstrap_read_word(reflected_metadata_storage,0x28)==0 &&
        !reflected_metadata->fields && reflected_metadata->reference_word==1 &&
        igb_bootstrap_read_word(reflected_fields_storage,0)==0x80475cb0 && igb_bootstrap_read_word(reflected_fields_storage,4)==1 &&
        igb_bootstrap_read_word(reflected_fields_storage,8)==0 && igb_bootstrap_read_word(reflected_fields_storage,12)==0xcccccccc &&
        !reflected_fields->backing && !reflected_fields->backing_bytes && reflected_fields->count==37,
        "Nested reflected initialization seeded a completed field list or published its member before construction returned.");
    check(metadata_init->completed_allocations.empty() && member_construct->objects.empty() &&
        f.constructed->instance_count==0 && lazy_metadata[0]->instance_count==0 &&
        f.core->enabled==1 && f.core->types->count==after_types && descriptors.size()==after_descriptors &&
        default_allocations==after_defaults && prototype_allocations==1 && clone_default_allocations==1,
        "Incomplete nested construction changed completed type/prototype counts or exposed a returned instance.");
    const auto after_reflected_names=string_allocations;const auto after_reflected_frees=freed_callback_lists.size();
    check(!call.resume(bootstrap).completed && reflected_allocations.size()==2 && reflected_init_calls.size()==4 &&
        member_calls.size()==1 && metadata_init->completed_allocations.empty() && !reflected_metadata->fields &&
        string_allocations==after_reflected_names && freed_callback_lists.size()==after_reflected_frees &&
        descriptors.size()==after_descriptors && default_allocations==after_defaults,
        "Pending nested memory construction replayed reflected allocation, header passes or prerequisite registration.");

    // Continue the retained62524 call through real element virtuals, ancestry,
    // aligned allocation, conditional zero fill and typed member publication.
    // Status globals are explicit preceding startup stores:8003D004 writes0
    // to805622D0 and8003D00C writes1 to805622D4 before ArkCore construction.
    auto memory_construct=std::make_shared<IgbMemoryFieldConstructServices>();memory_construct->values=values;
    memory_construct->reference_metadata=field_types->reference_metadata;
    memory_construct->status_success=0;memory_construct->status_failure=1;
    field_init->memory_construct=memory_construct;
    const auto reflected_backing=allocation(0x830000,64);reflected_backing.storage->bytes->initialized.assign(64,0);
    const auto reflected_mapping=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);
    bool memory_allocation_ready{};unsigned memory_allocation_polls{},memory_allocations{},memory_pool_queries{};
    std::vector<IgbMemoryFieldConstructCall> memory_calls;
    const auto memory_elements=make_native_igb_memory_field_virtual_factory(field_types,[&](const IgbMemoryFieldConstructCall& c){
        auto op=std::make_shared<IgbMemoryFieldConstructOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool){
                ++memory_pool_queries;check(c.handle==0 && c.object.fields==reflected_fields &&
                    igb_bootstrap_address({c.object.storage,c.object.offset})==0x810000,
                    "Nested memory construction changed its captured object/tag.");out->pool_result=f.pool;
            }else{
                check(c.kind==IgbMemoryFieldConstructServiceKind::allocate && c.pool==f.pool && c.bytes==64 && c.alignment==4,
                    "Native memory field changed the element-derived allocation size, alignment or pool.");
                ++memory_allocation_polls;if(!memory_allocation_ready)return MessengerCallResult{false,0,"prepared aligned field-list backing allocation"};
                ++memory_allocations;out->backing_result={reflected_backing.storage->bytes,igb_bootstrap_address(reflected_backing),0,{},reflected_mapping};
                field_backings.push_back(out->backing_result);
            }return MessengerCallResult{true,0,{}};};return op;
    });
    memory_construct->factory=[&,memory_elements](const IgbMemoryFieldConstructCall& c){memory_calls.push_back(c);return memory_elements(c);};
    // This is the already allocated metadata's arithmetic identity, not a
    // constructed class or a replacement for its pending reflected initializer.
    type_pointers->values.push_back({0x800000,reflected_metadata,{}});
    object_defaults->fields->pointer_lists->values.push_back({0x800000,reflected_metadata,{}});
    const auto pending_memory=call.resume(bootstrap);
    check(!pending_memory.completed && !call.faulted() && pending_memory.diagnostic=="prepared aligned field-list backing allocation" &&
        memory_pool_queries==1 && memory_allocation_polls==1 && memory_allocations==0 && memory_calls.size()==5,
        "Native62524/62BB8 did not retain its exact aligned allocator boundary.");
    check(memory_calls[0].kind==IgbMemoryFieldConstructServiceKind::field_virtual && memory_calls[0].field==prototype_field &&
        memory_calls[0].virtual_slot==0x64 && memory_calls[2].field==prototype_field && memory_calls[2].virtual_slot==0x64 &&
        memory_calls[3].field==prototype_field && memory_calls[3].virtual_slot==0x6c && !reflected_fields->backing &&
        !reflected_metadata->fields && igb_bootstrap_read_word(reflected_fields_storage,8)==0 &&
        metadata_init->completed_allocations.empty(),"Memory construction skipped independent element reads or published before allocation.");
    check(!call.resume(bootstrap).completed && memory_allocation_polls==2 && memory_calls.size()==5 && memory_pool_queries==1 &&
        reflected_allocations.size()==2,"Pending aligned allocation replayed selected size, alignment, pool or reflected allocations.");
    memory_allocation_ready=true;
    const auto initialized_metadata=call.resume(bootstrap);
    std::cout<<"Cold native metadata completion boundary: "<<initialized_metadata.diagnostic<<"; types="<<f.core->types->count<<
        ", field-list bytes="<<reflected_backing.storage->bytes->bytes.size()<<'\n';
    check(!initialized_metadata.completed && !call.faulted() && !remaining_calls.empty() &&
        remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x80023940,
        "Completed native metadata/list construction did not reach actual ThreadManager defaults80023940.");
    check(memory_allocations==1 && memory_allocation_polls==3 && memory_pool_queries==1 && memory_calls.size()==6 &&
        memory_calls.back().kind==IgbMemoryFieldConstructServiceKind::field_virtual && memory_calls.back().virtual_slot==0x58 &&
        memory_calls.back().field==prototype_field && reflected_fields->backing==reflected_mapping &&
        reflected_fields->backing_bytes==reflected_backing.storage->bytes && reflected_fields->backing_address==0x830000 &&
        igb_bootstrap_read_word(reflected_fields_storage,8)==0x830000,
        "Actual element ancestry/allocation did not publish the same physical and typed list backing.");
    check(reflected_metadata->fields==reflected_fields && igb_bootstrap_read_word(reflected_metadata_storage,0x28)==0x810000 &&
        reflected_fields->reference_word==1 && igb_bootstrap_read_word(reflected_fields_storage,4)==1 &&
        metadata_init->completed_allocations.size()==1 && metadata_init->completed_allocations.front().metadata==reflected_metadata &&
        member_construct->objects.size()==1 && member_construct->objects.front().fields==reflected_fields &&
        f.constructed->instance_count==1 && lazy_metadata[0]->instance_count==1,
        "Positive object-member assignment skipped publication, retain/release balance or completed instance ownership.");
    check(thread->metadata==reflected_metadata && reflected_metadata->class_flags==9 && reflected_metadata->parent==object_type &&
        reflected_metadata->name && *reflected_metadata->name=="igThreadManager" && reflected_metadata->native_size==0x10 &&
        reflected_metadata->flag_1a==1 && reflected_metadata->field_callback==0x80023920 && reflected_fields->count==2 &&
        reflected_mapping->at(0)==descriptors[0] && reflected_mapping->at(1)==descriptors[1] &&
        igb_buffer_word(reflected_backing.storage->bytes,0)==igb_bootstrap_address(descriptor_allocations[0]) &&
        igb_buffer_word(reflected_backing.storage->bytes,4)==igb_bootstrap_address(descriptor_allocations[1]),
        "Native constructor result did not feed the original prerequisite registrar and inherited field append.");
    for(std::size_t i=8;i<64;++i)check(reflected_backing.storage->bytes->initialized[i]==1 && reflected_backing.storage->bytes->bytes[i]==0,
        "Reference element ancestry did not zero untouched pointer storage before inheritance appended fields.");
    check(!**meta_enum->metadata && enum_probe_locals==0 && f.core->types->count==after_types && f.core->enabled==1 &&
        descriptors.size()==after_descriptors && default_allocations==after_defaults && prototype_allocations==1 && clone_default_allocations==1,
        "Pending ThreadManager fields fabricated enum completion or replayed existing schemas/prototypes.");
    const auto final_enabled_names=string_allocations;const auto final_enabled_frees=freed_callback_lists.size();
    const auto final_enabled_memory_calls=memory_calls.size();
    check(!call.resume(bootstrap).completed && memory_allocations==1 && memory_calls.size()==final_enabled_memory_calls &&
        reflected_allocations.size()==2 && metadata_init->completed_allocations.size()==1 &&
        f.constructed->instance_count==1 && lazy_metadata[0]->instance_count==1 && reflected_fields->count==2 &&
        string_allocations==final_enabled_names && freed_callback_lists.size()==final_enabled_frees &&
        descriptors.size()==after_descriptors && default_allocations==after_defaults,
        "Later prerequisite suspension replayed native memory construction, assignment, metadata initialization or inheritance.");

    // Further enabled factories receive new raw allocations and explicit typed
    // identities. Their headers, members and child lists remain the work of
    // the same reflected initializer used above.
    std::vector<IgbObjectInstance> enabled_allocations;
    std::shared_ptr<IgbNamedTypeDefaultsServices> named_defaults;
    std::shared_ptr<IgbPlatformThreadRegistrationServices> platform_thread;
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> platform_semaphore;
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> gamecube_thread;
    bool bind_new_instance_addresses{};
    std::vector<IgbPointerListBacking> enabled_backings;
    std::shared_ptr<IgbRegistrationMetaSlot> signed_int_cell,boolean_cell,signed_byte_cell;
    std::vector<IgbBootstrapPointer> signed_int_prototype_allocations,boolean_prototype_allocations,signed_byte_prototype_allocations;
    bool enabled_metadata_ready{};unsigned enabled_metadata_polls{};
    std::vector<IgbFileFactoryCall> unbound_reflected_allocations;
    IgbFileFactoryFactory resumed_reflected_allocations;
    prototypes->reflected->factory=[&](const IgbFileFactoryCall& c){
        check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && f.core->enabled==1,
            "Further reflected allocation changed its operation, pool or enabled runtime.");
        const bool known=c.metadata==f.constructed || c.metadata==lazy_metadata[0] || c.metadata==object_field ||
                (signed_int_cell && *signed_int_cell && c.metadata==**signed_int_cell) ||
                (boolean_cell && *boolean_cell && c.metadata==**boolean_cell) ||
                (signed_byte_cell && *signed_byte_cell && c.metadata==**signed_byte_cell);
        if(!known){unbound_reflected_allocations.push_back(c);
            return resumed_reflected_allocations?resumed_reflected_allocations(c):std::shared_ptr<IgbFileFactoryOperation>{};}
        reflected_allocations.push_back(c);auto op=std::make_shared<IgbFileFactoryOperation>();
        op->resume=[&,c,out=op.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            if(c.metadata==f.constructed){++enabled_metadata_polls;
                if(!enabled_metadata_ready)return MessengerCallResult{false,0,"prepared next metadata allocation"};}
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;
            if(c.metadata==f.constructed){
                check(c.bytes==0x64,"Lazy enabled metadata changed its allocation size.");
                instance.metadata=std::make_shared<IgbRuntimeMetaType>();
                raw.storage->bindings.push_back({0,{},instance.metadata});
                type_pointers->values.push_back({igb_bootstrap_address(raw),instance.metadata,{}});
                values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),instance.metadata,{}});
                if(named_defaults)named_defaults->physical_metadata.push_back(raw);
                if(platform_thread)platform_thread->physical_metadata.push_back(raw);
                if(platform_semaphore)platform_semaphore->physical_metadata.push_back(raw);
                if(gamecube_thread)gamecube_thread->physical_metadata.push_back(raw);
            }else if(c.metadata==lazy_metadata[0]){
                check(c.bytes==16,"Lazy enabled field list changed its allocation size.");
                instance.fields=std::make_shared<IgbDecodeFieldRegistry>();instance.fields->count=37;
                raw.storage->bindings.push_back({0,{},{},instance.fields});
            }else{
                const bool integer=signed_int_cell && *signed_int_cell && c.metadata==**signed_int_cell;
                const bool boolean=boolean_cell && *boolean_cell && c.metadata==**boolean_cell;
                const bool byte=signed_byte_cell && *signed_byte_cell && c.metadata==**signed_byte_cell;
                check(c.bytes==((integer||boolean||byte)?0x34u:0x44u),"Enabled descriptor factory changed its allocation size.");
                instance.field=std::make_shared<IgbDecodeRuntimeField>();
                raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
                if(integer && values->signed_int_prototype && !*values->signed_int_prototype)
                    signed_int_prototype_allocations.push_back(raw);
                else if(boolean && values->boolean_prototype && !*values->boolean_prototype)
                    boolean_prototype_allocations.push_back(raw);
                else if(byte && values->signed_byte_prototype && !*values->signed_byte_prototype)
                    signed_byte_prototype_allocations.push_back(raw);
                else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
                values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            }
            enabled_allocations.push_back(instance);
            if(bind_new_instance_addresses)igb_bind_object_field_instance(member_construct,instance);
            out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    const auto enabled_identity=[&](const IgbObjectInstance& object){return std::any_of(enabled_allocations.begin(),enabled_allocations.end(),
        [&](const auto& known){return known.storage==object.storage && known.offset==object.offset &&
            known.metadata==object.metadata && known.fields==object.fields && known.field==object.field &&
            known.types==object.types && known.file==object.file && known.runtime_object==object.runtime_object;});};
    member_construct->factory=[&](const IgbObjectFieldConstructCall& c){
        member_calls.push_back(c);
        if(c.kind!=IgbObjectFieldConstructServiceKind::tagged_pool)return std::shared_ptr<IgbObjectFieldConstructOperation>{};
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&,c,out=op.get()]{
            check(c.handle==0 && c.object.metadata && enabled_identity(c.object),
                "Enabled metadata member construction lost its fresh allocation identity.");
            out->pool_result=f.pool;return MessengerCallResult{true,0,{}};};return op;
    };
    prototypes->reflected->initializer->factory=make_native_igb_metadata_init_factory(metadata_init,
        make_native_igb_scalar_field_factory(make_native_igb_field_init_factory(field_init,[&](const IgbObjectInitCall& c){
            reflected_init_calls.push_back(c);
            if(c.kind!=IgbObjectInitServiceKind::allocation_pool && c.kind!=IgbObjectInitServiceKind::pool_index)
                return std::shared_ptr<IgbObjectInitOperation>{};
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[&,c,out=op.get()]{
                if(c.kind==IgbObjectInitServiceKind::allocation_pool){
                    check(enabled_identity(c.object) && c.address==igb_bootstrap_address({c.object.storage,c.object.offset}),
                        "Enabled initializer queried another allocation base.");out->pool_result=f.pool;
                }else check(c.pool==f.pool,"Enabled initializer changed its captured allocator.");
                return MessengerCallResult{true,0,{}};};return op;
        })));
    const auto subsequent_elements=make_native_igb_memory_field_virtual_factory(field_types,[&](const IgbMemoryFieldConstructCall& c){
        auto op=std::make_shared<IgbMemoryFieldConstructOperation>();op->resume=[&,c,out=op.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            if(c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool){
                check(c.handle==0 && c.object.fields && enabled_identity(c.object),
                    "Enabled memory construction changed its fresh field-list identity.");
                ++memory_pool_queries;out->pool_result=f.pool;
            }else{
                check(c.kind==IgbMemoryFieldConstructServiceKind::allocate && c.pool==f.pool && c.bytes==64 && c.alignment==4,
                    "Enabled memory construction changed its original aligned backing request.");
                auto raw=allocation(0xa00000+static_cast<std::uint32_t>(enabled_backings.size())*0x100,c.bytes);
                raw.storage->bytes->initialized.assign(c.bytes,0);
                out->backing_result={raw.storage->bytes,igb_bootstrap_address(raw),0,{},std::make_shared<IgbDecodeFieldRegistry::Storage>(c.bytes/4)};
                enabled_backings.push_back(out->backing_result);field_backings.push_back(out->backing_result);
                ++memory_allocations;++memory_allocation_polls;
            }done=true;return MessengerCallResult{true,0,{}};};return op;
    });
    memory_construct->factory=[&,subsequent_elements](const IgbMemoryFieldConstructCall& c){memory_calls.push_back(c);return subsequent_elements(c);};
    object_defaults->meta_fields->callbacks=f.services->callbacks;
    object_defaults->meta_fields->factory=make_native_igb_reflected_meta_field_factory(prototypes->reflected,object_defaults->meta_fields->factory);
    auto thread_dependencies=std::make_shared<IgbThreadDependenciesServices>();thread_dependencies->types=f.services->types;
    thread_dependencies->pools=f.services->pools;thread_dependencies->metadata=f.services->metadata;
    thread_dependencies->thread_list=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    thread_dependencies->semaphore=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    auto thread_defaults=std::make_shared<IgbThreadDefaultsServices>();thread_defaults->thread=thread;
    thread_defaults->dependencies=thread_dependencies;thread_defaults->callbacks=f.services->callbacks;
    thread_defaults->schema=metadata_defaults->schema;thread_defaults->physical_metadata.push_back(reflected_metadata_storage);
    f.remaining=make_native_igb_thread_defaults_factory(thread_defaults,f.remaining);
    const auto pending_dependency=call.resume(bootstrap);
    std::cout<<"Cold ThreadManager dependency boundary: "<<pending_dependency.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<'\n';
    check(!pending_dependency.completed && !call.faulted() && pending_dependency.diagnostic=="prepared next metadata allocation" &&
        enabled_metadata_polls==1 && enabled_allocations.size()==2 && reflected_allocations.size()==5,
        "Thread defaults did not reach the retained first lazy metadata allocation through two reflected descriptors.");
    check(descriptors.size()==55 && reflected_fields->count==4 && reflected_mapping->at(2)==descriptors[53] &&
        reflected_mapping->at(3)==descriptors[54] && object_field->instance_count==3 && default_allocations==56 &&
        reflected_metadata->flag_18==1 && igb_bootstrap_read_byte(reflected_metadata_storage,0x18)==1 &&
        reflected_metadata->factory_redirect==0 && !**thread_dependencies->thread_list && !**thread_dependencies->semaphore &&
        thread_dependencies->allocations.empty() && metadata_init->completed_allocations.size()==1 && f.core->types->count==7,
        "Pending lazy allocation fabricated dependency metadata, skipped field construction or published the redirect early.");
    const auto pending_dependency_names=string_allocations;const auto pending_dependency_callbacks=freed_callback_lists.size();
    check(!call.resume(bootstrap).completed && enabled_metadata_polls==2 && enabled_allocations.size()==2 &&
        reflected_allocations.size()==5 && descriptors.size()==55 && reflected_fields->count==4 && default_allocations==56 &&
        string_allocations==pending_dependency_names && freed_callback_lists.size()==pending_dependency_callbacks,
        "Pending lazy metadata allocation replayed constructor selection, field append or earlier defaults.");
    std::vector<std::uint32_t> thread_callback_trace;
    const auto before_thread_callbacks=additional_callbacks;
    additional_callbacks=[&,before_thread_callbacks](const IgbRegistrationCallbacksCall& c){
        if(c.kind==CK::invoke_function)thread_callback_trace.push_back(c.function);
        return before_thread_callbacks?before_thread_callbacks(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
    };
    enabled_metadata_ready=true;
    const auto completed_thread=call.resume(bootstrap);
    std::cout<<"Cold ThreadManager registration boundary: "<<completed_thread.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!completed_thread.completed && !call.faulted() && completed_thread.diagnostic.find("0x80023F28")!=std::string::npos &&
        !thread_callback_trace.empty() && thread_callback_trace.back()==0x80023f28 &&
        std::find(thread_callback_trace.begin(),thread_callback_trace.end(),0x80023a30)==thread_callback_trace.end() &&
        std::find(thread_callback_trace.begin(),thread_callback_trace.end(),0x80023a50)==thread_callback_trace.end(),
        "Completed ThreadManager defaults did not stop at the actual first deferred dependency registration.");
    const auto thread_list_metadata=**thread_dependencies->thread_list,semaphore_metadata=**thread_dependencies->semaphore;
    check(thread_list_metadata && semaphore_metadata && thread_list_metadata!=semaphore_metadata &&
        thread_dependencies->allocations.size()==2 && enabled_allocations.size()==6 && enabled_backings.size()==2 &&
        reflected_allocations.size()==8 && metadata_init->completed_allocations.size()==3 && memory_allocations==3 &&
        f.constructed->instance_count==3 && lazy_metadata[0]->instance_count==3 && object_field->instance_count==3,
        "Lazy dependency getters did not construct distinct native metadata and nested field-list instances.");
    for(std::size_t i=0;i<2;++i){const auto dependency=i==0?thread_list_metadata:semaphore_metadata;
        const auto descriptor=descriptors[53+i];const auto raw=descriptor_allocations[53+i];
        check(dependency->class_flags==0 && dependency->fields && dependency->fields->count==0 &&
            dependency->fields->backing_bytes==enabled_backings[i].bytes && dependency->fields->backing==enabled_backings[i].fields &&
            dependency->fields->backing_address==enabled_backings[i].address && dependency->native_size==0xffffffffu,
            "Lazy dependency allocation fabricated registration or skipped actual reflected member defaults.");
        check(descriptor->object_type && descriptor->object_type->lock()==dependency && descriptor->flag_34==1 &&
            igb_bootstrap_read_word(raw,0x38)==igb_bootstrap_address(thread_dependencies->allocations[i]) &&
            igb_bootstrap_read_byte(raw,0x34)==1 && descriptor->declaring_type && descriptor->declaring_type->lock()==reflected_metadata &&
            descriptor->name && *descriptor->name==**thread_defaults->names->at(i) && descriptor->offset==8+i*4 &&
            *thread_defaults->globals->at(i)->slot==descriptor && descriptor->ordinal==i+2,
            "Thread defaults lost physical type/construct stores, canonical schema publication or captured inheritance indices.");
        for(std::size_t at=0;at<64;++at)check(enabled_backings[i].bytes->initialized[at]==1 && enabled_backings[i].bytes->bytes[at]==0,
            "Lazy metadata's field list was preconstructed or lost its original zero-filled empty storage.");
    }
    check(reflected_metadata->class_flags==7 && reflected_metadata->factory_redirect==0x80023a10 &&
        igb_bootstrap_read_word(reflected_metadata_storage,0x3c)==0x80023a10 && f.core->types->count==8 &&
        f.core->types->backing->at(7)==reflected_metadata && f.core->bootstrap_callbacks && *f.core->bootstrap_callbacks &&
        (*f.core->bootstrap_callbacks)->count==1 && (*f.core->bootstrap_callbacks)->backing->at(0)==0x80023a50 &&
        root_callbacks && *root_callbacks->backing==IgbRegistrationCallbackList::Storage({0x80023f28,0x80026098,0x80023a30,0}) &&
        root_callbacks->count==3 && !root_callbacks->released && !**meta_enum->metadata && enum_probe_locals==0,
        "Thread completion mixed its class callbacks with the ArkCore bootstrap queue or completed the pending enum.");
    const auto completed_thread_names=string_allocations,completed_thread_defaults=default_allocations;
    const auto completed_thread_frees=freed_callback_lists.size(),completed_thread_memory=memory_calls.size();
    check(!call.resume(bootstrap).completed && enabled_allocations.size()==6 && enabled_backings.size()==2 && descriptors.size()==55 &&
        reflected_allocations.size()==8 && thread_dependencies->allocations.size()==2 && f.core->types->count==8 &&
        reflected_fields->count==4 && object_field->instance_count==3 && f.constructed->instance_count==3 && lazy_metadata[0]->instance_count==3 &&
        (*f.core->bootstrap_callbacks)->count==1 && string_allocations==completed_thread_names && default_allocations==completed_thread_defaults &&
        freed_callback_lists.size()==completed_thread_frees && memory_calls.size()==completed_thread_memory,
        "Pending dependency registration replayed ThreadManager defaults, lazy construction, publication or callback append.");

    // Resume the same actual deferred callback through its real parent chain.
    // DataList is the fifth child of igObject. Provide the reached allocator
    // reallocation while leaving growth, zero fill and append to native code.
    std::optional<IgbPointerListBacking> grown_object_children;
    const auto original_type_allocator=type_pointers->factory;
    type_pointers->factory=[&,original_type_allocator](const IgbPointerListCall& c){
        const bool grown_query=c.kind==IgbPointerListServiceKind::allocation_size && grown_object_children &&
            c.backing.address==grown_object_children->address;
        if(c.kind!=IgbPointerListServiceKind::reallocate && !grown_query)return original_type_allocator(c);
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,grown_query,out=op.get()]{
            if(grown_query){check(c.backing.bytes==grown_object_children->bytes && c.pool==f.pool,
                "Grown child-list query lost its physical allocation.");out->size_result=32;}
            else{
                check(!grown_object_children && object_type->children && c.types==*object_type->children && c.types->count==4 &&
                    c.pool==f.pool && c.bytes==32 && c.backing.bytes && c.backing.bytes->bytes.size()==16 && c.backing.types,
                    "Fifth object child changed the original growth request.");
                auto raw=allocation(0xb20000,c.bytes);raw.storage->bytes->initialized.assign(c.bytes,0);
                std::copy(c.backing.bytes->bytes.begin(),c.backing.bytes->bytes.end(),raw.storage->bytes->bytes.begin());
                std::copy(c.backing.bytes->initialized.begin(),c.backing.bytes->initialized.end(),raw.storage->bytes->initialized.begin());
                auto mapping=std::make_shared<IgbRuntimeTypeList::Storage>(8);
                std::copy(c.backing.types->begin(),c.backing.types->end(),mapping->begin());
                grown_object_children=IgbPointerListBacking{raw.storage->bytes,igb_bootstrap_address(raw),0,mapping,{}};
                out->backing_result=*grown_object_children;
            }return MessengerCallResult{true,0,{}};
        };return op;
    };
    auto data_registration=std::make_shared<IgbDataListRegistrationServices>();
    data_registration->data_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    data_registration->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    data_registration->dependencies=thread_dependencies;data_registration->types=f.services->types;
    data_registration->callbacks=f.services->callbacks;data_registration->pools=f.services->pools;data_registration->metadata=f.services->metadata;
    std::vector<IgbBootstrapPointer> data_probe_locals;
    data_registration->local_storage=[&](IgbDataListKind kind){
        check(kind==IgbDataListKind::data_list,"Parent-chain probe bypassed unfinished DataList registration.");
        auto local=allocation(0xb00000+static_cast<std::uint32_t>(data_probe_locals.size())*0x100,0x14);
        data_probe_locals.push_back(local);return local;
    };
    auto data_defaults=std::make_shared<IgbDataListDefaultsServices>();data_defaults->registration=data_registration;
    data_defaults->unsigned_char_prototype=std::shared_ptr<IgbRuntimeFieldSlot>(values,&values->unsigned_byte_prototype);
    data_defaults->lists=list_defaults;data_defaults->callbacks=f.services->callbacks;
    *data_defaults->unsigned_char_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    additional_callbacks=make_native_igb_data_list_registration_function_factory(data_registration,additional_callbacks);
    f.remaining=make_native_igb_data_list_registration_type_factory(data_registration,
        make_native_igb_data_list_defaults_factory(data_defaults,f.remaining));
    const auto integer_cell_boundary=call.resume(bootstrap);
    std::cout<<"Cold DataList integer registration boundary: "<<integer_cell_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!integer_cell_boundary.completed && !call.faulted() && data_probe_locals.size()==1 && descriptors.size()==55 &&
        enabled_allocations.size()==10 && enabled_backings.size()==4 && f.core->types->count==8,
        "DataList parent chain skipped original metadata/list allocation or entered an invented ready field class.");
    const auto data_type=**data_registration->data_metadata,nonref_type=**data_registration->object_metadata;
    check(data_type && nonref_type && data_type!=nonref_type && thread_list_metadata->class_flags==8 && nonref_type->class_flags==8 &&
        data_type->class_flags==9 && data_type->parent==object_type && data_type->fields->count==2 && data_type->native_size==0x14 &&
        data_type->name && *data_type->name=="igDataList" && data_type->type_word==0x80472fa0 &&
        igb_bootstrap_read_word(data_probe_locals.front(),0)==0x80472fa0,
        "DataList constructor probe, captured parent fields or pending registrar flags differ.");

    // Bind the reached canonical integer cells, then perform registration,
    // its actual probe and reflected prototype before the two schema factories.
    signed_int_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_int)]=signed_int_cell;
    values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> integer_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::signed_int,"New field probe replayed an earlier object class.");
        auto local=allocation(0xb10000+static_cast<std::uint32_t>(integer_probe_locals.size())*0x100,0x34);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        integer_probe_locals.push_back(local);return local;
    };
    const auto memory_class_boundary=call.resume(bootstrap);
    std::cout<<"Cold DataList memory-field boundary: "<<memory_class_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", enabled allocations="<<enabled_allocations.size()<<
        ", defaults="<<default_allocations<<", backings="<<enabled_backings.size()<<
        ", metadata/list instances="<<f.constructed->instance_count.value_or(0xffffffffu)<<'/'<<lazy_metadata[0]->instance_count.value_or(0xffffffffu)<<'\n';
    check(!memory_class_boundary.completed && !call.faulted() && memory_class_boundary.diagnostic.find("canonical metadata cell")!=std::string::npos,
        "DataList integer construction did not reach actual memory-reference field registration.");
    const auto integer_type=**signed_int_cell;const auto integer_prototype=*values->signed_int_prototype;
    check(integer_type && integer_prototype && integer_type->class_flags==7 && integer_type->parent==base_field &&
        integer_type->native_size==0x34 && integer_type->type_word==0x80475b60 && integer_type->fields->count==20 &&
        integer_type->instance_count==3 && f.core->types->count==9 && f.core->types->backing->at(8)==integer_type &&
        signed_int_prototype_allocations.size()==1 && integer_probe_locals.size()==1 &&
        igb_bootstrap_read_word(integer_probe_locals.front(),0)==0x80471914,
        "Integer class did not preserve actual inheritance, probe cleanup, publication and three reflected instances.");
    check(integer_prototype->vtable_word==0x80475b60 && integer_prototype->reference_word==1 && integer_prototype->ordinal==1 &&
        integer_prototype->type_ordinal==1 && integer_prototype->default_bytes && integer_prototype->default_bytes->buffer &&
        integer_prototype->default_bytes->buffer->bytes.size()==4 && (**prototypes->registry)->count==2 &&
        (**prototypes->registry)->backing->at(1)==integer_prototype,
        "Integer prototype skipped native initialization, registry append, release or finalization.");
    check(descriptors.size()==57 && data_type->fields->count==4 && data_type->fields->backing->at(2)==descriptors[55] &&
        data_type->fields->backing->at(3)==descriptors[56] && descriptors[55]->vtable_word==0x80475b60 &&
        descriptors[56]->vtable_word==0x80475b60 && !descriptors[55]->name && !descriptors[56]->name &&
        data_type->class_flags==9 && nonref_type->class_flags==8 && thread_list_metadata->class_flags==8 &&
        descriptors[55]->default_bytes && !descriptors[55]->default_bytes->buffer &&
        descriptors[56]->default_bytes && !descriptors[56]->default_bytes->buffer &&
        !**meta_enum->metadata && !**data_defaults->unsigned_char_prototype && default_allocations==57 &&
        enabled_allocations.size()==15 && enabled_backings.size()==5 && f.constructed->instance_count==6 && lazy_metadata[0]->instance_count==6,
        "Pending third DataList field invented schema completion, a byte prototype or ready parent classes.");
    check(grown_object_children && (*object_type->children)->count==5 &&
        (*object_type->children)->backing==grown_object_children->types && grown_object_children->types->at(4)==data_type &&
        igb_buffer_word(grown_object_children->bytes,16)==igb_bootstrap_address({enabled_allocations[8].storage,enabled_allocations[8].offset}) &&
        igb_buffer_word(grown_object_children->bytes,20)==0 && igb_buffer_word(grown_object_children->bytes,28)==0,
        "DataList inheritance skipped original child-list growth, pointer publication or spare-slot zero fill.");
    const auto final_new_allocations=reflected_allocations.size();
    const auto final_new_strings=string_allocations;
    check(!call.resume(bootstrap).completed && reflected_allocations.size()==final_new_allocations && string_allocations==final_new_strings &&
        descriptors.size()==57 && data_type->fields->count==4 && f.core->types->count==9 && integer_type->instance_count==3 &&
        (**prototypes->registry)->count==2 && integer_probe_locals.size()==1 && data_probe_locals.size()==1 &&
        (*f.core->bootstrap_callbacks)->count==1,
        "Pending memory-field registration replayed parent registration, probes, reflected factories or prototype publication.");

    // Supply the reached BSS cell and local storage; actual memory-field
    // registration must construct and initialize its metadata before defaults.
    auto memory_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=memory_cell;
    std::vector<IgbBootstrapPointer> memory_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::memory_reference,"Memory-field registration replayed an earlier class probe.");
        auto local=allocation(0xb30000+static_cast<std::uint32_t>(memory_probe_locals.size())*0x100,0x50);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        memory_probe_locals.push_back(local);return local;
    };
    const auto memory_defaults_boundary=call.resume(bootstrap);
    std::cout<<"Cold memory-field defaults boundary: "<<memory_defaults_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    const auto memory_type=**memory_cell;
    check(!memory_defaults_boundary.completed && !call.faulted() && !remaining_calls.empty() &&
        remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x8002af40 &&
        memory_type && memory_type->class_flags==9 && memory_type->parent==reference_field &&
        memory_type->native_size==0x50 && memory_type->type_word==0x80476088 && memory_type->fields->count==22 &&
        memory_probe_locals.size()==1 && igb_bootstrap_read_word(memory_probe_locals.front(),0)==0x80471914 &&
        igb_bootstrap_read_word(memory_probe_locals.front(),0x48)==0 && igb_bootstrap_read_word(memory_probe_locals.front(),0x4c)==0,
        "Memory-field metadata registration skipped inheritance, exact reset/cleanup or its required defaults.");
    check(enabled_allocations.size()==17 && enabled_backings.size()==6 && f.constructed->instance_count==7 &&
        lazy_metadata[0]->instance_count==7 && descriptors.size()==57 && default_allocations==57 &&
        f.core->types->count==9 && (**prototypes->registry)->count==2,
        "Pending memory-field defaults fabricated descriptor construction or class/prototype completion.");
    auto memory_defaults=std::make_shared<IgbMemoryFieldDefaultsServices>();memory_defaults->registration=field_types;
    memory_defaults->lists=list_defaults;
    f.remaining=make_native_igb_memory_field_defaults_factory(memory_defaults,f.remaining);
    const auto boolean_boundary=call.resume(bootstrap);
    std::cout<<"Cold memory-field boolean boundary: "<<boolean_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!boolean_boundary.completed && !call.faulted() && boolean_boundary.diagnostic.find("canonical metadata cell")!=std::string::npos &&
        memory_type->fields->count==24 && descriptors.size()==59 && memory_type->fields->backing->at(22)==descriptors[57] &&
        memory_type->fields->backing->at(23)==descriptors[58] && descriptors[57]->vtable_word==0x80475b60 &&
        descriptors[58]->vtable_word==0x804762fc && !descriptors[57]->name && !descriptors[58]->name,
        "Memory defaults did not use actual enabled field factories before boolean registration.");
    check(memory_type->class_flags==9 && memory_type->instance_count==0 && integer_type->instance_count==4 &&
        object_field->instance_count==4 && enabled_allocations.size()==19 && enabled_backings.size()==6 &&
        default_allocations==58 && f.core->types->count==9 && (**prototypes->registry)->count==2 &&
        data_type->fields->count==4 && !**meta_enum->metadata && !*values->memory_reference_prototype,
        "Pending memory schema completed its class/prototype, DataList parent or unreached enum.");
    const auto memory_names=string_allocations;
    const auto memory_reflected_calls=reflected_allocations.size();
    check(!call.resume(bootstrap).completed && memory_type->fields->count==24 && descriptors.size()==59 &&
        string_allocations==memory_names && reflected_allocations.size()==memory_reflected_calls &&
        default_allocations==58 && memory_probe_locals.size()==1 && enabled_allocations.size()==19,
        "Pending boolean registration replayed memory-field metadata, probe, inherited fields or descriptor factories.");

    // Resume the reached Boolean class through its original registration,
    // scalar probe and reflected prototype. The remaining batch must still
    // stop at the actual unrecovered string class, without configuring locals.
    boolean_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::boolean)]=boolean_cell;
    values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> boolean_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::boolean,"Boolean registration replayed a previous class probe.");
        auto local=allocation(0xb40000+static_cast<std::uint32_t>(boolean_probe_locals.size())*0x100,0x34);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        boolean_probe_locals.push_back(local);return local;
    };
    const auto string_boundary=call.resume(bootstrap);
    std::cout<<"Cold memory-field string boundary: "<<string_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", backings="<<enabled_backings.size()<<", metadata/list instances="<<f.constructed->instance_count.value_or(0xffffffffu)<<'/'<<
        lazy_metadata[0]->instance_count.value_or(0xffffffffu)<<'\n';
    const auto boolean_type=**boolean_cell;const auto boolean_prototype=*values->boolean_prototype;
    check(!string_boundary.completed && !call.faulted() && string_boundary.diagnostic.find("canonical metadata cell")!=std::string::npos,
        "Memory schema failed before its actual string class prerequisite.");
    check(boolean_type && boolean_prototype && boolean_type->class_flags==7 && boolean_type->parent==base_field &&
        boolean_type->native_size==0x34 && boolean_type->type_word==0x80473e30 && boolean_type->fields->count==20 &&
        boolean_type->instance_count==4 && f.core->types->count==10 && f.core->types->backing->at(9)==boolean_type &&
        boolean_prototype_allocations.size()==1 && boolean_probe_locals.size()==1 &&
        igb_bootstrap_read_word(boolean_probe_locals.front(),0)==0x80471914,
        "Boolean class skipped its actual inheritance, probe, publication or four reflected instances.");
    check(boolean_prototype->vtable_word==0x80473e30 && boolean_prototype->reference_word==1 && boolean_prototype->ordinal==2 &&
        boolean_prototype->type_ordinal==2 && boolean_prototype->default_bytes && boolean_prototype->default_bytes->buffer &&
        boolean_prototype->default_bytes->buffer->bytes.size()==1 && (**prototypes->registry)->count==3 &&
        (**prototypes->registry)->backing->at(2)==boolean_prototype,
        "Boolean prototype skipped initializer, registry publication or one-byte default finalization.");
    check(memory_type->fields->count==29 && descriptors.size()==64 && memory_type->class_flags==9 && memory_type->instance_count==0 &&
        integer_type->instance_count==5 && object_field->instance_count==5 && default_allocations==60 &&
        enabled_allocations.size()==27 && enabled_backings.size()==7 && f.constructed->instance_count==8 &&
        lazy_metadata[0]->instance_count==8 && data_type->fields->count==4 && !**meta_enum->metadata && !*values->memory_reference_prototype,
        "Pending string field invented finished memory schema, prototype or parent registration.");
    const std::array<std::uint32_t,7> local_vtables{0x80475b60,0x804762fc,0x80473e30,0x80473e30,0x80473e30,0x80475b60,0x804762fc};
    for(std::size_t i=0;i<local_vtables.size();++i)
        check(memory_type->fields->backing->at(22+i)==descriptors[57+i] && descriptors[57+i]->vtable_word==local_vtables[i] &&
            !descriptors[57+i]->name,"Memory defaults reordered, renamed or bypassed the seven enabled descriptor factories.");
    const auto string_names=string_allocations;const auto string_reflections=reflected_allocations.size();
    check(!call.resume(bootstrap).completed && memory_type->fields->count==29 && descriptors.size()==64 &&
        string_allocations==string_names && reflected_allocations.size()==string_reflections && default_allocations==60 &&
        enabled_allocations.size()==27 && boolean_probe_locals.size()==1 && boolean_type->instance_count==4 &&
        (**prototypes->registry)->count==3 && f.core->types->count==10 && (*f.core->bootstrap_callbacks)->count==1 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending string registration replayed the completed Boolean class or changed original bootstrap snapshots.");

    auto string_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::string)]=string_cell;
    values->string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> string_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::string,"String registration replayed a completed class probe.");
        auto local=allocation(0xb50000+static_cast<std::uint32_t>(string_probe_locals.size())*0x100,0x3c);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        string_probe_locals.push_back(local);return local;
    };
    const auto string_defaults_boundary=call.resume(bootstrap);const auto string_type=**string_cell;
    check(!string_defaults_boundary.completed && !call.faulted() && !remaining_calls.empty() &&
        remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x8002549c &&
        string_type && string_type->class_flags==9 && string_type->parent==reference_field &&
        string_type->native_size==0x3c && string_type->type_word==0x8047693c && string_type->fields->count==22 &&
        string_probe_locals.size()==1 && igb_bootstrap_read_word(string_probe_locals.front(),0)==0x80471914,
        "String registration skipped actual metadata construction, parent inheritance, reset/probe or defaults.");
    check(enabled_allocations.size()==29 && enabled_backings.size()==8 && f.constructed->instance_count==9 &&
        lazy_metadata[0]->instance_count==9 && descriptors.size()==64 && default_allocations==60 && f.core->types->count==10,
        "String metadata defaults boundary fabricated a descriptor or prototype.");
    auto string_defaults=std::make_shared<IgbStringFieldDefaultsServices>();string_defaults->registration=field_types;
    string_defaults->lists=list_defaults;f.remaining=make_native_igb_string_field_defaults_factory(string_defaults,f.remaining);
    const auto byte_boundary=call.resume(bootstrap);
    std::cout<<"Cold string-field byte-class boundary: "<<byte_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!byte_boundary.completed && !call.faulted() && byte_boundary.diagnostic.find("canonical metadata cell")!=std::string::npos &&
        string_type->fields->count==23 && string_type->fields->backing->at(22)==descriptors[64] &&
        descriptors[64]->vtable_word==0x80473e30 && !descriptors[64]->name && boolean_type->instance_count==5 &&
        descriptors.size()==65 && default_allocations==60 && enabled_allocations.size()==30 &&
        !*string_defaults->shallow_copy && !*string_defaults->destruct && !*values->string_prototype,
        "String defaults bypassed its Boolean factory or required signed-byte clone registration.");
    signed_byte_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_byte)]=signed_byte_cell;
    values->signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> byte_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::signed_byte,"Signed-byte registration replayed another class probe.");
        auto local=allocation(0xb60000+static_cast<std::uint32_t>(byte_probe_locals.size())*0x100,0x34);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        byte_probe_locals.push_back(local);return local;
    };
    std::vector<IgbFieldCloneCall> reached_reflected_clones;
    IgbFieldCloneFactory resumed_reflected_clones;
    clones->factory=[&](const IgbFieldCloneCall& c){reached_reflected_clones.push_back(c);
        return resumed_reflected_clones?resumed_reflected_clones(c):std::shared_ptr<IgbFieldCloneOperation>{};};
    const auto reflected_clone_boundary=call.resume(bootstrap);
    std::cout<<"Cold string-field reflected-clone boundary: "<<reflected_clone_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", backings="<<enabled_backings.size()<<", metadata/list instances="<<f.constructed->instance_count.value_or(0xffffffffu)<<'/'<<
        lazy_metadata[0]->instance_count.value_or(0xffffffffu)<<'\n';
    const auto byte_type=**signed_byte_cell;const auto byte_prototype=*values->signed_byte_prototype;
    check(!reflected_clone_boundary.completed && !call.faulted() && reached_reflected_clones.size()==1 &&
        reached_reflected_clones.front().kind==IgbFieldCloneServiceKind::reflected_clone &&
        reached_reflected_clones.front().function==0x800680b4 && reached_reflected_clones.front().argument==1 &&
        reached_reflected_clones.front().source==*field_defaults->tables[0].globals->at(11)->slot &&
        reached_reflected_clones.front().source->name && *reached_reflected_clones.front().source->name=="_shallowCopyMethod",
        "Enabled signed-byte clone did not reach actual680B4 with its captured inherited source.");
    check(byte_type && byte_prototype && byte_type->class_flags==7 && byte_type->parent==base_field &&
        byte_type->native_size==0x34 && byte_type->type_word==0x80473f24 && byte_type->fields->count==20 &&
        byte_type->instance_count==1 && f.core->types->count==11 && f.core->types->backing->at(10)==byte_type &&
        signed_byte_prototype_allocations.size()==1 && byte_probe_locals.size()==1 &&
        igb_bootstrap_read_word(byte_probe_locals.front(),0)==0x80471914 && byte_prototype->reference_word==1 &&
        byte_prototype->ordinal==3 && byte_prototype->type_ordinal==3 && byte_prototype->default_bytes &&
        byte_prototype->default_bytes->buffer && byte_prototype->default_bytes->buffer->bytes.size()==1 &&
        (**prototypes->registry)->count==4 && (**prototypes->registry)->backing->at(3)==byte_prototype,
        "Signed-byte class or reflected prototype skipped actual inheritance, publication, initialization or finalization.");
    check(descriptors.size()==65 && default_allocations==61 && enabled_allocations.size()==33 && enabled_backings.size()==9 &&
        f.constructed->instance_count==10 && lazy_metadata[0]->instance_count==10 && string_type->fields->count==23 &&
        string_type->class_flags==9 && memory_type->fields->count==29 && memory_type->class_flags==9 && data_type->fields->count==4 &&
        !*values->string_prototype && !*values->memory_reference_prototype && !**meta_enum->metadata,
        "Unbound enabled clone fabricated a descriptor, completed schema or later startup dependency.");
    const auto clone_names=string_allocations;const auto clone_reflections=reflected_allocations.size();
    check(!call.resume(bootstrap).completed && string_allocations==clone_names && reflected_allocations.size()==clone_reflections &&
        descriptors.size()==65 && default_allocations==61 && byte_probe_locals.size()==1 && string_probe_locals.size()==1 &&
        enabled_allocations.size()==33 && (**prototypes->registry)->count==4 && f.core->types->count==11 &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending reflected clone replayed completed classes, factories, prototypes or bootstrap snapshots.");

    // Continue the same retained source through actual680B4/6546C/67B60.
    // Bind existing arena identities; do not prepare an allocated clone result.
    for(const auto& binding:allocated_names)igb_bind_field_string(values,binding.address,binding.owner);
    const auto bind_existing=[&](const IgbBootstrapPointer& pointer){
        for(const auto& b:pointer.storage->bindings)if(b.offset==pointer.offset && b.metadata){
            IgbObjectInstance instance;instance.storage=pointer.storage;instance.offset=pointer.offset;instance.metadata=b.metadata;
            igb_bind_object_field_instance(member_construct,instance);
        }
    };
    for(const auto& raw:initial_allocations)bind_existing(raw);
    for(const auto& raw:lazy_allocations)bind_existing(raw);
    for(const auto& instance:enabled_allocations)if(instance.metadata)igb_bind_object_field_instance(member_construct,instance);
    auto member_copy=std::make_shared<IgbFieldMemberCopyServices>();member_copy->values=values;member_copy->objects=member_construct;
    std::vector<IgbFieldMemberCopyCall> missing_member_copies;
    IgbFieldMemberCopyFactory resumed_memory_copies;
    member_copy->factory=[&](const IgbFieldMemberCopyCall& c){missing_member_copies.push_back(c);
        return resumed_memory_copies?resumed_memory_copies(c):std::shared_ptr<IgbFieldMemberCopyOperation>{};};
    auto reflection_copy=std::make_shared<IgbReflectedCloneServices>();reflection_copy->reflected=prototypes->reflected;
    reflection_copy->object_metadata=object_type;
    unsigned reflected_clone_pools{};
    reflection_copy->factory=make_native_igb_reflected_clone_virtual_factory(prototypes->reflected->initializer,
        make_native_igb_reflected_member_copy_factory(member_copy,[&](const IgbReflectedCloneCall& c){
            if(c.kind!=IgbReflectedCloneServiceKind::tagged_pool)return std::shared_ptr<IgbReflectedCloneOperation>{};
            auto op=std::make_shared<IgbReflectedCloneOperation>();op->resume=[&,c,out=op.get()]{
                ++reflected_clone_pools;check(c.handle==0 && std::any_of(reached_reflected_clones.begin(),reached_reflected_clones.end(),
                    [&](const auto& clone){return clone.source==c.object.field;}),
                    "Enabled clone lost its captured source or tagged pool handle.");
                out->pool_result=f.pool;return MessengerCallResult{true,0,{}};};return op;
        }));
    resumed_reflected_clones=make_native_igb_reflected_field_clone_factory(reflection_copy);
    const auto member_copy_boundary=call.resume(bootstrap);
    std::cout<<"Cold reflected member-copy boundary: "<<member_copy_boundary.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!member_copy_boundary.completed && !call.faulted() && reflected_clone_pools==1 && !missing_member_copies.empty() &&
        missing_member_copies.back().function==0x80062824,"Enabled reflected clone did not reach actual memory member copying.");
    const auto native_clone=enabled_allocations.back().field;const auto native_source=reached_reflected_clones.front().source;
    check(native_clone && native_clone!=native_source && native_clone->vtable_word==0x80473f24 && native_clone->reference_word==1 &&
        native_clone->offset==native_source->offset && native_clone->name_string==native_source->name_string &&
        native_clone->type_ordinal==native_source->type_ordinal && native_clone->ordinal==native_source->ordinal &&
        native_clone->size==native_source->size && native_clone->declaring_type &&
        native_clone->declaring_type->lock()==native_source->declaring_type->lock() &&
        native_clone->global_slot_address==native_source->global_slot_address,
        "Reflected clone did not copy the actual seven members preceding its memory default.");
    check(descriptors.size()==66 && enabled_allocations.size()==34 && byte_type->instance_count==2 &&
        f.core->types->count==11 && (**prototypes->registry)->count==4 && !*string_defaults->shallow_copy &&
        !*values->string_prototype && string_type->class_flags==9 && memory_type->class_flags==9,
        "Pending memory copy supplied a finished replacement or changed the completed class registry.");
    const auto reflected_copy_names=string_allocations;const auto reflected_copy_allocations=reflected_allocations.size();
    const auto copied_name_references=(*native_source->name_string)->references;
    check(!call.resume(bootstrap).completed && reflected_clone_pools==1 && descriptors.size()==66 && enabled_allocations.size()==34 &&
        reflected_allocations.size()==reflected_copy_allocations && string_allocations==reflected_copy_names &&
        (*native_source->name_string)->references==copied_name_references &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending member copy replayed reflected allocation, prior fields, name ownership or bootstrap state.");

    auto buffer_copies=std::make_shared<IgbBufferServices>();buffer_copies->values=values;
    auto memory_copies=std::make_shared<IgbMemoryMemberCopyServices>();memory_copies->buffers=buffer_copies;
    memory_copies->objects=member_construct;memory_copies->registration=field_types;
    memory_copies->factory=make_native_igb_memory_member_virtual_factory(field_types);
    resumed_memory_copies=make_native_igb_memory_member_copy_factory(memory_copies);
    std::vector<IgbBufferCall> buffer_calls;
    unsigned buffer_copy_allocations{};bool buffer_allocation_ready=false;
    buffer_copies->factory=[&](const IgbBufferCall& c){
        buffer_calls.push_back(c);auto operation=std::make_shared<IgbBufferOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            if(c.kind==IgbBufferServiceKind::allocation_pool){
                check(c.buffer.buffer && c.address,"Memory-copy owner query lost its captured original buffer.");
                out->pool_result=f.pool;
            }else{
                check(c.pool==f.pool,"Memory-copy virtual selected a different owner pool.");
                if(c.virtual_slot==0x150){
                    check(c.buffer.buffer && !c.buffer.buffer->released && c.buffer.offset<=c.buffer.buffer->bytes.size(),
                        "Memory-copy size query reached unavailable backing.");
                    done=true;return MessengerCallResult{true,static_cast<std::uint32_t>(c.buffer.buffer->bytes.size()-c.buffer.offset),{}};
                }
                if(c.virtual_slot==0xd8){
                    if(!buffer_allocation_ready)return MessengerCallResult{false,0,"prepared memory-copy allocation"};
                    check(c.bytes>0 && c.bytes<=4 && !c.buffer.buffer && c.address==0,
                        "Memory-copy allocation changed the captured width or virtual arguments.");
                    auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(c.bytes,0xcc);bytes->initialized.assign(c.bytes,0);
                    ++default_allocations;++buffer_copy_allocations;
                    out->buffer_result={bytes,0};out->buffer_address=0x740000+default_allocations*0x100;
                    values->buffers.push_back({bytes,out->buffer_address});
                }else if(c.virtual_slot==0x108){
                    check(c.buffer.buffer && !c.buffer.buffer->released,"Memory copy freed unavailable backing.");
                    c.buffer.buffer->released=true;
                }else throw std::runtime_error("Unexpected memory-copy pool virtual.");
            }
            done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto buffer_boundary=call.resume(bootstrap);
    check(!buffer_boundary.completed && !call.faulted() && buffer_calls.size()==4 &&
        buffer_calls[0].kind==IgbBufferServiceKind::allocation_pool && buffer_calls[1].kind==IgbBufferServiceKind::allocation_pool &&
        buffer_calls[2].virtual_slot==0x150 && buffer_calls[3].virtual_slot==0xd8 && buffer_calls[3].bytes==1 &&
        buffer_copy_allocations==0 && default_allocations==61 && native_clone->default_bytes && !native_clone->default_bytes->buffer,
        "Cold memory copy skipped its null destination or repeated original owner/size queries.");
    buffer_allocation_ready=true;
    const auto after_memory_copy=call.resume(bootstrap);
    std::cout<<"Cold native memory-copy continuation: "<<after_memory_copy.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_memory_copy.completed && !call.faulted() && native_clone->default_bytes && native_clone->default_bytes->buffer &&
        native_source->default_bytes && native_clone->default_bytes->buffer!=native_source->default_bytes->buffer &&
        native_source->default_bytes->buffer->bytes[native_source->default_bytes->offset]==1,
        "Native memory-copy continuation lost its independent default allocation or changed its source.");
    check(buffer_copy_allocations==2 && descriptors.size()==67 && default_allocations==64 && enabled_allocations.size()==35 &&
        f.core->types->count==12 && string_type->class_flags==7 && string_type->fields->count==23 &&
        *string_defaults->shallow_copy==native_clone && *string_defaults->destruct &&
        native_clone->default_bytes->buffer->bytes[native_clone->default_bytes->offset]==2 &&
        (**string_defaults->destruct)->default_bytes->buffer->bytes[(**string_defaults->destruct)->default_bytes->offset]==1 &&
        !*values->string_prototype && !unbound_reflected_allocations.empty() &&
        unbound_reflected_allocations.back().metadata==string_type && unbound_reflected_allocations.back().bytes==0x3c,
        "Memory copying did not complete both inherited string replacements and reach actual prototype allocation.");
    // Supply only raw string-class allocations. Actual667F4 and7144C still
    // initialize the prototype and the memory schema's ordinary descriptor.
    std::vector<IgbBootstrapPointer> string_prototype_allocations;
    resumed_reflected_allocations=[&](const IgbFileFactoryCall& c){
        if(c.metadata!=string_type)return std::shared_ptr<IgbFileFactoryOperation>{};
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x3c,
                "String reflected factory changed its original allocation request.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            if(values->string_prototype && !*values->string_prototype)string_prototype_allocations.push_back(raw);
            else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);
            if(bind_new_instance_addresses)igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto after_string_prototype=call.resume(bootstrap);
    std::cout<<"Cold native string-prototype continuation: "<<after_string_prototype.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_string_prototype.completed && !call.faulted(),"String prototype incorrectly completed full native bootstrap.");
    const auto string_prototype=*values->string_prototype;
    check(string_prototype && string_prototype_allocations.size()==1 && string_type->instance_count==2 &&
        string_prototype->vtable_word==0x8047693c && string_prototype->ordinal==4 && string_prototype->type_ordinal==4 &&
        string_prototype->reference_word==1 && string_prototype->flag_38==1 && string_prototype->default_bytes &&
        string_prototype->default_bytes->buffer && igb_buffer_word(string_prototype->default_bytes->buffer,string_prototype->default_bytes->offset)==0 &&
        (**prototypes->registry)->count==5 && (**prototypes->registry)->backing->at(4)==string_prototype,
        "String prototype skipped actual reflected initialization, owned-string flag, finalization or registry publication.");
    check(buffer_copy_allocations==3 && descriptors.size()==69 && default_allocations==69 && enabled_allocations.size()==38 &&
        memory_type->class_flags==9 && memory_type->fields->count==30 && memory_type->fields->backing->at(29)==descriptors[67] &&
        descriptors[67]->vtable_word==0x8047693c && *memory_defaults->shallow_copy==descriptors[68] &&
        descriptors[68]->default_bytes && descriptors[68]->default_bytes->buffer->bytes[descriptors[68]->default_bytes->offset]==2 &&
        !schema_calls.empty() && schema_calls.back().kind==IgbFieldSchemaServiceKind::create_field &&
        schema_calls.back().function==0x80031378 && schema_calls.back().pool==f.pool &&
        !*memory_defaults->reset_method && !*values->memory_reference_prototype && !**meta_enum->metadata,
        "Memory defaults bypassed its eight-field batch, actual inherited clone or required reset-method constructor31378.");
    const auto completed_memory_names=string_allocations,completed_memory_defaults=default_allocations;
    const auto completed_memory_reflections=reflected_allocations.size();
    const auto completed_buffer_calls=buffer_calls.size();
    check(!call.resume(bootstrap).completed && !call.faulted() && descriptors.size()==69 && enabled_allocations.size()==38 &&
        string_allocations==completed_memory_names && default_allocations==completed_memory_defaults &&
        reflected_allocations.size()==completed_memory_reflections && buffer_calls.size()==completed_buffer_calls &&
        f.core->types->count==12 && (**prototypes->registry)->count==5 &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending reset-method construction replayed completed clones, prototypes, classes or original bootstrap snapshots.");

    // Resume the retained factory with represented null class cells, never
    // prebuilt metadata. Both layout probes execute their actual local resets.
    auto enumeration_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    auto pointer_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::enumeration)]=enumeration_cell;
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::pointer)]=pointer_cell;
    values->enumeration_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    // The enum class becomes the fifth child of igMetaField. Supply the
    // allocator's enlarged backing; native growth still copies/publishes slots.
    std::optional<IgbPointerListBacking> grown_base_field_children;
    const auto previous_type_allocator=type_pointers->factory;
    type_pointers->factory=[&,previous_type_allocator](const IgbPointerListCall& c){
        const bool query=c.kind==IgbPointerListServiceKind::allocation_size && grown_base_field_children &&
            c.backing.address==grown_base_field_children->address;
        const bool grow=c.kind==IgbPointerListServiceKind::reallocate && base_field->children && c.types==*base_field->children;
        if(!query && !grow)return previous_type_allocator(c);
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,query,out=op.get()]{
            if(query){check(c.backing.bytes==grown_base_field_children->bytes && c.pool==f.pool,
                "Base-field child query lost its grown allocation.");out->size_result=32;}
            else{
                check(!grown_base_field_children && c.types->count==4 && c.pool==f.pool && c.bytes==32 &&
                    c.backing.offset==0 && c.backing.bytes && c.backing.bytes->bytes.size()==16 && c.backing.types,
                    "Fifth base-field child changed its original reallocation request.");
                auto raw=allocation(0xb90000,c.bytes);raw.storage->bytes->initialized.assign(c.bytes,0);
                std::copy(c.backing.bytes->bytes.begin(),c.backing.bytes->bytes.end(),raw.storage->bytes->bytes.begin());
                std::copy(c.backing.bytes->initialized.begin(),c.backing.bytes->initialized.end(),raw.storage->bytes->initialized.begin());
                auto mapping=std::make_shared<IgbRuntimeTypeList::Storage>(8);
                std::copy(c.backing.types->begin(),c.backing.types->end(),mapping->begin());
                grown_base_field_children=IgbPointerListBacking{raw.storage->bytes,igb_bootstrap_address(raw),0,mapping,{}};
                out->backing_result=*grown_base_field_children;
            }return MessengerCallResult{true,0,{}};
        };return op;
    };
    std::vector<IgbBootstrapPointer> enumeration_probe_locals,pointer_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::enumeration || kind==IgbMetaFieldKind::pointer,
            "Enum dependency registration replayed a completed class probe.");
        auto& locals=kind==IgbMetaFieldKind::enumeration?enumeration_probe_locals:pointer_probe_locals;
        auto local=allocation((kind==IgbMetaFieldKind::enumeration?0xb70000u:0xb80000u)+static_cast<std::uint32_t>(locals.size())*0x100,0x38);
        local.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        locals.push_back(local);return local;
    };
    enumeration_constructor_ready=true;
    const auto enumeration_boundary=call.resume(bootstrap);
    check(!enumeration_boundary.completed && !call.faulted() && **enumeration_cell && !**pointer_cell &&
        remaining_calls.back().kind==TK::defaults && remaining_calls.back().function==0x80031614 &&
        (**enumeration_cell)->class_flags==9 && (**enumeration_cell)->fields->count==20 &&
        enumeration_probe_locals.size()==1 && pointer_probe_locals.empty(),
        "Enum construction did not enter original registration, inherited fields and its one-field defaults callback.");
    auto enumeration_defaults=std::make_shared<IgbEnumFieldDefaultsServices>();
    enumeration_defaults->registration=field_types;enumeration_defaults->schema=metadata_defaults->schema;
    f.remaining=make_native_igb_enum_field_defaults_factory(enumeration_defaults,f.remaining);
    const auto pointer_boundary=call.resume(bootstrap);
    check(!pointer_boundary.completed && !call.faulted() && **pointer_cell &&
        (**pointer_cell)->class_flags==7 && (**pointer_cell)->fields->count==22 && pointer_probe_locals.size()==1 &&
        !*values->pointer_prototype && unbound_reflected_allocations.back().metadata==**pointer_cell &&
        unbound_reflected_allocations.back().bytes==0x38 && (**enumeration_cell)->fields->count==20,
        "Enum defaults bypassed real pointer registration or fabricated its reflected prototype.");
    const auto previous_reflected_allocations=resumed_reflected_allocations;
    std::vector<IgbBootstrapPointer> enumeration_prototype_allocations,pointer_prototype_allocations,memory_prototype_allocations;
    resumed_reflected_allocations=[&,previous_reflected_allocations](const IgbFileFactoryCall& c){
        const bool enumeration=c.metadata==**enumeration_cell,pointer=c.metadata==**pointer_cell,memory=c.metadata==memory_type;
        if(!enumeration && !pointer && !memory)return previous_reflected_allocations(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,enumeration,pointer,memory,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==(memory?0x50u:0x38u),
                "Enum/pointer/memory reflected factory changed its original raw allocation request.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            const auto& prototype=enumeration?values->enumeration_prototype:pointer?values->pointer_prototype:values->memory_reference_prototype;
            if(prototype && !*prototype){auto& allocations=enumeration?enumeration_prototype_allocations:pointer?pointer_prototype_allocations:memory_prototype_allocations;
                allocations.push_back(raw);}
            else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);
            if(bind_new_instance_addresses)igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto after_enumeration=call.resume(bootstrap);
    std::cout<<"Cold native enum/pointer continuation: "<<after_enumeration.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_enumeration.completed && !call.faulted(),"Enum registration incorrectly completed full native bootstrap.");
    const auto enumeration_type=**enumeration_cell,pointer_type=**pointer_cell;
    const auto enum_member=enumeration_type->fields->backing->at(20);
    check(enumeration_type->class_flags==7 && enumeration_type->fields->count==21 && enumeration_type->native_size==0x38 &&
        enumeration_type->type_word==0x804758e4 && enumeration_type->instance_count==2 &&
        pointer_type->class_flags==7 && pointer_type->fields->count==22 && pointer_type->native_size==0x38 &&
        pointer_type->type_word==0x80476630 && pointer_type->instance_count==2 &&
        pointer_type->name && *pointer_type->name=="igRawRefMetaField",
        "Enum and raw-reference classes did not finish actual registration and both reflected instances.");
    check(enum_member && enum_member->vtable_word==0x80476630 && enum_member->name &&
        *enum_member->name=="_getMetaEnumFunction" && enum_member->offset==0x34 && enum_member->flag_26==0 &&
        enum_member->global_slot_address==0x80561c38 && *enumeration_defaults->globals->at(0)->slot==enum_member &&
        enum_member->declaring_type && enum_member->declaring_type->lock()==enumeration_type &&
        igb_bootstrap_read_byte(descriptor_allocations[69],0x26)==0,
        "Enum schema did not publish its real nonpersistent pointer member into physical and canonical storage.");
    check(grown_base_field_children && (*base_field->children)->count==5 &&
        (*base_field->children)->backing==grown_base_field_children->types && grown_base_field_children->types->at(4)==enumeration_type,
        "Enum registration bypassed base-field child growth and canonical parent publication.");
    for(std::size_t at=20;at<32;++at)check(grown_base_field_children->bytes->initialized[at] && grown_base_field_children->bytes->bytes[at]==0,
        "Native base-field child growth did not zero its unused slots.");
    check(memory_defaults->reset_method && memory_defaults->reset_method->has_value(),
        "Memory reset descriptor's canonical global remains unknown.");
    const auto reset_descriptor=**memory_defaults->reset_method;
    check(memory_type->class_flags==7 && memory_type->fields->count==30 && memory_type->instance_count==2 && reset_descriptor &&
        reset_descriptor->vtable_word==0x804758e4 && reset_descriptor->size==4 && reset_descriptor->offset==0x24 &&
        reset_descriptor->name && *reset_descriptor->name=="_resetMethod" && reset_descriptor->global_slot_address==0x80561834 &&
        reset_descriptor->declaring_type && reset_descriptor->declaring_type->lock()==memory_type &&
        reset_descriptor->method_function==0x8002a5ec && reset_descriptor->default_bytes &&
        igb_buffer_word(reset_descriptor->default_bytes->buffer,reset_descriptor->default_bytes->offset)==3 &&
        igb_bootstrap_read_word(descriptor_allocations[70],0x34)==0x8002a5ec &&
        std::find(memory_type->fields->backing->begin(),memory_type->fields->backing->end(),reset_descriptor)!=memory_type->fields->backing->end(),
        "Memory defaults did not complete their enum replacement, original four-byte value or getter publication.");
    const auto pointer_prototype=*values->pointer_prototype,enum_prototype=*values->enumeration_prototype;
    const auto memory_prototype=*values->memory_reference_prototype;
    check(pointer_prototype_allocations.size()==1 && enumeration_prototype_allocations.size()==1 && memory_prototype_allocations.size()==1 &&
        pointer_prototype && enum_prototype && memory_prototype && (**prototypes->registry)->count==8,
        "New prototypes did not execute exactly one reflected allocation apiece.");
    const std::array new_prototypes={pointer_prototype,enum_prototype,memory_prototype};
    for(std::size_t i=0;i<new_prototypes.size();++i){const auto& field=new_prototypes[i];
        check(field->reference_word==1 && field->ordinal==i+5 && field->type_ordinal==i+5 &&
            (**prototypes->registry)->backing->at(i+5)==field && field->default_bytes && field->default_bytes->buffer &&
            igb_buffer_word(field->default_bytes->buffer,field->default_bytes->offset)==0,
            "Prototype append, ordinal, finalization or independent default storage differs.");
    }
    check(enum_prototype->method_function==0 && igb_bootstrap_read_word(enumeration_prototype_allocations.front(),0x34)==0 &&
        pointer_prototype->flag_34==0 && pointer_prototype->flag_35==0,
        "Actual reflected member passes failed enum getter zeroing or reference flags.");
    // Enum reset policy writes four big-endian bytes at +24. The later
    // inherited byte defaults overwrite +25/+26/+27 in registry order;
    // this reflected instance is not equivalent to the raw reset's byte3.
    check(memory_prototype->default_mode==0 && memory_prototype->flag_25==0 && memory_prototype->flag_26==1 &&
        memory_prototype->flag_27==2 && igb_bootstrap_read_word(memory_prototype_allocations.front(),0x24)==0x00000102,
        "Reflected memory defaults lost the original overlapping enum and later byte-store order.");
    check(after_enumeration.diagnostic=="Registration requires original function 0x80023234." &&
        descriptors.size()==72 && default_allocations==77 && enabled_allocations.size()==48 && f.core->types->count==15 &&
        buffer_copy_allocations==3 && data_type->class_flags==9 && data_type->fields->count==5 &&
        data_type->fields->backing->at(4)==descriptors.back() && descriptors.back()->vtable_word==0x80476088 &&
        !**data_defaults->unsigned_char_prototype && !**meta_enum->metadata,
        "Completed memory schema did not return to DataList's actual unsigned-character prototype registration boundary.");
    const auto enum_names=string_allocations,enum_defaults=default_allocations;
    const auto enum_reflections=reflected_allocations.size(),enum_buffer_calls=buffer_calls.size();
    check(!call.resume(bootstrap).completed && !call.faulted() && string_allocations==enum_names && default_allocations==enum_defaults &&
        reflected_allocations.size()==enum_reflections && buffer_calls.size()==enum_buffer_calls &&
        descriptors.size()==72 && enabled_allocations.size()==48 && f.core->types->count==15 && (**prototypes->registry)->count==8 &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending unsigned-byte registration replayed completed factories, schemas, prototypes or original bootstrap snapshots.");

    // The retained23358 getter must see the same canonical prototype cell as
    // actual232C0. Registration receives a null class cell and raw allocations;
    // every metadata/header/default store still belongs to the native callers.
    auto unsigned_byte_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_byte)]=unsigned_byte_cell;
    const auto pending_data_descriptor=descriptors[71];
    const auto pending_data_flags=std::array{igb_bootstrap_read_byte(descriptor_allocations[71],0x35),
        igb_bootstrap_read_byte(descriptor_allocations[71],0x40),igb_bootstrap_read_byte(descriptor_allocations[71],0x41)};
    std::vector<IgbBootstrapPointer> unsigned_byte_probe_locals,unsigned_byte_prototype_allocations;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::unsigned_byte,"Unsigned-byte registration replayed a completed field probe.");
        auto raw=allocation(0xba0000+static_cast<std::uint32_t>(unsigned_byte_probe_locals.size())*0x100,0x34);
        raw.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        unsigned_byte_probe_locals.push_back(raw);return raw;
    };
    unsigned_byte_registration_ready=true;
    native_lists->callbacks=f.services->callbacks;
    const auto unsigned_byte_allocation=call.resume(bootstrap);
    std::cout<<"Cold unsigned-byte prototype boundary: "<<unsigned_byte_allocation.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!unsigned_byte_allocation.completed && !call.faulted() && **unsigned_byte_cell &&
        (**unsigned_byte_cell)->class_flags==7 && (**unsigned_byte_cell)->fields->count==20 &&
        unsigned_byte_probe_locals.size()==1 && !*values->unsigned_byte_prototype &&
        unbound_reflected_allocations.back().metadata==**unsigned_byte_cell && unbound_reflected_allocations.back().bytes==0x34,
        "Unsigned-byte registration bypassed its real probe, inherited schema or reflected prototype allocator.");
    const auto unsigned_byte_type=**unsigned_byte_cell;
    check(unsigned_byte_type->name && *unsigned_byte_type->name=="igUnsignedCharMetaField" &&
        unsigned_byte_type->parent==base_field && unsigned_byte_type->native_size==0x34 && unsigned_byte_type->type_word==0x80470a44 &&
        f.core->types->count==16 && f.core->types->backing->at(15)==unsigned_byte_type &&
        !pending_data_descriptor->name && pending_data_descriptor->element_field && pending_data_descriptor->element_field->expired() &&
        igb_bootstrap_read_word(descriptor_allocations[71],0x3c)==0 &&
        pending_data_flags==std::array{igb_bootstrap_read_byte(descriptor_allocations[71],0x35),
            igb_bootstrap_read_byte(descriptor_allocations[71],0x40),igb_bootstrap_read_byte(descriptor_allocations[71],0x41)},
        "Pending unsigned-byte prototype changed DataList's later element/configuration stores or mispublished its class.");

    // Publish arena identities only, including names allocated later during
    // DataList schema publication. These bindings do not initialize a member,
    // retain a reference or substitute a ready object for reflected copying.
    live_value_bindings=values;
    for(const auto& binding:allocated_names)igb_bind_field_string(values,binding.address,binding.owner);
    for(const auto& instance:enabled_allocations)igb_bind_object_field_instance(member_construct,instance);
    bind_new_instance_addresses=true;
    const auto preceding_reflected_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,preceding_reflected_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=**unsigned_byte_cell)return preceding_reflected_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x34 &&
                unsigned_byte_prototype_allocations.empty() && !*values->unsigned_byte_prototype,
                "Unsigned-byte prototype changed its original allocation request or allocated twice.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            unsigned_byte_prototype_allocations.push_back(raw);
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    std::vector<IgbDataListKind> resumed_list_probes;
    data_registration->local_storage=[&](IgbDataListKind kind){
        check(kind==IgbDataListKind::nonref_object || kind==IgbDataListKind::thread_list,
            "Resumed list registration replayed the completed DataList probe.");
        auto raw=allocation(0xbb0000+static_cast<std::uint32_t>(resumed_list_probes.size())*0x100,0x14);
        data_probe_locals.push_back(raw);resumed_list_probes.push_back(kind);return raw;
    };
    const auto after_unsigned_byte=call.resume(bootstrap);
    std::cout<<"Cold native unsigned-byte continuation: "<<after_unsigned_byte.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_unsigned_byte.completed && !call.faulted(),"Unsigned-byte registration incorrectly completed full native bootstrap.");
    const auto unsigned_byte_prototype=*values->unsigned_byte_prototype;
    check(unsigned_byte_prototype && **data_defaults->unsigned_char_prototype==unsigned_byte_prototype &&
        unsigned_byte_prototype_allocations.size()==1 && unsigned_byte_type->instance_count==1 &&
        unsigned_byte_prototype->vtable_word==0x80470a44 && unsigned_byte_prototype->reference_word==1 &&
        unsigned_byte_prototype->ordinal==8 && unsigned_byte_prototype->type_ordinal==8 && unsigned_byte_prototype->size==1 &&
        (**prototypes->registry)->count==9 && (**prototypes->registry)->backing->at(8)==unsigned_byte_prototype &&
        unsigned_byte_prototype->default_bytes && unsigned_byte_prototype->default_bytes->buffer &&
        unsigned_byte_prototype->default_bytes->buffer->bytes.size()==1 &&
        igb_buffer_bytes(unsigned_byte_prototype->default_bytes->buffer,unsigned_byte_prototype->default_bytes->offset,1)[0]==0,
        "Unsigned-byte prototype skipped reflected initialization, shared publication, registry ordering or one-byte default storage.");
    for(std::size_t i=0;i<3;++i){const auto& field=data_type->fields->backing->at(2+i);
        check(field && field->name && *field->name==**data_defaults->names->at(i) && field->offset==*data_defaults->offsets->at(i) &&
            *data_defaults->globals->at(i)->slot==field && field->global_slot_address==data_defaults->globals->at(i)->address &&
            field->declaring_type && field->declaring_type->lock()==data_type,
            "DataList did not finish its original three-field schema and canonical global publication.");
    }
    check(pending_data_descriptor->element_field->lock()==unsigned_byte_prototype && pending_data_descriptor->flag_35==1 &&
        pending_data_descriptor->flag_40==1 && pending_data_descriptor->flag_41==1 &&
        igb_bootstrap_read_word(descriptor_allocations[71],0x3c)==igb_bootstrap_address(unsigned_byte_prototype_allocations.front()) &&
        igb_bootstrap_read_byte(descriptor_allocations[71],0x35)==1 && igb_bootstrap_read_byte(descriptor_allocations[71],0x40)==1 &&
        igb_bootstrap_read_byte(descriptor_allocations[71],0x41)==1,
        "DataList's retained prototype getter did not precede its physical and canonical element/flag stores.");

    // Actual parent linkage now needs the first enabled metadata child list.
    // Its type was registered earlier, but its instance must run the reflected
    // initializer and memory-member allocation rather than a raw constructor.
    check(!enabled_type_list_calls.empty() && enabled_type_list_calls.back().entry==IgbTypeListFactoryEntry::children &&
        enabled_type_list_calls.back().metadata==**native_lists->children_metadata &&
        enabled_type_list_calls.back().pool==f.pool && data_type->class_flags==7,
        "DataList completion did not reach the actual enabled child-list factory.");
    metadata_init->type_lists=native_lists;
    resumed_type_lists=make_native_igb_reflected_type_list_factory(metadata_init);
    const auto before_type_list_allocator=resumed_reflected_allocations;
    std::vector<IgbObjectInstance> enabled_type_list_allocations;
    std::vector<IgbPointerListBacking> enabled_type_backings;
    resumed_reflected_allocations=[&,before_type_list_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=**native_lists->children_metadata)return before_type_list_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==16,
                "Enabled child-list factory changed its actual raw allocation request.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.types=std::make_shared<IgbRuntimeTypeList>();
            instance.types->count=37;
            raw.storage->bindings.push_back({0,{},{},{},instance.types});
            enabled_type_list_allocations.push_back(instance);enabled_allocations.push_back(instance);
            igb_bind_object_field_instance(member_construct,instance);reflected_allocations.push_back(c);
            out->allocation_result=raw.storage;done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto field_list_memory=memory_construct->factory;
    bool pending_type_backing{};
    memory_construct->factory=[&,field_list_memory](const IgbMemoryFieldConstructCall& c){
        const bool type_pool=c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool && c.object.types;
        // The original allocator receives pool/size/alignment, not an object.
        // Its prepared typed mapping follows the preceding actual owner query.
        if(!type_pool && !(c.kind==IgbMemoryFieldConstructServiceKind::allocate && pending_type_backing))return field_list_memory(c);
        memory_calls.push_back(c);auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            if(c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool){
                check(c.handle==0 && enabled_identity(c.object),"Enabled child-list backing lost its fresh typed owner.");
                check(!pending_type_backing,"Enabled type backing was requested before its earlier allocation completed.");
                pending_type_backing=true;++memory_pool_queries;out->pool_result=f.pool;
            }else{
                check(c.pool==f.pool && c.bytes==64 && c.alignment==4,
                    "Enabled child-list memory constructor changed the original aligned backing request.");
                auto raw=allocation(0xbc0000+static_cast<std::uint32_t>(enabled_type_backings.size())*0x100,c.bytes);
                raw.storage->bytes->initialized.assign(c.bytes,0);
                out->backing_result={raw.storage->bytes,igb_bootstrap_address(raw),0,
                    std::make_shared<IgbRuntimeTypeList::Storage>(c.bytes/4),{}};
                enabled_type_backings.push_back(out->backing_result);pending_type_backing=false;++memory_allocations;++memory_allocation_polls;
            }
            done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto earlier_type_backing_queries=type_pointers->factory;
    type_pointers->factory=[&,earlier_type_backing_queries](const IgbPointerListCall& c){
        if(c.kind!=IgbPointerListServiceKind::allocation_size)return earlier_type_backing_queries(c);
        for(const auto& backing:enabled_type_backings)if(c.backing.address==backing.address){
            auto operation=std::make_shared<IgbPointerListOperation>();operation->resume=[&,c,backing,out=operation.get()]{
                check(c.pool==f.pool && c.backing.bytes==backing.bytes && c.backing.types==backing.types,
                    "Enabled child-list capacity query changed its physical backing identity.");
                out->size_result=static_cast<std::uint32_t>(backing.bytes->bytes.size());return MessengerCallResult{true,0,{}};
            };return operation;
        }
        return earlier_type_backing_queries(c);
    };
    const auto after_type_lists=call.resume(bootstrap);
    std::cout<<"Cold native reflected child-list continuation: "<<after_type_lists.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_type_lists.completed && !call.faulted(),"Reflected child lists incorrectly completed full native bootstrap.");
    check(after_type_lists.diagnostic=="Registration requires original function 0x80023C40." &&
        f.core->types->count==19 && descriptors.size()==73 && default_allocations==82 && enabled_allocations.size()==54 &&
        (**prototypes->registry)->count==9 && buffer_copy_allocations==4 &&
        data_type->class_flags==7 && nonref_type->class_flags==7 && thread_list_metadata->class_flags==7 &&
        f.core->types->backing->at(16)==data_type && f.core->types->backing->at(17)==nonref_type &&
        f.core->types->backing->at(18)==thread_list_metadata && !**meta_enum->metadata,
        "Completed DataList/object/thread-list registration did not reach the original deferred thread class.");
    check(nonref_type->parent==data_type && thread_list_metadata->parent==nonref_type &&
        nonref_type->fields->count==5 && thread_list_metadata->fields->count==5 &&
        resumed_list_probes==std::vector<IgbDataListKind>{IgbDataListKind::nonref_object,IgbDataListKind::thread_list} &&
        enabled_type_list_allocations.size()==2 && enabled_type_backings.size()==2 && !pending_type_backing,
        "Completed list classes lost their original inheritance, probe order or reflected child-list allocations.");
    const std::array parent_types{data_type,nonref_type},child_types{nonref_type,thread_list_metadata};
    for(std::size_t i=0;i<2;++i){const auto& instance=enabled_type_list_allocations[i];const auto& backing=enabled_type_backings[i];
        const auto& list=instance.types;const IgbBootstrapPointer physical{instance.storage,instance.offset};
        check(parent_types[i]->children && *parent_types[i]->children==list && list->count==1 &&
            list->backing==backing.types && list->backing_bytes==backing.bytes && list->backing_address==backing.address &&
            list->backing->at(0)==child_types[i] && list->backing->size()==16 &&
            igb_bootstrap_read_word(physical,0)==0x80475c54 && igb_bootstrap_read_word(physical,8)==backing.address &&
            igb_bootstrap_read_word(physical,12)==1 &&
            std::count_if(metadata_init->completed_allocations.begin(),metadata_init->completed_allocations.end(),
                [&](const auto& kept){return kept.storage==instance.storage && kept.types==list;})==1,
            "Enabled child list skipped native header/defaults, backing publication, parent append or retained identity.");
        for(std::size_t at=4;at<64;++at)check(backing.bytes->initialized[at] && backing.bytes->bytes[at]==0,
            "Enabled child-list memory construction did not zero its unused physical pointer slots.");
        for(std::size_t at=1;at<16;++at)check(!list->backing->at(at),"Enabled child-list pointer mapping lost zero initialization.");
    }
    const auto specialized_data=descriptors[72];const auto& specialized_storage=descriptor_allocations[72];
    check(specialized_data!=pending_data_descriptor && *data_defaults->nonref_data==specialized_data &&
        specialized_data->global_slot_address==0x80561734 && specialized_data->offset==16 && specialized_data->name &&
        *specialized_data->name=="_data" && specialized_data->element_field && specialized_data->element_field->lock()==prototype_field &&
        specialized_data->word_38==0 && specialized_data->flag_42==0 && specialized_data->declaring_type &&
        specialized_data->declaring_type->lock()==nonref_type && nonref_type->fields->backing->at(4)==specialized_data &&
        thread_list_metadata->fields->backing->at(4)==specialized_data && data_type->fields->backing->at(4)==pending_data_descriptor &&
        pending_data_descriptor->element_field->lock()==unsigned_byte_prototype &&
        igb_bootstrap_read_word(specialized_storage,0x1c)==0x80561734 &&
        igb_bootstrap_read_word(specialized_storage,0x3c)==igb_bootstrap_address(prototype_storage) &&
        igb_bootstrap_read_word(specialized_storage,0x38)==0 && igb_bootstrap_read_byte(specialized_storage,0x42)==0 &&
        specialized_data->default_bytes && pending_data_descriptor->default_bytes &&
        specialized_data->default_bytes->buffer!=pending_data_descriptor->default_bytes->buffer,
        "Object-list specialization skipped its actual reflected memory clone or modified the DataList source descriptor.");
    check(igb_bootstrap_read_byte(descriptor_allocations[71],0x12)==0 &&
        igb_bootstrap_read_byte(descriptor_allocations[71],0x13)==4 && pending_data_descriptor->ordinal==4 &&
        igb_bootstrap_read_byte(specialized_storage,0x12)==0 && igb_bootstrap_read_byte(specialized_storage,0x13)==4 &&
        specialized_data->ordinal==4,
        "Original layout failed to publish physical ordinals before derived-list lookup consumed the canonical data descriptor.");
    const auto finished_names=string_allocations,finished_defaults=default_allocations,finished_copies=buffer_copy_allocations;
    const auto finished_reflections=reflected_allocations.size(),finished_memory_calls=memory_calls.size();
    const auto finished_buffer_calls=buffer_calls.size(),finished_clones=reached_reflected_clones.size();
    const auto finished_type_calls=enabled_type_list_calls.size(),finished_callback_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_type_lists.diagnostic && !call.completed() && !call.faulted() &&
        string_allocations==finished_names && default_allocations==finished_defaults && buffer_copy_allocations==finished_copies &&
        reflected_allocations.size()==finished_reflections && memory_calls.size()==finished_memory_calls &&
        buffer_calls.size()==finished_buffer_calls && reached_reflected_clones.size()==finished_clones &&
        enabled_type_list_calls.size()==finished_type_calls && freed_callback_lists.size()==finished_callback_frees &&
        unsigned_byte_probe_locals.size()==1 && resumed_list_probes.size()==2 && enabled_allocations.size()==54 &&
        f.core->types->count==19 && (**prototypes->registry)->count==9 && descriptors.size()==73 &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending Thread registration replayed completed native classes, clones, child-list allocation or bootstrap snapshots.");

    // Resume the reached thread-class wrapper through its original NamedObject
    // parent. The allocator still supplies raw memory and typed identities;
    // registration, schema, layout and parent attachment establish readiness.
    auto named_registration=std::make_shared<IgbNamedTypeRegistrationServices>();
    named_registration->named_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    named_registration->thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    named_registration->dependencies=thread_dependencies;
    named_registration->types=f.services->types;named_registration->callbacks=f.services->callbacks;
    named_registration->pools=f.services->pools;named_registration->metadata=f.services->metadata;
    std::vector<IgbBootstrapPointer> named_probe_locals;
    named_registration->local_storage=[&]{
        auto raw=allocation(0xbd0000+static_cast<std::uint32_t>(named_probe_locals.size())*0x100,12);
        named_probe_locals.push_back(raw);return raw;
    };
    named_defaults=std::make_shared<IgbNamedTypeDefaultsServices>();
    named_defaults->registration=named_registration;named_defaults->schema=metadata_defaults->schema;named_defaults->values=values;
    for(const auto& instance:enabled_allocations)if(instance.metadata)
        named_defaults->physical_metadata.push_back({instance.storage,instance.offset});
    additional_callbacks=make_native_igb_named_type_registration_function_factory(named_registration,additional_callbacks);
    f.remaining=make_native_igb_named_type_registration_type_factory(named_registration,
        make_native_igb_named_type_defaults_factory(named_defaults,f.remaining));
    const auto after_named_registration=call.resume(bootstrap);
    std::cout<<"Cold native named/thread continuation: "<<after_named_registration.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_named_registration.completed && !call.faulted(),"Named/thread schemas incorrectly completed full startup.");
    const auto named_type=**named_registration->named_metadata,native_thread_type=**named_registration->thread_metadata;
    check(after_named_registration.diagnostic=="Registration requires original function 0x80022CD8." &&
        named_type && native_thread_type && named_type->class_flags==7 && native_thread_type->class_flags==9 &&
        f.core->types->count==20 && f.core->types->backing->at(19)==named_type &&
        descriptors.size()==80 && default_allocations==83 && enabled_allocations.size()==66 &&
        (**prototypes->registry)->count==9 && buffer_copy_allocations==4,
        "NamedObject registration did not reach Thread's original unsigned-integer descriptor dependency.");
    check(named_type->name && *named_type->name=="igNamedObject" && named_type->parent==object_type &&
        named_type->native_size==12 && named_type->type_word==0x8047650c && named_type->fields->count==3 &&
        named_probe_locals.size()==1 && igb_bootstrap_read_word(named_probe_locals.front(),0)==0x8047650c &&
        igb_bootstrap_read_word(named_probe_locals.front(),4)==1 && igb_bootstrap_read_word(named_probe_locals.front(),8)==0,
        "NamedObject skipped its actual base-reset probe, name initialization or inherited schema.");
    const auto name_field=descriptors[73];const auto name_storage=descriptor_allocations[73];
    const auto& named_tables=named_defaults->tables[static_cast<std::size_t>(IgbNamedTypeKind::named_object)];
    check(named_type->fields->backing->at(2)==name_field && *named_tables.globals->at(0)->slot==name_field,
        "NamedObject schema lost its canonical string-field owner or global cell.");
    check(name_field->name && *name_field->name=="_name" && name_field->vtable_word==0x8047693c &&
        name_field->offset==8 && name_field->global_slot_address==0x80561748 &&
        name_field->declaring_type && name_field->declaring_type->lock()==named_type &&
        igb_bootstrap_read_word(name_storage,0x1c)==0x80561748,
        "NamedObject string schema changed its name, offset, declaring class or physical global pointer.");
    check(name_field->default_bytes && name_field->default_bytes->buffer &&
        name_field->default_bytes->buffer!=string_prototype->default_bytes->buffer &&
        igb_buffer_word(name_field->default_bytes->buffer,name_field->default_bytes->offset)==0,
        "NamedObject null-string default did not retain an independent zero-valued buffer.");
    check(native_thread_type->name && *native_thread_type->name=="igThread" &&
        native_thread_type->parent==named_type && native_thread_type->native_size==0x30 &&
        native_thread_type->flag_1a==1 && native_thread_type->fields->count==9 &&
        named_type->children && *named_type->children && (*named_type->children)->count==1 &&
        (*named_type->children)->backing->at(0)==native_thread_type &&
        enabled_type_list_allocations.size()==3 && enabled_type_backings.size()==3 && !pending_type_backing,
        "Thread metadata skipped native parent linkage, abstract registration or the retained ten-field batch.");
    for(std::size_t i=0;i<3;++i)check(native_thread_type->fields->backing->at(i)==named_type->fields->backing->at(i),
        "Thread inherited another canonical descriptor owner.");
    for(std::size_t i=0;i<6;++i){const auto& descriptor=descriptors[74+i];
        check(native_thread_type->fields->backing->at(3+i)==descriptor && !descriptor->name &&
            descriptor->vtable_word==(i<4?0x804758e4u:0x80476630u) && descriptor->default_bytes &&
            !descriptor->default_bytes->buffer && igb_bootstrap_read_word(descriptor_allocations[74+i],0x20)==0,
            "Pending Thread batch replaced its real reflected enum/raw-reference descriptor or published a later schema.");
        if(i<4)check(descriptor->method_function==0 && igb_bootstrap_read_word(descriptor_allocations[74+i],0x34)==0,
            "Thread stored enum getter methods before its ten-field batch completed.");
    }
    const auto& thread_tables=named_defaults->tables[static_cast<std::size_t>(IgbNamedTypeKind::thread)];
    for(const auto& global:*thread_tables.globals)check(global && !*global->slot,
        "Pending Thread batch prematurely published a final schema global.");
    check(native_thread_type->factory_redirect==0 && semaphore_metadata->class_flags==0 &&
        semaphore_metadata->factory_redirect==0 && **thread_dependencies->semaphore==semaphore_metadata &&
        !**meta_enum->metadata && (*f.core->bootstrap_callbacks)->count==1 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending Thread dependency fabricated a redirect, advanced Semaphore/startup or altered bootstrap snapshots.");
    const auto named_names=string_allocations,named_buffers=default_allocations,named_copies=buffer_copy_allocations;
    const auto named_reflections=reflected_allocations.size(),named_memory_calls=memory_calls.size();
    const auto named_frees=freed_callback_lists.size(),named_schema_calls=schema_calls.size();
    check(call.resume(bootstrap).diagnostic==after_named_registration.diagnostic && !call.faulted() &&
        string_allocations==named_names && default_allocations==named_buffers && buffer_copy_allocations==named_copies &&
        reflected_allocations.size()==named_reflections && memory_calls.size()==named_memory_calls &&
        freed_callback_lists.size()==named_frees && schema_calls.size()==named_schema_calls && named_probe_locals.size()==1 &&
        f.core->types->count==20 && descriptors.size()==80 && enabled_allocations.size()==66,
        "Pending unsigned-integer registration replayed completed NamedObject work or Thread's partial field batch.");

    // Continue the same Thread batch through its unsigned-integer class.
    // The legacy factory and scalar getter share these canonical cells.
    auto unsigned_int_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata=unsigned_int_cell;
    std::vector<IgbBootstrapPointer> unsigned_int_probe_locals,unsigned_int_prototype_allocations;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::unsigned_int,"Unsigned-integer registration replayed a completed field probe.");
        auto raw=allocation(0xbe0000+static_cast<std::uint32_t>(unsigned_int_probe_locals.size())*0x100,0x34);
        raw.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        unsigned_int_probe_locals.push_back(raw);return raw;
    };
    unsigned_int_registration_ready=true;
    const auto unsigned_int_allocation=call.resume(bootstrap);
    std::cout<<"Cold unsigned-integer prototype boundary: "<<unsigned_int_allocation.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    check(!unsigned_int_allocation.completed && !call.faulted() && **unsigned_int_cell &&
        (**unsigned_int_cell)->class_flags==7 && (**unsigned_int_cell)->fields->count==20 &&
        unsigned_int_probe_locals.size()==1 && !*values->fields->unsigned_int_prototype &&
        unbound_reflected_allocations.back().metadata==**unsigned_int_cell && unbound_reflected_allocations.back().bytes==0x34,
        "Unsigned-integer class skipped its actual inherited schema, probe or reflected prototype allocator.");
    const auto unsigned_int_type=**unsigned_int_cell;
    check(unsigned_int_type->parent==base_field && unsigned_int_type->name && *unsigned_int_type->name=="igUnsignedIntMetaField" &&
        unsigned_int_type->type_word==0x80476e6c && unsigned_int_type->native_size==0x34 &&
        igb_bootstrap_read_word(unsigned_int_probe_locals.front(),0)==0x80471914 &&
        igb_field_type_metadata_slot(field_types,IgbFieldTypeKind::unsigned_int)==unsigned_int_cell &&
        igb_field_prototype_slot(prototypes,IgbFieldTypeKind::unsigned_int).get()==&values->fields->unsigned_int_prototype,
        "Unsigned-integer lifecycle lost its original class or shared factory/scalar cells.");

    const auto before_unsigned_int_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,before_unsigned_int_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=unsigned_int_type)return before_unsigned_int_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x34,
                "Unsigned-integer reflected allocation changed its original request.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            if(!*values->fields->unsigned_int_prototype)unsigned_int_prototype_allocations.push_back(raw);
            else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto after_unsigned_int=call.resume(bootstrap);
    std::cout<<"Cold native unsigned-integer continuation: "<<after_unsigned_int.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", buffer clones="<<buffer_copy_allocations<<'\n';
    check(!after_unsigned_int.completed && !call.faulted(),"Unsigned-integer continuation incorrectly completed full startup.");
    check(after_unsigned_int.diagnostic=="Registration requires original function 0x80023A30." &&
        f.core->types->count==23 && descriptors.size()==84 && default_allocations==94 && enabled_allocations.size()==73 &&
        (**prototypes->registry)->count==10 && buffer_copy_allocations==4 &&
        f.core->types->backing->at(20)==unsigned_int_type && f.core->types->backing->at(21)==native_thread_type &&
        f.core->types->backing->at(22)==semaphore_metadata,
        "Unsigned-integer completion did not retain publication order through the real ThreadManager startup boundary.");
    check(unsigned_int_prototype_allocations.size()==1 && unsigned_int_type->instance_count==2 &&
        native_thread_type->class_flags==7 && native_thread_type->fields->count==13 && native_thread_type->factory_redirect==0x80023e20 &&
        semaphore_metadata->class_flags==7 && semaphore_metadata->fields->count==3 && semaphore_metadata->parent==named_type &&
        semaphore_metadata->flag_1a==1 && semaphore_metadata->native_size==12 && semaphore_metadata->factory_redirect==0x80026160 &&
        (*named_type->children)->count==2 && (*named_type->children)->backing->at(1)==semaphore_metadata,
        "Thread/Semaphore metadata did not complete original inheritance, schema or redirect publication.");
    const auto unsigned_int_prototype=*values->fields->unsigned_int_prototype;
    const auto unsigned_int_storage=unsigned_int_prototype_allocations.front();
    check(unsigned_int_prototype && (**prototypes->registry)->backing->at(9)==unsigned_int_prototype &&
        unsigned_int_prototype->ordinal==9 && unsigned_int_prototype->type_ordinal==9 && unsigned_int_prototype->size==4 &&
        unsigned_int_prototype->reference_word==1 && igb_bootstrap_read_word(unsigned_int_storage,0)==0x80476e6c &&
        igb_bootstrap_read_word(unsigned_int_storage,4)==1 &&
        igb_bootstrap_read_word(unsigned_int_storage,0x10)==0x00090009,
        "Unsigned-integer prototype skipped reflected construction, physical ownership or ordinal finalization.");
    for(const auto& type:{native_thread_type,semaphore_metadata}){
        const auto found=std::find_if(named_defaults->physical_metadata.begin(),named_defaults->physical_metadata.end(),
            [&](const auto& allocation){return igb_bootstrap_metadata(allocation)==type;});
        check(found!=named_defaults->physical_metadata.end(),"Completed named metadata lost its physical identity.");
        check(igb_bootstrap_read_word(*found,0x3c)==*type->factory_redirect,
            "Completed thread/semaphore redirect left stale physical metadata.");
    }
    const auto thread_identity=std::find_if(values->fields->pointer_lists->values.begin(),values->fields->pointer_lists->values.end(),
        [&](const auto& value){return value.metadata==native_thread_type;});
    check(thread_identity!=values->fields->pointer_lists->values.end(),"Thread schema lost its arithmetic declaring-type identity.");
    for(std::size_t i=0;i<10;++i){const auto& descriptor=descriptors[74+i];const auto raw=descriptor_allocations[74+i];
        check(native_thread_type->fields->backing->at(3+i)==descriptor && *thread_tables.globals->at(i)->slot==descriptor &&
            descriptor->name && *descriptor->name==**thread_tables.names->at(i) && descriptor->offset==*thread_tables.offsets->at(i) &&
            descriptor->declaring_type && descriptor->declaring_type->lock()==native_thread_type &&
            descriptor->default_bytes && descriptor->default_bytes->buffer &&
            igb_bootstrap_read_word(raw,8)==descriptor->offset && igb_bootstrap_read_word(raw,0x18)==thread_identity->address &&
            igb_bootstrap_read_word(raw,0x1c)==thread_tables.globals->at(i)->address,
            "Thread batch did not finalize its ten original fields and physical schema words.");
        if(i<4){constexpr std::array<std::uint32_t,4> methods{0x80023a70,0x80023b4c,0x80023b00,0x80023abc};
            check(descriptor->method_function==methods[i] && igb_bootstrap_read_word(raw,0x34)==methods[i] &&
                igb_buffer_word(descriptor->default_bytes->buffer,descriptor->default_bytes->offset)==(i==0?0x80u:0u),
                "Thread enum setter or getter-word publication differs from the original defaults.");}
    }
    const auto stack_size=descriptors[80];
    check(stack_size->vtable_word==0x80476e6c && stack_size->type_ordinal==9 &&
        igb_buffer_word(stack_size->default_bytes->buffer,stack_size->default_bytes->offset)==0x1000 &&
        stack_size->default_bytes->buffer!=unsigned_int_prototype->default_bytes->buffer &&
        descriptors[0]->type_ordinal==0xffff && descriptors[1]->type_ordinal==0xffff,
        "Thread stack default failed to use its registered unsigned-integer type or rewrote older descriptors.");
    const auto completed_names=string_allocations,completed_defaults=default_allocations;
    const auto completed_reflections=reflected_allocations.size(),completed_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_unsigned_int.diagnostic && !call.faulted() &&
        f.core->types->count==23 && descriptors.size()==84 && default_allocations==completed_defaults &&
        enabled_allocations.size()==73 && (**prototypes->registry)->count==10 && string_allocations==completed_names &&
        reflected_allocations.size()==completed_reflections && freed_callback_lists.size()==completed_frees &&
        unsigned_int_probe_locals.size()==1 && unsigned_int_prototype_allocations.size()==1 &&
        !**meta_enum->metadata && (*f.core->bootstrap_callbacks)->count==1 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending ThreadManager startup replayed completed classes, prototypes, schemas or bootstrap snapshots.");

    // Continue the retained activation callback through the original pool,
    // factory, redirect and concrete manager registrar. The pool registry is
    // explicit test preparation; no completed manager/thread object is seeded.
    auto startup=std::make_shared<IgbThreadStartupServices>();
    startup->manager_metadata=std::shared_ptr<IgbRegistrationMetaSlot>(thread,&thread->metadata);
    startup->thread_metadata=named_registration->thread_metadata;
    startup->callbacks=f.services->callbacks;startup->reflected=prototypes->reflected;startup->pools=f.services->pools;
    IgbThreadStartupFactory concrete_thread_adoption;
    std::vector<IgbThreadStartupCall> startup_virtuals;
    const auto startup_services=make_native_igb_thread_startup_service_factory(startup);
    startup->factory=[&,startup_services](const IgbThreadStartupCall& c){
        if(c.kind==IgbThreadStartupServiceKind::object_virtual)startup_virtuals.push_back(c);
        if(concrete_thread_adoption){const auto operation=concrete_thread_adoption(c);if(operation)return operation;}
        return startup_services(c);
    };
    additional_callbacks=make_native_igb_thread_startup_function_factory(startup,additional_callbacks);
    const auto unknown_handle=call.resume(bootstrap);
    check(!unknown_handle.completed && unknown_handle.diagnostic=="Thread startup requires live pool handle805621F0." &&
        !startup->manager && !startup->pool_handle && f.core->types->count==23,
        "Thread activation invented a pool handle or advanced past its first original load.");
    publish_original_igb_thread_pool_handle(*startup);
    const auto unknown_pool=call.resume(bootstrap);
    check(!unknown_pool.completed && startup->pool_handle==4 && f.services->types->metadata_pool_handle==5 &&
        unknown_pool.diagnostic=="Pool-by-handle access requires its captured physical manager slot." && !startup->manager,
        "Thread startup confused handle4 with the separately initialized metadata handle5.");
    f.manager->pools[4]=f.pool;
    const auto allocation_factory=prototypes->reflected->factory;
    std::vector<IgbFileFactoryCall> startup_redirects;
    IgbFileFactoryFactory concrete_redirect;
    prototypes->reflected->factory=[&,allocation_factory](const IgbFileFactoryCall& c){
        if(c.kind!=IgbFileFactoryServiceKind::redirect_type)return allocation_factory(c);
        startup_redirects.push_back(c);return concrete_redirect?concrete_redirect(c):std::shared_ptr<IgbFileFactoryOperation>{};
    };
    const auto manager_redirect=call.resume(bootstrap);
    check(!manager_redirect.completed && !call.faulted() && startup_redirects.size()==1 &&
        startup_redirects.front().metadata==*thread->metadata && startup_redirects.front().redirect==0x80023a10 &&
        manager_redirect.diagnostic=="Reflected factory requires its captured metadata+3C redirect callback." &&
        !startup->manager && enabled_allocations.size()==73,
        "Actual manager factory skipped registration/abstract redirect or fabricated a returned runtime manager.");

    platform_thread=std::make_shared<IgbPlatformThreadRegistrationServices>();
    platform_thread->state=std::make_shared<IgbPlatformThreadRegistrationState>();
    platform_thread->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    platform_thread->parent=thread;platform_thread->callbacks=f.services->callbacks;platform_thread->types=f.services->types;
    std::vector<IgbBootstrapPointer> platform_manager_probes;
    platform_thread->local_storage=[&]{
        auto raw=allocation(0xbf0000+static_cast<std::uint32_t>(platform_manager_probes.size())*0x100,16);
        platform_manager_probes.push_back(raw);return raw;
    };
    concrete_redirect=make_native_igb_platform_thread_redirect_factory(platform_thread);
    additional_callbacks=make_native_igb_platform_thread_registration_function_factory(platform_thread,additional_callbacks);
    f.remaining=make_native_igb_platform_thread_registration_type_factory(platform_thread,f.remaining);
    const auto after_platform_manager=call.resume(bootstrap);
    std::cout<<"Cold native ThreadManager activation: "<<after_platform_manager.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<'\n';
    check(!after_platform_manager.completed && !call.faulted() && platform_thread->state->metadata &&
        *platform_thread->state->metadata,"Concrete manager redirect did not enter its actual registrar.");
    const auto concrete_manager=*platform_thread->state->metadata;
    check(concrete_manager->name && *concrete_manager->name=="igGamecubeThreadManager" &&
        concrete_manager->class_flags==7 && concrete_manager->flag_1a==0 && concrete_manager->native_size==16 &&
        concrete_manager->parent==*thread->metadata && concrete_manager->fields->count==4 &&
        concrete_manager->type_word==0x804700a8 && concrete_manager->word_40==0x80023920 &&
        concrete_manager->factory_redirect==0 && f.core->types->count==24 && f.core->types->backing->at(23)==concrete_manager,
        "Concrete manager registration lost its parent schema, physical probe, default getter or publication order.");
    check(platform_manager_probes.size()==1 && igb_bootstrap_read_word(platform_manager_probes.front(),0)==0x804700a8 &&
        igb_bootstrap_read_word(platform_manager_probes.front(),4)==1 && platform_thread->physical_metadata.size()==1 &&
        igb_bootstrap_read_word(platform_thread->physical_metadata.front(),0x40)==0x80023920,
        "Concrete manager probe/defaults did not perform the original physical stores.");
    for(std::size_t i=0;i<4;++i)check(concrete_manager->fields->backing->at(i)==(*thread->metadata)->fields->backing->at(i),
        "Concrete manager changed its inherited descriptor identity.");
    check(after_platform_manager.diagnostic=="Reflected factory requires the captured pool allocation virtual+D8." &&
        unbound_reflected_allocations.back().metadata==concrete_manager && unbound_reflected_allocations.back().pool==f.pool &&
        unbound_reflected_allocations.back().bytes==16 && !startup->manager && concrete_manager->instance_count==0 &&
        descriptors.size()==84 && default_allocations==94 && (**prototypes->registry)->count==10 &&
        !**meta_enum->metadata && (*f.core->bootstrap_callbacks)->count==1,
        "Thread startup fabricated runtime allocation/publication or advanced to thread creation before the manager returned.");
    const auto platform_allocations=enabled_allocations.size(),platform_reflections=reflected_allocations.size();
    const auto platform_names=string_allocations;const auto platform_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_platform_manager.diagnostic && !call.faulted() &&
        platform_manager_probes.size()==1 && platform_thread->physical_metadata.size()==1 &&
        enabled_allocations.size()==platform_allocations && reflected_allocations.size()==platform_reflections &&
        string_allocations==platform_names && freed_callback_lists.size()==platform_frees &&
        f.core->types->count==24 && descriptors.size()==84 && default_allocations==94 &&
        !startup->manager && !**meta_enum->metadata &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending concrete manager allocation replayed registration or changed startup snapshots.");

    // Supply raw runtime allocations with distinct native identities. Their
    // member bytes remain unknown until the real reflected field passes run.
    auto runtime_init=std::make_shared<IgbThreadRuntimeInitServices>();
    runtime_init->manager_registration=platform_thread;runtime_init->dependencies=thread_dependencies;
    runtime_init->objects=member_construct;
    const auto preceding_runtime_allocator=resumed_reflected_allocations;
    std::vector<IgbObjectInstance> runtime_allocations;
    resumed_reflected_allocations=[&,preceding_runtime_allocator](const IgbFileFactoryCall& c){
        const bool manager=c.metadata==concrete_manager,list=c.metadata==thread_list_metadata;
        if(!manager && !list)return preceding_runtime_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,manager,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==(manager?16u:20u),
                "Runtime manager/list allocator changed the reflected size or captured pool.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            raw.storage->bytes->initialized.assign(c.bytes,0);
            IgbObjectInstance instance;instance.storage=raw.storage;
            instance.runtime_object=std::make_shared<IgbRuntimeObject>(manager?IgbRuntimeObjectKind::thread_manager:IgbRuntimeObjectKind::thread_list);
            instance.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{raw.storage,0};
            IgbObjectBinding binding;binding.runtime_object=instance.runtime_object;raw.storage->bindings.push_back(binding);
            enabled_allocations.push_back(instance);runtime_allocations.push_back(instance);
            igb_bind_object_field_instance(member_construct,instance);reflected_allocations.push_back(c);
            out->allocation_result=raw.storage;done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto preceding_runtime_member=member_construct->factory;
    member_construct->factory=[&,preceding_runtime_member](const IgbObjectFieldConstructCall& c){
        if(c.kind!=IgbObjectFieldConstructServiceKind::tagged_pool || !c.object.runtime_object)return preceding_runtime_member(c);
        member_calls.push_back(c);auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
        operation->resume=[&,c,out=operation.get()]{
            check(c.handle==0 && enabled_identity(c.object) && c.object.runtime_object->kind==IgbRuntimeObjectKind::thread_manager,
                "Manager member construction did not use its own physical pool tag.");
            out->pool_result=f.pool;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto preceding_runtime_init=prototypes->reflected->initializer->factory;
    IgbObjectInitFactory runtime_memory_defaults,runtime_struct_defaults;
    bool semaphore_runtime_ready{};
    const IgbObjectInitFactory runtime_fallback=[&,preceding_runtime_init](const IgbObjectInitCall& c){
            if(runtime_memory_defaults){const auto operation=runtime_memory_defaults(c);if(operation)return operation;}
            if(runtime_struct_defaults){const auto operation=runtime_struct_defaults(c);if(operation)return operation;}
            return preceding_runtime_init(c);
        };
    const auto native_runtime_init=make_native_igb_thread_runtime_init_factory(runtime_init,runtime_fallback);
    prototypes->reflected->initializer->factory=[&,native_runtime_init,runtime_fallback](const IgbObjectInitCall& c){
        if(c.kind==IgbObjectInitServiceKind::object_virtual && c.vtable_word==0x80470314 && !semaphore_runtime_ready)
            return runtime_fallback(c);
        return native_runtime_init(c);
    };
    const auto after_runtime_manager=call.resume(bootstrap);
    std::cout<<"Cold native manager runtime initialization: "<<after_runtime_manager.diagnostic<<"; types="<<f.core->types->count<<
        ", runtime allocations="<<runtime_allocations.size()<<", enabled allocations="<<enabled_allocations.size()<<'\n';
    if(!reflected_init_calls.empty()){const auto& reached=reflected_init_calls.back();
        std::cout<<"Runtime reached init kind="<<static_cast<unsigned>(reached.kind)<<", slot="<<reached.virtual_slot<<
            ", field="<<(reached.field && reached.field->name?*reached.field->name:std::string("none"))<<'\n';}
    check(!after_runtime_manager.completed && !call.faulted() && runtime_allocations.size()==2 &&
        !startup->manager && concrete_manager->instance_count==0,
        "Runtime manager construction skipped nested list initialization or published a premature singleton.");
    const auto manager_instance=runtime_allocations[0],thread_list_instance=runtime_allocations[1];
    const IgbBootstrapPointer manager_storage{manager_instance.storage,manager_instance.offset};
    const IgbBootstrapPointer thread_list_storage{thread_list_instance.storage,thread_list_instance.offset};
    const auto data_descriptor=thread_list_metadata->fields->backing->at(4);
    check(reflected_init_calls.back().kind==IgbObjectInitServiceKind::field_virtual &&
        reflected_init_calls.back().virtual_slot==0x70 && reflected_init_calls.back().field==data_descriptor &&
        data_descriptor->vtable_word==0x80476088 && data_descriptor->flag_34==0 && data_descriptor->flag_42==0 &&
        data_descriptor->flag_41==1 && data_descriptor->default_mode==0 &&
        igb_buffer_word(data_descriptor->default_bytes->buffer,data_descriptor->default_bytes->offset)==0,
        "Nested thread list did not reach the actual non-owning memory default branch.");
    check(igb_bootstrap_read_word(manager_storage,0)==0x804700a8 && igb_bootstrap_read_word(manager_storage,4)==1 &&
        igb_bootstrap_read_word(manager_storage,8)==0 && igb_bootstrap_read_word(manager_storage,12)==0 &&
        igb_bootstrap_read_word(thread_list_storage,0)==0x80476ce0 && igb_bootstrap_read_word(thread_list_storage,4)==1 &&
        igb_bootstrap_read_word(thread_list_storage,8)==0 && igb_bootstrap_read_word(thread_list_storage,12)==0 &&
        igb_bootstrap_read_word(thread_list_storage,16)==0 && thread_list_metadata->instance_count==0 &&
        !manager_instance.runtime_object->construction_active_.expired() && !thread_list_instance.runtime_object->construction_active_.expired(),
        "Pending list defaults skipped reflected headers, both manager clears, scalar defaults or construction leases.");
    const auto before_runtime_buffers=buffer_calls.size(),before_runtime_memory=memory_calls.size();
    runtime_memory_defaults=make_native_igb_memory_member_defaults_factory(memory_copies);
    const auto after_runtime_list=call.resume(bootstrap);
    std::cout<<"Cold native runtime thread-list completion: "<<after_runtime_list.diagnostic<<"; types="<<f.core->types->count<<
        ", runtime allocations="<<runtime_allocations.size()<<", enabled allocations="<<enabled_allocations.size()<<
        ", list instances="<<thread_list_metadata->instance_count.value_or(0xffffffffu)<<'\n';
    check(!after_runtime_list.completed && !call.faulted() && startup_redirects.back().metadata==semaphore_metadata &&
        startup_redirects.back().redirect==0x80026160 &&
        after_runtime_list.diagnostic=="Reflected factory requires its captured metadata+3C redirect callback.",
        "Constructed thread list did not resume actual semaphore construction through its concrete metadata redirect.");
    check(thread_list_metadata->instance_count==1 && concrete_manager->instance_count==0 &&
        igb_bootstrap_read_word(manager_storage,8)==igb_bootstrap_address(thread_list_storage) &&
        igb_bootstrap_read_word(manager_storage,12)==0 && igb_bootstrap_read_word(thread_list_storage,4)==1 &&
        thread_list_instance.runtime_object->reference_word==1 && thread_list_instance.runtime_object->vtable_word==0x80476ce0 &&
        thread_list_instance.runtime_object->construction_active_.expired() &&
        !manager_instance.runtime_object->construction_active_.expired() && !startup->manager,
        "Runtime list assignment skipped original ownership, physical publication or initializer completion.");
    check(buffer_calls.size()==before_runtime_buffers && memory_calls.size()==before_runtime_memory &&
        runtime_allocations.size()==2 && enabled_allocations.size()==78 &&
        f.core->types->count==24 && descriptors.size()==84 && default_allocations==94 && (**prototypes->registry)->count==10,
        "Actual null memory default allocated a substitute backing or changed completed registrations.");
    const auto finished_runtime_members=member_calls.size(),finished_runtime_reflections=reflected_allocations.size();
    check(call.resume(bootstrap).diagnostic==after_runtime_list.diagnostic && !call.faulted() &&
        member_calls.size()==finished_runtime_members && reflected_allocations.size()==finished_runtime_reflections &&
        thread_list_metadata->instance_count==1 && concrete_manager->instance_count==0 &&
        runtime_allocations.size()==2 && enabled_allocations.size()==78 && !startup->manager && !**meta_enum->metadata &&
        (*f.core->bootstrap_callbacks)->count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending semaphore redirect replayed runtime construction, list ownership or earlier startup work.");

    // The concrete redirect performs actual class registration. Its fourth
    // local descriptor still requires the original StructMetaField factory.
    platform_semaphore=std::make_shared<IgbPlatformSemaphoreRegistrationServices>();
    platform_semaphore->state=std::make_shared<IgbPlatformSemaphoreRegistrationState>();
    platform_semaphore->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    platform_semaphore->dependencies=thread_dependencies;platform_semaphore->types=f.services->types;
    platform_semaphore->callbacks=f.services->callbacks;platform_semaphore->schema=metadata_defaults->schema;
    std::vector<IgbBootstrapPointer> platform_semaphore_probes;
    platform_semaphore->local_storage=[&]{
        auto raw=allocation(0xc00000+static_cast<std::uint32_t>(platform_semaphore_probes.size())*0x100,0x24);
        // Only the existing reference header is represented before6665C;
        // untouched derived fields must not acquire fabricated values.
        raw.storage->bytes->initialized.assign(0x24,0);
        for(unsigned i=4;i<8;++i)raw.storage->bytes->initialized[i]=1;
        platform_semaphore_probes.push_back(raw);return raw;
    };
    concrete_redirect=make_native_igb_platform_semaphore_redirect_factory(platform_semaphore,concrete_redirect);
    additional_callbacks=make_native_igb_platform_semaphore_registration_function_factory(platform_semaphore,additional_callbacks);
    f.remaining=make_native_igb_platform_semaphore_registration_type_factory(platform_semaphore,f.remaining);
    const auto after_platform_semaphore=call.resume(bootstrap);
    std::cout<<"Cold native concrete semaphore registration: "<<after_platform_semaphore.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", probes="<<platform_semaphore_probes.size()<<'\n';
    check(!after_platform_semaphore.completed && !call.faulted() && platform_semaphore->state->metadata &&
        *platform_semaphore->state->metadata && !startup->manager && runtime_allocations.size()==2 &&
        thread_list_metadata->instance_count==1 && concrete_manager->instance_count==0,
        "Concrete semaphore registration skipped its real field dependency or fabricated a runtime instance.");
    const auto concrete_semaphore=*platform_semaphore->state->metadata;
    std::cout<<"Concrete semaphore pending state: flags="<<unsigned(concrete_semaphore->class_flags)<<
        ", fields="<<(concrete_semaphore->fields?concrete_semaphore->fields->count:-1)<<
        ", size="<<concrete_semaphore->native_size<<'\n';
    check(after_platform_semaphore.diagnostic=="Field schema reached an unbound native operation." &&
        schema_calls.back().kind==IgbFieldSchemaServiceKind::create_field && schema_calls.back().function==0x8002450c &&
        schema_calls.back().pool==f.pool && concrete_semaphore->class_flags==9 && concrete_semaphore->native_size==0x24 &&
        concrete_semaphore->type_word==0x80470314 && concrete_semaphore->field_callback==0x80026160 &&
        concrete_semaphore->fields && concrete_semaphore->fields->count==6 &&
        concrete_semaphore->instance_count==0 && descriptors.size()==87 && default_allocations==94 && enabled_allocations.size()==84,
        "Concrete semaphore did not reach its fourth StructMetaField factory after actual parent inheritance and three descriptor allocations.");
    check(platform_semaphore_probes.size()==1 && platform_semaphore->physical_metadata.size()==1 &&
        igb_bootstrap_read_word(platform_semaphore_probes.front(),0)==0x8047650c &&
        igb_bootstrap_read_word(platform_semaphore_probes.front(),4)==1 &&
        igb_bootstrap_read_word(platform_semaphore_probes.front(),8)==0 &&
        std::all_of(platform_semaphore_probes.front().storage->bytes->initialized.begin()+12,
            platform_semaphore_probes.front().storage->bytes->initialized.end(),[](auto byte){return byte==0;}),
        "Concrete semaphore probe lost the saved derived type, named-table restoration or unknown untouched members.");
    for(unsigned i=0;i<3;++i)check(concrete_semaphore->fields->backing->at(i)==semaphore_metadata->fields->backing->at(i),
        "Concrete semaphore replaced its inherited named/base descriptor identity.");
    for(const auto& global:*platform_semaphore->globals)check(global && global->slot && !*global->slot,
        "Incomplete semaphore field batch published a schema global prematurely.");
    const auto semaphore_allocations=enabled_allocations.size(),semaphore_descriptors=descriptors.size();
    const auto semaphore_names=string_allocations;
    const auto semaphore_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_platform_semaphore.diagnostic && !call.faulted() &&
        enabled_allocations.size()==semaphore_allocations && descriptors.size()==semaphore_descriptors &&
        string_allocations==semaphore_names && freed_callback_lists.size()==semaphore_frees &&
        platform_semaphore_probes.size()==1 && f.core->types->count==24 && concrete_semaphore->fields->count==6 &&
        concrete_semaphore->class_flags==9 && concrete_semaphore->instance_count==0 &&
        runtime_allocations.size()==2 && !startup->manager && !**meta_enum->metadata &&
        igb_bootstrap_read_word(manager_storage,12)==0 && igb_bootstrap_read_word(ark.allocation,4)==7 &&
        igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending StructMetaField replayed semaphore registration or fabricated runtime manager completion.");

    // Enable the reached original struct factory and its actual defaults.
    // The inherited unsigned-short clone remains a distinct real dependency.
    auto structure_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::structure)]=structure_cell;
    std::vector<IgbBootstrapPointer> structure_probe_locals;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::structure,"Struct-field registration replayed an earlier field probe.");
        auto raw=allocation(0xc10000+static_cast<std::uint32_t>(structure_probe_locals.size())*0x100,0x40);
        raw.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        std::fill(raw.storage->bytes->initialized.begin()+0x34,raw.storage->bytes->initialized.end(),0);
        structure_probe_locals.push_back(raw);return raw;
    };
    auto structure_defaults=std::make_shared<IgbStructFieldDefaultsServices>();
    structure_defaults->registration=field_types;structure_defaults->lists=list_defaults;
    f.remaining=make_native_igb_struct_field_defaults_factory(structure_defaults,f.remaining);
    structure_constructor_ready=true;
    const auto after_structure_defaults=call.resume(bootstrap);
    std::cout<<"Cold native struct-field defaults: "<<after_structure_defaults.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", probes="<<structure_probe_locals.size()<<'\n';
    check(!after_structure_defaults.completed && !call.faulted() && *structure_cell && **structure_cell &&
        !startup->manager && runtime_allocations.size()==2 && concrete_semaphore->fields->count==6,
        "Struct-field registration fabricated its missing clone or completed runtime semaphore construction.");
    const auto structure_type=**structure_cell;
    std::cout<<"Struct-field pending state: flags="<<unsigned(structure_type->class_flags)<<
        ", fields="<<(structure_type->fields?structure_type->fields->count:-1)<<
        ", size="<<structure_type->native_size<<'\n';
    check(after_structure_defaults.diagnostic=="Struct-field defaults reached an unbound original clone or field virtual." &&
        list_defaults_calls.back().kind==IgbListDefaultsServiceKind::clone_field &&
        list_defaults_calls.back().vtable_word==0x80477054 && list_defaults_calls.back().virtual_slot==0x54 &&
        list_defaults_calls.back().argument==1 && list_defaults_calls.back().field &&
        list_defaults_calls.back().field->name && *list_defaults_calls.back().field->name=="_size" &&
        structure_type->class_flags==9 && structure_type->native_size==0x40 && structure_type->type_word==0x80470e00 &&
        structure_type->parent==**field_types->base_metadata && structure_type->fields->count==23 &&
        descriptors.size()==90 && default_allocations==94 && enabled_allocations.size()==89,
        "Struct-field defaults did not append all three local descriptors before the original unsigned-short size clone.");
    check(structure_probe_locals.size()==1 && igb_bootstrap_read_word(structure_probe_locals.front(),0)==0x80471914 &&
        igb_bootstrap_read_word(structure_probe_locals.front(),4)==1 && igb_bootstrap_read_word(structure_probe_locals.front(),12)==0 &&
        std::all_of(structure_probe_locals.front().storage->bytes->initialized.begin()+0x34,
            structure_probe_locals.front().storage->bytes->initialized.end(),[](auto byte){return byte==0;}),
        "Struct probe invented derived field values, skipped name cleanup or returned its final base table as the type word.");
    for(unsigned i=0;i<20;++i)check(structure_type->fields->backing->at(i)==(**field_types->base_metadata)->fields->backing->at(i),
        "Pending inherited size clone modified the base schema or replaced an inherited descriptor early.");
    check(structure_type->fields->backing->at(20)->vtable_word==0x80475b60 &&
        structure_type->fields->backing->at(21)->vtable_word==0x80476630 && structure_type->fields->backing->at(22)->vtable_word==0x80476630 &&
        !*structure_defaults->size_descriptor && structure_type->instance_count==0 &&
        concrete_semaphore->class_flags==9 && concrete_semaphore->fields->count==6 && concrete_semaphore->instance_count==0,
        "Pending struct defaults fabricated specialization, a ready descriptor instance or semaphore completion.");
    for(const auto& global:*structure_defaults->globals)check(global && global->slot && !*global->slot,
        "Pending size clone published a struct local schema global.");
    const auto structure_allocations=enabled_allocations.size(),structure_reflections=reflected_allocations.size();
    const auto structure_names=string_allocations;
    check(call.resume(bootstrap).diagnostic==after_structure_defaults.diagnostic && !call.faulted() &&
        enabled_allocations.size()==structure_allocations && reflected_allocations.size()==structure_reflections &&
        string_allocations==structure_names && descriptors.size()==90 && default_allocations==94 && f.core->types->count==24 &&
        structure_probe_locals.size()==1 && structure_type->fields->count==23 && !*structure_defaults->size_descriptor &&
        runtime_allocations.size()==2 && thread_list_metadata->instance_count==1 && concrete_manager->instance_count==0 &&
        !startup->manager && !**meta_enum->metadata && igb_bootstrap_read_word(manager_storage,12)==0 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending unsigned-short clone replayed struct construction or advanced runtime startup.");

    // Continue the reached clone through actual unsigned-short registration,
    // prototype construction and reflected copy. Only raw storage is supplied.
    std::optional<IgbPointerListBacking> expanded_base_field_children;
    const auto before_unsigned_short_type_allocator=type_pointers->factory;
    type_pointers->factory=[&,before_unsigned_short_type_allocator](const IgbPointerListCall& c){
        const bool query=c.kind==IgbPointerListServiceKind::allocation_size && expanded_base_field_children &&
            c.backing.address==expanded_base_field_children->address;
        const bool grow=c.kind==IgbPointerListServiceKind::reallocate && base_field->children && c.types==*base_field->children;
        if(!query && !grow)return before_unsigned_short_type_allocator(c);
        auto operation=std::make_shared<IgbPointerListOperation>();operation->resume=[&,c,query,out=operation.get()]{
            if(query){check(c.backing.bytes==expanded_base_field_children->bytes && c.pool==f.pool,
                "Expanded base-field child query lost its physical allocation.");out->size_result=64;}
            else{
                check(!expanded_base_field_children && c.types->count==8 && c.pool==f.pool && c.bytes==64 &&
                    c.backing.offset==0 && c.backing.bytes==grown_base_field_children->bytes && c.backing.types==grown_base_field_children->types,
                    "Ninth base-field child changed its original reallocation request.");
                auto raw=allocation(0xc30000,c.bytes);raw.storage->bytes->initialized.assign(c.bytes,0);
                std::copy(c.backing.bytes->bytes.begin(),c.backing.bytes->bytes.end(),raw.storage->bytes->bytes.begin());
                std::copy(c.backing.bytes->initialized.begin(),c.backing.bytes->initialized.end(),raw.storage->bytes->initialized.begin());
                auto mapping=std::make_shared<IgbRuntimeTypeList::Storage>(16);
                std::copy(c.backing.types->begin(),c.backing.types->end(),mapping->begin());
                expanded_base_field_children=IgbPointerListBacking{raw.storage->bytes,igb_bootstrap_address(raw),0,mapping,{}};
                out->backing_result=*expanded_base_field_children;
            }return MessengerCallResult{true,0,{}};
        };return operation;
    };
    auto unsigned_short_cell=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    object_defaults->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_short)]=unsigned_short_cell;
    values->unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> unsigned_short_probe_locals,unsigned_short_prototype_allocations;
    field_probe->profile_local_storage=[&](IgbMetaFieldKind kind){
        check(kind==IgbMetaFieldKind::unsigned_short,"Unsigned-short registration replayed an earlier field probe.");
        auto raw=allocation(0xc20000+static_cast<std::uint32_t>(unsigned_short_probe_locals.size())*0x100,0x34);
        raw.storage->bindings.push_back({0,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        unsigned_short_probe_locals.push_back(raw);return raw;
    };
    const auto before_unsigned_short_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,before_unsigned_short_allocator](const IgbFileFactoryCall& c){
        if(!*unsigned_short_cell || c.metadata!=**unsigned_short_cell)return before_unsigned_short_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x34,
                "Unsigned-short reflected allocation changed its actual pool, size or operation.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            if(!*values->unsigned_short_prototype)unsigned_short_prototype_allocations.push_back(raw);
            else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    unsigned_short_clone_ready=true;
    const auto after_unsigned_short=call.resume(bootstrap);
    std::cout<<"Cold native unsigned-short continuation: "<<after_unsigned_short.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", copies="<<buffer_copy_allocations<<'\n';
    check(!after_unsigned_short.completed && !call.faulted(),"Unsigned-short continuation fabricated complete native startup.");
    const auto unsigned_short_type=**unsigned_short_cell;
    const auto unsigned_short_prototype=*values->unsigned_short_prototype;
    check(after_unsigned_short.diagnostic=="Registration requires original function 0x800247E4." &&
        f.core->types->count==26 && descriptors.size()==91 && default_allocations==99 && enabled_allocations.size()==93 &&
        (**prototypes->registry)->count==11 && buffer_copy_allocations==5 &&
        f.core->types->backing->at(24)==unsigned_short_type && f.core->types->backing->at(25)==structure_type,
        "Unsigned-short registration/clone did not finish struct defaults before its actual prototype callback.");
    check(unsigned_short_type && unsigned_short_type->class_flags==7 && unsigned_short_type->fields->count==20 &&
        unsigned_short_type->parent==base_field && unsigned_short_type->native_size==0x34 &&
        unsigned_short_type->type_word==0x80477054 && unsigned_short_type->instance_count==2 &&
        unsigned_short_type->name && *unsigned_short_type->name=="igUnsignedShortMetaField" &&
        igb_field_type_metadata_slot(field_types,IgbFieldTypeKind::unsigned_short)==unsigned_short_cell &&
        igb_field_prototype_slot(prototypes,IgbFieldTypeKind::unsigned_short).get()==&values->unsigned_short_prototype,
        "Unsigned-short class changed its inherited schema, physical type or canonical prototype cell.");
    check(unsigned_short_probe_locals.size()==1 && unsigned_short_prototype_allocations.size()==1 &&
        igb_bootstrap_read_word(unsigned_short_probe_locals.front(),0)==0x80471914 &&
        unsigned_short_prototype && unsigned_short_prototype->vtable_word==0x80477054 &&
        unsigned_short_prototype->reference_word==1 && unsigned_short_prototype->ordinal==10 &&
        unsigned_short_prototype->type_ordinal==10 && unsigned_short_prototype->size==2 &&
        (**prototypes->registry)->backing->at(10)==unsigned_short_prototype &&
        igb_bootstrap_read_word(unsigned_short_prototype_allocations.front(),0x10)==0x000a000a &&
        unsigned_short_prototype->default_bytes && unsigned_short_prototype->default_bytes->buffer &&
        unsigned_short_prototype->default_bytes->buffer->bytes.size()==2,
        "Unsigned-short prototype skipped native initialization, balanced ownership, ordinals or two-byte buffer allocation.");
    check(expanded_base_field_children && (*base_field->children)->count==9 &&
        (*base_field->children)->backing==expanded_base_field_children->types &&
        (*base_field->children)->backing->at(8)==unsigned_short_type,
        "Unsigned-short registration bypassed the real ninth-child growth and parent publication.");
    for(std::size_t i=0;i<8;++i)check(expanded_base_field_children->types->at(i)==grown_base_field_children->types->at(i),
        "Base-field child growth changed a previously registered class identity.");
    for(std::size_t at=36;at<64;++at)check(expanded_base_field_children->bytes->initialized[at] &&
        expanded_base_field_children->bytes->bytes[at]==0,"Native child-list growth did not clear its spare physical slots.");
    check(structure_defaults->size_descriptor && *structure_defaults->size_descriptor,
        "Struct size clone did not publish its represented canonical cell.");
    const auto specialized_size=**structure_defaults->size_descriptor;
    const auto base_size=base_field->fields->backing->at(6);
    check(specialized_size && specialized_size!=base_size && specialized_size==descriptors.back() &&
        structure_type->fields->backing->at(6)==specialized_size && specialized_size->name && *specialized_size->name=="_size" &&
        specialized_size->offset==0x14 && specialized_size->size==2 && specialized_size->reference_word==1 &&
        specialized_size->type_ordinal==10 && specialized_size->ordinal==6 && specialized_size->flag_26==0 &&
        specialized_size->global_slot_address==0x80561568 && specialized_size->default_bytes &&
        std::ranges::equal(igb_buffer_bytes(specialized_size->default_bytes->buffer,specialized_size->default_bytes->offset,2),std::array<std::uint8_t,2>{0,1}),
        "Struct inherited size clone lost its independent buffer, original width, schema identity or temporary release.");
    const auto specialized_size_storage=descriptor_allocations.back();
    check(igb_bootstrap_read_word(specialized_size_storage,4)==1 && igb_bootstrap_read_word(specialized_size_storage,8)==0x14 &&
        igb_bootstrap_read_word(specialized_size_storage,0x1c)==0x80561568 && igb_bootstrap_read_byte(specialized_size_storage,0x26)==0 &&
        specialized_size->default_bytes->buffer!=base_size->default_bytes->buffer &&
        specialized_size->default_bytes->buffer!=unsigned_short_prototype->default_bytes->buffer &&
        *field_defaults->tables[0].globals->at(4)->slot==base_size && base_size->global_slot_address==0x805617d0 &&
        std::ranges::equal(igb_buffer_bytes(base_size->default_bytes->buffer,base_size->default_bytes->offset,2),std::array<std::uint8_t,2>{0,0}),
        "Struct specialization changed the base size global or shared its copied default allocation.");
    for(unsigned i=0;i<20;++i)if(i!=6)check(structure_type->fields->backing->at(i)==base_field->fields->backing->at(i),
        "Struct specialization replaced an unrelated inherited descriptor.");
    for(unsigned i=0;i<3;++i){const auto field=structure_type->fields->backing->at(20+i);
        check(field==descriptors[87+i] && *structure_defaults->globals->at(i)->slot==field &&
            field->name && *field->name==**structure_defaults->names->at(i) && field->offset==*structure_defaults->offsets->at(i) &&
            field->global_slot_address==structure_defaults->globals->at(i)->address && field->declaring_type &&
            field->declaring_type->lock()==structure_type,
            "Struct local schema did not publish its original global, offset, name or declaring type.");
    }
    const auto alignment_field=structure_type->fields->backing->at(20);
    check(alignment_field->flag_26==0 && alignment_field->size==4 &&
        igb_buffer_word(alignment_field->default_bytes->buffer,alignment_field->default_bytes->offset)==1 &&
        structure_type->class_flags==7 && structure_type->fields->count==23 && structure_type->instance_count==0 &&
        concrete_semaphore->class_flags==9 && concrete_semaphore->fields->count==6 && concrete_semaphore->instance_count==0 &&
        !startup->manager && runtime_allocations.size()==2 && igb_bootstrap_read_word(manager_storage,12)==0,
        "Completed struct defaults changed the alignment width or fabricated its descriptor prototype/semaphore instance.");
    const auto short_allocations=enabled_allocations.size(),short_reflections=reflected_allocations.size();
    const auto short_names=string_allocations;const auto short_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_unsigned_short.diagnostic && !call.faulted() &&
        enabled_allocations.size()==short_allocations && reflected_allocations.size()==short_reflections &&
        string_allocations==short_names && freed_callback_lists.size()==short_frees && descriptors.size()==91 &&
        default_allocations==99 && buffer_copy_allocations==5 && f.core->types->count==26 &&
        (**prototypes->registry)->count==11 && unsigned_short_probe_locals.size()==1 &&
        structure_probe_locals.size()==1 && !startup->manager && !**meta_enum->metadata &&
        thread_list_metadata->instance_count==1 && concrete_manager->instance_count==0 &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending struct prototype replayed the completed unsigned-short class/clone or changed runtime startup.");

    // Construct the real struct descriptor prototype from its inherited and
    // local defaults. Do not supply ready size/alignment/function words.
    values->struct_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    std::vector<IgbBootstrapPointer> struct_prototype_allocations,struct_instance_allocations;
    const auto before_struct_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,before_struct_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=structure_type)return before_struct_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x40,
                "Struct reflected allocation changed its actual pool, size or operation.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            raw.storage->bytes->initialized.assign(c.bytes,0);
            IgbObjectInstance instance;instance.storage=raw.storage;instance.field=std::make_shared<IgbDecodeRuntimeField>();
            raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
            if(!*values->struct_prototype)struct_prototype_allocations.push_back(raw);
            else{struct_instance_allocations.push_back(raw);descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
            values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            enabled_allocations.push_back(instance);igb_bind_object_field_instance(member_construct,instance);
            reflected_allocations.push_back(c);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
    std::vector<IgbFieldOperationCall> struct_buffer_calls;
    const auto before_struct_buffers=object_defaults->fields->factory;
    bool gamecube_thread_buffers{};
    object_defaults->fields->factory=[&,before_struct_buffers](const IgbFieldOperationCall& c){
        const bool structure=c.field && c.field->vtable_word==0x80470e00;
        const bool reached_size=c.bytes==12 || (gamecube_thread_buffers && (c.bytes==0x318 || c.bytes==0x28));
        if(!structure || (c.kind!=IgbFieldOperationKind::free_default &&
            !(c.kind==IgbFieldOperationKind::allocate_default && reached_size)))return before_struct_buffers(c);
        struct_buffer_calls.push_back(c);auto operation=std::make_shared<IgbFieldOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.pool==f.pool,"Struct default-buffer allocation/free changed its selected pool.");
            if(c.kind==IgbFieldOperationKind::free_default){
                check(c.buffer.buffer && !c.buffer.buffer->released,"Struct default free lost its live allocation.");
                c.buffer.buffer->released=true;
            }else{
                check(c.alignment==1,"Struct default-buffer allocation used layout alignment instead of the original byte request.");
                ++default_allocations;auto bytes=std::make_shared<IgbScratchBuffer>();
                // Same explicit initialized-zero allocator preparation as the
                // existing arena fixture; not a claim that struct+84 clears it.
                bytes->bytes.assign(c.bytes,0);bytes->initialized.assign(c.bytes,1);
                out->buffer_result={bytes,0};
                out->buffer_address=gamecube_thread_buffers?0xd00000+default_allocations*0x1000:0x740000+default_allocations*0x100;
            }done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto before_runtime_semaphore_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,before_runtime_semaphore_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=concrete_semaphore)return before_runtime_semaphore_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x24,
                "Runtime semaphore allocator changed its reflected size or selected pool.");
            auto raw=allocation(0x900000+static_cast<std::uint32_t>(enabled_allocations.size())*0x100,c.bytes);
            raw.storage->bytes->initialized.assign(c.bytes,0);
            IgbObjectInstance instance;instance.storage=raw.storage;
            instance.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::semaphore);
            instance.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{raw.storage,0};
            IgbObjectBinding binding;binding.runtime_object=instance.runtime_object;raw.storage->bindings.push_back(binding);
            enabled_allocations.push_back(instance);runtime_allocations.push_back(instance);
            igb_bind_object_field_instance(member_construct,instance);reflected_allocations.push_back(c);
            out->allocation_result=raw.storage;done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    struct_prototype_ready=true;
    const auto after_struct_prototype=call.resume(bootstrap);
    std::cout<<"Cold native struct prototype continuation: "<<after_struct_prototype.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", struct prototypes="<<struct_prototype_allocations.size()<<
        ", struct instances="<<struct_instance_allocations.size()<<", runtime allocations="<<runtime_allocations.size()<<'\n';
    check(!after_struct_prototype.completed && !call.faulted(),"Struct prototype fabricated complete native startup.");
    if(!reflected_init_calls.empty()){const auto& reached=reflected_init_calls.back();
        std::cout<<"After struct prototype reached init: kind="<<static_cast<unsigned>(reached.kind)<<", slot="<<reached.virtual_slot<<
            ", table="<<reached.vtable_word<<'\n';}
    check(after_struct_prototype.diagnostic=="Object initialization reached an unbound virtual, allocator or instance-list operation." &&
        f.core->types->count==27 && descriptors.size()==92 && default_allocations==104 && enabled_allocations.size()==96 &&
        (**prototypes->registry)->count==12 && struct_prototype_allocations.size()==1 && struct_instance_allocations.size()==1 &&
        runtime_allocations.size()==3 && structure_type->instance_count==2 && concrete_semaphore->instance_count==0 &&
        concrete_semaphore->class_flags==7 && f.core->types->backing->at(26)==concrete_semaphore,
        "Struct prototype and concrete descriptor did not complete the semaphore schema before runtime initialization.");
    const auto struct_prototype=*values->struct_prototype;
    const auto semaphore_struct=structure_type->instance_count?concrete_semaphore->fields->backing->at(6):nullptr;
    check(struct_prototype && struct_prototype->vtable_word==0x80470e00 && struct_prototype->reference_word==1 &&
        struct_prototype->ordinal==11 && struct_prototype->type_ordinal==11 && struct_prototype->size==1 &&
        struct_prototype->struct_alignment==1 && struct_prototype->struct_constructor==0 && struct_prototype->struct_destructor==0 &&
        (**prototypes->registry)->backing->at(11)==struct_prototype &&
        igb_field_prototype_slot(prototypes,IgbFieldTypeKind::structure).get()==&values->struct_prototype &&
        igb_bootstrap_read_word(struct_prototype_allocations.front(),0x10)==0x000b000b,
        "Struct prototype bypassed its schema-derived words, canonical cell, ownership or final ordinals.");
    check(semaphore_struct && semaphore_struct==descriptors.back() && semaphore_struct!=struct_prototype &&
        semaphore_struct->vtable_word==0x80470e00 && semaphore_struct->size==12 && semaphore_struct->offset==0x18 &&
        semaphore_struct->struct_alignment==1 && semaphore_struct->struct_constructor==0 && semaphore_struct->struct_destructor==0 &&
        semaphore_struct->type_ordinal==11 && semaphore_struct->ordinal==6 && semaphore_struct->reference_word==1 &&
        semaphore_struct->default_bytes && struct_prototype->default_bytes &&
        semaphore_struct->default_bytes->buffer!=struct_prototype->default_bytes->buffer &&
        semaphore_struct->default_bytes->buffer->bytes.size()==12 && struct_prototype->default_bytes->buffer->bytes.size()==1 &&
        struct_buffer_calls.size()==1 && struct_buffer_calls.front().kind==IgbFieldOperationKind::allocate_default &&
        struct_buffer_calls.front().field==semaphore_struct && struct_buffer_calls.front().bytes==12,
        "Semaphore struct descriptor lost its live size specialization or independently allocated twelve-byte default.");
    for(const auto& pointer:{struct_prototype_allocations.front(),struct_instance_allocations.front()}){
        const auto field=igb_bootstrap_field(pointer);
        check(igb_bootstrap_read_word(pointer,0x34)==1 && igb_bootstrap_read_word(pointer,0x38)==0 &&
            igb_bootstrap_read_word(pointer,0x3c)==0 && !field->flag_34 && !field->flag_35 && !field->method_function &&
            !field->word_38 && !field->element_field && !field->object_type && !field->flag_3c,
            "Struct physical defaults were missing or aliased unrelated reference/memory descriptor members.");
        check(!pointer.storage->bytes->initialized[0x16] && !pointer.storage->bytes->initialized[0x17] &&
            pointer.storage->bytes->initialized[0x14] && pointer.storage->bytes->initialized[0x15],
            "Struct size default or getter initialized/required the untouched halfword padding.");
    }
    check(concrete_semaphore->fields->count==7 && concrete_semaphore->native_size==0x24 && concrete_semaphore->native_alignment==4 &&
        concrete_semaphore->word_40==0x8002612c && concrete_semaphore->factory_redirect==0,
        "Concrete semaphore layout or post-schema callback differs from its original completed metadata.");
    for(unsigned i=0;i<4;++i){const auto field=concrete_semaphore->fields->backing->at(3+i);
        check(*platform_semaphore->globals->at(i)->slot==field && field->name && *field->name==**platform_semaphore->names->at(i) &&
            field->offset==*platform_semaphore->offsets->at(i) && field->global_slot_address==platform_semaphore->globals->at(i)->address &&
            field->declaring_type && field->declaring_type->lock()==concrete_semaphore,
            "Concrete semaphore schema failed to publish the original four local fields.");
        if(i<2)check(igb_buffer_word(field->default_bytes->buffer,field->default_bytes->offset)==1,
            "Semaphore count setter did not publish its actual four-byte default one.");
    }
    const auto semaphore_instance=runtime_allocations.back();
    const IgbBootstrapPointer semaphore_storage{semaphore_instance.storage,semaphore_instance.offset};
    check(semaphore_instance.runtime_object && semaphore_instance.runtime_object->kind==IgbRuntimeObjectKind::semaphore &&
        !semaphore_instance.runtime_object->construction_active_.expired() &&
        reflected_init_calls.back().kind==IgbObjectInitServiceKind::object_virtual &&
        reflected_init_calls.back().object.runtime_object==semaphore_instance.runtime_object &&
        reflected_init_calls.back().vtable_word==0x80470314 && reflected_init_calls.back().virtual_slot==0x58 &&
        igb_bootstrap_read_word(semaphore_storage,0)==0x80470314 && igb_bootstrap_read_word(semaphore_storage,4)==1 &&
        !startup->manager && concrete_manager->instance_count==0 && igb_bootstrap_read_word(manager_storage,12)==0,
        "Runtime semaphore was replaced by a field descriptor or bypassed its first original metadata getter.");
    for(unsigned at=8;at<0x24;++at)check(!semaphore_storage.storage->bytes->initialized[at],
        "Pending semaphore metadata getter fabricated runtime defaults or OS initialization bytes.");
    const auto struct_allocations=enabled_allocations.size(),struct_reflections=reflected_allocations.size();
    const auto struct_names=string_allocations;const auto struct_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_struct_prototype.diagnostic && !call.faulted() &&
        enabled_allocations.size()==struct_allocations && reflected_allocations.size()==struct_reflections &&
        string_allocations==struct_names && freed_callback_lists.size()==struct_frees && default_allocations==104 &&
        descriptors.size()==92 && f.core->types->count==27 && (**prototypes->registry)->count==12 &&
        runtime_allocations.size()==3 && struct_prototype_allocations.size()==1 && struct_instance_allocations.size()==1 &&
        structure_type->instance_count==2 && concrete_semaphore->instance_count==0 && !startup->manager && !**meta_enum->metadata &&
        thread_list_metadata->instance_count==1 && igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending runtime semaphore getter replayed struct prototypes, semaphore schema or prior startup effects.");

    // Resume the same physical semaphore through its actual metadata getter.
    // Its inline struct member is admitted separately, without prepared data.
    runtime_init->semaphore_registration=platform_semaphore;semaphore_runtime_ready=true;
    const auto after_semaphore_identity=call.resume(bootstrap);
    std::cout<<"Cold native semaphore reflected member: "<<after_semaphore_identity.diagnostic<<'\n';
    check(!after_semaphore_identity.completed && !call.faulted() && !startup->manager &&
        reflected_init_calls.back().kind==IgbObjectInitServiceKind::field_virtual &&
        reflected_init_calls.back().virtual_slot==0x84 && reflected_init_calls.back().field==semaphore_struct &&
        reflected_init_calls.back().object.runtime_object==semaphore_instance.runtime_object &&
        concrete_semaphore->instance_count==0 && igb_bootstrap_read_word(semaphore_storage,8)==0,
        "Semaphore metadata getter bypassed the actual inline struct first pass or named-member clear.");
    for(unsigned at=12;at<0x24;++at)check(!semaphore_storage.storage->bytes->initialized[at],
        "Semaphore metadata getter or first pass fabricated scalar, active or inline struct defaults.");

    auto struct_members=std::make_shared<IgbStructMemberServices>();struct_members->values=values;
    unsigned inline_callbacks{};
    struct_members->factory=[&](const IgbStructMemberCall&){++inline_callbacks;return std::shared_ptr<IgbStructMemberOperation>{};};
    runtime_struct_defaults=make_native_igb_struct_member_factory(struct_members);
    std::vector<IgbThreadRuntimeInitCall> semaphore_runtime_calls;
    IgbThreadRuntimeInitFactory runtime_activation;
    runtime_init->factory=[&](const IgbThreadRuntimeInitCall& c){
        semaphore_runtime_calls.push_back(c);
        return runtime_activation?runtime_activation(c):std::shared_ptr<IgbThreadRuntimeInitOperation>{};
    };
    const auto after_inline_semaphore=call.resume(bootstrap);
    std::cout<<"Cold native semaphore inline initialization: "<<after_inline_semaphore.diagnostic<<
        "; instances="<<concrete_semaphore->instance_count.value_or(0xffffffffu)<<
        ", runtime calls="<<semaphore_runtime_calls.size()<<'\n';
    check(!after_inline_semaphore.completed && !call.faulted() && concrete_semaphore->instance_count==1 &&
        semaphore_instance.runtime_object->construction_active_.expired() &&
        !manager_instance.runtime_object->construction_active_.expired() && concrete_manager->instance_count==0 && !startup->manager,
        "Inline struct initialization did not finish the actual semaphore before the manager activation boundary.");
    check(semaphore_runtime_calls.size()==1 && semaphore_runtime_calls.front().object.runtime_object==semaphore_instance.runtime_object &&
        semaphore_runtime_calls.front().vtable_word==0x80470314 && semaphore_runtime_calls.front().virtual_slot==0x5c &&
        igb_bootstrap_read_word(manager_storage,12)==igb_bootstrap_address(semaphore_storage),
        "Manager initialization did not capture its freshly published semaphore and call actual virtual+5C.");
    const auto activation_result=semaphore_runtime_calls.front().result;
    check(activation_result && activation_result->bytes.size()==4 &&
        activation_result->initialized==std::vector<std::uint8_t>(4,0) && inline_callbacks==0,
        "Unbound semaphore activation fabricated status or invoked a zero inline constructor/destructor.");
    check(semaphore_struct->default_mode==0 && igb_bootstrap_read_byte(struct_instance_allocations.front(),0x24)==0 &&
        semaphore_struct->struct_constructor==0 && semaphore_struct->struct_destructor==0 &&
        igb_bootstrap_read_word(semaphore_storage,8)==0 && igb_bootstrap_read_word(semaphore_storage,12)==1 &&
        igb_bootstrap_read_word(semaphore_storage,16)==1 && igb_bootstrap_read_byte(semaphore_storage,20)==0,
        "Semaphore field passes missed actual reset mode, count defaults, name or inactive flag.");
    for(unsigned at=0;at<12;++at)check(semaphore_storage.storage->bytes->initialized[0x18+at] &&
        semaphore_storage.storage->bytes->bytes[0x18+at]==semaphore_struct->default_bytes->buffer->bytes[semaphore_struct->default_bytes->offset+at],
        "Struct zero-constructor branch did not copy its actual twelve default bytes into the runtime semaphore.");
    for(unsigned at=0x15;at<0x18;++at)check(!semaphore_storage.storage->bytes->initialized[at],
        "Inline member copy or boolean default initialized unrelated semaphore padding.");
    const auto completed_runtime_members=member_calls.size();
    check(call.resume(bootstrap).diagnostic==after_inline_semaphore.diagnostic && !call.faulted() &&
        semaphore_runtime_calls.size()==2 && semaphore_runtime_calls.back().result==activation_result &&
        activation_result->initialized==std::vector<std::uint8_t>(4,0) && inline_callbacks==0 &&
        member_calls.size()==completed_runtime_members && enabled_allocations.size()==struct_allocations &&
        reflected_allocations.size()==struct_reflections && descriptors.size()==92 && default_allocations==104 &&
        f.core->types->count==27 && (**prototypes->registry)->count==12 && concrete_semaphore->instance_count==1 &&
        concrete_manager->instance_count==0 && !startup->manager && !**meta_enum->metadata,
        "Pending semaphore activation replayed construction, default copying, result allocation or singleton publication.");

    // The platform exclusion bit is explicit input, not inferred boot state.
    // Success status aliases the cell already used by memory construction.
    auto activation_services=std::make_shared<IgbSemaphoreRuntimeInitServices>();
    activation_services->status_success=std::shared_ptr<std::optional<std::uint32_t>>(memory_construct,&memory_construct->status_success);
    auto interrupt_state=std::make_shared<IgbExecutionInterruptState>();
    activation_services->interrupts=make_native_igb_interrupt_factory(interrupt_state);
    runtime_activation=make_native_igb_semaphore_runtime_init_factory(activation_services);
    const auto before_activation_bytes=semaphore_storage.storage->bytes->bytes;
    const auto before_activation_mask=semaphore_storage.storage->bytes->initialized;
    const auto missing_interrupt_state=call.resume(bootstrap);
    check(!missing_interrupt_state.completed && !call.faulted() && !interrupt_state->enabled &&
        !startup->manager && concrete_manager->instance_count==0 && semaphore_runtime_calls.size()==3 &&
        semaphore_runtime_calls.back().result==activation_result &&
        semaphore_storage.storage->bytes->bytes==before_activation_bytes &&
        semaphore_storage.storage->bytes->initialized==before_activation_mask &&
        activation_result->initialized==std::vector<std::uint8_t>(4,0),
        "Unknown execution-exclusion state was inferred or activation advanced past its first interrupt dependency.");
    check(call.resume(bootstrap).diagnostic==missing_interrupt_state.diagnostic && semaphore_runtime_calls.size()==3 &&
        !call.faulted() && !startup->manager && semaphore_storage.storage->bytes->bytes==before_activation_bytes,
        "Pending interrupt state selected another semaphore operation or replayed preceding initialization.");
    interrupt_state->enabled=true;
    const auto after_semaphore_activation=call.resume(bootstrap);
    std::cout<<"Cold native semaphore activation: "<<after_semaphore_activation.diagnostic<<
        "; manager instances="<<concrete_manager->instance_count.value_or(0xffffffffu)<<
        ", manager published="<<bool(startup->manager)<<", runtime calls="<<semaphore_runtime_calls.size()<<'\n';
    if(!startup_redirects.empty())std::cout<<"After activation redirect="<<startup_redirects.back().redirect<<'\n';
    check(!after_semaphore_activation.completed && !call.faulted() && interrupt_state->enabled==true &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 && igb_bootstrap_read_word(semaphore_storage,0x1c)==0 &&
        igb_bootstrap_read_word(semaphore_storage,0x20)==0 && igb_bootstrap_read_byte(semaphore_storage,0x14)==1 &&
        activation_result->initialized==std::vector<std::uint8_t>(4,1) &&
        igb_buffer_word(activation_result,0)==*memory_construct->status_success &&
        activation_services->status_success.get()==&memory_construct->status_success,
        "Semaphore activation missed captured count, queue initialization, restored exclusion state or canonical status publication.");
    check(startup->manager && startup->manager->storage==manager_storage.storage && startup->manager->offset==manager_storage.offset &&
        concrete_manager->instance_count==1 && manager_instance.runtime_object->construction_active_.expired() &&
        concrete_semaphore->instance_count==1 && thread_list_metadata->instance_count==1 &&
        igb_bootstrap_read_word(manager_storage,12)==igb_bootstrap_address(semaphore_storage),
        "Completed manager initialization did not publish the same runtime singleton with its actual owned members.");
    check(after_semaphore_activation.diagnostic=="Reflected factory requires its captured metadata+3C redirect callback." &&
        startup_redirects.back().redirect==0x80023e20 && startup_redirects.back().metadata==**startup->thread_metadata &&
        (**startup->thread_metadata)->class_flags==7 && (**startup->thread_metadata)->factory_redirect==0x80023e20 &&
        startup->pool_handle==4 && f.core->types->count==27 && descriptors.size()==92 && default_allocations==104 &&
        (**prototypes->registry)->count==12 && enabled_allocations.size()==96 && runtime_allocations.size()==3 &&
        structure_type->instance_count==2,
        "Published manager bypassed actual main-thread registration/redirect or fabricated another runtime allocation.");
    for(unsigned at=0x15;at<0x18;++at)check(!semaphore_storage.storage->bytes->initialized[at],
        "Semaphore activation initialized unrelated inline padding.");
    const auto after_activation_allocations=enabled_allocations.size(),after_activation_reflections=reflected_allocations.size();
    check(call.resume(bootstrap).diagnostic==after_semaphore_activation.diagnostic && !call.faulted() &&
        semaphore_runtime_calls.size()==3 && enabled_allocations.size()==after_activation_allocations &&
        reflected_allocations.size()==after_activation_reflections && concrete_manager->instance_count==1 &&
        concrete_semaphore->instance_count==1 && igb_buffer_word(activation_result,0)==*memory_construct->status_success &&
        interrupt_state->enabled==true && startup->manager->storage==manager_storage.storage && !**meta_enum->metadata,
        "The subsequent startup boundary replayed semaphore activation, manager publication or runtime construction.");

    // Resume the actual concrete thread redirect in the same startup. The
    // stack probe and raw allocator bytes remain explicit prepared storage;
    // registration, field factories and schema publication execute natively.
    gamecube_thread=std::make_shared<IgbGamecubeThreadRegistrationServices>();
    gamecube_thread->state=std::make_shared<IgbGamecubeThreadRegistrationState>();
    gamecube_thread->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    gamecube_thread->parent=named_registration;gamecube_thread->dependencies=thread_dependencies;
    gamecube_thread->types=f.services->types;gamecube_thread->callbacks=f.services->callbacks;
    gamecube_thread->schema=metadata_defaults->schema;
    std::vector<IgbBootstrapPointer> gamecube_thread_probes;
    gamecube_thread->local_storage=[&]{
        auto raw=allocation(0xc30000+static_cast<std::uint32_t>(gamecube_thread_probes.size())*0x1000,0x388);
        raw.storage->bytes->initialized.assign(0x388,0);
        for(unsigned i=4;i<8;++i)raw.storage->bytes->initialized[i]=1;
        gamecube_thread_probes.push_back(raw);return raw;
    };
    gamecube_thread_buffers=true;
    concrete_redirect=make_native_igb_gamecube_thread_redirect_factory(gamecube_thread,concrete_redirect);
    additional_callbacks=make_native_igb_gamecube_thread_registration_function_factory(gamecube_thread,additional_callbacks);
    f.remaining=make_native_igb_gamecube_thread_registration_type_factory(gamecube_thread,f.remaining);
    const auto after_gamecube_thread=call.resume(bootstrap);
    std::cout<<"Cold native concrete thread registration: "<<after_gamecube_thread.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", prototypes="<<(**prototypes->registry)->count<<", runtime allocations="<<runtime_allocations.size()<<'\n';
    check(!after_gamecube_thread.completed && !call.faulted() && gamecube_thread->state->metadata &&
        *gamecube_thread->state->metadata && startup->manager && runtime_allocations.size()==3,
        "Concrete thread registration bypassed the actual factory or supplied a constructed runtime thread.");
    const auto concrete_thread=*gamecube_thread->state->metadata;
    check(concrete_thread->name && *concrete_thread->name=="igGamecubeThread" && concrete_thread->class_flags==7 &&
        concrete_thread->parent==native_thread_type && concrete_thread->native_size==0x388 && concrete_thread->native_alignment==4 &&
        concrete_thread->flag_1a==0 && concrete_thread->type_word==0x80470110 && concrete_thread->field_callback==0x80023e20 &&
        concrete_thread->factory_redirect==0 && concrete_thread->word_40==0x80023cd4 && concrete_thread->fields->count==22 &&
        concrete_thread->instance_count==0 && f.core->types->count==28 && f.core->types->backing->at(27)==concrete_thread,
        "Concrete thread lost original parent fields, physical layout, registration state or post-schema callback.");
    check(after_gamecube_thread.diagnostic=="Reflected factory requires the captured pool allocation virtual+D8." &&
        unbound_reflected_allocations.back().metadata==concrete_thread && unbound_reflected_allocations.back().pool==f.pool &&
        unbound_reflected_allocations.back().bytes==0x388 && descriptors.size()==101 && default_allocations==113 &&
        enabled_allocations.size()==108 && (**prototypes->registry)->count==12 && structure_type->instance_count==4 &&
        struct_instance_allocations.size()==3 && concrete_manager->instance_count==1 && concrete_semaphore->instance_count==1 &&
        !**meta_enum->metadata && (*f.core->bootstrap_callbacks)->count==1,
        "Concrete thread registration fabricated allocation or replayed preceding startup instances.");
    check(gamecube_thread_probes.size()==1 && gamecube_thread->physical_metadata.size()==1 &&
        igb_bootstrap_read_word(gamecube_thread_probes.front(),0)==0x8047650c &&
        igb_bootstrap_read_word(gamecube_thread_probes.front(),4)==1 &&
        igb_bootstrap_read_word(gamecube_thread_probes.front(),8)==0 &&
        igb_bootstrap_read_word(gamecube_thread_probes.front(),0x380)==0 &&
        igb_bootstrap_read_word(gamecube_thread->physical_metadata.front(),0x40)==0x80023cd4,
        "Concrete thread probe or fresh metadata callback store lost its physical output.");
    for(unsigned at=12;at<0x388;++at)if(at<0x380 || at>=0x384)
        check(!gamecube_thread_probes.front().storage->bytes->initialized[at],
            "Thread layout probe fabricated untouched stack, OS-thread, alarm or padding bytes.");
    for(unsigned i=0;i<13;++i)check(concrete_thread->fields->backing->at(i)==native_thread_type->fields->backing->at(i),
        "Concrete thread replaced an inherited abstract-thread descriptor.");
    for(unsigned i=0;i<9;++i){const auto field=concrete_thread->fields->backing->at(13+i);
        check(*gamecube_thread->globals->at(i)->slot==field && field->name && *field->name==**gamecube_thread->names->at(i) &&
            field->offset==gamecube_thread->offsets->at(i) && field->declaring_type && field->declaring_type->lock()==concrete_thread,
            "Concrete thread schema did not publish its actual names, offsets, globals or declaring owner.");
    }
    const auto thread_struct=concrete_thread->fields->backing->at(13),alarm_struct=concrete_thread->fields->backing->at(15),
        thread_done_semaphore=concrete_thread->fields->backing->at(17),deferred=concrete_thread->fields->backing->at(18);
    check(thread_struct->size==0x318 && alarm_struct->size==0x28 && thread_struct->struct_alignment==1 && alarm_struct->struct_alignment==1 &&
        thread_struct->default_bytes && alarm_struct->default_bytes && thread_struct->default_bytes->buffer->bytes.size()==0x318 &&
        alarm_struct->default_bytes->buffer->bytes.size()==0x28 && thread_struct->default_bytes->buffer!=alarm_struct->default_bytes->buffer &&
        thread_struct->default_bytes->buffer!=semaphore_struct->default_bytes->buffer &&
        alarm_struct->default_bytes->buffer!=struct_prototype->default_bytes->buffer &&
        thread_done_semaphore->object_type && thread_done_semaphore->object_type->lock()==semaphore_metadata && thread_done_semaphore->flag_34==1 &&
        deferred->default_bytes && deferred->default_bytes->buffer->bytes.at(deferred->default_bytes->offset)==1,
        "Concrete thread defaults lost independent inline buffers, abstract semaphore construction type or deferred-byte value.");
    const auto thread_allocations=enabled_allocations.size(),thread_reflections=reflected_allocations.size();
    const auto thread_names=string_allocations,thread_default_count=default_allocations;
    const auto thread_frees=freed_callback_lists.size();
    check(call.resume(bootstrap).diagnostic==after_gamecube_thread.diagnostic && !call.faulted() &&
        enabled_allocations.size()==thread_allocations && reflected_allocations.size()==thread_reflections &&
        string_allocations==thread_names && default_allocations==thread_default_count && freed_callback_lists.size()==thread_frees &&
        gamecube_thread_probes.size()==1 && gamecube_thread->physical_metadata.size()==1 &&
        concrete_thread->instance_count==0 && concrete_thread->class_flags==7 && descriptors.size()==101 &&
        f.core->types->count==28 && structure_type->instance_count==4 && runtime_allocations.size()==3 &&
        semaphore_runtime_calls.size()==3 && startup->manager->storage==manager_storage.storage && interrupt_state->enabled==true &&
        igb_bootstrap_read_word(ark.allocation,4)==7 && igb_bootstrap_read_word(ark.allocation,8)==1,
        "Pending native thread allocation replayed registration/defaults, manager activation or startup snapshots.");

    // Allocate only raw thread storage. The same original reflected initializer
    // must establish its header, inherited defaults and owned done semaphore.
    IgbObjectInitFactory concrete_thread_initializer;
    const auto before_thread_initializer=prototypes->reflected->initializer->factory;
    prototypes->reflected->initializer->factory=[&,before_thread_initializer](const IgbObjectInitCall& c){
        if(concrete_thread_initializer){const auto operation=concrete_thread_initializer(c);if(operation)return operation;}
        return before_thread_initializer(c);
    };
    const auto before_thread_runtime_allocator=resumed_reflected_allocations;
    resumed_reflected_allocations=[&,before_thread_runtime_allocator](const IgbFileFactoryCall& c){
        if(c.metadata!=concrete_thread)return before_thread_runtime_allocator(c);
        auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && c.bytes==0x388,
                "Concrete thread allocation changed its original pool or layout size.");
            auto raw=allocation(0xe00000,c.bytes);raw.storage->bytes->initialized.assign(c.bytes,0);
            IgbObjectInstance instance;instance.storage=raw.storage;
            instance.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::thread);
            instance.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{raw.storage,0};
            IgbObjectBinding binding;binding.runtime_object=instance.runtime_object;raw.storage->bindings.push_back(binding);
            enabled_allocations.push_back(instance);runtime_allocations.push_back(instance);
            igb_bind_object_field_instance(member_construct,instance);reflected_allocations.push_back(c);
            out->allocation_result=raw.storage;done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto at_thread_getter=call.resume(bootstrap);
    std::cout<<"Cold native concrete thread object: "<<at_thread_getter.diagnostic<<"; runtime allocations="<<runtime_allocations.size()<<'\n';
    check(!at_thread_getter.completed && !call.faulted() && runtime_allocations.size()==4 && concrete_thread->instance_count==0 &&
        reflected_init_calls.back().kind==IgbObjectInitServiceKind::object_virtual &&
        reflected_init_calls.back().vtable_word==0x80470110 && reflected_init_calls.back().virtual_slot==0x58,
        "Raw thread allocation skipped its first actual metadata getter or fabricated completed construction.");
    const auto runtime_thread=runtime_allocations.back();const IgbBootstrapPointer runtime_thread_storage{runtime_thread.storage,runtime_thread.offset};
    check(runtime_thread.runtime_object->kind==IgbRuntimeObjectKind::thread && igb_bootstrap_read_word(runtime_thread_storage,0)==0x80470110 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==1,
        "Reflected thread header lost its distinct runtime identity or original type word.");
    for(unsigned at=8;at<0x388;++at)check(!runtime_thread.storage->bytes->initialized[at],
        "Raw thread allocation or metadata boundary fabricated a runtime member.");

    auto concrete_thread_init=std::make_shared<IgbGamecubeThreadInitServices>();
    concrete_thread_init->registration=gamecube_thread;concrete_thread_init->runtime=runtime_init;concrete_thread_init->memory=memory_construct;
    concrete_thread_initializer=make_native_igb_gamecube_thread_init_factory(concrete_thread_init);
    runtime_activation=make_native_igb_gamecube_thread_runtime_factory(concrete_thread_init,runtime_activation);
    const auto before_thread_member_pool=member_construct->factory;
    member_construct->factory=[&,before_thread_member_pool](const IgbObjectFieldConstructCall& c){
        if(c.kind!=IgbObjectFieldConstructServiceKind::tagged_pool || !c.object.runtime_object ||
            c.object.runtime_object->kind!=IgbRuntimeObjectKind::thread)return before_thread_member_pool(c);
        member_calls.push_back(c);auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
        operation->resume=[&,c,out=operation.get()]{
            check(c.handle==0 && c.object.storage==runtime_thread.storage && enabled_identity(c.object),
                "Thread done-semaphore construction lost its captured physical pool tag.");
            out->pool_result=f.pool;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto before_thread_stack_pool=memory_construct->factory;
    bool thread_stack_pool_ready{},thread_stack_allocation_ready{};
    unsigned thread_stack_queries{},thread_stack_allocations{},thread_stack_polls{};
    std::vector<IgbMemoryFieldConstructCall> thread_stack_calls;
    auto thread_stack_bytes=std::make_shared<IgbScratchBuffer>();
    memory_construct->factory=[&,before_thread_stack_pool](const IgbMemoryFieldConstructCall& c){
        if(!c.object.runtime_object || c.object.runtime_object->kind!=IgbRuntimeObjectKind::thread)return before_thread_stack_pool(c);
        thread_stack_calls.push_back(c);auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.object.storage==runtime_thread.storage,"Thread stack service changed its captured receiver.");
            if(c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool){
                ++thread_stack_queries;check(c.handle==0,"Thread stack lookup changed the original one-byte reference tag.");
                if(!thread_stack_pool_ready)return MessengerCallResult{false,0,"Prepared thread stack tagged-pool lookup."};
                out->pool_result=f.pool;
            }else{
                check(c.kind==IgbMemoryFieldConstructServiceKind::allocate && c.pool==f.pool && c.virtual_slot==0xdc &&
                    c.bytes==0x1000 && c.alignment==0x20,"Thread stack allocation lost original size, alignment or pool+DC.");
                ++thread_stack_polls;
                if(!thread_stack_allocation_ready)return MessengerCallResult{false,0,"Prepared aligned thread stack allocation."};
                ++thread_stack_allocations;thread_stack_bytes->bytes.assign(c.bytes,0xcc);thread_stack_bytes->initialized.assign(c.bytes,0);
                out->backing_result={thread_stack_bytes,0xf00000,0,{},{}};
            }
            done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto at_thread_stack_pool=call.resume(bootstrap);
    std::cout<<"Cold native concrete thread initializer: "<<at_thread_stack_pool.diagnostic<<"; runtime allocations="<<runtime_allocations.size()<<
        ", thread instances="<<concrete_thread->instance_count.value_or(0xffffffffu)<<'\n';
    if(!reflected_init_calls.empty()){const auto& reached=reflected_init_calls.back();
        std::cout<<"Thread initialization pending: kind="<<static_cast<unsigned>(reached.kind)<<", table="<<reached.vtable_word<<
            ", slot="<<reached.virtual_slot<<", field="<<(reached.field && reached.field->name?*reached.field->name:"<none>")<<'\n';}
    check(!at_thread_stack_pool.completed && !call.faulted() &&
        at_thread_stack_pool.diagnostic=="Prepared thread stack tagged-pool lookup." &&
        runtime_allocations.size()==5 && runtime_allocations.back().runtime_object->kind==IgbRuntimeObjectKind::semaphore &&
        concrete_thread->instance_count==0 && concrete_semaphore->instance_count==2 &&
        !runtime_thread.runtime_object->construction_active_.expired() && thread_stack_calls.size()==1 &&
        thread_stack_queries==1 && thread_stack_allocations==0 && semaphore_runtime_calls.size()==4 &&
        semaphore_runtime_calls.back().object.runtime_object==runtime_thread.runtime_object &&
        semaphore_runtime_calls.back().virtual_slot==0xa8 && igb_bootstrap_read_word(runtime_thread_storage,0x20)==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,0x24)==0x1000 && igb_bootstrap_read_byte(runtime_thread_storage,0x2c)==0 &&
        igb_bootstrap_read_byte(runtime_thread_storage,0x34)==1,
        "Thread field initialization skipped its owned semaphore, stack defaults or actual aligned-pool lookup.");
    const auto done_semaphore=runtime_allocations.back();const IgbBootstrapPointer done_semaphore_storage{done_semaphore.storage,done_semaphore.offset};
    check(done_semaphore.runtime_object!=semaphore_instance.runtime_object &&
        igb_bootstrap_read_word(runtime_thread_storage,0x380)==igb_bootstrap_address(done_semaphore_storage) &&
        igb_bootstrap_read_word(done_semaphore_storage,0x0c)==1 && igb_bootstrap_read_word(done_semaphore_storage,0x10)==1 &&
        igb_bootstrap_read_byte(done_semaphore_storage,0x14)==0 && done_semaphore.runtime_object->construction_active_.expired() &&
        igb_bootstrap_read_byte(semaphore_storage,0x14)==1,
        "Thread completion semaphore reused the manager semaphore or ran activation before the count setter.");
    thread_stack_pool_ready=true;
    const auto at_thread_stack_allocation=call.resume(bootstrap);
    check(!at_thread_stack_allocation.completed && !call.faulted() &&
        at_thread_stack_allocation.diagnostic=="Prepared aligned thread stack allocation." &&
        thread_stack_calls.size()==2 && thread_stack_queries==2 && thread_stack_polls==1 && thread_stack_allocations==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,0x20)==0 && igb_bootstrap_read_word(done_semaphore_storage,0x0c)==1 &&
        concrete_thread->instance_count==0 && semaphore_runtime_calls.size()==4,
        "Pending aligned allocation published a stack or ran the semaphore setter prematurely.");
    check(call.resume(bootstrap).diagnostic==at_thread_stack_allocation.diagnostic && thread_stack_calls.size()==2 &&
        thread_stack_queries==2 && thread_stack_polls==2 && thread_stack_allocations==0 && runtime_allocations.size()==5 &&
        concrete_thread->instance_count==0 && semaphore_runtime_calls.size()==4,
        "Pending stack allocation repeated pool lookup, object construction or a selected runtime virtual.");
    thread_stack_allocation_ready=true;
    const auto after_thread_init=call.resume(bootstrap);
    std::cout<<"Cold native concrete thread initialized: "<<after_thread_init.diagnostic<<"; thread instances="<<
        concrete_thread->instance_count.value_or(0xffffffffu)<<", stack allocations="<<thread_stack_allocations<<
        ", enabled allocations="<<enabled_allocations.size()<<", runtime calls="<<semaphore_runtime_calls.size()<<'\n';
    check(!after_thread_init.completed && !call.faulted() && concrete_thread->instance_count==1 &&
        runtime_thread.runtime_object->construction_active_.expired() && thread_stack_allocations==1 &&
        thread_stack_queries==2 && thread_stack_polls==3 && igb_bootstrap_read_word(runtime_thread_storage,0x20)==0xf00000 &&
        igb_bootstrap_read_byte(runtime_thread_storage,0x2c)==1 && semaphore_runtime_calls.size()==5 &&
        semaphore_runtime_calls.back().object.runtime_object==done_semaphore.runtime_object &&
        semaphore_runtime_calls.back().virtual_slot==0x68 && semaphore_runtime_calls.back().argument==0 &&
        semaphore_runtime_calls.back().result && igb_buffer_word(semaphore_runtime_calls.back().result,0)==*memory_construct->status_success &&
        igb_bootstrap_read_word(done_semaphore_storage,0x0c)==0 && igb_bootstrap_read_word(done_semaphore_storage,0x10)==1 &&
        igb_bootstrap_read_byte(done_semaphore_storage,0x14)==0,
        "Thread initializer lost stack publication, actual semaphore setter, constructor completion or startup master publication.");
    check(std::all_of(thread_stack_bytes->initialized.begin(),thread_stack_bytes->initialized.end(),[](auto byte){return byte==0;}) &&
        std::all_of(thread_stack_bytes->bytes.begin(),thread_stack_bytes->bytes.end(),[](auto byte){return byte==0xcc;}) &&
        enabled_allocations.size()==110 && runtime_allocations.size()==5 && f.core->types->count==28 &&
        descriptors.size()==101 && default_allocations==113 && (**prototypes->registry)->count==12 &&
        concrete_manager->instance_count==1 && concrete_semaphore->instance_count==2 && !**meta_enum->metadata &&
        startup->manager->storage==manager_storage.storage && interrupt_state->enabled==true,
        "Thread initialization touched unread stack contents or changed completed manager/class state.");
    const auto after_thread_init_members=member_calls.size(),after_thread_init_reflections=reflected_allocations.size();
    check(call.resume(bootstrap).diagnostic==after_thread_init.diagnostic && !call.faulted() &&
        member_calls.size()==after_thread_init_members && reflected_allocations.size()==after_thread_init_reflections &&
        thread_stack_allocations==1 && thread_stack_queries==2 && thread_stack_polls==3 &&
        semaphore_runtime_calls.size()==5 && enabled_allocations.size()==110 && concrete_thread->instance_count==1 &&
        concrete_semaphore->instance_count==2 && igb_bootstrap_read_word(runtime_thread_storage,0x20)==0xf00000,
        "Next startup boundary replayed thread initialization, stack allocation or completion-semaphore setup.");

    // The actual+DC continuation needs original OS state. A prepared raw context
    // below exercises composition; it does not stand in for OS initialization.
    auto thread_adoption=std::make_shared<IgbGamecubeThreadAdoptServices>();
    thread_adoption->os=std::make_shared<IgbOsThreadState>();thread_adoption->memory=memory_construct;
    concrete_thread_adoption=make_native_igb_gamecube_thread_adopt_factory(thread_adoption);
    const auto unknown_os=call.resume(bootstrap);
    check(!unknown_os.completed && !call.faulted() &&
        unknown_os.diagnostic=="Thread adoption requires canonical current OS thread global800000E4." &&
        !thread_adoption->os->current_thread_address && igb_bootstrap_read_word(runtime_thread_storage,0x350)==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,0x20)==0xf00000 && thread_stack_queries==2 &&
        startup_virtuals.back().vtable_word==0x80470110 && startup_virtuals.back().virtual_slot==0xdc &&
        startup_virtuals.back().object.storage==runtime_thread.storage,
        "Current-thread adoption invented OS state or ran stack release before the first canonical load.");
    check(call.resume(bootstrap).diagnostic==unknown_os.diagnostic && !call.faulted() &&
        thread_stack_allocations==1 && thread_stack_queries==2 && enabled_allocations.size()==110,
        "Unavailable OS state replayed thread construction or stack allocation.");
    thread_adoption->os->current_thread_address=0x805151d0;
    const auto unknown_os_mapping=call.resume(bootstrap);
    check(!unknown_os_mapping.completed && !call.faulted() &&
        unknown_os_mapping.diagnostic=="Thread adoption requires its captured current-thread source mapping." &&
        igb_bootstrap_read_word(runtime_thread_storage,0x350)==0x805151d0 && thread_stack_queries==2,
        "Adoption did not publish the first current-thread pointer before requiring its captured copy source.");

    auto prepared_os_context=allocation(0x805151d0,0x318);
    std::vector<std::uint8_t> expected_os_context(0x318);
    for(std::size_t i=0;i<expected_os_context.size();++i)expected_os_context[i]=static_cast<std::uint8_t>(11+37*i);
    prepared_os_context.storage->bytes->bytes=expected_os_context;
    prepared_os_context.storage->bytes->initialized.assign(0x318,1);
    thread_adoption->os->mappings.push_back(prepared_os_context);
    bool thread_stack_free_ready{};unsigned thread_stack_free_polls{},thread_stack_frees{};
    std::vector<IgbGamecubeThreadAdoptCall> thread_free_calls;
    thread_adoption->factory=[&](const IgbGamecubeThreadAdoptCall& c){
        thread_free_calls.push_back(c);auto operation=std::make_shared<IgbGamecubeThreadAdoptOperation>();
        operation->resume=[&,c,done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            check(c.object.storage==runtime_thread.storage && c.pool==f.pool && c.address==0xf00000 && c.virtual_slot==0x108,
                "Adoption stack release changed its captured stack, tagged pool or original virtual slot.");
            ++thread_stack_free_polls;
            if(!thread_stack_free_ready)return MessengerCallResult{false,0,"Prepared thread stack release."};
            ++thread_stack_frees;thread_stack_bytes->released=true;done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    thread_stack_pool_ready=false;
    const auto copied_os=call.resume(bootstrap);
    check(!copied_os.completed && !call.faulted() && copied_os.diagnostic=="Prepared thread stack tagged-pool lookup." &&
        thread_stack_queries==3 && thread_stack_calls.size()==3 && thread_free_calls.empty() &&
        igb_bootstrap_read_word(runtime_thread_storage,0x20)==0xf00000 && !thread_stack_bytes->released,
        "Adoption skipped the separate fresh tagged-pool lookup or freed/cleared the stack before the selected call.");
    for(std::size_t i=0;i<expected_os_context.size();++i)check(
        runtime_thread.storage->bytes->bytes[0x38+i]==expected_os_context[i] &&
        runtime_thread.storage->bytes->initialized[0x38+i],
        "Current-thread adoption lost an ordered context-copy byte.");
    prepared_os_context.storage->bytes->bytes.assign(0x318,0xee);
    thread_adoption->os->current_thread_address=0xdeadbeef;
    thread_stack_pool_ready=true;
    const auto awaiting_free=call.resume(bootstrap);
    check(!awaiting_free.completed && !call.faulted() && awaiting_free.diagnostic=="Prepared thread stack release." &&
        thread_stack_queries==4 && thread_free_calls.size()==1 && thread_stack_free_polls==1 && thread_stack_frees==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,0x20)==0xf00000 &&
        igb_bootstrap_read_word(runtime_thread_storage,0x350)==0x805151d0 && !thread_stack_bytes->released &&
        std::equal(expected_os_context.begin(),expected_os_context.end(),runtime_thread.storage->bytes->bytes.begin()+0x38),
        "Suspended stack release replayed OS loads/copy or cleared the live stack too early.");
    check(call.resume(bootstrap).diagnostic==awaiting_free.diagnostic && thread_stack_queries==4 &&
        thread_free_calls.size()==1 && thread_stack_free_polls==2 && thread_stack_frees==0,
        "Pending adoption reselected its completed pool lookup or retained free operation.");
    thread_stack_free_ready=true;
    const auto after_thread_adoption=call.resume(bootstrap);
    std::cout<<"Cold native current-thread adoption: "<<after_thread_adoption.diagnostic<<"; stack frees="<<thread_stack_frees<<
        ", next slot="<<startup_virtuals.back().virtual_slot<<'\n';
    check(!after_thread_adoption.completed && !call.faulted() &&
        after_thread_adoption.diagnostic=="Thread startup requires its selected physical object virtual." &&
        thread_stack_frees==1 && thread_stack_free_polls==3 && thread_stack_bytes->released &&
        igb_bootstrap_read_word(runtime_thread_storage,0x20)==0 && igb_bootstrap_read_word(runtime_thread_storage,4)==1 &&
        startup_virtuals.back().object.storage==manager_storage.storage && startup_virtuals.back().virtual_slot==0x60 &&
        startup_virtuals.back().vtable_word==0x804700a8 && startup_virtuals.back().argument.storage==runtime_thread.storage,
        "Completed adoption failed to clear the freed stack or reach the real manager insertion before releasing the thread.");
    check(call.resume(bootstrap).diagnostic==after_thread_adoption.diagnostic && !call.faulted() &&
        thread_stack_frees==1 && thread_stack_queries==4 && thread_free_calls.size()==1 &&
        thread_stack_allocations==1 && semaphore_runtime_calls.size()==5 && enabled_allocations.size()==110 &&
        runtime_allocations.size()==5 && concrete_thread->instance_count==1 && concrete_semaphore->instance_count==2 &&
        f.core->types->count==28 && descriptors.size()==101 && default_allocations==113 && (**prototypes->registry)->count==12 &&
        igb_bootstrap_read_byte(done_semaphore_storage,0x14)==0 && igb_bootstrap_read_byte(semaphore_storage,0x14)==1,
        "Pending manager insertion replayed completed adoption or changed independent semaphore/runtime state.");

    auto thread_insertion=std::make_shared<IgbThreadManagerInsertServices>();
    thread_insertion->runtime=runtime_init;thread_insertion->memory=memory_construct;
    thread_insertion->interrupts=activation_services->interrupts;
    std::vector<IgbThreadManagerInsertCall> insertion_lower_calls;
    IgbThreadManagerInsertFactory concrete_insertion_lower;
    thread_insertion->factory=[&](const IgbThreadManagerInsertCall& c){
        insertion_lower_calls.push_back(c);
        return concrete_insertion_lower?concrete_insertion_lower(c):std::shared_ptr<IgbThreadManagerInsertOperation>{};
    };
    concrete_thread_adoption=make_native_igb_thread_manager_insert_factory(thread_insertion,concrete_thread_adoption);
    const auto at_manager_wait=call.resume(bootstrap);
    check(!at_manager_wait.completed && !call.faulted() &&
        at_manager_wait.diagnostic=="Thread insertion reached an unbound semaphore virtual." &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && runtime_thread.runtime_object->reference_word==2 &&
        semaphore_runtime_calls.back().object.runtime_object==semaphore_instance.runtime_object &&
        semaphore_runtime_calls.back().virtual_slot==0x8c && semaphore_runtime_calls.back().vtable_word==0x80470314 &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 && igb_bootstrap_read_word(thread_list_storage,8)==0 &&
        insertion_lower_calls.empty() && interrupt_state->enabled==true,
        "Manager insertion lost its pre-wait full-word retain or fabricated a completed semaphore wait/list insertion.");
    const auto manager_wait_result=semaphore_runtime_calls.back().result;
    check(manager_wait_result && manager_wait_result->initialized==std::vector<std::uint8_t>(4,0) &&
        manager_wait_result!=activation_result && thread_stack_frees==1,
        "Manager wait reused an activation result or replayed current-thread adoption.");
    check(call.resume(bootstrap).diagnostic==at_manager_wait.diagnostic && !call.faulted() &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && semaphore_runtime_calls.back().result==manager_wait_result &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 && insertion_lower_calls.empty(),
        "Pending manager wait replayed thread retention or changed its captured hidden result.");

    runtime_activation=make_native_igb_semaphore_runtime_access_factory(thread_insertion,runtime_activation);
    const auto at_manager_growth=call.resume(bootstrap);
    std::cout<<"Cold native manager insertion: "<<at_manager_growth.diagnostic<<"; thread references="<<
        igb_bootstrap_read_word(runtime_thread_storage,4)<<", inline semaphore count="<<
        igb_bootstrap_read_word(semaphore_storage,0x18)<<'\n';
    check(!at_manager_growth.completed && !call.faulted() &&
        at_manager_growth.diagnostic=="Thread manager operation reached unbound growth or scheduling." &&
        insertion_lower_calls.size()==1 && insertion_lower_calls.back().kind==IgbThreadManagerInsertServiceKind::grow_list &&
        insertion_lower_calls.back().object.runtime_object==thread_list_instance.runtime_object &&
        insertion_lower_calls.back().count==1 && insertion_lower_calls.back().element_bytes==4 &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==0 && interrupt_state->enabled==true &&
        igb_buffer_word(manager_wait_result,0)==*memory_construct->status_success &&
        igb_bootstrap_read_word(thread_list_storage,8)==0 && igb_bootstrap_read_word(thread_list_storage,12)==0 &&
        igb_bootstrap_read_word(thread_list_storage,16)==0 && igb_bootstrap_read_word(runtime_thread_storage,4)==2,
        "Native manager wait did not consume/restore the semaphore before reaching actual41660 list growth.");
    const auto calls_before_growth=semaphore_runtime_calls.size();
    check(call.resume(bootstrap).diagnostic==at_manager_growth.diagnostic && !call.faulted() &&
        insertion_lower_calls.size()==2 && insertion_lower_calls.back().kind==IgbThreadManagerInsertServiceKind::grow_list &&
        semaphore_runtime_calls.size()==calls_before_growth && thread_stack_queries==4 && thread_stack_frees==1 &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==0 && igb_bootstrap_read_byte(semaphore_storage,0x14)==1 &&
        igb_bootstrap_read_word(done_semaphore_storage,0x0c)==0 && igb_bootstrap_read_byte(done_semaphore_storage,0x14)==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && enabled_allocations.size()==110 &&
        runtime_allocations.size()==5 && f.core->types->count==28 && descriptors.size()==101 && default_allocations==113 &&
        (**prototypes->registry)->count==12 && concrete_thread->instance_count==1 && concrete_semaphore->instance_count==2 &&
        interrupt_state->enabled==true && !**meta_enum->metadata,
        "Pending list growth replayed retain/wait/adoption or fabricated insertion, signal or temporary-reference release.");

    auto native_growth=std::make_shared<IgbDataListGrowthServices>();
    native_growth->data_defaults=data_defaults;native_growth->objects=prototypes->reflected->initializer;
    native_growth->memory=memory_construct;
    unsigned growth_frees{};
    native_growth->factory=[&](const IgbDataListGrowthCall&){++growth_frees;return std::shared_ptr<IgbDataListGrowthOperation>{};};
    concrete_insertion_lower=make_native_igb_data_list_growth_factory(native_growth);
    const auto before_growth_memory=memory_construct->factory;
    bool growth_pool_ready{},growth_allocation_ready{},growth_allocation_selected{};
    unsigned growth_pool_polls{},growth_allocation_polls{},growth_allocations{};
    std::vector<IgbMemoryFieldConstructCall> growth_memory_calls;
    IgbPointerListBacking growth_backing;
    memory_construct->factory=[&,before_growth_memory](const IgbMemoryFieldConstructCall& c){
        growth_memory_calls.push_back(c);
        const bool list_pool=c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool &&
            c.object.runtime_object==thread_list_instance.runtime_object;
        if(!list_pool && !(c.kind==IgbMemoryFieldConstructServiceKind::allocate && growth_allocation_selected))
            return before_growth_memory(c);
        memory_calls.push_back(c);auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
        operation->resume=[&,c,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            if(c.kind==IgbMemoryFieldConstructServiceKind::tagged_pool){
                ++growth_pool_polls;
                check(c.handle==0 && c.object.storage==thread_list_storage.storage,
                    "Data-list growth changed the original one-byte list pool tag or physical receiver.");
                if(!growth_pool_ready)return MessengerCallResult{false,0,"Prepared thread-list backing pool."};
                check(!growth_allocation_selected,"List backing overlapped an earlier prepared allocation.");
                growth_allocation_selected=true;out->pool_result=f.pool;
            }else{
                ++growth_allocation_polls;
                check(c.pool==f.pool && c.bytes==16 && c.alignment==4 && c.virtual_slot==0xdc,
                    "Actual list growth changed its four-element aligned allocation request.");
                if(!growth_allocation_ready)return MessengerCallResult{false,0,"Prepared thread-list backing allocation."};
                auto raw=allocation(0x01000000,c.bytes);
                raw.storage->bytes->bytes.assign(c.bytes,0xcd);raw.storage->bytes->initialized.assign(c.bytes,0);
                out->backing_result={raw.storage->bytes,igb_bootstrap_address(raw),0,{},{}};
                growth_backing=out->backing_result;
                // This endpoint supplies raw memory only. The original allocator
                // body decides whether to clear it and publishes the list pointer.
                igb_bind_memory_field_allocation(memory_construct,out->backing_result);
                growth_allocation_selected=false;++growth_allocations;
            }
            done=true;return MessengerCallResult{true,0,{}};
        };return operation;
    };
    const auto at_growth_pool=call.resume(bootstrap);
    std::cout<<"Cold native list growth pool: "<<at_growth_pool.diagnostic<<'\n';
    check(!at_growth_pool.completed && !call.faulted() && at_growth_pool.diagnostic=="Prepared thread-list backing pool." &&
        growth_pool_polls==1 && growth_allocations==0 && growth_memory_calls.size()==1 &&
        igb_bootstrap_read_word(thread_list_storage,8)==0 && igb_bootstrap_read_word(thread_list_storage,12)==0 &&
        igb_bootstrap_read_word(thread_list_storage,16)==0 && insertion_lower_calls.size()==3 &&
        semaphore_runtime_calls.size()==calls_before_growth && interrupt_state->enabled==true,
        "Data-list growth did not reach its actual metadata/ordinal/tag lookup before element allocation.");
    growth_pool_ready=true;
    const auto at_growth_allocation=call.resume(bootstrap);
    std::cout<<"Cold native list growth allocation: "<<at_growth_allocation.diagnostic<<'\n';
    check(!at_growth_allocation.completed && !call.faulted() &&
        at_growth_allocation.diagnostic=="Prepared thread-list backing allocation." && growth_pool_polls==2 &&
        growth_allocation_polls==1 && growth_allocations==0 && growth_memory_calls.size()==5 &&
        growth_memory_calls[1].virtual_slot==0x64 && growth_memory_calls[2].virtual_slot==0x64 &&
        growth_memory_calls[3].virtual_slot==0x6c && growth_memory_calls[4].kind==IgbMemoryFieldConstructServiceKind::allocate &&
        growth_memory_calls[1].field==growth_memory_calls[2].field &&
        igb_bootstrap_read_word(thread_list_storage,16)==0 && igb_bootstrap_read_word(thread_list_storage,12)==0 &&
        igb_bootstrap_read_word(thread_list_storage,8)==0 && growth_frees==0,
        "Capacity growth merged the outer size with actual62BB8 size/alignment or published before allocation.");
    check(call.resume(bootstrap).diagnostic==at_growth_allocation.diagnostic && growth_pool_polls==2 &&
        growth_allocation_polls==2 && growth_memory_calls.size()==5 && insertion_lower_calls.size()==3 &&
        semaphore_runtime_calls.size()==calls_before_growth && igb_bootstrap_read_word(runtime_thread_storage,4)==2,
        "Pending list backing allocation replayed metadata/pool/size queries, growth selection or thread retention.");
    growth_allocation_ready=true;
    const auto at_manager_wake=call.resume(bootstrap);
    std::cout<<"Cold native list inserted: "<<at_manager_wake.diagnostic<<"; count="<<
        igb_bootstrap_read_word(thread_list_storage,8)<<", capacity="<<igb_bootstrap_read_word(thread_list_storage,12)<<
        ", inline semaphore count="<<igb_bootstrap_read_word(semaphore_storage,0x18)<<'\n';
    check(!at_manager_wake.completed && !call.faulted() &&
        at_manager_wake.diagnostic=="Thread manager operation reached unbound growth or scheduling." &&
        growth_allocations==1 && growth_allocation_polls==3 && growth_pool_polls==2 && growth_memory_calls.size()==6 &&
        growth_memory_calls.back().virtual_slot==0x58 && growth_memory_calls.back().field==growth_memory_calls[2].field &&
        growth_frees==0 && growth_backing.bytes && !growth_backing.fields && !growth_backing.types &&
        igb_bootstrap_read_word(thread_list_storage,8)==1 && igb_bootstrap_read_word(thread_list_storage,12)==4 &&
        igb_bootstrap_read_word(thread_list_storage,16)==growth_backing.address &&
        igb_buffer_word(growth_backing.bytes,0)==igb_bootstrap_address(runtime_thread_storage) &&
        igb_buffer_word(growth_backing.bytes,4)==0 && igb_buffer_word(growth_backing.bytes,8)==0 &&
        igb_buffer_word(growth_backing.bytes,12)==0 &&
        std::all_of(growth_backing.bytes->initialized.begin(),growth_backing.bytes->initialized.end(),[](auto b){return b==1;}),
        "Real growth failed to clear/publish the raw backing, store the first thread, or preserve capacity/count order.");
    check(insertion_lower_calls.size()==4 && insertion_lower_calls.back().kind==IgbThreadManagerInsertServiceKind::wake_thread &&
        insertion_lower_calls.back().queue.storage==semaphore_storage.storage &&
        insertion_lower_calls.back().queue.offset==semaphore_storage.offset+0x1c &&
        semaphore_runtime_calls.size()==calls_before_growth+1 && semaphore_runtime_calls.back().virtual_slot==0x7c &&
        semaphore_runtime_calls.back().object.runtime_object==semaphore_instance.runtime_object &&
        semaphore_runtime_calls.back().result!=manager_wait_result &&
        !semaphore_runtime_calls.back().result->initialized[0] &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 && interrupt_state->enabled==false &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && runtime_thread.runtime_object->reference_word==2,
        "Manager signal skipped its actual pending wake or restored exclusion/released the temporary thread too early.");
    check(call.resume(bootstrap).diagnostic==at_manager_wake.diagnostic && !call.faulted() && insertion_lower_calls.size()==5 &&
        growth_allocations==1 && growth_memory_calls.size()==6 && growth_frees==0 && thread_stack_queries==4 && thread_stack_frees==1 &&
        enabled_allocations.size()==110 && runtime_allocations.size()==5 && f.core->types->count==28 && descriptors.size()==101 &&
        default_allocations==113 && (**prototypes->registry)->count==12 && concrete_thread->instance_count==1 &&
        concrete_semaphore->instance_count==2 && igb_bootstrap_read_byte(done_semaphore_storage,0x14)==0 &&
        igb_bootstrap_read_word(done_semaphore_storage,0x0c)==0 && igb_bootstrap_read_word(semaphore_storage,0x18)==1 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && interrupt_state->enabled==false && !**meta_enum->metadata,
        "Pending wake replayed growth/insertion/signal or fabricated completed startup and scheduling.");

    auto os_wake=std::make_shared<IgbOsThreadWakeServices>();
    os_wake->os=thread_adoption->os;os_wake->scheduler=std::make_shared<IgbOsSchedulerState>();
    std::vector<IgbInterruptCall> wake_interrupt_calls;
    const auto original_interrupt_factory=thread_insertion->interrupts;
    const IgbInterruptFactory observed_interrupts=[&,original_interrupt_factory](const IgbInterruptCall& request){
        wake_interrupt_calls.push_back(request);return original_interrupt_factory(request);
    };
    thread_insertion->interrupts=observed_interrupts;os_wake->interrupts=observed_interrupts;
    unsigned schedule_selections{};
    os_wake->factory=[&](const IgbOsThreadWakeCall&){++schedule_selections;return std::shared_ptr<IgbOsThreadWakeOperation>{};};
    concrete_insertion_lower=make_native_igb_os_thread_wake_factory(os_wake,concrete_insertion_lower);
    const auto pending_hint=call.resume(bootstrap);
    std::cout<<"Cold native OS wake: "<<pending_hint.diagnostic<<'\n';
    check(!pending_hint.completed && !call.faulted() &&
        pending_hint.diagnostic=="OS wake requires fresh canonical runhint80565F14 after the queue became empty." &&
        wake_interrupt_calls.size()==1 && wake_interrupt_calls[0].kind==IgbInterruptServiceKind::disable &&
        interrupt_state->enabled==false && !os_wake->scheduler->run_bits && !os_wake->scheduler->run_hint &&
        !os_wake->scheduler->disable_count && schedule_selections==0 && insertion_lower_calls.size()==6 &&
        igb_bootstrap_read_word(semaphore_storage,0x1c)==0 && igb_bootstrap_read_word(semaphore_storage,0x20)==0 &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 && igb_bootstrap_read_word(runtime_thread_storage,4)==2 &&
        !semaphore_runtime_calls.back().result->initialized[0],
        "Actual OS wake invented a scheduler hint, restored exclusion or published signal success before the reached global read.");
    check(call.resume(bootstrap).diagnostic==pending_hint.diagnostic && wake_interrupt_calls.size()==1 &&
        insertion_lower_calls.size()==6 && growth_allocations==1 && schedule_selections==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==2 && interrupt_state->enabled==false,
        "Unknown scheduler hint replayed wake selection, its disable prefix, insertion or thread release.");

    // Explicit preparation exercises the proven empty-queue/no-reschedule
    // branch. Actual entry zeroing and OS initialization remain required.
    os_wake->scheduler->run_hint=0;
    const auto after_os_wake=call.resume(bootstrap);
    std::cout<<"Cold native thread startup after OS wake: "<<after_os_wake.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", runtime allocations="<<runtime_allocations.size()<<'\n';
    check(!after_os_wake.completed && !call.faulted() &&
        after_os_wake.diagnostic=="Data-list registration requires its canonical metadata cell after prerequisites." && wake_interrupt_calls.size()==3 &&
        wake_interrupt_calls[1].kind==IgbInterruptServiceKind::restore && wake_interrupt_calls[1].restore_enabled==0 &&
        wake_interrupt_calls[2].kind==IgbInterruptServiceKind::restore && wake_interrupt_calls[2].restore_enabled==1 &&
        interrupt_state->enabled==true && schedule_selections==0 && !os_wake->scheduler->run_bits &&
        !os_wake->scheduler->disable_count && os_wake->scheduler->run_hint==0 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==1 && runtime_thread.runtime_object->reference_word==1 &&
        igb_bootstrap_read_word(thread_list_storage,8)==1 && igb_bootstrap_read_word(thread_list_storage,12)==4 &&
        igb_buffer_word(growth_backing.bytes,0)==igb_bootstrap_address(runtime_thread_storage) &&
        igb_bootstrap_read_word(semaphore_storage,0x18)==1 &&
        igb_buffer_word(semaphore_runtime_calls.back().result,0)==*memory_construct->status_success &&
        f.core->types->count==28 && descriptors.size()==101 && default_allocations==113 &&
        enabled_allocations.size()==110 && runtime_allocations.size()==5 && (**prototypes->registry)->count==12 &&
        !**meta_enum->metadata,
        "Completed native wake lost nested restore order, signal success, list ownership or the outer temporary-reference release.");
    const auto after_wake_allocations=enabled_allocations.size(),after_wake_members=member_calls.size();
    check(call.resume(bootstrap).diagnostic==after_os_wake.diagnostic && !call.faulted() &&
        wake_interrupt_calls.size()==3 && insertion_lower_calls.size()==6 && growth_allocations==1 &&
        enabled_allocations.size()==after_wake_allocations && member_calls.size()==after_wake_members &&
        schedule_selections==0 && thread_stack_frees==1 && igb_bootstrap_read_word(runtime_thread_storage,4)==1,
        "The next original startup boundary replayed completed manager insertion, wake or temporary-reference release.");

    // These separate original globals start known null in this prepared cold
    // fixture. Their classes, fields and ownership are produced by the same
    // retained registrar; do not alias the already registered nonref lists.
    const auto ref_prior_child_allocations=enabled_type_list_allocations.size();
    data_registration->ref_object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    data_registration->ref_thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    std::vector<IgbDataListKind> ref_list_probes;
    std::vector<IgbBootstrapPointer> ref_list_probe_locals;
    data_registration->local_storage=[&](IgbDataListKind kind){
        check(kind==IgbDataListKind::ref_object || kind==IgbDataListKind::ref_thread,
            "Reference-counted list registration replayed a completed nonref class probe.");
        auto raw=allocation(0xc70000+static_cast<std::uint32_t>(ref_list_probe_locals.size())*0x100,0x14);
        ref_list_probes.push_back(kind);ref_list_probe_locals.push_back(raw);return raw;
    };
    const auto after_ref_lists=call.resume(bootstrap);
    std::cout<<"Cold native reference-counted lists: "<<after_ref_lists.diagnostic<<"; types="<<f.core->types->count<<
        ", descriptors="<<descriptors.size()<<", defaults="<<default_allocations<<", enabled allocations="<<enabled_allocations.size()<<
        ", runtime allocations="<<runtime_allocations.size()<<", probes="<<ref_list_probes.size()<<
        ", child lists="<<ref_prior_child_allocations<<"->"<<enabled_type_list_allocations.size()<<'\n';
    check(!after_ref_lists.completed && !call.faulted(),
        "Reference-counted list registration incorrectly completed full game startup.");
    const auto ref_object_type=**data_registration->ref_object_metadata,ref_thread_type=**data_registration->ref_thread_metadata;
    check(after_ref_lists.diagnostic=="Registration requires original function 0x80031250." &&
        f.core->types->count==30 && descriptors.size()==102 && default_allocations==114 && enabled_allocations.size()==116 &&
        runtime_allocations.size()==5 && (**prototypes->registry)->count==12 && !**meta_enum->metadata &&
        ref_object_type && ref_thread_type && ref_object_type!=nonref_type && ref_thread_type!=thread_list_metadata &&
        ref_object_type->class_flags==7 && ref_thread_type->class_flags==7 &&
        f.core->types->backing->at(28)==ref_object_type && f.core->types->backing->at(29)==ref_thread_type,
        "Actual reference-counted list registration did not preserve independent classes and reach the original error-handler prerequisite.");
    check(ref_object_type->parent==data_type && ref_thread_type->parent==ref_object_type &&
        ref_object_type->name && *ref_object_type->name=="igObjectList" && ref_thread_type->name && *ref_thread_type->name=="igThreadList" &&
        ref_object_type->native_size==0x14 && ref_thread_type->native_size==0x14 &&
        ref_object_type->type_word==0x80476c7c && ref_thread_type->type_word==0x80476bb4 &&
        ref_list_probes==std::vector<IgbDataListKind>{IgbDataListKind::ref_object,IgbDataListKind::ref_thread} &&
        igb_bootstrap_read_word(ref_list_probe_locals[0],0)==0x80476c7c &&
        igb_bootstrap_read_word(ref_list_probe_locals[1],0)==0x80476bb4,
        "Reference classes lost their actual parent chain, names, sizes or ordered native probes.");
    const auto ref_data=descriptors.back();const auto ref_data_storage=descriptor_allocations.back();
    check(ref_data!=pending_data_descriptor && ref_data!=specialized_data && *data_defaults->ref_data==ref_data &&
        *data_defaults->nonref_data==specialized_data && data_type->fields->backing->at(4)==pending_data_descriptor &&
        nonref_type->fields->backing->at(4)==specialized_data && thread_list_metadata->fields->backing->at(4)==specialized_data &&
        ref_object_type->fields->count==5 && ref_thread_type->fields->count==5 &&
        ref_object_type->fields->backing->at(4)==ref_data && ref_thread_type->fields->backing->at(4)==ref_data &&
        ref_data->name && *ref_data->name=="_data" && ref_data->global_slot_address==0x8056170c &&
        ref_data->ordinal==4 && ref_data->offset==16 && ref_data->declaring_type && ref_data->declaring_type->lock()==ref_object_type &&
        ref_data->element_field && ref_data->element_field->lock()==prototype_field && ref_data->word_38==0,
        "Reference-list defaults reused a nonref descriptor or bypassed actual clone, replacement and physical layout.");
    check(igb_bootstrap_read_word(ref_data_storage,0x1c)==0x8056170c &&
        igb_bootstrap_read_word(ref_data_storage,0x38)==0 &&
        igb_bootstrap_read_word(ref_data_storage,0x3c)==igb_bootstrap_address(prototype_storage) &&
        igb_bootstrap_read_byte(ref_data_storage,0x12)==0 && igb_bootstrap_read_byte(ref_data_storage,0x13)==4 &&
        ref_data->flag_42==pending_data_descriptor->flag_42 &&
        ref_data_storage.storage->bytes->bytes[ref_data_storage.offset+0x42]==
            descriptor_allocations[71].storage->bytes->bytes[descriptor_allocations[71].offset+0x42] &&
        ref_data_storage.storage->bytes->initialized[ref_data_storage.offset+0x42]==
            descriptor_allocations[71].storage->bytes->initialized[descriptor_allocations[71].offset+0x42] &&
        ref_data->default_bytes && ref_data->default_bytes->buffer!=pending_data_descriptor->default_bytes->buffer &&
        ref_data->default_bytes->buffer!=specialized_data->default_bytes->buffer,
        "2910C cleared the inherited reference flag or lost distinct physical descriptor/default-buffer ownership.");
    check(data_type->children && (*data_type->children)->count==2 &&
        (*data_type->children)->backing->at(0)==nonref_type && (*data_type->children)->backing->at(1)==ref_object_type &&
        ref_object_type->children && (*ref_object_type->children)->count==1 &&
        (*ref_object_type->children)->backing->at(0)==ref_thread_type &&
        enabled_type_list_allocations.size()==ref_prior_child_allocations+1 &&
        enabled_type_list_allocations.back().types==*ref_object_type->children &&
        wake_interrupt_calls.size()==3 && schedule_selections==0 && thread_stack_frees==1 &&
        igb_bootstrap_read_word(runtime_thread_storage,4)==1 && interrupt_state->enabled==true,
        "Reference class linkage replayed thread startup or substituted a ready child-list instance.");
    const auto ref_finished_allocations=enabled_allocations.size();const auto ref_finished_names=string_allocations;
    const auto ref_finished_callbacks=freed_callback_lists.size(),ref_finished_clones=reached_reflected_clones.size();
    check(call.resume(bootstrap).diagnostic==after_ref_lists.diagnostic && !call.faulted() &&
        enabled_allocations.size()==ref_finished_allocations && string_allocations==ref_finished_names &&
        freed_callback_lists.size()==ref_finished_callbacks && reached_reflected_clones.size()==ref_finished_clones &&
        ref_list_probes.size()==2 && descriptors.size()==102 && default_allocations==114 && f.core->types->count==30 &&
        wake_interrupt_calls.size()==3 && runtime_allocations.size()==5,
        "Pending error-handler registration replayed the completed reference classes, clone or callback cleanup.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_metadata_registration_tests");
    exact_arguments();cold_body_and_ready();layout_and_defaults_boundaries();full_getter();raw_getter();cycles_and_adapters();actual_cold_bootstrap_prefix();
    std::cout<<"igb_metadata_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Metadata registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
