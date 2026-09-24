#include "dw4/igb_thread_runtime_init.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;
using Kind=IgbRuntimeObjectKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class Fn>void rejects(Fn&& fn,const char* message){
    bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,message);
}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){
    return a.storage==b.storage && a.offset==b.offset && a.runtime_object==b.runtime_object;
}
IgbObjectInstance object(std::uint32_t address,Kind kind,std::uint32_t table){
    IgbObjectInstance result;result.storage=std::make_shared<IgbObjectStorage>();result.offset=0x10;
    result.storage->original_address=address;result.storage->bytes=std::make_shared<IgbScratchBuffer>();
    result.storage->bytes->bytes.assign(0x80,0xcd);result.storage->bytes->initialized.assign(0x80,0);
    result.runtime_object=std::make_shared<IgbRuntimeObject>(kind);
    result.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{result.storage,result.offset};
    IgbObjectBinding binding;binding.offset=result.offset;binding.runtime_object=result.runtime_object;
    result.storage->bindings.push_back(binding);
    igb_bootstrap_write_word(pointer(result),0,table);igb_object_set_reference_word(result,0xa5000003);
    return result;
}
IgbObjectInitCall virtual_call(const IgbObjectInstance& object,std::uint32_t table,std::uint32_t slot){
    IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::object_virtual;call.object=object;
    call.vtable_word=table;call.virtual_slot=slot;return call;
}

// Prepared physical manager/semaphore identities and one virtual endpoint
// isolate original755C0. These fixtures do not implement semaphore77434,
// reflected member construction, OS primitives or a complete startup path.
struct Fixture {
    std::shared_ptr<IgbThreadRuntimeInitServices> services=std::make_shared<IgbThreadRuntimeInitServices>();
    IgbObjectInstance manager=object(0x100000,Kind::thread_manager,0x804700a8);
    IgbObjectInstance semaphore=object(0x200000,Kind::semaphore,0x80470314);
    IgbObjectInstance replacement=object(0x300000,Kind::semaphore,0x12345678);
    std::vector<IgbThreadRuntimeInitCall> selected;
    std::vector<std::shared_ptr<IgbThreadRuntimeInitOperation>> operations;
    std::function<MessengerCallResult(const IgbThreadRuntimeInitCall&)> action;
    unsigned polls{};
    bool ready=true;
    Fixture(){
        services->objects=std::make_shared<IgbObjectFieldConstructServices>();
        services->objects->objects={semaphore,replacement};
        igb_bootstrap_write_word(pointer(manager),0x0c,igb_bootstrap_address(pointer(semaphore)));
        services->factory=[this](const IgbThreadRuntimeInitCall& call){
            selected.push_back(call);auto operation=std::make_shared<IgbThreadRuntimeInitOperation>();
            operation->resume=[this,call]{++polls;if(action)return action(call);
                return MessengerCallResult{ready,0xdeadbeef,ready?"":"Prepared semaphore initialization is pending."};};
            operations.push_back(operation);return operation;
        };
    }
};

void hidden_result_and_ignored_status(){
    for(const auto value:{0u,1u,0xffffffffu}){
        Fixture f;const auto manager_bytes=f.manager.storage->bytes->bytes;
        const auto semaphore_bytes=f.semaphore.storage->bytes->bytes;
        f.action=[&](const IgbThreadRuntimeInitCall& call){
            check(same(call.object,f.semaphore) && call.virtual_slot==0x5c && call.vtable_word==0x80470314,
                "755C0 did not call the captured semaphore's actual +5C slot.");
            check(call.result && call.result->bytes.size()==4 && call.result->initialized.size()==4 &&
                std::none_of(call.result->initialized.begin(),call.result->initialized.end(),[](auto byte){return byte!=0;}),
                "Semaphore result destination was not four initially unknown stack bytes.");
            check(call.result!=f.manager.storage->bytes && call.result!=f.semaphore.storage->bytes,
                "Hidden result destination aliases a runtime object allocation.");
            // The original caller never reads this value. Leave bytes unknown
            // for one case to ensure the native caller does not manufacture a read.
            if(value){call.result->bytes={0x12,0x34,0x56,0x78};call.result->initialized.assign(4,1);}
            return MessengerCallResult{true,value,"Prepared nonzero status is ignored by755C0."};
        };
        IgbThreadManagerInitInvocation run(f.manager);const auto status=run.resume(f.services);
        check(status.completed && status.value==0 && status.diagnostic.empty() && run.completed() && !run.faulted(),
            "755C0 propagated the ignored semaphore result or failed to complete.");
        check(f.manager.storage->bytes->bytes==manager_bytes && f.semaphore.storage->bytes->bytes==semaphore_bytes &&
            igb_object_reference_word(f.manager)==0xa5000003 && igb_object_reference_word(f.semaphore)==0xa5000003,
            "Manager initialization wrote object fields or adjusted packed ownership.");
        check(run.resume().completed && f.selected.size()==1 && f.polls==1,
            "Completed manager initialization repeated the semaphore operation.");
    }
}

void captured_member_and_delayed_resolution(){
    Fixture f;f.services->objects->objects.clear();IgbThreadManagerInitInvocation run(f.manager);
    check(!run.resume(f.services).completed && !run.faulted() && f.selected.empty(),
        "Unknown semaphore arithmetic identity was fabricated or treated as null.");
    igb_bootstrap_write_word(pointer(f.manager),0x0c,igb_bootstrap_address(pointer(f.replacement)));
    igb_bootstrap_write_word(pointer(f.semaphore),0,0x88776655);
    // Physical VT must win over a stale host header mirror.
    f.semaphore.runtime_object->vtable_word=0x11111111;
    f.services->objects->objects={f.semaphore,f.semaphore,f.replacement};
    check(run.resume(f.services).completed && f.selected.size()==1 && same(f.selected[0].object,f.semaphore) &&
        f.selected[0].vtable_word==0x88776655,
        "Late identity resolution reread the manager member or used a stale semaphore vtable.");

    Fixture absent;IgbThreadManagerInitInvocation delayed(absent.manager);
    check(!delayed.resume().completed && !delayed.faulted(),"Missing resolution services unexpectedly completed or faulted.");
    igb_bootstrap_write_word(pointer(absent.manager),0x0c,0);
    check(delayed.resume(absent.services).completed && same(absent.selected.front().object,absent.semaphore),
        "Late services failed to resolve the originally captured nonnull semaphore.");

    Fixture missing_factory;missing_factory.services->factory={};IgbThreadManagerInitInvocation selected(missing_factory.manager);
    check(!selected.resume(missing_factory.services).completed && !selected.faulted(),"Missing endpoint factory became a completed virtual.");
    igb_bootstrap_write_word(pointer(missing_factory.semaphore),0,0x76543210);
    unsigned selections{};missing_factory.services->factory=[&](const IgbThreadRuntimeInitCall& call){
        ++selections;check(call.vtable_word==0x80470314 && call.virtual_slot==0x5c,
            "Late endpoint binding recaptured a virtual already selected from physical storage.");
        auto op=std::make_shared<IgbThreadRuntimeInitOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;
    };
    check(selected.resume(missing_factory.services).completed && selections==1,"Late endpoint factory could not resume the selected virtual.");
}

void retained_virtual_and_result_destination(){
    Fixture f;f.ready=false;IgbThreadManagerInitInvocation run(f.manager);
    const auto pending=run.resume(f.services);
    check(!pending.completed && pending.value==0xdeadbeef && f.selected.size()==1 && f.polls==1,
        "Pending semaphore operation lost its explicit pending status.");
    const auto stack_result=f.selected.front().result;
    stack_result->bytes[0]=0x5a;stack_result->initialized[0]=1;
    f.operations.front()->resume=[]()->MessengerCallResult{throw std::runtime_error("Replaced continuation was called.");};
    f.services->factory=[](const auto&)->std::shared_ptr<IgbThreadRuntimeInitOperation>{throw std::runtime_error("Replaced factory was selected.");};
    igb_bootstrap_write_word(pointer(f.manager),0x0c,igb_bootstrap_address(pointer(f.replacement)));
    igb_bootstrap_write_word(pointer(f.semaphore),0,0);
    f.services->objects->objects.clear();f.ready=true;
    check(run.resume().completed && !run.faulted() && f.selected.size()==1 && f.polls==2,
        "Resuming the captured semaphore call required services, reread its VT or replaced its continuation.");
    check(stack_result==f.selected.front().result && stack_result->initialized==std::vector<std::uint8_t>({1,0,0,0}) &&
        stack_result->bytes[0]==0x5a,"Pending virtual lost or reinitialized its hidden result destination.");

    Fixture absent;unsigned selections{};bool endpoint_ready{};std::shared_ptr<IgbScratchBuffer> destination;
    absent.services->factory=[&](const IgbThreadRuntimeInitCall& call){
        ++selections;if(!destination)destination=call.result;
        check(destination==call.result,"Unbound virtual recreated its stack-result destination.");
        if(!endpoint_ready)return std::shared_ptr<IgbThreadRuntimeInitOperation>{};
        auto op=std::make_shared<IgbThreadRuntimeInitOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;
    };
    IgbThreadManagerInitInvocation lookup(absent.manager);
    check(!lookup.resume(absent.services).completed && selections==1,"Absent endpoint did not stop at the reached virtual.");
    absent.services->factory=[](const auto&)->std::shared_ptr<IgbThreadRuntimeInitOperation>{throw std::runtime_error("Changed unbound factory was used.");};
    endpoint_ready=true;check(lookup.resume(absent.services).completed && selections==2,"Unbound endpoint lost its retained factory.");

    Fixture empty;auto operation=std::make_shared<IgbThreadRuntimeInitOperation>();unsigned empty_selections{};
    empty.services->factory=[&](const auto&){++empty_selections;return operation;};IgbThreadManagerInitInvocation callback(empty.manager);
    check(!callback.resume(empty.services).completed,"Missing endpoint continuation was treated as a no-op.");
    operation->resume=[] {return MessengerCallResult{true,0,{}};};
    check(callback.resume().completed && empty_selections==1,"Late continuation reselected the already captured operation.");
}

void invalid_storage_and_partial_failures(){
    for(unsigned scenario=0;scenario<7;++scenario){
        Fixture f;
        if(scenario==0)igb_bootstrap_write_word(pointer(f.manager),0x0c,0);
        if(scenario==1)igb_bootstrap_write_word(pointer(f.semaphore),0,0);
        if(scenario==2)f.manager.storage->bytes->initialized[f.manager.offset+0x0d]=0;
        if(scenario==3)f.semaphore.storage->bytes->initialized[f.semaphore.offset+1]=0;
        if(scenario==4)f.semaphore.runtime_object->physical_storage.reset();
        if(scenario==5)f.manager.runtime_object->physical_storage->offset+=4;
        if(scenario==6){auto conflicting=object(0x200000,Kind::semaphore,0x80470314);f.services->objects->objects.push_back(conflicting);}
        IgbThreadManagerInitInvocation run(f.manager);
        rejects([&]{run.resume(f.services);},"Invalid physical manager/semaphore state reached an endpoint.");
        check(run.faulted() && !run.completed() && f.selected.empty(),"Pre-call fault did not remain terminal before endpoint selection.");
        rejects([&]{run.resume(f.services);},"Faulted manager invocation restarted.");
    }
    IgbThreadManagerInitInvocation null_manager({});
    rejects([&]{null_manager.resume();},"Null manager was accepted as a runtime object.");
    check(null_manager.faulted(),"Null manager did not retain its fault.");

    Fixture partial;std::shared_ptr<IgbScratchBuffer> result;
    partial.action=[&](const IgbThreadRuntimeInitCall& call)->MessengerCallResult{
        result=call.result;result->bytes[2]=0x91;result->initialized[2]=1;
        igb_bootstrap_write_byte(pointer(partial.semaphore),0x14,1);
        throw std::runtime_error("Prepared semaphore fault after a physical effect.");
    };
    IgbThreadManagerInitInvocation run(partial.manager);
    rejects([&]{run.resume(partial.services);},"Throwing semaphore endpoint was swallowed.");
    check(run.faulted() && result && result->initialized[2]==1 && result->bytes[2]==0x91 &&
        igb_bootstrap_read_byte(pointer(partial.semaphore),0x14)==1 && partial.polls==1,
        "Partial semaphore fault erased prior physical effects.");
    rejects([&]{run.resume(partial.services);},"Partially faulted manager initializer replayed its call.");
}

void reentry_and_abandoned_lifetimes(){
    Fixture f;IgbThreadManagerInitInvocation run(f.manager);
    f.action=[&](const auto&){
        rejects([&]{run.resume(f.services);},"Recursive manager initialization was accepted.");
        return MessengerCallResult{true,0,{}};
    };
    rejects([&]{run.resume(f.services);},"Endpoint swallowed a recursive invalidation and completed the outer caller.");
    check(run.faulted() && !run.completed() && f.polls==1,"Recursive invocation lost its terminal fault.");

    Fixture abandoned;abandoned.ready=false;std::shared_ptr<IgbScratchBuffer> stack;
    {IgbThreadManagerInitInvocation pending(abandoned.manager);
        check(!pending.resume(abandoned.services).completed,"Abandonment fixture did not pause inside its endpoint.");
        stack=abandoned.selected.front().result;}
    const auto retained=[&](const std::shared_ptr<void>& value){return std::find(abandoned.services->retained.begin(),abandoned.services->retained.end(),value)!=abandoned.services->retained.end();};
    check(retained(abandoned.manager.storage) && retained(abandoned.semaphore.storage) && retained(stack),
        "Abandoned native call discarded a runtime allocation or its pending stack result.");
}

void exact_initializer_adapter(){
    Fixture f;const auto list=object(0x400000,Kind::thread_list,0x80476ce0);
    unsigned delegated{};auto sentinel=std::make_shared<IgbObjectInitOperation>();
    auto factory=make_native_igb_thread_runtime_init_factory(f.services,[&](const auto&){++delegated;return sentinel;});
    for(const bool manager:{false,true})for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u}){
        if(manager && slot==0x2c)continue;
        const auto instance=manager?f.manager:list;const auto original=instance.storage->bytes->bytes;
        const auto op=factory(virtual_call(instance,manager?0x804700a8:0x80476ce0,slot));
        check(op && op!=sentinel && op->resume().completed && op->resume().completed,
            "A verified BLR manager/list hook was not represented exactly.");
        check(instance.storage->bytes->bytes==original && f.selected.empty(),"BLR hook performed member initialization or changed storage.");
    }
    f.ready=false;const auto manager_tail=factory(virtual_call(f.manager,0x804700a8,0x2c));
    check(manager_tail && !manager_tail->resume().completed && f.selected.size()==1 && f.selected.front().virtual_slot==0x5c,
        "Manager +2C was replaced by a successful BLR stub.");
    f.ready=true;check(manager_tail->resume().completed,"Manager +2C adapter did not resume the actual retained755C0 caller.");
    for(const auto slot:{0x38u,0x48u,0x60u})check(factory(virtual_call(list,0x80476ce0,slot))==sentinel,
        "Unsupported list runtime operation was silently treated as an initializer leaf.");
    check(factory(virtual_call(f.manager,0x804700a8,0x60))==sentinel,"Manager list insertion was claimed by the initializer adapter.");
    auto other=virtual_call({},0x12340000,0x58);check(factory(other)==sentinel,"Unknown vtable was accepted by the thread initializer adapter.");
    other.kind=IgbObjectInitServiceKind::field_virtual;other.vtable_word=0x804700a8;
    check(factory(other)==sentinel && delegated==6,"Non-object call was intercepted or delegation count changed.");
    for(const auto& wrong:{list,f.semaphore})rejects([&]{factory(virtual_call(wrong,0x804700a8,0x0c));},
        "Manager vtable accepted a different runtime layout kind.");
    rejects([&]{factory(virtual_call(f.manager,0x80476ce0,0x58));},"List getter accepted a manager owner.");
    auto untyped=f.manager;untyped.runtime_object.reset();
    rejects([&]{factory(virtual_call(untyped,0x804700a8,0x24));},"Initializer accepted raw storage without canonical runtime identity.");
}

void fresh_plain_metadata_getters(){
    for(const bool manager:{false,true}){
        Fixture f;const auto instance=manager?f.manager:object(0x400000,Kind::thread_list,0x80476ce0);
        const auto table=manager?0x804700a8u:0x80476ce0u;
        auto factory=make_native_igb_thread_runtime_init_factory(f.services);
        auto op=factory(virtual_call(instance,table,0x58));
        check(op && !op->resume().completed && f.selected.empty(),"Unknown canonical metadata triggered registration or became null.");
        f.services->manager_registration=std::make_shared<IgbPlatformThreadRegistrationServices>();
        f.services->manager_registration->state=std::make_shared<IgbPlatformThreadRegistrationState>();
        f.services->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        f.services->dependencies->thread_list=std::make_shared<IgbRegistrationMetaSlot>();
        check(!op->resume().completed,"Unknown canonical cell was mistaken for a represented null pointer.");
        const auto publish=[&](std::shared_ptr<IgbRuntimeMetaType> type){
            if(manager)f.services->manager_registration->state->metadata=std::move(type);
            else *f.services->dependencies->thread_list=std::move(type);
        };
        publish({});check(op->resume().completed && !op->metadata_result,
            "Plain metadata load registered or rejected its known null canonical pointer.");
        auto first=std::make_shared<IgbRuntimeMetaType>();first->class_flags=0;
        auto second=std::make_shared<IgbRuntimeMetaType>();second->class_flags=8;publish(first);
        check(op->resume().completed && !op->metadata_result,"Completed plain getter reread a replaced canonical global.");
        op=factory(virtual_call(instance,table,0x58));publish(second);
        check(op->resume().completed && op->metadata_result==second && f.selected.empty(),
            "New plain getter captured metadata too early or incorrectly required class readiness.");
        publish(first);auto fresh=factory(virtual_call(instance,table,0x58));
        check(fresh->resume().completed && fresh->metadata_result==first && first->class_flags==0,
            "Plain runtime metadata getter performed full registration instead of a fresh canonical load.");
    }
    auto services=std::make_shared<IgbThreadRuntimeInitServices>();
    const auto manager=object(0x500000,Kind::thread_manager,0x804700a8);
    const auto factory=make_native_igb_thread_runtime_init_factory(services);services.reset();
    rejects([&]{factory(virtual_call(manager,0x804700a8,0x58));},"Expired metadata services were silently recreated.");
    check(factory(virtual_call(manager,0x804700a8,0x0c))->resume().completed,
        "A verified BLR hook acquired an unnecessary registration dependency.");
}
}

int main(){
    try{hidden_result_and_ignored_status();captured_member_and_delayed_resolution();retained_virtual_and_result_destination();
        invalid_storage_and_partial_failures();reentry_and_abandoned_lifetimes();exact_initializer_adapter();fresh_plain_metadata_getters();
        std::cout<<"Thread runtime initializer checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"Thread runtime initializer check failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
