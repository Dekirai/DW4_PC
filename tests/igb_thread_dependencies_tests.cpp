#include "dw4/igb_thread_dependencies.hpp"

#include <iostream>

namespace {
using namespace dw4;using E=IgbThreadDependencyEntry;using M=IgbMetadataFactoryServiceKind;
using C=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
IgbBootstrapPointer allocation(std::uint32_t address,const std::shared_ptr<IgbRuntimeMetaType>& metadata){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(0x64,0xcc);storage->bytes->initialized.assign(0x64,1);
    storage->bindings.push_back({0,{},metadata,{}});return {storage,0};
}
// Prepared reflected allocation isolates the actual lazy getter, actual pool
// lookup and actual metadata factory. Full field construction is covered by
// the cold-bootstrap integration, not claimed by this fixture.
struct Fixture {
    std::shared_ptr<IgbThreadDependenciesServices> services=std::make_shared<IgbThreadDependenciesServices>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    IgbBootstrapPointer object=allocation(0x100000,metadata);
    unsigned registrations{},selections{},polls{};bool ready=true;
    std::function<void()> during;
    Fixture(){
        services->thread_list=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->semaphore=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->enabled=1;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->bootstrap->runtime=services->types->runtime;services->metadata->metadata=std::make_shared<IgbRuntimeMetaType>();
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){
            auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==M::register_type)op->resume=[this]{++registrations;return MessengerCallResult{true,0,{}};};
            else{
                check(c.kind==M::reflected_factory && c.pool==pool,"Lazy getter changed its captured pool or metadata path.");++selections;
                op->resume=[this,out=op.get()]{++polls;if(during)during();if(!ready)return MessengerCallResult{false,0,"Prepared allocation."};
                    out->allocation_result=object;return MessengerCallResult{true,0,{}};};
            }return op;
        };
    }
    auto slot(E entry){return entry==E::thread_list?services->thread_list:services->semaphore;}
};
void pointer_gate_and_construction(){
    for(const auto entry:{E::thread_list,E::semaphore}){
        for(const auto flags:{0u,4u,8u,0xffffffffu}){
            Fixture f;f.metadata->class_flags=flags;*f.slot(entry)=f.metadata;f.services->types.reset();f.services->pools.reset();f.services->metadata.reset();
            IgbThreadDependencyLookupInvocation run(entry);check(run.resume(f.services).completed && run.result()==f.metadata && !run.allocation().storage,
                "Existing dependency pointer required class readiness or an allocator.");
            *f.slot(entry)=std::shared_ptr<IgbRuntimeMetaType>{};check(run.resume().completed && run.result()==f.metadata,"Completed lazy getter reread the global.");
        }
        Fixture f;IgbThreadDependencyLookupInvocation run(entry);f.slot(entry)->reset();
        check(!run.resume(f.services).completed && f.registrations==0,"Unknown metadata cell was treated as null.");
        *f.slot(entry)=std::shared_ptr<IgbRuntimeMetaType>{};f.services->types->metadata_pool_handle.reset();
        check(!run.resume(f.services).completed && f.selections==0,"Missing metadata handle was silently supplied.");
        f.services->types->metadata_pool_handle=5;
        check(run.resume(f.services).completed && run.result()==f.metadata && *f.slot(entry)==f.metadata,"Lazy metadata was not published into the original canonical cell.");
        check(f.registrations==1 && f.selections==1 && f.polls==1 && f.metadata->class_flags==0,"Lazy allocation registered the dependency class or replayed its factory.");
        check(run.allocation().storage==f.object.storage && f.services->allocations.size()==1,"Lazy result lost its physical allocation owner.");
        IgbThreadDependencyLookupInvocation again(entry);check(again.resume(f.services).completed && again.allocation().storage==f.object.storage && f.selections==1,
            "Existing pointer lookup forgot its physical binding or reallocated.");
    }
    rejects([]{IgbThreadDependencyLookupInvocation bad(static_cast<E>(99));},"Unknown getter profile was accepted.");
}
void continuations_and_publication(){
    for(const auto entry:{E::thread_list,E::semaphore}){
        Fixture f;f.ready=false;const auto original_slot=f.slot(entry);IgbThreadDependencyLookupInvocation run(entry);
        check(!run.resume(f.services).completed && f.registrations==1 && f.selections==1,"Prepared lazy allocator failed to suspend.");
        rejects([&]{run.result();},"Pending lazy getter exposed a result.");
        auto injected=std::make_shared<IgbRuntimeMetaType>();injected->reference_word=73;*original_slot=injected;std::weak_ptr<IgbRuntimeMetaType> weak=injected;
        const auto replacement=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        if(entry==E::thread_list)f.services->thread_list=replacement;else f.services->semaphore=replacement;
        f.services->metadata=std::make_shared<IgbMetadataFactoryServices>();f.services->pools.reset();f.ready=true;
        check(run.resume(f.services).completed && *original_slot==f.metadata && !**replacement,"Lazy publication followed a replaced service cell or lost its selected factory.");
        check(f.polls==2 && f.selections==1 && f.registrations==1 && injected->reference_word==73,"Resuming replayed a call or decremented a raw overwritten pointer.");
        injected.reset();check(!weak.expired(),"Overwritten guest owner was destroyed by host publication.");
    }
    Fixture null;null.object={};IgbThreadDependencyLookupInvocation empty(E::semaphore);
    check(empty.resume(null.services).completed && !empty.result() && !empty.allocation().storage && !**null.services->semaphore,
        "Original null reflected result was replaced by a fabricated metadata object.");
    Fixture recursive;IgbThreadDependencyLookupInvocation bad(E::thread_list);
    recursive.during=[&]{try{bad.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{bad.resume(recursive.services);},"Swallowed recursive call did not invalidate lazy caller.");
    check(bad.faulted() && !**recursive.services->thread_list,"Faulted getter published a completed dependency.");
    rejects([&]{bad.resume(recursive.services);},"Faulted lazy getter restarted.");
    Fixture conflict;*conflict.services->thread_list=conflict.metadata;conflict.services->allocations={conflict.object,allocation(0x200000,conflict.metadata)};
    IgbThreadDependencyLookupInvocation alias(E::thread_list);check(alias.resume(conflict.services).completed,"Existing canonical getter failed before address lookup.");
    rejects([&]{alias.allocation();},"Conflicting physical metadata allocations were accepted.");
}
void bootstrap_append(){
    auto services=std::make_shared<IgbRegistrationCallbacksServices>();services->runtime=std::make_shared<IgbRuntimeTypesServices>();
    auto captured=std::make_shared<IgbRuntimeTypeRegistry>(),fresh=std::make_shared<IgbRuntimeTypeRegistry>();
    services->runtime->singleton=captured;auto list=std::make_shared<IgbRegistrationCallbackList>();
    auto fresh_list=std::make_shared<IgbRegistrationCallbackList>();fresh->bootstrap_callbacks=fresh_list;
    auto pool=std::make_shared<ModelSceneData>();fresh->registration_callback_pool=pool;
    IgbBootstrapCallbackAppendInvocation run(0x80023a50);
    check(!run.resume(services).completed,"Missing captured ArkCore list was fabricated.");
    services->runtime->singleton=fresh;captured->bootstrap_callbacks=list;
    unsigned selections{},polls{};bool ready{};
    services->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==C::allocate_backing && c.pool==pool && c.bytes==16,
        "Inner append did not reload ArkCore for its actual allocation pool.");++selections;
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&,out=op.get()]{++polls;
            if(!ready)return MessengerCallResult{false,0,"Prepared callback allocation."};
            out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);return MessengerCallResult{true,0,{}};};return op;};
    check(!run.resume(services).completed && list->capacity==4 && list->count==0 && selections==1,"Append allocation pause lost its capacity prefix.");
    services->factory={};captured->bootstrap_callbacks=fresh_list;services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};ready=true;
    check(run.resume(services).completed && list->count==1 && (*list->backing)[0]==0x80023a50 && fresh_list->count==0,
        "Bootstrap append followed a replaced list instead of its captured receiver.");
    check(run.resume().completed && selections==1 && polls==2,"Bootstrap append replayed a completed allocator or store.");
    IgbBootstrapCallbackAppendInvocation null_core(1);rejects([&]{null_core.resume(services);},"Known-null ArkCore was treated as missing input.");
    check(null_core.faulted(),"Null ArkCore fault did not persist.");
    services->runtime->singleton=fresh;fresh->bootstrap_callbacks=std::shared_ptr<IgbRegistrationCallbackList>{};
    IgbBootstrapCallbackAppendInvocation null_list(1);rejects([&]{null_list.resume(services);},"Known-null bootstrap list was silently created.");
    auto preallocated=std::make_shared<IgbRegistrationCallbackList>();preallocated->capacity=1;preallocated->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(1);
    fresh->bootstrap_callbacks=preallocated;IgbBootstrapCallbackAppendInvocation zero(0);
    check(zero.resume(services).completed && preallocated->count==1 && (*preallocated->backing)[0]==0,"Append incorrectly validated a callback before execution.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_thread_dependencies_tests");
    pointer_gate_and_construction();continuations_and_publication();bootstrap_append();
    std::cout<<"igb_thread_dependencies_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
