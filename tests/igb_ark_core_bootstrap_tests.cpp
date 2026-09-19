#include "dw4/igb_ark_core_bootstrap.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbArkCoreServiceKind;
unsigned checks{};
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();
    storage->bytes->bytes.assign(size,0xa5);storage->bytes->initialized.assign(size,1);return {storage,0};
}
struct Fixture {
    IgbArkCoreObject object{allocation(0x100000,0x3a0),std::make_shared<IgbRuntimeTypeRegistry>()};
    std::shared_ptr<IgbArkCoreBootstrapServices> services=std::make_shared<IgbArkCoreBootstrapServices>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=std::make_shared<IgbMemoryPoolRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>(),other_pool=std::make_shared<ModelSceneData>();
    std::vector<IgbArkCoreCall> calls;
    std::vector<std::uint32_t> registrations;
    std::vector<std::shared_ptr<IgbRuntimeTypeList>> lists;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> callback_lists;
    std::function<void(const IgbArkCoreCall&,IgbArkCoreOperation&,MessengerCallResult&)> action;
    std::function<void(std::uint32_t,MessengerCallResult&)> registration_action;
    Fixture(){
        services->bootstrap=std::make_shared<IgbBootstrapServices>();services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->bootstrap->runtime->singleton=object.core;services->bootstrap->default_pool_handle=7;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[7]=pool;manager->pools[11]=other_pool;
        services->enum_types=std::make_shared<IgbRuntimeTypeList>();(*services->enum_types)->count=3;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->bootstrap->runtime;
        services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){
            if(c.kind!=IgbRegistrationCallbacksServiceKind::invoke_function)return std::shared_ptr<IgbRegistrationCallbacksOperation>{};
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[this,c]{registrations.push_back(c.function);
                MessengerCallResult r{true,0,{}};if(registration_action)registration_action(c.function,r);return r;};return op;};
        services->factory=[this](const IgbArkCoreCall& c){auto op=std::make_shared<IgbArkCoreOperation>();op->resume=[this,c,out=op.get()]{
            calls.push_back(c);MessengerCallResult r{true,0,{}};
            if(c.kind==K::allocate_manager || c.kind==K::construct_manager){out->allocation_result=allocation(0x200000,0x19f4);out->manager_result=manager;}
            if(c.kind==K::create_type_list){auto list=std::make_shared<IgbRuntimeTypeList>();list->backing=std::make_shared<IgbRuntimeTypeList::Storage>();
                lists.push_back(list);out->types_result=list;out->allocation_result=allocation(0x300000+static_cast<std::uint32_t>(lists.size())*16,16);}
            if(c.kind==K::backing_size)r.value=16;
            if(c.kind==K::allocate_callbacks){auto list=std::make_shared<IgbRegistrationCallbackList>();list->count=13;list->capacity=19;
                callback_lists.push_back(list);out->callbacks_result=list;out->allocation_result=allocation(0x400000+static_cast<std::uint32_t>(callback_lists.size())*16,12);}
            if(c.kind==K::final_metadata)out->metadata_result=std::make_shared<IgbRuntimeMetaType>();
            if(action)action(c,*out,r);return r;};return op;};
        igb_construct_ark_core(object);
    }
};
void constructor_stores(){
    Fixture f;const auto& c=*f.object.core;
    check(c.enabled==0 && c.field_initialization_enabled==0 && c.vtable_offset==0,"Cold constructor did not disable reflection/field initialization.");
    check(!c.types && c.secondary_types && !*c.secondary_types && c.bootstrap_callbacks && !*c.bootstrap_callbacks,"Cold constructor missed canonical pointer stores.");
    check(!c.loader,"Constructor fabricated a loader value at untouched+40.");
    check(igb_bootstrap_read_word(f.object.allocation,0x40)==0xa5a5a5a5 && igb_bootstrap_read_byte(f.object.allocation,0x17)==0xa5,"Constructor zeroed untouched physical memory.");
    check(igb_bootstrap_read_word(f.object.allocation,0x7c)==0x80000 && igb_bootstrap_read_word(f.object.allocation,0x8c)==0xffffffff,"Constructor constants differ.");
    auto loader=std::make_shared<SystemStageLoader>();f.object.core->loader=loader;igb_construct_ark_core(f.object);
    check(*f.object.core->loader==loader,"Repeated raw construction changed untouched loader ownership.");
    IgbArkCoreObject short_object{allocation(0x110000,0x398),std::make_shared<IgbRuntimeTypeRegistry>()};
    rejects([&]{igb_construct_ark_core(short_object);},"Constructor accepted unavailable first+398 store.");
    check(short_object.core->bootstrap_fault_,"Partial constructor failure did not poison its owner.");
}
void complete_caller(){
    Fixture f;f.registration_action=[&](std::uint32_t function,auto&){
        if(function==0x8002a0d0){check(f.object.core->enabled==0,"Canonical metadata registration ran after enable.");f.object.core->types->count=7;}
        else check(f.object.core->enabled==1,"Post-bootstrap registration ran before enable.");};
    IgbArkCoreBootstrapInvocation call(f.object);check(call.resume(f.services).completed,"Bound bootstrap caller did not finish.");
    check(f.object.core->enabled==1 && f.object.core->field_initialization_enabled==1,"Bootstrap gate stores differ.");
    check(f.registrations.size()==23 && f.registrations.front()==0x8002a0d0 && f.registrations[1]==0x8002aafc && f.registrations.back()==0x80023234,"Bootstrap registration count/order differs.");
    check(igb_bootstrap_read_word(f.object.allocation,4)==7 && igb_bootstrap_read_word(f.object.allocation,8)==3,"Snapshot counts were invented or read too early.");
    check(f.object.core->types==f.lists[0] && *f.object.core->secondary_types==f.lists[1],"Canonical type-list publications differ.");
    check(*f.object.core->bootstrap_callbacks==f.callback_lists[0] && *f.object.core->registration_callbacks==f.callback_lists[1],"Callback lists were combined or lost.");
    for(const auto& list:f.callback_lists)check(list->count==0 && list->capacity==0 && !list->backing,"Actual callback-list constructor was skipped.");
    check(f.calls.front().kind==K::platform_initialize && f.calls.back().kind==K::manager_virtual && f.calls.back().virtual_slot==0xac,"Bootstrap boundary order differs.");
    check(f.services->final_metadata && *f.services->final_metadata,"Final canonical80561688 publication missing.");
    const auto calls=f.calls.size();check(call.resume().completed && f.calls.size()==calls,"Completed bootstrap replayed operations.");
}
void live_reads_and_threshold(){
    for(const auto size:{0u,4092u,4096u,4100u}){
        Fixture f;unsigned reserves{};f.action=[&](const auto& c,auto&,auto& r){
            if(c.kind==K::backing_size)r.value=size;
            if(c.kind==K::reserve_types){++reserves;check(c.argument==1024 && c.types==f.lists[0],"Reserve lost the captured list or1024 request.");}};
        IgbArkCoreBootstrapInvocation call(f.object);check(call.resume(f.services).completed,"Capacity fixture did not finish.");
        check(reserves==(size<=4096?1u:0u),"Bootstrap capacity threshold differs at equality.");
    }
    Fixture fresh;fresh.action=[&](const auto& c,auto&,auto&){
        if(c.kind==K::platform_initialize){fresh.object.core->vtable_offset=55;igb_bootstrap_write_word(fresh.object.allocation,0x394,55);}
        if(c.kind==K::reserve_types)fresh.services->bootstrap->default_pool_handle=11;
        if(c.kind==K::create_type_list)check(c.pool==(fresh.lists.size()==1?fresh.pool:fresh.other_pool),"Second list reused stale default pool selection.");};
    IgbArkCoreBootstrapInvocation changing(fresh.object);check(changing.resume(fresh.services).completed && fresh.object.core->vtable_offset==0,"Platform completion did not precede the original vtable-offset reset.");
    Fixture create;create.services->pools->memory_pools->fallback=std::shared_ptr<IgbMemoryPoolRegistry>{};
    IgbArkCoreBootstrapInvocation allocate_manager(create.object);check(allocate_manager.resume(create.services).completed,"Missing manager allocation path did not complete.");
    check(create.calls[1].kind==K::allocate_manager && create.calls[1].bytes==0x19f4 && create.calls[2].kind==K::construct_manager,"Manager construction changed its original allocation request.");
}
void boundaries_and_poison(){
    Fixture f;unsigned visits{};f.registration_action=[&](std::uint32_t function,auto& r){if(function==0x8002a0d0)r.completed=++visits>1;};
    IgbArkCoreBootstrapInvocation call(f.object);check(!call.resume(f.services).completed && f.object.core->enabled==0,"Pending metadata registration enabled reflection early.");
    check(igb_bootstrap_read_word(f.object.allocation,4)==0xa5a5a5a5,"Pending metadata registration published future count snapshots.");
    const auto factory=f.services->callbacks->factory;f.services->callbacks->factory={};
    check(!call.resume(f.services).completed && visits==2 && f.object.core->enabled==1,"Selected metadata callback was replaced or next unbound callback completed.");
    // Above resumes its retained metadata call, then reaches the first unbound
    // post-enable callback. It must not invent completion for that new call.
    f.services->callbacks->factory=factory;check(call.resume(f.services).completed && visits==2,"Rebinding the later callback replayed completed metadata registration.");
}
void failure_prefixes(){
    Fixture missing;missing.services->callbacks->factory={};IgbArkCoreBootstrapInvocation blocked(missing.object);
    check(!blocked.resume(missing.services).completed && missing.object.core->enabled==0,"Unbound canonical registration was treated as successful bootstrap.");
    Fixture null_manager;null_manager.services->pools->memory_pools->fallback=std::shared_ptr<IgbMemoryPoolRegistry>{};
    null_manager.action=[](const auto& c,auto& op,auto&){if(c.kind==K::allocate_manager){op.allocation_result={};op.manager_result.reset();}};
    IgbArkCoreBootstrapInvocation invalid(null_manager.object);rejects([&]{invalid.resume(null_manager.services);},"Null manager allocation bypassed reached virtual dereference.");
    check(null_manager.object.core->enabled==0 && null_manager.object.core->field_initialization_enabled==1,"Null manager lost the reached startup prefix.");
    Fixture enums;enums.services->enum_types=std::shared_ptr<IgbRuntimeTypeList>{};IgbArkCoreBootstrapInvocation enum_null(enums.object);
    rejects([&]{enum_null.resume(enums.services);},"Represented null enum list was replaced with a zero count.");
    check(enums.object.core->enabled==1 && igb_bootstrap_read_word(enums.object.allocation,8)==0xa5a5a5a5,"Enum failure did not preserve enable-before-count order.");
    Fixture abandon;abandon.services->factory={};{IgbArkCoreBootstrapInvocation unfinished(abandon.object);check(!unfinished.resume(abandon.services).completed,"Unbound platform operation completed.");}
    IgbArkCoreBootstrapInvocation restart(abandon.object);rejects([&]{restart.resume(abandon.services);},"Abandoned cold prefix was replayed.");
    Fixture recursion;IgbArkCoreBootstrapInvocation active(recursion.object);recursion.action=[&](const auto&,auto&,auto&){try{active.resume(recursion.services);}catch(const std::runtime_error&) {}};
    rejects([&]{active.resume(recursion.services);},"Swallowed recursive bootstrap failure completed.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_ark_core_bootstrap_tests");
    constructor_stores();complete_caller();live_reads_and_threshold();boundaries_and_poison();failure_prefixes();
    std::cout<<"igb_ark_core_bootstrap_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
