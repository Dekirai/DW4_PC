#include "dw4/igb_metadata_registration.hpp"
#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_thread_defaults.hpp"
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
#include "dw4/igb_field_prototype.hpp"
#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_clone.hpp"
#include "dw4/igb_meta_enum.hpp"
#include "dw4/igb_metadata_init.hpp"
#include "dw4/igb_object_field_construct.hpp"
#include "dw4/igb_memory_field_construct.hpp"
#include "dw4/igb_memory_field_defaults.hpp"

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
    native_lists->factory=[&](const IgbTypeListCall& c){
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
    const auto original_function_bindings=f.services->callbacks->factory;
    f.services->callbacks->factory=[&,original_function_bindings](const IgbRegistrationCallbacksCall& c){
        if(additional_callbacks){auto result=additional_callbacks(c);if(result)return result;}
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
    native_strings->factory=[&](const IgbStringPoolCall& c){
        check(c.kind==IgbStringPoolServiceKind::allocate_string && c.arena==f.pool,"Cold name interning reached an unexpected arena operation.");
        auto op=std::make_shared<IgbStringPoolOperation>();op->resume=[&,c,out=op.get()]{
            ++string_allocations;out->entry_result=std::make_shared<IgbStringEntry>();out->entry_result->owner=std::make_shared<IgbInternedString>();
            allocated_names.push_back({0x780000+string_allocations*0x100,out->entry_result->owner});
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
    bool schema_constructors_ready{};
    const auto schema_constructors=make_native_igb_field_schema_factory(metadata_defaults->schema);
    metadata_defaults->schema->factory=[&](const IgbFieldSchemaCall& c){
        if(schema_constructors_ready)return schema_constructors(c);
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
                physical.storage->bindings.push_back({0,{},type,{}});lazy_metadata.push_back(type);}
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
    list_defaults->factory=[&](const IgbListDefaultsCall& c){
        list_defaults_calls.push_back(c);
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
    std::vector<IgbPointerListBacking> enabled_backings;
    std::shared_ptr<IgbRegistrationMetaSlot> signed_int_cell,boolean_cell;
    std::vector<IgbBootstrapPointer> signed_int_prototype_allocations,boolean_prototype_allocations;
    bool enabled_metadata_ready{};unsigned enabled_metadata_polls{};
    prototypes->reflected->factory=[&](const IgbFileFactoryCall& c){
        check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==f.pool && f.core->enabled==1 &&
            (c.metadata==f.constructed || c.metadata==lazy_metadata[0] || c.metadata==object_field ||
                (signed_int_cell && *signed_int_cell && c.metadata==**signed_int_cell) ||
                (boolean_cell && *boolean_cell && c.metadata==**boolean_cell)),
            "Further reflected allocation reached an unknown original class or pool.");
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
            }else if(c.metadata==lazy_metadata[0]){
                check(c.bytes==16,"Lazy enabled field list changed its allocation size.");
                instance.fields=std::make_shared<IgbDecodeFieldRegistry>();instance.fields->count=37;
                raw.storage->bindings.push_back({0,{},{},instance.fields});
            }else{
                const bool integer=signed_int_cell && *signed_int_cell && c.metadata==**signed_int_cell;
                const bool boolean=boolean_cell && *boolean_cell && c.metadata==**boolean_cell;
                check(c.bytes==((integer||boolean)?0x34u:0x44u),"Enabled descriptor factory changed its allocation size.");
                instance.field=std::make_shared<IgbDecodeRuntimeField>();
                raw.storage->bindings.push_back({0,{},{},{},{},instance.field});
                if(integer && values->signed_int_prototype && !*values->signed_int_prototype)
                    signed_int_prototype_allocations.push_back(raw);
                else if(boolean && values->boolean_prototype && !*values->boolean_prototype)
                    boolean_prototype_allocations.push_back(raw);
                else{descriptors.push_back(instance.field);descriptor_allocations.push_back(raw);}
                values->fields->pointer_lists->values.push_back({igb_bootstrap_address(raw),{},instance.field});
            }
            enabled_allocations.push_back(instance);out->allocation_result=raw.storage;done=true;
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    const auto enabled_identity=[&](const IgbObjectInstance& object){return std::any_of(enabled_allocations.begin(),enabled_allocations.end(),
        [&](const auto& known){return known.storage==object.storage && known.offset==object.offset &&
            known.metadata==object.metadata && known.fields==object.fields && known.field==object.field;});};
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
    check(!string_boundary.completed && !call.faulted() && string_boundary.diagnostic.find("0x80025404")!=std::string::npos,
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
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_metadata_registration_tests");
    exact_arguments();cold_body_and_ready();layout_and_defaults_boundaries();full_getter();raw_getter();cycles_and_adapters();actual_cold_bootstrap_prefix();
    std::cout<<"igb_metadata_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Metadata registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
