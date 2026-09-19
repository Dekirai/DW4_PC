#include "dw4/igb_metadata_registration.hpp"
#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_ark_core_bootstrap.hpp"

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
    // Platform/memory-pool activation, the two initial runtime type-list
    // owners and their reserve operation are explicit prepared endpoints.
    // Registration, metadata allocation
    // orchestration and both concrete object constructors execute natively.
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
                out->backing_result={backing,{}};}
            return MessengerCallResult{true,0,{}};};return op;
    };
    auto bootstrap=std::make_shared<IgbArkCoreBootstrapServices>();bootstrap->bootstrap=f.services->metadata->bootstrap;
    bootstrap->pools=f.services->pools;bootstrap->callbacks=f.services->callbacks;
    f.services->callbacks->runtime=f.services->types->runtime;
    std::vector<IgbArkCoreServiceKind> prefix;
    std::vector<std::shared_ptr<IgbRuntimeTypeList>> initial_lists;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> initial_callbacks;
    bootstrap->factory=[&](const IgbArkCoreCall& c){
        auto op=std::make_shared<IgbArkCoreOperation>();op->resume=[&,c,out=op.get()]{
            prefix.push_back(c.kind);check(f.core->enabled==0,"Prepared bootstrap prefix ran after an invented enable store.");
            switch(c.kind){
            case AK::platform_initialize:break;
            case AK::manager_virtual:check(c.manager==f.manager && c.virtual_slot==0xa8,"Cold bootstrap selected a different manager virtual.");break;
            case AK::create_type_list:{
                check(c.pool==f.pool,"Initial runtime list used a different selected pool.");
                auto list=std::make_shared<IgbRuntimeTypeList>();initial_lists.push_back(list);out->types_result=list;
                out->allocation_result=allocation(0x500000+static_cast<std::uint32_t>(initial_lists.size())*16,16);break;
            }
            case AK::reserve_types:
                check(c.types==initial_lists.front() && c.argument==1024,"Bootstrap reserve lost its selected type list.");
                c.types->backing=std::make_shared<IgbRuntimeTypeList::Storage>(1024);break;
            case AK::allocate_callbacks:{
                check(c.bytes==12,"Bootstrap callback allocation changed size.");
                auto list=std::make_shared<IgbRegistrationCallbackList>();list->count=17;list->capacity=19;
                initial_callbacks.push_back(list);out->callbacks_result=list;
                out->allocation_result=allocation(0x600000+static_cast<std::uint32_t>(initial_callbacks.size())*16,12);break;
            }
            default:throw std::runtime_error("Cold integration reached an unexpected prepared bootstrap endpoint");
            }
            return MessengerCallResult{true,0,{}};};return op;
    };
    IgbArkCoreBootstrapInvocation call(ark);
    check(!call.resume(bootstrap).completed && !call.faulted(),"Real cold chain did not suspend at physical field-list backing allocation.");
    check(f.services->metadata->metadata && !*f.services->metadata->metadata && f.allocations==2 && f.calls.empty(),"Pending nested list construction prematurely published metadata.");
    check(igb_bootstrap_read_word(f.object,0)==0x804761d8 && igb_bootstrap_read_word(f.object,0x28)==0 && igb_bootstrap_read_word(f.list,0)==0x80475c54,"Native constructor prefixes were replaced by prepared objects.");
    check(initial_lists.size()==2 && initial_callbacks.size()==2 && f.core->types==initial_lists.front() && *f.core->secondary_types==initial_lists.back(),"Bootstrap did not publish both distinct runtime lists.");
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
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_metadata_registration_tests");
    exact_arguments();cold_body_and_ready();layout_and_defaults_boundaries();full_getter();raw_getter();cycles_and_adapters();actual_cold_bootstrap_prefix();
    std::cout<<"igb_metadata_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Metadata registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
