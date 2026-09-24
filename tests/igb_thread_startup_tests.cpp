#include "dw4/igb_thread_startup.hpp"
#include "dw4/igb_file_factory.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;
using K=IgbThreadStartupServiceKind;
using F=IgbFileFactoryServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class Fn>void rejects(Fn&& fn,const char* message){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
bool same(const IgbBootstrapPointer& a,const IgbBootstrapPointer& b){return a.storage==b.storage && a.offset==b.offset;}
IgbBootstrapPointer raw(std::uint32_t address,std::uint32_t table,std::uint32_t references=2){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(0x50,0xcc);
    storage->bytes->initialized.assign(0x50,1);IgbBootstrapPointer result{storage,0x10};
    igb_bootstrap_write_word(result,0,table);igb_bootstrap_write_word(result,4,references);return result;
}

// Prepared pool/factory/virtual endpoints isolate original755F8. These raw
// synthetic pointers do not claim constructed manager, thread or semaphore
// runtime owners, host threads, or recovered virtual implementations.
struct StartupFixture {
    std::shared_ptr<IgbThreadStartupServices> services=std::make_shared<IgbThreadStartupServices>();
    std::shared_ptr<ModelSceneObject> first_pool=std::make_shared<ModelSceneData>(),second_pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer old=raw(0x100000,0x100100,77),manager=raw(0x110000,0x110100),thread=raw(0x200000,0x200100);
    IgbBootstrapPointer replacement=raw(0x120000,0x120100);
    std::vector<IgbThreadStartupCall> selected;
    std::vector<unsigned> polls;
    std::vector<std::shared_ptr<IgbThreadStartupOperation>> operations;
    std::function<void(const IgbThreadStartupCall&,IgbThreadStartupOperation&)> action;
    std::optional<K> pause_kind;
    std::uint32_t pause_slot{};
    bool ready=true;
    StartupFixture(){
        services->pool_handle=4;services->manager=old;
        services->factory=[this](const IgbThreadStartupCall& call){
            const auto index=selected.size();selected.push_back(call);polls.push_back(0);
            auto op=std::make_shared<IgbThreadStartupOperation>();
            op->resume=[this,call,index,out=op.get()]{
                ++polls[index];if(action)action(call,*out);
                if(pause_kind==call.kind && (!pause_slot || pause_slot==call.virtual_slot) && !ready)
                    return MessengerCallResult{false,0,"Prepared original caller endpoint."};
                if(call.kind==K::pool_for_handle)out->pool_result=call.handle==4?first_pool:second_pool;
                else if(call.kind==K::create_manager)out->object_result=manager;
                else if(call.kind==K::create_thread)out->object_result=thread;
                return MessengerCallResult{true,0,{}};
            };operations.push_back(op);return op;
        };
    }
};

void original_order_and_live_reads(){
    StartupFixture f;const auto saved_thread=f.thread;
    f.action=[&](const auto& c,auto&){
        if(c.kind==K::pool_for_handle && c.handle==4)f.services->pool_handle=9;
        if(c.kind==K::create_thread)igb_bootstrap_write_word(saved_thread,0,0x220100);
        if(c.kind==K::object_virtual && c.virtual_slot==0xdc){
            check(igb_bootstrap_read_byte(c.object,0x2c)==1,"Master byte was not stored before the thread virtual.");
            f.services->manager=f.replacement;igb_bootstrap_write_word(f.replacement,0,0x330100);
        }
        if(c.kind==K::object_virtual && c.virtual_slot==0x60){
            check(same(c.argument,saved_thread),"Manager insertion lost the saved thread.");
            igb_bootstrap_write_word(saved_thread,4,0xa5000002);
        }
    };
    IgbThreadStartupInvocation run;check(run.resume(f.services).completed,"Startup caller did not finish its prepared operations.");
    check(f.selected.size()==6,"Startup invented or omitted a reached endpoint.");
    check(f.selected[0].kind==K::pool_for_handle && f.selected[0].handle==4 &&
        f.selected[1].kind==K::create_manager && f.selected[1].pool==f.first_pool &&
        f.selected[2].kind==K::pool_for_handle && f.selected[2].handle==9 &&
        f.selected[3].kind==K::create_thread && f.selected[3].pool==f.second_pool,
        "Startup reused its first handle/pool instead of the second original read.");
    check(f.selected[4].virtual_slot==0xdc && f.selected[4].vtable_word==0x220100 && same(f.selected[4].object,saved_thread) &&
        f.selected[5].virtual_slot==0x60 && f.selected[5].vtable_word==0x330100 && same(f.selected[5].object,f.replacement),
        "Startup cached a stale object, singleton or vtable across a callback.");
    check(same(*f.services->manager,f.replacement) && igb_bootstrap_read_word(saved_thread,4)==0xa5000001 &&
        igb_bootstrap_read_byte(saved_thread,0x2d)==0xcc,"Caller changed the wrong object/header or neighboring master byte.");
    check(f.services->retained.size()==1 && same(f.services->retained.front(),f.old) && igb_bootstrap_read_word(f.old,4)==77,
        "Singleton publication released or lost the displaced raw owner.");
    check(run.resume().completed && f.selected.size()==6,"Completed startup replayed its operations.");
}

void retained_calls_and_partial_effects(){
    StartupFixture f;f.pause_kind=K::pool_for_handle;f.ready=false;IgbThreadStartupInvocation run;
    check(!run.resume(f.services).completed && f.selected.size()==1 && same(*f.services->manager,f.old),
        "Pending first pool selected construction or published a manager.");
    const auto original_factory=f.services->factory;unsigned later_selections{};
    f.services->factory=[&](const auto& c){++later_selections;return original_factory(c);};
    f.operations.front()->resume=[]()->MessengerCallResult{throw std::runtime_error("Replaced endpoint continuation");};
    f.services->pool_handle=12;f.ready=true;
    check(run.resume(f.services).completed && f.polls[0]==2 && later_selections==5 &&
        f.selected[1].pool==f.first_pool && f.selected[2].handle==12,
        "Pending pool call lost its captured handle/continuation or blocked later fresh selections.");

    StartupFixture active;active.pause_kind=K::object_virtual;active.pause_slot=0xdc;active.ready=false;
    IgbThreadStartupInvocation wait;
    check(!wait.resume(active.services).completed && active.selected.size()==5 &&
        same(*active.services->manager,active.manager) && igb_bootstrap_read_byte(active.thread,0x2c)==1,
        "Pending thread virtual lost the manager/master-byte prefix.");
    igb_bootstrap_write_word(active.thread,0,0x44444444);active.services->manager=active.replacement;active.ready=true;
    check(wait.resume(active.services).completed && active.selected.size()==6 && active.polls[4]==2 &&
        active.selected[4].vtable_word==0x200100 && same(active.selected[5].object,active.replacement),
        "Pending thread virtual was reselected or manager reload happened too early.");

    StartupFixture destroy;igb_bootstrap_write_word(destroy.thread,4,0xa5000001);
    destroy.pause_kind=K::destroy_object;destroy.ready=false;IgbThreadStartupInvocation release;
    check(!release.resume(destroy.services).completed && destroy.selected.size()==7 &&
        igb_bootstrap_read_word(destroy.thread,4)==0xa5000000,"Destructor pause did not preserve the full-word decrement.");
    igb_bootstrap_write_word(destroy.thread,4,0x12345678);destroy.services->factory={};destroy.ready=true;
    check(release.resume(destroy.services).completed && destroy.polls[6]==2 &&
        igb_bootstrap_read_word(destroy.thread,4)==0x12345678,"Resuming destruction repeated release or lost its selected operation.");

    StartupFixture publication;publication.pause_kind=K::create_manager;publication.ready=false;
    const auto returned_manager=publication.manager;IgbThreadStartupInvocation publish;
    check(!publish.resume(publication.services).completed && publication.selected.size()==2,
        "Manager-publication regression failed to pause in its prepared factory.");
    publication.ready=true;
    check(!publish.resume().completed && !publish.faulted() && publication.polls[1]==2 &&
        same(*publication.services->manager,publication.old),
        "Absent singleton services lost the completed manager result or fabricated publication.");
    publication.manager=publication.replacement;
    check(publish.resume(publication.services).completed && same(*publication.services->manager,returned_manager) &&
        publication.selected.size()==6 && publication.polls[1]==2,
        "Late singleton storage replayed the manager factory or replaced its captured result.");
}

void null_virtual_tables(){
    for(const bool thread_table:{true,false}){
        StartupFixture f;igb_bootstrap_write_word(thread_table?f.thread:f.manager,0,0);
        IgbThreadStartupInvocation run;
        rejects([&]{run.resume(f.services);},"Known null vtable reached a prepared virtual endpoint.");
        check(run.faulted() && same(*f.services->manager,f.manager) &&
            igb_bootstrap_read_byte(f.thread,0x2c)==1 && igb_bootstrap_read_word(f.thread,4)==2,
            "Null-vtable fault erased prior publication/master-byte effects or released the saved thread.");
        check(f.selected.size()==(thread_table?4u:5u),
            "Null thread/manager vtable was dereferenced after selecting the impossible virtual.");
        if(!thread_table)check(f.selected.back().kind==K::object_virtual && f.selected.back().virtual_slot==0xdc,
            "Null manager vtable skipped the preceding completed thread activation.");
        rejects([&]{run.resume(f.services);},"Null-vtable fault allowed the original prefix to restart.");
    }
}

void null_unknown_and_reference_edges(){
    StartupFixture unknown;unknown.services->pool_handle.reset();IgbThreadStartupInvocation late;
    check(!late.resume(unknown.services).completed && unknown.selected.empty(),"Unknown thread-pool handle became a default.");
    publish_original_igb_thread_pool_handle(*unknown.services);
    check(unknown.services->pool_handle==4 && late.resume(unknown.services).completed,"Proven handle publication failed to resume caller.");

    StartupFixture null_thread;null_thread.thread={};IgbThreadStartupInvocation bad;
    rejects([&]{bad.resume(null_thread.services);},"Null thread did not fault at the master-byte write.");
    check(bad.faulted() && null_thread.selected.size()==4 && same(*null_thread.services->manager,null_thread.manager),
        "Null-thread fault rolled back manager publication or called a later virtual.");
    rejects([&]{bad.resume(null_thread.services);},"Faulted startup restarted.");

    for(const bool null_value:{false,true}){
        StartupFixture f;f.action=[&](const auto& c,auto&){if(c.kind==K::object_virtual && c.virtual_slot==0xdc){
            if(null_value)f.services->manager=IgbBootstrapPointer{};else f.services->manager.reset();}};
        IgbThreadStartupInvocation run;
        if(null_value){rejects([&]{run.resume(f.services);},"Represented null live manager was treated as unknown.");check(run.faulted(),"Null manager fault was not retained.");}
        else{
            check(!run.resume(f.services).completed && !run.faulted(),"Unknown live manager was fabricated or treated as null.");
            f.services->manager=f.replacement;
            check(run.resume(f.services).completed && same(f.selected.back().object,f.replacement),"Late manager binding replayed activation.");
        }
        check(f.polls[4]==1,"Live-manager handling replayed the captured thread activation.");
    }
    for(const std::uint32_t initial:{0u,1u,2u,0x800000u,0x800001u,0xa5000001u,0xffffffffu}){
        StartupFixture f;igb_bootstrap_write_word(f.thread,4,initial);IgbThreadStartupInvocation run;
        check(run.resume(f.services).completed,"Reference corpus did not complete.");
        const auto expected=initial-1u;const bool destroys=(expected&0x7fffffu)==0;
        check(igb_bootstrap_read_word(f.thread,4)==expected && f.selected.size()==(destroys?7u:6u),
            "Startup used low-bit decrement or full-word zero instead of full decrement/low23 destruction.");
        if(destroys)check(f.selected.back().kind==K::destroy_object && same(f.selected.back().object,f.thread),
            "Destructor received a different thread identity.");
    }
}

void failures_recursion_and_abandonment(){
    StartupFixture failure;failure.action=[](const auto& c,auto&){if(c.kind==K::create_thread)throw std::runtime_error("Prepared thread factory failure");};
    IgbThreadStartupInvocation bad;rejects([&]{bad.resume(failure.services);},"Factory exception was swallowed.");
    check(bad.faulted() && same(*failure.services->manager,failure.manager) && failure.selected.size()==4,
        "Failed factory erased the completed singleton prefix.");
    StartupFixture recursive;IgbThreadStartupInvocation nested;
    recursive.action=[&](const auto& c,auto&){if(c.kind==K::object_virtual && c.virtual_slot==0xdc)
        try{nested.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{nested.resume(recursive.services);},"Swallowed recursive startup call failed to invalidate its outer invocation.");
    check(nested.faulted() && recursive.selected.size()==5,"Recursive startup continued into manager insertion.");
    StartupFixture abandoned;abandoned.pause_kind=K::object_virtual;abandoned.pause_slot=0xdc;abandoned.ready=false;
    {IgbThreadStartupInvocation pending;check(!pending.resume(abandoned.services).completed,"Abandonment case failed to suspend.");}
    check(std::any_of(abandoned.services->retained.begin(),abandoned.services->retained.end(),
        [&](const auto& p){return same(p,abandoned.thread);}) && igb_bootstrap_read_word(abandoned.thread,4)==2,
        "Abandonment released the original temporary or lost its physical owner.");
}

// Only registration bodies and redirect/allocation endpoints are prepared.
// The actual66188 and6546C callers execute. Null allocator output provides an
// honest completed-null result; no file/descriptor impersonates a manager.
struct FactoryFixture {
    IgbThreadObjectKind kind;
    std::shared_ptr<IgbThreadStartupServices> services=std::make_shared<IgbThreadStartupServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> original=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>(),concrete=std::make_shared<IgbRuntimeMetaType>();
    std::vector<std::uint32_t> registrations;std::vector<IgbFileFactoryCall> reflection;
    std::function<void()> registration_action;
    unsigned registration_polls{},redirect_polls{},allocation_polls{};
    bool registration_ready=true,redirect_ready=true,allocation_ready=true;
    IgbBootstrapPointer allocation;
    explicit FactoryFixture(IgbThreadObjectKind k):kind(k){
        original->class_flags=7;original->flag_1a=1;original->factory_redirect=0;
        fresh->flag_1a=1;fresh->factory_redirect=kind==IgbThreadObjectKind::manager?0x80023a10:0x80023e20;
        concrete->flag_1a=0;concrete->factory_redirect=0;concrete->allocation_prefix=0;concrete->native_size=0x10;
        services->manager_metadata=std::make_shared<IgbRegistrationMetaSlot>(original);
        services->thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(original);
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
        services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){
            check(c.kind==IgbRegistrationCallbacksServiceKind::invoke_function &&
                c.function==(kind==IgbThreadObjectKind::manager?0x800238a8u:0x80023c68u),"Object factory skipped or changed its actual registration body.");
            registrations.push_back(c.function);auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[this]{++registration_polls;if(registration_action)registration_action();return MessengerCallResult{registration_ready,0,"Prepared registration body."};};return op;
        };
        services->reflected=std::make_shared<IgbFileFactoryServices>();
        services->reflected->factory=[this](const IgbFileFactoryCall& c){
            reflection.push_back(c);auto op=std::make_shared<IgbFileFactoryOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==F::redirect_type){++redirect_polls;out->metadata_result=concrete;return MessengerCallResult{redirect_ready,0,"Prepared concrete metadata getter."};}
                check(c.kind==F::allocate,"Explicit incoming pool unexpectedly invoked fallback selection.");++allocation_polls;
                out->allocation_result=allocation.storage;return MessengerCallResult{allocation_ready,0,"Prepared physical allocation."};
            };return op;
        };
    }
    auto& slot(){return kind==IgbThreadObjectKind::manager?services->manager_metadata:services->thread_metadata;}
};

void actual_object_factories(){
    for(const auto kind:{IgbThreadObjectKind::manager,IgbThreadObjectKind::thread}){
        FactoryFixture f(kind);f.registration_ready=false;IgbThreadObjectFactoryInvocation run(kind,f.pool);
        check(!run.resume(f.services).completed && f.registrations.size()==1 && f.reflection.empty(),
            "Ready metadata bypassed mandatory factory registration.");
        rejects([&]{run.result();},"Pending object factory exposed an allocation.");
        f.slot()=std::make_shared<IgbRegistrationMetaSlot>(f.fresh);f.registration_ready=true;f.redirect_ready=false;
        check(!run.resume(f.services).completed && f.registration_polls==2 && f.reflection.size()==1 &&
            f.reflection[0].metadata==f.fresh && f.reflection[0].redirect==*f.fresh->factory_redirect,
            "Factory captured metadata before registration or skipped abstract metadata redirection.");
        f.fresh->factory_redirect=0;*f.slot()=f.original;f.redirect_ready=true;f.allocation_ready=false;
        check(!run.resume(f.services).completed && f.redirect_polls==2 && f.reflection.size()==2 &&
            f.reflection[1].metadata==f.concrete && f.reflection[1].pool==f.pool && f.reflection[1].bytes==0x10,
            "Redirect continuation lost captured pool/metadata or followed a changed global.");
        f.services->reflected->factory={};f.allocation_ready=true;
        check(run.resume(f.services).completed && !run.result().storage && f.allocation_polls==2,
            "Actual null allocation was fabricated into a manager or lost retained allocation continuation.");
        check(run.resume().completed && f.registrations.size()==1,"Completed factory replayed registration.");

        FactoryFixture unknown(kind);unknown.slot()->reset();IgbThreadObjectFactoryInvocation late(kind,unknown.pool);
        check(!late.resume(unknown.services).completed && unknown.registrations.size()==1,"Unknown metadata was treated as a null dereference.");
        *unknown.slot()=std::shared_ptr<IgbRuntimeMetaType>{};
        rejects([&]{late.resume(unknown.services);},"Known null metadata silently paused or returned an object.");
        check(late.faulted() && unknown.registrations.size()==1,"Null metadata fault replayed registration.");

        FactoryFixture unbound(kind);*unbound.slot()=unbound.concrete;unbound.allocation=raw(0x400000,0);
        IgbThreadObjectFactoryInvocation boundary(kind,unbound.pool);
        check(!boundary.resume(unbound.services).completed && unbound.allocation_polls==1 && !boundary.faulted(),
            "Raw allocation without a runtime owner was presented as a constructed manager/thread.");
        rejects([&]{boundary.result();},"Uninitialized allocation exposed a completed factory pointer.");
    }
    FactoryFixture recursive(IgbThreadObjectKind::manager);IgbThreadObjectFactoryInvocation bad(recursive.kind,recursive.pool);
    recursive.registration_action=[&]{try{bad.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{bad.resume(recursive.services);},"Recursive object factory call did not invalidate outer caller.");
    check(bad.faulted() && recursive.reflection.empty(),"Faulted factory continued into reflection.");
    rejects([]{IgbThreadObjectFactoryInvocation bad(static_cast<IgbThreadObjectKind>(99),{});},"Unknown object factory profile was accepted.");
}

void native_adapters(){
    auto services=std::make_shared<IgbThreadStartupServices>();auto pool=std::make_shared<ModelSceneData>();
    services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
    services->pools->memory_pools->selector=0;auto manager=make_original_igb_memory_pool_registry();
    manager->original_pool_access_bound=true;manager->pools[4]=pool;services->pools->memory_pools->fallback=manager;
    unsigned delegated{};auto factory=make_native_igb_thread_startup_service_factory(services,[&](const auto&){++delegated;return std::shared_ptr<IgbThreadStartupOperation>{};});
    IgbThreadStartupCall call;call.kind=K::pool_for_handle;call.handle=4;auto selected=factory(call);
    check(selected && selected->resume().completed && selected->pool_result==pool,"Startup pool adapter failed actual handle4 lookup.");
    call.handle=0xffffffff;selected=factory(call);check(selected->resume().completed && !selected->pool_result,"Invalid original pool handle was replaced by handle4.");
    call.kind=K::object_virtual;check(!factory(call) && delegated==1,"Pool/factory adapter swallowed unrecovered object virtual.");
    call.kind=K::destroy_object;check(!factory(call) && delegated==2,"Pool/factory adapter fabricated object destruction.");
    for(const auto kind:{K::create_manager,K::create_thread}){call.kind=kind;call.pool=pool;selected=factory(call);
        check(selected && !selected->resume().completed && !selected->object_result.storage,"Factory adapter completed an unregistered runtime object.");}

    StartupFixture callback;auto callbacks=make_native_igb_thread_startup_function_factory(callback.services);
    IgbRegistrationCallbacksCall request;request.kind=IgbRegistrationCallbacksServiceKind::invoke_function;
    request.function=0x80023a30;auto operation=callbacks(request);check(operation && operation->resume().completed,"23A30 startup adapter was not routed.");
    request.function=0x80023a50;check(!callbacks(request),"Startup adapter consumed distinct teardown callback23A50.");
    request.function=0x8007568c;check(!callbacks(request),"Startup adapter claimed teardown body7568C.");
    request.function=0x800755f8;check(bool(callbacks(request)),"Direct755F8 caller adapter was missing.");
    auto ephemeral=std::make_shared<IgbThreadStartupServices>();auto expired=make_native_igb_thread_startup_service_factory(ephemeral);ephemeral.reset();
    call.kind=K::pool_for_handle;rejects([&]{expired(call);},"Expired service adapter invented new canonical services.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_thread_startup_tests");
    original_order_and_live_reads();retained_calls_and_partial_effects();null_virtual_tables();null_unknown_and_reference_edges();
    failures_recursion_and_abandonment();actual_object_factories();native_adapters();
    std::cout<<"igb_thread_startup_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Thread startup tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
