#include "dw4/igb_gamecube_thread_init.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// The original tagged-pool and aligned-allocation endpoints are prepared
// explicitly. These tests do not claim current-thread adoption or scheduling.
namespace {
using namespace dw4;
using MK=IgbMemoryFieldConstructServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t fill=0xcd,std::uint8_t known=0){
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,fill);result->initialized.assign(size,known);return result;
}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
IgbObjectInstance object(IgbRuntimeObjectKind kind,std::uint32_t table,std::uint32_t address,std::size_t size){
    IgbObjectInstance result;result.offset=0x10;result.storage=std::make_shared<IgbObjectStorage>();
    result.storage->original_address=address;result.storage->bytes=bytes(size+result.offset);
    result.runtime_object=std::make_shared<IgbRuntimeObject>(kind);
    result.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{result.storage,result.offset};
    IgbObjectBinding binding;binding.offset=result.offset;binding.runtime_object=result.runtime_object;result.storage->bindings.push_back(binding);
    igb_bootstrap_write_word(pointer(result),0,table);return result;
}
bool word_known(const IgbObjectInstance& object,unsigned relative){const auto b=object.storage->bytes;const auto at=object.offset+relative;
    return at+4<=b->initialized.size() && std::all_of(b->initialized.begin()+at,b->initialized.begin()+at+4,[](auto value){return value!=0;});}
std::uint32_t word(const IgbObjectInstance& object,unsigned relative){return igb_bootstrap_read_word(pointer(object),relative);}
struct Fixture {
    std::shared_ptr<IgbGamecubeThreadInitServices> services=std::make_shared<IgbGamecubeThreadInitServices>();
    IgbObjectInstance thread=object(IgbRuntimeObjectKind::thread,0x80470110,0x100000,0x388);
    IgbObjectInstance semaphore=object(IgbRuntimeObjectKind::semaphore,0x80470314,0x200000,0x28);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbPointerListBacking backing;
    std::vector<IgbThreadRuntimeInitCall> runtime_calls;
    std::vector<IgbMemoryFieldConstructCall> memory_calls;
    std::vector<std::shared_ptr<IgbThreadRuntimeInitOperation>> runtime_operations;
    std::vector<std::shared_ptr<IgbMemoryFieldConstructOperation>> memory_operations;
    std::function<std::optional<MessengerCallResult>(const IgbThreadRuntimeInitCall&)> runtime_before;
    std::function<void(const IgbThreadRuntimeInitCall&)> runtime_after;
    std::function<std::optional<MessengerCallResult>(const IgbMemoryFieldConstructCall&)> memory_before;
    std::function<void(const IgbMemoryFieldConstructCall&)> memory_after;
    unsigned runtime_polls{},memory_polls{};
    Fixture(){
        services->registration=std::make_shared<IgbGamecubeThreadRegistrationServices>();
        services->registration->state=std::make_shared<IgbGamecubeThreadRegistrationState>();
        services->runtime=std::make_shared<IgbThreadRuntimeInitServices>();
        services->runtime->objects=std::make_shared<IgbObjectFieldConstructServices>();
        services->runtime->objects->objects.push_back(semaphore);
        services->memory=std::make_shared<IgbMemoryFieldConstructServices>();
        services->memory->status_success=0x11223344;services->memory->status_failure=0xaabbccdd;
        igb_bootstrap_write_byte(pointer(thread),4,0xa5); // Remaining reference bytes deliberately unknown.
        igb_bootstrap_write_word(pointer(thread),0x24,0x1800);
        igb_bootstrap_write_word(pointer(thread),0x380,igb_bootstrap_address(pointer(semaphore)));
        igb_bootstrap_write_word(pointer(semaphore),0x0c,0x55667788);igb_bootstrap_write_byte(pointer(semaphore),0x14,0);
        backing.address=0x800000;backing.bytes=bytes(0x1800,0xa7);
        const auto native=make_native_igb_gamecube_thread_runtime_factory(services);
        services->runtime->factory=[this,native](const IgbThreadRuntimeInitCall& call){
            runtime_calls.push_back(call);const auto selected=native(call);if(!selected)return selected;
            auto result=std::make_shared<IgbThreadRuntimeInitOperation>();
            result->resume_checked=[this,call,selected](const std::function<void()>& guard){++runtime_polls;
                if(runtime_before){const auto value=runtime_before(call);if(value)return *value;}
                const auto value=selected->resume_checked?selected->resume_checked(guard):selected->resume();
                if(runtime_after)runtime_after(call);return value;};
            result->resume=[out=result.get()]{return out->resume_checked({});};runtime_operations.push_back(result);return result;
        };
        services->memory->factory=[this](const IgbMemoryFieldConstructCall& call){
            memory_calls.push_back(call);auto result=std::make_shared<IgbMemoryFieldConstructOperation>();
            result->resume=[this,call,out=result.get()]{++memory_polls;if(memory_before){const auto value=memory_before(call);if(value)return *value;}
                if(call.kind==MK::tagged_pool)out->pool_result=pool;
                else if(call.kind==MK::allocate)out->backing_result=backing;
                else throw std::runtime_error("Unexpected prepared thread memory endpoint.");
                if(memory_after)memory_after(call);return MessengerCallResult{true,0xdeadbeef,{}};};
            memory_operations.push_back(result);return result;
        };
    }
};

void initial_count_branches_and_width(){
    for(const auto active:{0u,1u,0x80u,0xffu})for(const auto count:{0u,1u,0x80000000u,0xffffffffu}){
        Fixture f;igb_bootstrap_write_byte(pointer(f.semaphore),0x14,static_cast<std::uint8_t>(active));auto result=bytes(8,0xe1);
        const auto before=f.semaphore.storage->bytes->bytes,known=f.semaphore.storage->bytes->initialized;
        IgbSemaphoreInitialCountInvocation run(f.semaphore,count,result);
        check(run.resume(f.services).completed && run.completed() && !run.faulted(),"Initial-count setter did not finish its selected active branch.");
        check(word(f.semaphore,0x0c)==(active?0x55667788:count) && igb_bootstrap_read_byte(pointer(f.semaphore),0x14)==active,
            "Initial-count setter clamped count, used only part of active, or changed active state.");
        check(igb_buffer_word(result,0)==(active?0xaabbccdd:0x11223344) && result->bytes[4]==0xe1 && !result->initialized[4],
            "Initial-count setter selected the wrong full status or changed adjacent result bytes.");
        for(unsigned at=0;at<before.size();++at)if(active || at<f.semaphore.offset+0x0c || at>=f.semaphore.offset+0x10)
            check(f.semaphore.storage->bytes->bytes[at]==before[at] && f.semaphore.storage->bytes->initialized[at]==known[at],
                "Count setter changed queue, inline count, padding, header or allocation boundary.");
        f.services->memory->status_success=7;f.services->memory->status_failure=8;
        igb_bootstrap_write_word(pointer(f.semaphore),0x0c,0xabcdef01);
        check(run.resume().completed && word(f.semaphore,0x0c)==0xabcdef01 && igb_buffer_word(result,0)==(active?0xaabbccdd:0x11223344),
            "Completed initial-count setter replayed a count or fresh status load.");
        check(f.runtime_calls.empty() && f.memory_calls.empty(),"Simple count setter invoked allocation or runtime services.");
    }
}

void captured_active_fresh_status_and_late_result_faults(){
    for(bool active:{false,true}){
        Fixture f;igb_bootstrap_write_byte(pointer(f.semaphore),0x14,active?1:0);auto result=bytes(4);
        (active?f.services->memory->status_failure:f.services->memory->status_success).reset();
        IgbSemaphoreInitialCountInvocation run(f.semaphore,0x87654321,result);
        check(!run.resume(f.services).completed && !result->initialized[0] && word(f.semaphore,0x0c)==(active?0x55667788:0x87654321),
            "Unknown status fabricated success or delayed an already reached count store.");
        igb_bootstrap_write_byte(pointer(f.semaphore),0x14,active?0:1);igb_bootstrap_write_word(pointer(f.semaphore),0x0c,0x44444444);
        (active?f.services->memory->status_failure:f.services->memory->status_success)=0x8765abcd;
        check(run.resume().completed && word(f.semaphore,0x0c)==0x44444444 && igb_buffer_word(result,0)==0x8765abcd,
            "Pending status recaptured active, replayed count or missed the chosen fresh canonical status cell.");
    }
    for(bool active:{false,true})for(unsigned invalid=0;invalid<4;++invalid){
        Fixture f;igb_bootstrap_write_byte(pointer(f.semaphore),0x14,active?1:0);auto result=bytes(4,0xe7);const auto original=result;
        if(invalid==0)result.reset();if(invalid==1)result->bytes.resize(3);if(invalid==2)result->initialized.resize(3);if(invalid==3)result->released=true;
        const auto before=original->bytes,known=original->initialized;IgbSemaphoreInitialCountInvocation run(f.semaphore,0xfedcba98,result);
        rejects([&]{run.resume(f.services);},"Unavailable setter result destination was accepted.");
        check(run.faulted() && word(f.semaphore,0x0c)==(active?0x55667788:0xfedcba98) && original->bytes==before && original->initialized==known,
            "Result failure preceded the count branch or wrote a partial hidden status.");
        original->bytes.resize(4);original->initialized.resize(4);original->released=false;
        rejects([&]{run.resume(f.services);},"Failed count setter revived after result repair.");
    }
    Fixture unknown;unknown.semaphore.storage->bytes->initialized[unknown.semaphore.offset+0x14]=0;auto result=bytes(4);
    IgbSemaphoreInitialCountInvocation run(unknown.semaphore,0,result);rejects([&]{run.resume(unknown.services);},"Unknown active byte became inactive.");
    check(word(unknown.semaphore,0x0c)==0x55667788 && !result->initialized[0],"Unknown active read changed later count/status state.");
}

void count_store_boundary_and_borrowed_reentry(){
    for(unsigned boundary=0;boundary<2;++boundary){Fixture f;auto result=bytes(4);IgbSemaphoreInitialCountInvocation run(f.semaphore,0x77777777,result);bool injected{};
        const auto guard=[&]{const bool stored=word(f.semaphore,0x0c)==0x77777777;
            if((boundary==0 && !stored) || (boundary==1 && stored)){injected=true;try{run.resume(f.services);}catch(const std::runtime_error&){};}};
        rejects([&]{run.resume(f.services,guard);},"Caught recursive count setter escaped its enclosing guard.");
        check(injected && run.faulted() && word(f.semaphore,0x0c)==(boundary?0x77777777:0x55667788) && !result->initialized[0],
            "Caught recursive setter performed a subsequent count/status store or lost its preceding store.");
        rejects([&]{run.resume(f.services);},"Faulted setter replayed after removing its guard.");
    }
}

void thread_stack_then_semaphore_and_null_allocation(){
    for(bool null_stack:{false,true})for(bool active:{false,true}){
        Fixture f;if(null_stack)f.backing={};igb_bootstrap_write_byte(pointer(f.semaphore),0x14,active?1:0);
        const auto before=f.thread.storage->bytes->bytes,known=f.thread.storage->bytes->initialized;const auto stack=f.backing.bytes;
        f.runtime_before=[&](const IgbThreadRuntimeInitCall& call)->std::optional<MessengerCallResult>{if(call.virtual_slot==0x68){
            check(word(f.thread,0x20)==f.backing.address && call.argument==0 && call.object.runtime_object==f.semaphore.runtime_object &&
                call.result && call.result->bytes.size()==4 && call.result->initialized==std::vector<std::uint8_t>(4,0),
                "Semaphore call preceded stack publication or lost its explicit zero argument/unknown result.");}return {};};
        IgbGamecubeThreadInitInvocation run(f.thread);check(run.resume(f.services).completed && run.completed() && !run.faulted(),
            "Thread initialization rejected a null stack return or failed its actual nested count setter.");
        check(f.runtime_calls.size()==2 && f.runtime_calls[0].virtual_slot==0xa8 && f.runtime_calls[0].vtable_word==0x80470110 &&
            f.runtime_calls[1].virtual_slot==0x68 && f.memory_calls.size()==2 && f.memory_calls[0].kind==MK::tagged_pool &&
            f.memory_calls[0].handle==0xa5 && f.memory_calls[1].kind==MK::allocate && f.memory_calls[1].virtual_slot==0xdc &&
            f.memory_calls[1].bytes==0x1800 && f.memory_calls[1].alignment==32 && f.memory_calls[1].pool==f.pool,
            "Thread stack allocation changed the byte tag, exact +DC service, size or literal alignment.");
        check(word(f.semaphore,0x0c)==(active?0x55667788:0) && igb_bootstrap_read_byte(pointer(f.semaphore),0x14)==unsigned(active) &&
            igb_buffer_word(f.runtime_calls[1].result,0)==(active?0xaabbccdd:0x11223344),
            "Thread wrapper checked a status, activated its semaphore or omitted the count setter.");
        for(unsigned at=0;at<before.size();++at)if(at<f.thread.offset+0x20 || at>=f.thread.offset+0x24)
            check(f.thread.storage->bytes->bytes[at]==before[at] && f.thread.storage->bytes->initialized[at]==known[at],
                "Thread initialization changed unrelated storage or initialized unrepresented OS-thread bytes.");
        if(stack)check(stack->bytes==std::vector<std::uint8_t>(0x1800,0xa7) && stack->initialized==std::vector<std::uint8_t>(0x1800,0),
            "Thread stack allocation inferred zero initialization or read its unknown payload.");
        check(run.resume().completed && f.runtime_polls==2 && f.memory_polls==2,"Completed thread initialization replayed selected helpers.");
    }
}

void captured_alignment_fresh_size_and_pool_operands(){
    Fixture f;bool aligned{},pooled{},allocated{};
    f.runtime_before=[&](const IgbThreadRuntimeInitCall& call)->std::optional<MessengerCallResult>{if(call.virtual_slot==0xa8)
        return MessengerCallResult{aligned,0x98760010,"Prepared captured alignment."};return {};};
    f.memory_before=[&](const IgbMemoryFieldConstructCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==MK::tagged_pool && !pooled)return MessengerCallResult{false,0,"Prepared pool lookup."};
        if(call.kind==MK::allocate && !allocated)return MessengerCallResult{false,0,"Prepared aligned allocation."};return {};};
    IgbGamecubeThreadInitInvocation run(f.thread);check(!run.resume(f.services).completed && f.runtime_calls.size()==1 && f.memory_calls.empty(),
        "Pending alignment consumed the stack-size or tagged-pool operands early.");
    igb_bootstrap_write_word(pointer(f.thread),0,0x12345678);igb_bootstrap_write_word(pointer(f.thread),0x24,0x9876);
    igb_bootstrap_write_byte(pointer(f.thread),4,0x7f);aligned=true;
    check(!run.resume().completed && f.runtime_calls.size()==1 && f.memory_calls.size()==1 && f.memory_calls[0].handle==0x7f,
        "Resumed alignment reselected its captured vtable or missed the subsequent fresh byte tag.");
    igb_bootstrap_write_word(pointer(f.thread),0x24,0x4444);igb_bootstrap_write_byte(pointer(f.thread),4,3);pooled=true;
    check(!run.resume().completed && f.memory_calls.size()==2 && f.memory_calls[1].bytes==0x9876 && f.memory_calls[1].alignment==16 &&
        f.memory_calls[1].pool==f.pool && !word_known(f.thread,0x20),"Pending pool lookup recaptured size/alignment/tag or published stack early.");
    const auto selected_pool=f.pool;f.pool=std::make_shared<ModelSceneData>();allocated=true;
    check(run.resume().completed && f.memory_calls[1].pool==selected_pool && f.runtime_calls[1].vtable_word==0x80470314 &&
        f.memory_calls[1].object.runtime_object==f.thread.runtime_object,"Resumed allocation redirected its selected pool or semaphore vtable.");
}

void fresh_semaphore_then_captured_identity_and_continuation(){
    Fixture f;auto second=object(IgbRuntimeObjectKind::semaphore,0x80470314,0x300000,0x28);
    igb_bootstrap_write_byte(pointer(second),0x14,0);igb_bootstrap_write_word(pointer(second),0x0c,0x99887766);
    bool allocated{};f.memory_before=[&](const IgbMemoryFieldConstructCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==MK::allocate && !allocated)return MessengerCallResult{false,0,"Prepared allocation delay."};return {};};
    IgbGamecubeThreadInitInvocation run(f.thread);check(!run.resume(f.services).completed && !word_known(f.thread,0x20),"Pending allocation skipped stack publication ordering.");
    igb_bootstrap_write_word(pointer(f.thread),0x380,igb_bootstrap_address(pointer(second)));allocated=true;
    check(!run.resume().completed && word(f.thread,0x20)==0x800000 && f.runtime_calls.size()==1,
        "Missing fresh semaphore identity was fabricated or prevented the preceding stack store.");
    igb_bootstrap_write_word(pointer(f.thread),0x380,igb_bootstrap_address(pointer(f.semaphore)));
    f.services->runtime->objects->objects.push_back(second);f.services->memory->status_success.reset();
    check(!run.resume().completed && f.runtime_calls.size()==2 && f.runtime_calls[1].object.runtime_object==second.runtime_object &&
        word(second,0x0c)==0 && word(f.semaphore,0x0c)==0x55667788,"Late semaphore mapping recaptured +380 or lost the actual count-store prefix.");
    const auto result=f.runtime_calls[1].result;
    f.runtime_operations[1]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{throw std::runtime_error("Replacement must not run.");};
    f.services->runtime->factory={};f.services->memory->status_success=0xfedc1234;
    check(run.resume().completed && igb_buffer_word(result,0)==0xfedc1234 && f.runtime_polls==3 && f.memory_calls.size()==2,
        "Suspended semaphore setter lost its retained checked callback/result or replayed allocation.");
}

void late_factories_and_empty_callbacks(){
    Fixture f;const auto runtime=f.services->runtime->factory;const auto memory=f.services->memory->factory;f.services->runtime->factory={};
    IgbGamecubeThreadInitInvocation run(f.thread);check(!run.resume(f.services).completed && f.runtime_calls.empty(),"Absent alignment service became a literal fallback.");
    f.services->runtime->factory=runtime;f.services->memory->factory={};check(!run.resume().completed && f.runtime_calls.size()==1 && f.memory_calls.empty(),
        "Absent tagged pool service was inferred from another allocator.");
    f.services->memory->factory=memory;check(run.resume().completed,"Late original lower services did not resume the selected operation.");
    Fixture callback;auto delayed=std::make_shared<IgbThreadRuntimeInitOperation>();const auto eventual=callback.services->runtime->factory;
    callback.services->runtime->factory=[&](const IgbThreadRuntimeInitCall& call){return call.virtual_slot==0xa8?delayed:eventual(call);};
    IgbGamecubeThreadInitInvocation pending(callback.thread);check(!pending.resume(callback.services).completed && callback.memory_calls.empty(),
        "Missing selected alignment callback became completed alignment zero.");
    delayed->resume=[] {return MessengerCallResult{true,0x10000,{}};};
    check(pending.resume().completed && callback.memory_calls[1].alignment==0,"Late legacy alignment callback did not retain low16 zero semantics.");
}

void stack_and_semaphore_fault_prefixes(){
    Fixture unknown;unknown.thread.storage->bytes->initialized[unknown.thread.offset+0x380]=0;IgbGamecubeThreadInitInvocation run(unknown.thread);
    rejects([&]{run.resume(unknown.services);},"Unknown reached semaphore pointer was treated as null or pending.");
    check(run.faulted() && word(unknown.thread,0x20)==0x800000 && unknown.memory_calls.size()==2 && unknown.runtime_calls.size()==1,
        "Unknown semaphore-pointer fault lost the completed stack store or selected its setter.");
    unknown.thread.storage->bytes->initialized[unknown.thread.offset+0x380]=1;
    rejects([&]{run.resume(unknown.services);},"Failed semaphore read replayed after pointer repair.");
    for(unsigned invalid=0;invalid<4;++invalid){Fixture f;
        if(invalid==0)igb_bootstrap_write_word(pointer(f.thread),0x380,0);
        if(invalid==1)igb_bootstrap_write_word(pointer(f.semaphore),0,0);
        if(invalid==2)f.semaphore.storage->bytes->initialized[f.semaphore.offset]=0;
        if(invalid==3){auto conflict=f.semaphore;conflict.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::semaphore);
            f.services->runtime->objects->objects.push_back(conflict);}
        IgbGamecubeThreadInitInvocation bad(f.thread);rejects([&]{bad.resume(f.services);},"Invalid reached done-semaphore representation was accepted.");
        check(bad.faulted() && word(f.thread,0x20)==0x800000 && word(f.semaphore,0x0c)==0x55667788 && f.runtime_calls.size()==1,
            "Semaphore dispatch failure changed count or discarded the stack publication prefix.");
    }
    for(unsigned invalid=0;invalid<3;++invalid){Fixture f;
        if(invalid==0)f.pool.reset();if(invalid==1){f.backing.address=0;}if(invalid==2){f.backing.fields=std::make_shared<IgbDecodeFieldRegistry::Storage>();}
        IgbGamecubeThreadInitInvocation bad(f.thread);rejects([&]{bad.resume(f.services);},"Null pool or inconsistent allocation mapping was accepted.");
        check(bad.faulted() && !word_known(f.thread,0x20) && f.runtime_calls.size()==1,"Bad allocation published stack or reached the semaphore.");
    }
}

void callback_identity_and_reentry_faults(){
    for(const auto stage:{MK::tagged_pool,MK::allocate})for(unsigned mutation=0;mutation<5;++mutation){
        Fixture f;IgbGamecubeThreadInitInvocation run(f.thread);const auto owner=f.thread.storage->bytes;const auto bindings=f.thread.storage->bindings;
        const auto original_address=f.thread.storage->original_address;const auto physical=f.thread.runtime_object->physical_storage;unsigned caught{};
        f.memory_after=[&](const IgbMemoryFieldConstructCall& call){if(call.kind!=stage)return;
            if(mutation==0){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}}
            if(mutation==1)f.thread.storage->bytes=bytes(owner->bytes.size());
            if(mutation==2)f.thread.storage->original_address+=0x100;
            if(mutation==3)f.thread.runtime_object->physical_storage.reset();
            if(mutation==4)owner->released=true;
        };
        rejects([&]{run.resume(f.services);},"Caught reentry or changed thread physical identity reached stack publication.");
        check(run.faulted() && !owner->initialized[f.thread.offset+0x20] && f.runtime_calls.size()==1 && (mutation!=0 || caught==1),
            "Invalidating memory callback changed stack/semaphore prefix or missed recursion.");
        f.thread.storage->bytes=owner;owner->released=false;f.thread.storage->original_address=original_address;
        f.thread.runtime_object->physical_storage=physical;f.thread.storage->bindings=bindings;f.memory_after={};
        const auto polls=f.memory_polls;rejects([&]{run.resume(f.services);},"Faulted thread operation revived after physical identity repair.");
        check(f.memory_polls==polls,"Repaired thread identity replayed an allocation callback.");
    }
}

void nested_setter_guard_and_result_ownership(){
    for(unsigned mutation=0;mutation<4;++mutation){Fixture f;IgbGamecubeThreadInitInvocation run(f.thread);
        const auto owner=f.semaphore.storage->bytes;const auto binding=f.semaphore.runtime_object->physical_storage;unsigned caught{};
        f.runtime_before=[&](const IgbThreadRuntimeInitCall& call)->std::optional<MessengerCallResult>{if(call.virtual_slot==0x68){
            if(mutation==0){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}}
            if(mutation==1)f.semaphore.runtime_object->physical_storage.reset();
            if(mutation==2)f.semaphore.storage->bytes=bytes(owner->bytes.size());
            if(mutation==3)call.result->released=true;
        }return {};};
        rejects([&]{run.resume(f.services);},"Invalidated nested setter continued after its thread callback or result failure.");
        check(run.faulted() && word(f.thread,0x20)==0x800000 && f.runtime_calls.size()==2 && f.memory_calls.size()==2 &&
            igb_buffer_word(owner,f.semaphore.offset+0x0c)==(mutation==3?0:0x55667788) && !f.runtime_calls[1].result->initialized[0] &&
            (mutation!=0 || caught==1),"Nested setter failure lost stack/count prefix or wrote the hidden result after invalidation.");
        f.semaphore.storage->bytes=owner;f.semaphore.runtime_object->physical_storage=binding;f.runtime_before={};f.runtime_calls[1].result->released=false;
        const auto polls=f.runtime_polls;rejects([&]{run.resume(f.services);},"Invalidated nested setter resumed after physical/result repair.");
        check(f.runtime_polls==polls,"Nested setter repair replayed its captured runtime callback.");
    }
    Fixture active;igb_bootstrap_write_byte(pointer(active.semaphore),0x14,0x80);
    for(unsigned at=0;at<4;++at)active.semaphore.storage->bytes->initialized[active.semaphore.offset+0x0c+at]=0;
    auto result=bytes(4);IgbSemaphoreInitialCountInvocation ignored_count(active.semaphore,0xffffffff,result);
    check(ignored_count.resume(active.services).completed && !word_known(active.semaphore,0x0c) && igb_buffer_word(result,0)==0xaabbccdd,
        "Active setter read or initialized an unused count word.");
}

void exact_leaves_do_not_read_receivers(){
    Fixture f;auto sentinel=std::make_shared<IgbObjectInitOperation>();auto runtime_sentinel=std::make_shared<IgbThreadRuntimeInitOperation>();
    const auto factory=make_native_igb_gamecube_thread_init_factory(f.services,[&](const IgbObjectInitCall&){return sentinel;});
    const auto runtime=make_native_igb_gamecube_thread_runtime_factory(f.services,[&](const IgbThreadRuntimeInitCall&){return runtime_sentinel;});
    for(unsigned invalid=0;invalid<5;++invalid){IgbObjectInstance receiver;
        if(invalid){receiver=f.thread;if(invalid==1)receiver.storage->bytes.reset();
            if(invalid==2){receiver.storage=std::make_shared<IgbObjectStorage>();receiver.storage->bytes=bytes(1);}
            if(invalid==3)receiver.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::semaphore);
            if(invalid==4)receiver.metadata=std::make_shared<IgbRuntimeMetaType>();}
        IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::object_virtual;call.object=receiver;call.vtable_word=0x80470110;
        for(const auto slot:{0x0cu,0x24u,0x34u}){call.virtual_slot=slot;const auto op=factory(call);check(op && op!=sentinel && op->resume().completed,
            "Literal thread BLR read an unused null or conflicting receiver.");}
        call.virtual_slot=0x58;f.services->registration->state->metadata.reset();const auto getter=factory(call);
        check(getter && !getter->resume().completed,"Plain thread getter fabricated unknown canonical metadata.");
        f.services->registration->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
        check(getter->resume().completed && !getter->metadata_result,"Plain thread getter rejected known null or dereferenced the receiver.");
        auto metadata=std::make_shared<IgbRuntimeMetaType>();f.services->registration->state->metadata=metadata;const auto nonnull=factory(call);
        check(nonnull->resume().completed && nonnull->metadata_result==metadata,"Plain thread getter missed fresh canonical metadata identity.");
        IgbThreadRuntimeInitCall aligned;aligned.object=receiver;aligned.vtable_word=0x80470110;aligned.virtual_slot=0xa8;
        check(runtime(aligned)->resume().value==32,"Literal thread alignment read an unused receiver.");
    }
    IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::object_virtual;call.vtable_word=0x80470110;
    for(const auto slot:{0xdcu,0xa8u,0x60u,0x5cu,0x68u,0x7cu}){call.virtual_slot=slot;check(factory(call)==sentinel,"Thread adapter admitted unrelated OS adoption or runtime work.");}
    call.virtual_slot=0x58;call.kind=IgbObjectInitServiceKind::field_virtual;check(factory(call)==sentinel,"Thread adapter consumed a field service.");
    call.kind=IgbObjectInitServiceKind::object_virtual;call.vtable_word=0x80470314;check(factory(call)==sentinel,"Thread adapter claimed semaphore metadata.");
    IgbThreadRuntimeInitCall other;other.vtable_word=0x80470314;
    for(const auto slot:{0x5cu,0x7cu,0x8cu,0xa8u}){other.virtual_slot=slot;check(runtime(other)==runtime_sentinel,"Count adapter admitted activation, wait, signal or unrelated alignment.");}
    other.vtable_word=0x80470110;other.virtual_slot=0xdc;check(runtime(other)==runtime_sentinel,"Runtime adapter claimed current-thread adoption.");
    check(f.runtime_calls.empty() && f.memory_calls.empty(),"Literal leaf invoked thread or memory services.");
}

void real_object_initializer_borrows_guard(){
    for(const auto stage:{MK::tagged_pool,MK::allocate}){
        Fixture f;auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->type_word=0x80470110;metadata->allocation_prefix=0;
        metadata->class_flags=4;metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();metadata->instance_count=0;
        f.services->registration->state->metadata=metadata;
        auto init=std::make_shared<IgbObjectInitServices>();init->runtime=std::make_shared<IgbRuntimeTypesServices>();
        auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->vtable_offset=0;init->runtime->singleton=core;init->object_metadata=metadata;
        const auto native=make_native_igb_gamecube_thread_init_factory(f.services);
        init->factory=[native](const IgbObjectInitCall& call){if(call.kind==IgbObjectInitServiceKind::allocation_pool){
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;}return native(call);};
        IgbObjectInitInvocation run(f.thread.storage,f.thread.offset,metadata);unsigned caught{};
        f.memory_after=[&](const IgbMemoryFieldConstructCall& call){if(call.kind==stage){try{run.resume(init);}catch(const std::runtime_error&){++caught;}}};
        rejects([&]{run.resume(init);},"Caught recursive real object initialization did not invalidate its nested thread initializer.");
        check(caught==1 && run.faulted() && f.thread.storage->faulted() && f.thread.runtime_object->construction_fault_ &&
            !word_known(f.thread,0x20) && word(f.semaphore,0x0c)==0x55667788 && f.runtime_calls.size()==1,
            "Nested thread initializer performed stack or semaphore stores after its actual parent faulted.");
        const auto polls=f.memory_polls;rejects([&]{run.resume(init);},"Faulted outer initializer replayed the nested callback.");
        check(f.memory_polls==polls,"Failed real object initialization repeated its prepared pool endpoint.");
    }
}
}
int main(){try{
    initial_count_branches_and_width();captured_active_fresh_status_and_late_result_faults();count_store_boundary_and_borrowed_reentry();
    thread_stack_then_semaphore_and_null_allocation();captured_alignment_fresh_size_and_pool_operands();
    fresh_semaphore_then_captured_identity_and_continuation();late_factories_and_empty_callbacks();stack_and_semaphore_fault_prefixes();
    callback_identity_and_reentry_faults();nested_setter_guard_and_result_ownership();exact_leaves_do_not_read_receivers();real_object_initializer_borrows_guard();
    std::cout<<"igb_gamecube_thread_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"GameCube thread initialization tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
