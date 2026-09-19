#include "dw4/igb_type_list_factory.hpp"
#include "dw4/igb_field_registry_factory.hpp"
#include "dw4/igb_pointer_list.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbTypeListServiceKind;using B=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
template<class F>void rejects(F&& action,const char* why){++checks;try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xa5);storage->bytes->initialized.assign(size,1);return {storage,0};
}
struct Fixture {
    std::shared_ptr<IgbTypeListServices> services=std::make_shared<IgbTypeListServices>();
    std::shared_ptr<IgbRuntimeTypeList> types=std::make_shared<IgbRuntimeTypeList>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> object_pool=std::make_shared<ModelSceneData>(),backing_pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer object=allocation(0x100000,16),backing=allocation(0x200000,16);
    std::vector<IgbBootstrapCall> bootstrap_calls;std::vector<IgbTypeListCall> calls;
    std::function<void(const IgbBootstrapCall&,IgbBootstrapOperation&,MessengerCallResult&)> bootstrap_action;
    std::function<void(const IgbTypeListCall&,IgbTypeListOperation&,MessengerCallResult&)> action;
    Fixture(){
        object.storage->bindings.push_back({0,{},{},{},types});types->count=17;types->capacity=91;
        services->children_metadata=std::make_shared<IgbRegistrationMetaSlot>();
        services->registry_metadata=std::make_shared<IgbRegistrationMetaSlot>();
        services->bootstrap=std::make_shared<IgbBootstrapServices>();services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->bootstrap->runtime->singleton=core;services->bootstrap->default_pool_handle=7;
        auto base=std::make_shared<IgbRuntimeMetaType>();base->allocation_prefix=0;
        services->bootstrap->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(base);
        services->bootstrap->factory=[this](const IgbBootstrapCall& c){auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{
            bootstrap_calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==B::pool_for_handle)out->pool_result=object_pool;
            else if(c.kind==B::pool_allocate || c.kind==B::heap_allocate)out->allocation_result=object;
            else if(c.kind==B::allocation_pool)out->pool_result=object_pool;
            else if(c.kind==B::pool_index)r.value=0xabcdef37;
            if(bootstrap_action)bootstrap_action(c,*out,r);return r;};return op;};
        services->factory=[this](const IgbTypeListCall& c){auto op=std::make_shared<IgbTypeListOperation>();op->resume=[this,c,out=op.get()]{
            calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==K::object_pool)out->pool_result=backing_pool;
            if(c.kind==K::allocate_backing)out->backing_result={backing,{}};
            if(action)action(c,*out,r);return r;};return op;};
    }
};
void disabled_factory(){
    Fixture f;IgbTypeListFactoryInvocation call(f.object_pool);
    check(call.resume(f.services).completed && call.result()==f.types,"Disabled field-list factory did not construct its canonical registry.");
    check(call.allocation().storage==f.object.storage,"Factory lost its physical allocation.");
    check(f.bootstrap_calls.size()==1 && f.bootstrap_calls[0].kind==B::pool_allocate && f.bootstrap_calls[0].bytes==16 && f.bootstrap_calls[0].pool==f.object_pool,"Object allocation changed the original pool or16-byte size.");
    check(f.calls.size()==2 && f.calls[0].kind==K::object_pool && f.calls[0].handle==0 && f.calls[1].kind==K::allocate_backing && f.calls[1].bytes==16 && f.calls[1].pool==f.backing_pool,"Backing allocation reused the incoming pool or wrong handle namespace.");
    check(igb_bootstrap_read_word(f.object,0)==0x80475c54 && igb_bootstrap_read_word(f.object,4)==1 && f.types->reference_word==1,"Child-list base vtable or packed reference word differs.");
    check(igb_bootstrap_read_word(f.object,8)==0x200000 && igb_bootstrap_read_word(f.object,12)==0 && f.types->count==0,"Backing pointer/count stores differ.");
    check(!f.types->backing && f.types->backing_bytes==f.backing.storage->bytes && f.types->backing_address==0x200000,"Uninitialized pointer storage manufactured typed field entries.");
    check(f.types->capacity==91,"Constructor invented an allocator capacity from the16-byte request.");
    check(f.services->completed_allocations.size()==1 && f.services->completed_allocations.front().storage==f.object.storage,"Completed factory lost physical object storage needed by later native append.");
    const auto count=f.calls.size();check(call.resume().completed && f.calls.size()==count,"Completed factory replayed allocation.");
    Fixture fallback;IgbTypeListFactoryInvocation without_pool;
    check(without_pool.resume(fallback.services).completed && fallback.bootstrap_calls.front().kind==B::pool_for_handle && fallback.bootstrap_calls.front().handle==7,"Missing pool did not use actual raw allocation selection.");
    Fixture null_backing;null_backing.action=[](const auto& c,auto& op,auto&){if(c.kind==K::allocate_backing)op.backing_result={};};
    IgbTypeListFactoryInvocation empty;check(empty.resume(null_backing.services).completed && !null_backing.types->backing && null_backing.types->backing_address==0,"Null backing was incorrectly treated as failed object construction.");
}
void direct_constructors_and_live_gate(){
    for(const auto entry:{IgbTypeListConstructor::derived,IgbTypeListConstructor::base,IgbTypeListConstructor::reset}){
        Fixture f;IgbTypeListConstructorInvocation call(f.object,entry);check(call.resume(f.services).completed,"Direct constructor did not complete.");
        check(igb_bootstrap_read_word(f.object,0)==(entry==IgbTypeListConstructor::derived?0x80475cb0u:0x80475c54u),"Direct constructor published the wrong vtable.");
        check(f.calls.size()==(entry==IgbTypeListConstructor::reset?0u:2u),"Reset allocated backing or full constructor omitted it.");
    }
    Fixture changed;changed.bootstrap_action=[&](const auto& c,auto&,auto&){if(c.kind==B::pool_allocate)changed.core->enabled=1;};
    changed.action=[&](const auto& c,auto&,auto&){if(c.kind==K::object_pool)check(c.handle==0 && changed.types->reference_word==1,"Second base reset failed to clear the freshly assigned pool tag.");};
    IgbTypeListFactoryInvocation call(changed.object_pool);check(call.resume(changed.services).completed,"Fresh enabled base-constructor branch failed.");
    check(changed.bootstrap_calls.size()==3 && changed.bootstrap_calls[1].kind==B::allocation_pool && changed.bootstrap_calls[1].address==0x100000 && changed.bootstrap_calls[2].kind==B::pool_index,"Base constructor skipped actual allocation-base/tag work after gate mutation.");
}
void failure_and_pending_prefixes(){
    Fixture null;null.bootstrap_action=[](const auto& c,auto& op,auto&){if(c.kind==B::pool_allocate)op.allocation_result={};};
    IgbTypeListFactoryInvocation missing(null.object_pool);rejects([&]{missing.resume(null.services);},"Null object allocation returned successfully instead of failing at the tail vtable.");
    Fixture header;header.object.storage->bytes->initialized[4]=0;IgbTypeListConstructorInvocation unreadable(header.object);
    rejects([&]{unreadable.resume(header.services);},"Constructor skipped its actual old reference-word read.");
    check(igb_bootstrap_read_word(header.object,0)==0x804717f0 && header.types->count==17,"Failure did not retain the reached base-vtable prefix.");
    Fixture pool;pool.action=[](const auto& c,auto& op,auto&){if(c.kind==K::object_pool)op.pool_result.reset();};
    IgbTypeListConstructorInvocation no_pool(pool.object);rejects([&]{no_pool.resume(pool.services);},"Null object pool was accepted for backing allocation.");
    check(pool.types->count==0 && igb_bootstrap_read_word(pool.object,8)==0,"Reached null pool lost completed list resets.");
    Fixture pending;auto selected=pending.backing_pool;unsigned visits{};
    pending.action=[&](const auto& c,auto&,auto& r){if(c.kind==K::allocate_backing){++visits;check(c.pool==selected && c.bytes==16,"Pending allocator lost captured pool/size.");r.completed=visits>1;}};
    IgbTypeListFactoryInvocation delayed(pending.object_pool);check(!delayed.resume(pending.services).completed,"Prepared pending backing operation completed.");
    pending.backing_pool=std::make_shared<ModelSceneData>();pending.services->factory={};
    check(delayed.resume(pending.services).completed && visits==2,"Pending operation was recreated or lost its captured callback.");
    check(pending.bootstrap_calls.size()==1 && pending.calls.size()==3,"Pending backing replayed object allocation or pool lookup.");
    Fixture absent;absent.services->bootstrap->runtime->singleton.reset();IgbTypeListFactoryInvocation gate;
    check(!gate.resume(absent.services).completed,"Missing ArkCore binding became disabled state.");
    absent.services->bootstrap->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};rejects([&]{gate.resume(absent.services);},"Represented null ArkCore did not fault.");
}
void enabled_branch_and_raw_owners(){
    Fixture f;f.core->enabled=1;auto first=std::make_shared<IgbRuntimeMetaType>(),live=std::make_shared<IgbRuntimeMetaType>();*f.services->children_metadata=first;
    f.action=[&](const auto& c,auto& op,auto&){if(c.kind==K::register_type){check(c.function==0x8002e054,"Child list selected the registry registration wrapper.");*f.services->children_metadata=live;}
        if(c.kind==K::reflected_factory){check(c.metadata==live && c.pool==f.object_pool,"Reflected list factory reused pre-registration metadata or lost its pool.");op.allocation_result=f.object;}};
    IgbTypeListFactoryInvocation call(f.object_pool);check(call.resume(f.services).completed && call.result()==f.types && f.bootstrap_calls.empty(),"Enabled path was replaced with direct construction.");
    check(igb_bootstrap_read_word(f.object,0)==0xa5a5a5a5,"Enabled path silently constructed the prepared reflected result.");
    Fixture old;auto previous=std::make_shared<IgbRuntimeTypeList::Storage>(1);std::weak_ptr<IgbRuntimeTypeList::Storage> weak=previous;
    old.types->backing=previous;previous.reset();
    IgbTypeListConstructorInvocation reset(old.object,IgbTypeListConstructor::reset);check(reset.resume().completed && !weak.expired(),"Raw reset implicitly freed the overwritten field-pointer backing.");
    Fixture abandoned;abandoned.services->factory={};{IgbTypeListConstructorInvocation pending(abandoned.object);check(!pending.resume(abandoned.services).completed,"Unbound object-pool lookup completed.");}
    IgbTypeListConstructorInvocation restart(abandoned.object);rejects([&]{restart.resume(abandoned.services);},"Abandoned partial constructor restarted.");
    Fixture recursive;IgbTypeListConstructorInvocation reentry(recursive.object);recursive.action=[&](const auto&,auto&,auto&){try{reentry.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{reentry.resume(recursive.services);},"Swallowed recursive constructor failure completed.");

    std::weak_ptr<IgbRuntimeTypeList> no_owner_fields;std::weak_ptr<IgbObjectStorage> no_owner_storage;
    {Fixture unowned;no_owner_fields=unowned.types;no_owner_storage=unowned.object.storage;
        {IgbTypeListConstructorInvocation partial(unowned.object);check(!partial.resume().completed,"Unbound base constructor unexpectedly completed.");}}
    check(no_owner_fields.expired() && no_owner_storage.expired(),"Abandonment created a registry/allocation ownership cycle.");

    Fixture null_vtable;auto object=null_vtable.object;
    auto destructor_pool=std::shared_ptr<ModelSceneObject>(new ModelSceneData,[object](ModelSceneObject* value){igb_bootstrap_write_word(object,0,0);delete value;});
    null_vtable.backing_pool=destructor_pool;destructor_pool.reset();
    null_vtable.action=[&](const auto& c,auto&,auto&){if(c.kind==K::allocate_backing){null_vtable.calls.clear();null_vtable.backing_pool.reset();}};
    IgbTypeListFactoryInvocation null_tail(null_vtable.object_pool);
    rejects([&]{null_tail.resume(null_vtable.services);},"Null tail vtable was delegated as an unknown virtual.");
}
void actual_bootstrap_type_lists_and_reserve(){
    // Original constructors and list growth execute. Only physical allocators,
    // platform/manager hooks and the missing registration body remain external.
    using AK=IgbArkCoreServiceKind;using PK=IgbPointerListServiceKind;
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();
    IgbArkCoreObject ark{allocation(0x300000,0x3a0),core};igb_construct_ark_core(ark);
    auto object_pool=std::make_shared<ModelSceneData>(),backing_pool=std::make_shared<ModelSceneData>();
    auto lists=std::make_shared<IgbTypeListServices>();lists->bootstrap=std::make_shared<IgbBootstrapServices>();
    lists->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();lists->bootstrap->runtime->singleton=core;
    lists->bootstrap->default_pool_handle=5;
    auto manager=make_original_igb_memory_pool_registry();manager->original_pool_access_bound=true;manager->pools[5]=object_pool;
    auto pools=std::make_shared<IgbFilePoolsServices>();pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
    pools->memory_pools->selector=0;pools->memory_pools->fallback=manager;
    std::vector<IgbBootstrapPointer> objects,backings;
    std::vector<std::shared_ptr<IgbRuntimeTypeList>> owners;
    for(unsigned i=0;i<2;++i){
        auto owner=std::make_shared<IgbRuntimeTypeList>();owner->count=11;owner->capacity=91;
        auto object=allocation(0x400000+i*0x100,16);object.storage->bindings.push_back({0,{},{},{},owner});
        auto backing=allocation(0x500000+i*0x100,16);backing.storage->bytes->initialized.assign(16,0);
        objects.push_back(object);backings.push_back(backing);owners.push_back(owner);
    }
    unsigned object_allocations{},backing_allocations{},object_lookups{};
    lists->bootstrap->factory=[&](const IgbBootstrapCall& c){
        check(c.kind==B::pool_allocate,"Cold list allocation reached an unexpected bootstrap endpoint.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,c,out=op.get()]{
            check(c.pool==object_pool && c.bytes==16 && core->enabled==0 && object_allocations<2,"Cold list object allocation changed pool, size or gate.");
            out->allocation_result=objects.at(object_allocations++);return MessengerCallResult{true,0,{}};};return op;
    };
    lists->factory=[&](const IgbTypeListCall& c){auto op=std::make_shared<IgbTypeListOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==K::object_pool){++object_lookups;check(c.handle==0,"Native second base reset did not clear the cold list pool tag.");out->pool_result=backing_pool;}
        else if(c.kind==K::allocate_backing){check(c.pool==backing_pool && c.bytes==16 && backing_allocations<2,"Cold backing allocation lost its distinct pool or size.");
            out->backing_result={backings.at(backing_allocations++),{}};}
        else throw std::runtime_error("Cold type-list factory reached unexpected registration or reflection");
        return MessengerCallResult{true,0,{}};};return op;
    };
    auto pointers=std::make_shared<IgbPointerListServices>();
    auto grown=allocation(0x600000,1536*4);grown.storage->bytes->initialized.assign(1536*4,0);
    unsigned allocation_lookups{},size_queries{},growth_pool_lookups{},growths{};
    pointers->factory=[&](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,out=op.get()]{
        switch(c.kind){
        case PK::allocation_pool:
            ++allocation_lookups;check(c.backing.address==0x500000 && c.backing.bytes==backings[0].storage->bytes,"Actual capacity query lost original physical backing.");out->pool_result=backing_pool;break;
        case PK::allocation_size:
            ++size_queries;check(c.pool==backing_pool && c.backing.address==0x500000,"Capacity virtual lost captured pool/backing.");out->size_result=16;break;
        case PK::object_pool:
            ++growth_pool_lookups;check(c.handle==0,"Growth did not use live packed list pool tag.");out->pool_result=backing_pool;break;
        case PK::reallocate:
            ++growths;check(c.pool==backing_pool && c.backing.address==0x500000 && c.bytes==1536*4,"Native reserve1024 did not reach strict-greater1536 allocation.");
            out->backing_result={grown.storage->bytes,igb_bootstrap_address(grown),grown.offset,{},{}};break;
        }
        return MessengerCallResult{true,0,{}};};return op;
    };
    auto bootstrap=std::make_shared<IgbArkCoreBootstrapServices>();bootstrap->bootstrap=lists->bootstrap;bootstrap->pools=pools;
    bootstrap->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    bootstrap->callbacks->runtime=lists->bootstrap->runtime;
    unsigned platforms{},manager_calls{},callback_allocations{};
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> callback_owners;
    bootstrap->factory=make_native_igb_type_list_bootstrap_factory(lists,
        make_native_igb_pointer_list_bootstrap_factory(pointers,[&](const IgbArkCoreCall& c){
            auto op=std::make_shared<IgbArkCoreOperation>();op->resume=[&,c,out=op.get()]{
                check(core->enabled==0,"Cold bootstrap enabled reflection before registration.");
                if(c.kind==AK::platform_initialize)++platforms;
                else if(c.kind==AK::manager_virtual){++manager_calls;check(c.manager==manager && c.virtual_slot==0xa8,"Cold manager call changed owner/slot.");}
                else if(c.kind==AK::allocate_callbacks){check(c.bytes==12 && callback_allocations<2,"Callback physical allocation changed size/count.");
                    auto owner=std::make_shared<IgbRegistrationCallbackList>();owner->count=23;owner->capacity=29;callback_owners.push_back(owner);
                    out->callbacks_result=owner;out->allocation_result=allocation(0x700000+callback_allocations++*16,12);}
                else throw std::runtime_error("Prepared endpoint replaced real cold list construction or reserve");
                return MessengerCallResult{true,0,{}};};return op;
        }));
    IgbArkCoreBootstrapInvocation call(ark);const auto status=call.resume(bootstrap);
    check(!status.completed && !call.faulted() && status.diagnostic.find("8002A0D0")!=std::string::npos,"Composed bootstrap did not stop at actual missing registration body.");
    check(core->enabled==0 && igb_bootstrap_read_byte(ark.allocation,0)==0 && core->field_initialization_enabled==1,"Native list prefix enabled reflection prematurely.");
    check(core->types==owners[0] && *core->secondary_types==owners[1] && owners[0]!=owners[1],"Cold bootstrap failed to publish distinct constructed canonical lists.");
    check(igb_bootstrap_read_word(ark.allocation,0x18)==0x400000 && igb_bootstrap_read_word(ark.allocation,0x24)==0x400100,"Cold bootstrap lost actual physical list addresses.");
    check(object_allocations==2 && backing_allocations==2 && object_lookups==2 && lists->completed_allocations.size()==2,"Composed cold prefix skipped or replayed an original list allocation.");
    check(allocation_lookups==3 && size_queries==3 && growth_pool_lookups==1 && growths==1,"Actual bootstrap capacity/reserve query order changed.");
    check(owners[0]->backing_address==0x600000 && owners[0]->backing_bytes==grown.storage->bytes && !owners[0]->backing && owners[0]->count==0 && owners[0]->capacity==91,"Native growth fabricated mappings, host capacity or list entries.");
    check(owners[1]->backing_address==0x500100 && owners[1]->backing_bytes==backings[1].storage->bytes && !owners[1]->backing && owners[1]->count==0,"Second list inherited the first list's resized backing.");
    for(std::size_t i=0;i<grown.storage->bytes->bytes.size();++i){
        check(grown.storage->bytes->initialized[i]==(i<16?0:1),"Native reserve changed preserved prefix or omitted initialization of grown bytes.");
        check(grown.storage->bytes->bytes[i]==(i<16?0xa5:0),"Native reserve zeroed old bytes or failed to zero new backing bytes.");
    }
    for(const auto& object:objects)check(igb_bootstrap_read_word(object,0)==0x80475cb0 && igb_bootstrap_read_word(object,4)==1,"Cold registry did not execute actual derived constructor.");
    for(const auto& callback:callback_owners)check(callback->count==0 && callback->capacity==0 && !callback->backing,"Bootstrap omitted actual callback-list constructor.");
    check(platforms==1 && manager_calls==1 && callback_allocations==2,"Composed startup prefix replayed external hooks.");
    check(igb_bootstrap_read_word(ark.allocation,4)==0xa5a5a5a5 && igb_bootstrap_read_word(ark.allocation,8)==0xa5a5a5a5,"Missing registration published future count snapshots.");
    check(!call.resume(bootstrap).completed && object_allocations==2 && growths==1 && size_queries==3 && callback_allocations==2,"Pending registration replayed completed native list work.");
}

void distinct_kinds_offsets_and_adapters(){
    Fixture f;IgbTypeListFactoryInvocation registry(f.object_pool,IgbTypeListFactoryEntry::registry);
    check(registry.resume(f.services).completed && igb_bootstrap_read_word(f.object,0)==0x80475cb0,"Bootstrap registry2DB4C did not select derived53CB8.");
    Fixture offset;offset.backing.storage->bytes->bytes.resize(32,0xcc);offset.backing.storage->bytes->initialized.assign(32,0);
    offset.backing.offset=12;IgbTypeListFactoryInvocation shifted(offset.object_pool);
    check(shifted.resume(offset.services).completed && offset.types->backing_address==0x20000c && offset.types->backing_offset==12,"Backing suballocation lost original address/physical offset.");
    for(const auto initialized:offset.types->backing_bytes->initialized)check(initialized==0,"Backing allocation fabricated initialized metadata pointer words.");
    Fixture conflict;conflict.object.storage->bindings.push_back({0,{},{},std::make_shared<IgbDecodeFieldRegistry>()});
    IgbTypeListFactoryInvocation wrong(conflict.object_pool);rejects([&]{wrong.resume(conflict.services);},"Type list accepted a separate field-registry identity at its physical offset.");

    Fixture aliases;aliases.core->enabled=1;auto fields_services=std::make_shared<IgbFieldRegistryServices>();
    fields_services->metadata=std::make_shared<IgbRuntimeMetaType>();
    aliases.services->registry_metadata=std::shared_ptr<IgbRegistrationMetaSlot>(fields_services,&fields_services->metadata);
    auto live=std::make_shared<IgbRuntimeMetaType>();std::vector<std::uint32_t> bodies;
    aliases.services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    aliases.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&,c]{bodies.push_back(c.function);fields_services->metadata=live;return MessengerCallResult{true,0,{}};};return op;};
    aliases.action=[&](const auto& c,auto& op,auto&){check(c.kind==K::reflected_factory && c.metadata==live,"Actual registration runner did not reload the shared canonical registry cache.");op.allocation_result=aliases.object;};
    IgbTypeListFactoryInvocation reflected(aliases.object_pool,IgbTypeListFactoryEntry::registry);
    check(reflected.resume(aliases.services).completed && bodies==std::vector<std::uint32_t>{0x8002dd40},"Registry path skipped exact registration body2DD40.");

    Fixture child;auto metadata_adapter=make_native_igb_type_list_metadata_factory(child.services);
    IgbMetadataOperationCall create;create.kind=IgbMetadataOperationKind::create_children;create.pool=child.object_pool;
    auto op=metadata_adapter(create);check(op && op->resume().completed && op->children_result==child.types && igb_bootstrap_read_word(child.object,0)==0x80475c54,"Parent attachment adapter did not construct actual child-list identity.");
    create.kind=IgbMetadataOperationKind::append_child;check(!metadata_adapter(create),"Factory adapter invented raw append.");
    Fixture composed;auto parent=std::make_shared<IgbRuntimeMetaType>(),derived=std::make_shared<IgbRuntimeMetaType>();
    parent->children=std::shared_ptr<IgbRuntimeTypeList>{};parent->fields=std::make_shared<IgbDecodeFieldRegistry>();
    parent->type_word=0x80470010;parent->native_alignment=8;parent->allocation_prefix=12;parent->native_size=32;
    auto parent_services=std::make_shared<IgbMetadataOperationsServices>();parent_services->types=std::make_shared<IgbTypeRegistrationServices>();parent_services->types->metadata_pool_handle=7;
    unsigned appends{};parent_services->factory=make_native_igb_type_list_metadata_factory(composed.services,[&](const IgbMetadataOperationCall& c){
        auto result=std::make_shared<IgbMetadataOperation>();result->resume=[&,c,out=result.get()]{
            if(c.kind==IgbMetadataOperationKind::pool_for_handle){check(c.handle==7,"Parent lost its metadata allocation handle.");out->pool_result=composed.object_pool;}
            else if(c.kind==IgbMetadataOperationKind::append_child){++appends;check(c.children==composed.types && c.metadata==derived,"Parent caller appended to a different constructed list.");}
            else throw std::runtime_error("Unexpected composed metadata operation");return MessengerCallResult{true,0,{}};};return result;});
    IgbMetadataParentInvocation attach(derived,parent);check(attach.resume(parent_services).completed && appends==1 && *parent->children==composed.types && derived->parent==parent,"Actual65640 did not consume native2DE88 before its explicit append endpoint.");
    Fixture bootstrap;auto adapter=make_native_igb_type_list_bootstrap_factory(bootstrap.services);
    IgbArkCoreCall call;call.kind=IgbArkCoreServiceKind::create_type_list;call.pool=bootstrap.object_pool;
    auto prepared=adapter(call);check(prepared && prepared->resume().completed && prepared->types_result==bootstrap.types && prepared->allocation_result.storage==bootstrap.object.storage && igb_bootstrap_read_word(bootstrap.object,0)==0x80475cb0,"Cold bootstrap adapter lost the exact registry factory or physical allocation.");
    call.kind=IgbArkCoreServiceKind::reserve_types;check(!adapter(call),"Factory adapter invented reserve/growth behavior.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_type_list_factory_tests");
    disabled_factory();direct_constructors_and_live_gate();failure_and_pending_prefixes();enabled_branch_and_raw_owners();distinct_kinds_offsets_and_adapters();actual_bootstrap_type_lists_and_reserve();
    std::cout<<"igb_type_list_factory_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
