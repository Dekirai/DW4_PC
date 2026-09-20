#include "dw4/igb_field_registry_factory.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbFieldRegistryServiceKind;using B=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
template<class F>void rejects(F&& action,const char* why){++checks;try{action();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xa5);storage->bytes->initialized.assign(size,1);return {storage,0};
}
struct Fixture {
    std::shared_ptr<IgbFieldRegistryServices> services=std::make_shared<IgbFieldRegistryServices>();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> object_pool=std::make_shared<ModelSceneData>(),backing_pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer object=allocation(0x100000,16),backing=allocation(0x200000,16);
    std::vector<IgbBootstrapCall> bootstrap_calls;std::vector<IgbFieldRegistryCall> calls;
    std::function<void(const IgbBootstrapCall&,IgbBootstrapOperation&,MessengerCallResult&)> bootstrap_action;
    std::function<void(const IgbFieldRegistryCall&,IgbFieldRegistryOperation&,MessengerCallResult&)> action;
    Fixture(){
        object.storage->bindings.push_back({0,{},{},fields});fields->count=17;
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
        services->factory=[this](const IgbFieldRegistryCall& c){auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[this,c,out=op.get()]{
            calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==K::object_pool)out->pool_result=backing_pool;
            if(c.kind==K::allocate_backing)out->backing_result={backing,{}};
            if(action)action(c,*out,r);return r;};return op;};
    }
};
void disabled_factory(){
    Fixture f;IgbFieldRegistryFactoryInvocation call(f.object_pool);
    check(call.resume(f.services).completed && call.result()==f.fields,"Disabled field-list factory did not construct its canonical registry.");
    check(call.allocation().storage==f.object.storage,"Factory lost its physical allocation.");
    check(f.bootstrap_calls.size()==1 && f.bootstrap_calls[0].kind==B::pool_allocate && f.bootstrap_calls[0].bytes==16 && f.bootstrap_calls[0].pool==f.object_pool,"Object allocation changed the original pool or16-byte size.");
    check(f.calls.size()==2 && f.calls[0].kind==K::object_pool && f.calls[0].handle==0 && f.calls[1].kind==K::allocate_backing && f.calls[1].bytes==16 && f.calls[1].pool==f.backing_pool,"Backing allocation reused the incoming pool or wrong handle namespace.");
    check(igb_bootstrap_read_word(f.object,0)==0x80475cb0 && igb_bootstrap_read_word(f.object,4)==1 && f.fields->reference_word==1,"Derived vtable or packed reference word differs.");
    check(igb_bootstrap_read_word(f.object,8)==0x200000 && igb_bootstrap_read_word(f.object,12)==0 && f.fields->count==0,"Backing pointer/count stores differ.");
    check(!f.fields->backing && f.fields->backing_bytes==f.backing.storage->bytes && f.fields->backing_address==0x200000,"Uninitialized pointer storage manufactured typed field entries.");
    const auto count=f.calls.size();check(call.resume().completed && f.calls.size()==count,"Completed factory replayed allocation.");
    Fixture fallback;IgbFieldRegistryFactoryInvocation without_pool;
    check(without_pool.resume(fallback.services).completed && fallback.bootstrap_calls.front().kind==B::pool_for_handle && fallback.bootstrap_calls.front().handle==7,"Missing pool did not use actual raw allocation selection.");
    Fixture null_backing;null_backing.action=[](const auto& c,auto& op,auto&){if(c.kind==K::allocate_backing)op.backing_result={};};
    IgbFieldRegistryFactoryInvocation empty;check(empty.resume(null_backing.services).completed && !null_backing.fields->backing && null_backing.fields->backing_address==0,"Null backing was incorrectly treated as failed object construction.");
}
void direct_constructors_and_live_gate(){
    for(const auto entry:{IgbFieldRegistryConstructor::derived,IgbFieldRegistryConstructor::base,IgbFieldRegistryConstructor::reset,IgbFieldRegistryConstructor::derived_reset}){
        Fixture f;IgbFieldRegistryConstructorInvocation call(f.object,entry);check(call.resume(f.services).completed,"Direct constructor did not complete.");
        check(igb_bootstrap_read_word(f.object,0)==((entry==IgbFieldRegistryConstructor::derived || entry==IgbFieldRegistryConstructor::derived_reset)?0x80475cb0u:0x80475c54u),"Direct constructor published the wrong vtable.");
        check(f.calls.size()==((entry==IgbFieldRegistryConstructor::reset || entry==IgbFieldRegistryConstructor::derived_reset)?0u:2u),"Reset allocated backing or full constructor omitted it.");
    }
    Fixture changed;changed.bootstrap_action=[&](const auto& c,auto&,auto&){if(c.kind==B::pool_allocate)changed.core->enabled=1;};
    changed.action=[&](const auto& c,auto&,auto&){if(c.kind==K::object_pool)check(c.handle==0 && changed.fields->reference_word==1,"Second base reset failed to clear the freshly assigned pool tag.");};
    IgbFieldRegistryFactoryInvocation call(changed.object_pool);check(call.resume(changed.services).completed,"Fresh enabled base-constructor branch failed.");
    check(changed.bootstrap_calls.size()==3 && changed.bootstrap_calls[1].kind==B::allocation_pool && changed.bootstrap_calls[1].address==0x100000 && changed.bootstrap_calls[2].kind==B::pool_index,"Base constructor skipped actual allocation-base/tag work after gate mutation.");
}
void failure_and_pending_prefixes(){
    Fixture null;null.bootstrap_action=[](const auto& c,auto& op,auto&){if(c.kind==B::pool_allocate)op.allocation_result={};};
    IgbFieldRegistryFactoryInvocation missing(null.object_pool);rejects([&]{missing.resume(null.services);},"Null object allocation returned successfully instead of failing at the tail vtable.");
    Fixture header;header.object.storage->bytes->initialized[4]=0;IgbFieldRegistryConstructorInvocation unreadable(header.object);
    rejects([&]{unreadable.resume(header.services);},"Constructor skipped its actual old reference-word read.");
    check(igb_bootstrap_read_word(header.object,0)==0x804717f0 && header.fields->count==17,"Failure did not retain the reached base-vtable prefix.");
    Fixture pool;pool.action=[](const auto& c,auto& op,auto&){if(c.kind==K::object_pool)op.pool_result.reset();};
    IgbFieldRegistryConstructorInvocation no_pool(pool.object);rejects([&]{no_pool.resume(pool.services);},"Null object pool was accepted for backing allocation.");
    check(pool.fields->count==0 && igb_bootstrap_read_word(pool.object,8)==0,"Reached null pool lost completed list resets.");
    Fixture pending;auto selected=pending.backing_pool;unsigned visits{};
    pending.action=[&](const auto& c,auto&,auto& r){if(c.kind==K::allocate_backing){++visits;check(c.pool==selected && c.bytes==16,"Pending allocator lost captured pool/size.");r.completed=visits>1;}};
    IgbFieldRegistryFactoryInvocation delayed(pending.object_pool);check(!delayed.resume(pending.services).completed,"Prepared pending backing operation completed.");
    pending.backing_pool=std::make_shared<ModelSceneData>();pending.services->factory={};
    check(delayed.resume(pending.services).completed && visits==2,"Pending operation was recreated or lost its captured callback.");
    check(pending.bootstrap_calls.size()==1 && pending.calls.size()==3,"Pending backing replayed object allocation or pool lookup.");
    Fixture absent;absent.services->bootstrap->runtime->singleton.reset();IgbFieldRegistryFactoryInvocation gate;
    check(!gate.resume(absent.services).completed,"Missing ArkCore binding became disabled state.");
    absent.services->bootstrap->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};rejects([&]{gate.resume(absent.services);},"Represented null ArkCore did not fault.");
}
void enabled_branch_and_raw_owners(){
    Fixture f;f.core->enabled=1;auto first=std::make_shared<IgbRuntimeMetaType>(),live=std::make_shared<IgbRuntimeMetaType>();f.services->metadata=first;
    f.action=[&](const auto& c,auto& op,auto&){if(c.kind==K::register_type)f.services->metadata=live;
        if(c.kind==K::reflected_factory){check(c.metadata==live && c.pool==f.object_pool,"Reflected list factory reused pre-registration metadata or lost its pool.");op.allocation_result=f.object;}};
    IgbFieldRegistryFactoryInvocation call(f.object_pool);check(call.resume(f.services).completed && call.result()==f.fields && f.bootstrap_calls.empty(),"Enabled path was replaced with direct construction.");
    check(igb_bootstrap_read_word(f.object,0)==0xa5a5a5a5,"Enabled path silently constructed the prepared reflected result.");
    Fixture old;auto previous=std::make_shared<IgbDecodeFieldRegistry::Storage>(1);std::weak_ptr<IgbDecodeFieldRegistry::Storage> weak=previous;
    old.fields->backing=previous;previous.reset();
    IgbFieldRegistryConstructorInvocation reset(old.object,IgbFieldRegistryConstructor::reset);check(reset.resume().completed && !weak.expired(),"Raw reset implicitly freed the overwritten field-pointer backing.");
    Fixture abandoned;abandoned.services->factory={};{IgbFieldRegistryConstructorInvocation pending(abandoned.object);check(!pending.resume(abandoned.services).completed,"Unbound object-pool lookup completed.");}
    IgbFieldRegistryConstructorInvocation restart(abandoned.object);rejects([&]{restart.resume(abandoned.services);},"Abandoned partial constructor restarted.");
    Fixture recursive;IgbFieldRegistryConstructorInvocation reentry(recursive.object);recursive.action=[&](const auto&,auto&,auto&){try{reentry.resume(recursive.services);}catch(const std::runtime_error&) {}};
    rejects([&]{reentry.resume(recursive.services);},"Swallowed recursive constructor failure completed.");

    std::weak_ptr<IgbDecodeFieldRegistry> no_owner_fields;std::weak_ptr<IgbObjectStorage> no_owner_storage;
    {Fixture unowned;no_owner_fields=unowned.fields;no_owner_storage=unowned.object.storage;
        {IgbFieldRegistryConstructorInvocation partial(unowned.object);check(!partial.resume().completed,"Unbound base constructor unexpectedly completed.");}}
    check(no_owner_fields.expired() && no_owner_storage.expired(),"Abandonment created a registry/allocation ownership cycle.");

    Fixture null_vtable;auto object=null_vtable.object;
    auto destructor_pool=std::shared_ptr<ModelSceneObject>(new ModelSceneData,[object](ModelSceneObject* value){igb_bootstrap_write_word(object,0,0);delete value;});
    null_vtable.backing_pool=destructor_pool;destructor_pool.reset();
    null_vtable.action=[&](const auto& c,auto&,auto&){if(c.kind==K::allocate_backing){null_vtable.calls.clear();null_vtable.backing_pool.reset();}};
    IgbFieldRegistryFactoryInvocation null_tail(null_vtable.object_pool);
    rejects([&]{null_tail.resume(null_vtable.services);},"Null tail vtable was delegated as an unknown virtual.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_registry_factory_tests");
    disabled_factory();direct_constructors_and_live_gate();failure_and_pending_prefixes();enabled_branch_and_raw_owners();
    std::cout<<"igb_field_registry_factory_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
