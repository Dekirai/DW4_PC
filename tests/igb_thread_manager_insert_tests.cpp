#include "dw4/igb_thread_manager_insert.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Sleep, wake and data-list growth are explicit prepared lower operations.
// Their completion in a fixture does not claim native scheduling or growth.
namespace {
using namespace dw4;
using K=IgbThreadManagerInsertServiceKind;
using IK=IgbInterruptServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t fill=0xcd,std::uint8_t known=0){auto value=std::make_shared<IgbScratchBuffer>();
    value->bytes.assign(size,fill);value->initialized.assign(size,known);return value;}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address;storage->bytes=bytes(size+0x10);return {storage,0x10};}
IgbObjectInstance object(IgbRuntimeObjectKind kind,std::uint32_t table,std::uint32_t address,unsigned size){
    const auto allocation=raw(address,size);IgbObjectInstance value;value.storage=allocation.storage;value.offset=allocation.offset;
    value.runtime_object=std::make_shared<IgbRuntimeObject>(kind);value.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{value.storage,value.offset};
    IgbObjectBinding binding;binding.offset=value.offset;binding.runtime_object=value.runtime_object;value.storage->bindings.push_back(binding);
    igb_bootstrap_write_word(pointer(value),0,table);igb_object_set_reference_word(value,0xa5000002);return value;
}
std::uint32_t word(const IgbObjectInstance& value,unsigned offset){return igb_bootstrap_read_word(pointer(value),offset);}
void word(const IgbObjectInstance& value,unsigned offset,std::uint32_t data){igb_bootstrap_write_word(pointer(value),offset,data);}
void active(const IgbObjectInstance& value,std::uint8_t data){igb_bootstrap_write_byte(pointer(value),0x14,data);}
bool known(IgbBootstrapPointer value,unsigned offset,unsigned size=4){const auto b=value.storage->bytes;const auto at=value.offset+offset;
    return at+size<=b->initialized.size() && std::all_of(b->initialized.begin()+at,b->initialized.begin()+at+size,[](auto v){return v!=0;});}
void bind_backing(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,IgbBootstrapPointer value){
    IgbPointerListBacking backing;backing.bytes=value.storage->bytes;backing.address=igb_bootstrap_address(value);backing.offset=value.offset;
    igb_bind_memory_field_allocation(services,std::move(backing));
}
struct Fixture {
    std::shared_ptr<IgbThreadManagerInsertServices> services=std::make_shared<IgbThreadManagerInsertServices>();
    std::shared_ptr<IgbExecutionInterruptState> interrupts=std::make_shared<IgbExecutionInterruptState>();
    IgbObjectInstance manager=object(IgbRuntimeObjectKind::thread_manager,0x804700a8,0x100000,0x10);
    IgbObjectInstance thread=object(IgbRuntimeObjectKind::thread,0x80470110,0x200000,0x388);
    IgbObjectInstance semaphore=object(IgbRuntimeObjectKind::semaphore,0x80470314,0x300000,0x28);
    IgbObjectInstance list=object(IgbRuntimeObjectKind::thread_list,0x80476ce0,0x400000,0x14);
    IgbBootstrapPointer backing=raw(0x900000,32);
    std::vector<IgbInterruptCall> interrupt_calls;
    std::vector<IgbThreadRuntimeInitCall> runtime_calls;
    std::vector<IgbThreadManagerInsertCall> lower_calls;
    std::vector<std::shared_ptr<IgbInterruptOperation>> interrupt_operations;
    std::vector<std::shared_ptr<IgbThreadRuntimeInitOperation>> runtime_operations;
    std::vector<std::shared_ptr<IgbThreadManagerInsertOperation>> lower_operations;
    std::function<std::optional<MessengerCallResult>(const IgbInterruptCall&)> interrupt_before;
    std::function<void(const IgbInterruptCall&)> interrupt_after;
    std::function<std::optional<MessengerCallResult>(const IgbThreadRuntimeInitCall&)> runtime_before;
    std::function<void(const IgbThreadRuntimeInitCall&)> runtime_after;
    std::function<MessengerCallResult(const IgbThreadManagerInsertCall&,const std::function<void()>&)> lower_action;
    unsigned interrupt_polls{},runtime_polls{},lower_polls{};
    Fixture(){
        services->runtime=std::make_shared<IgbThreadRuntimeInitServices>();services->runtime->objects=std::make_shared<IgbObjectFieldConstructServices>();
        services->runtime->objects->objects={manager,thread,semaphore,list};services->memory=std::make_shared<IgbMemoryFieldConstructServices>();
        services->memory->status_success=0x11223344;services->memory->status_failure=0xaabbccdd;interrupts->enabled=true;
        word(manager,8,igb_bootstrap_address(pointer(list)));word(manager,0x0c,igb_bootstrap_address(pointer(semaphore)));
        word(list,8,0);word(list,0x0c,4);word(list,0x10,igb_bootstrap_address(backing));bind_backing(services->memory,backing);
        active(semaphore,1);word(semaphore,0x18,1);word(semaphore,0x10,1);
        const auto irq=make_native_igb_interrupt_factory(interrupts);
        services->interrupts=[this,irq](const IgbInterruptCall& call){interrupt_calls.push_back(call);const auto native=irq(call);
            auto out=std::make_shared<IgbInterruptOperation>();out->resume=[this,call,native]{++interrupt_polls;
                if(interrupt_before){const auto state=interrupt_before(call);if(state)return *state;}
                const auto state=native->resume();if(interrupt_after)interrupt_after(call);return state;};interrupt_operations.push_back(out);return out;};
        const auto access=make_native_igb_semaphore_runtime_access_factory(services);
        services->runtime->factory=[this,access](const IgbThreadRuntimeInitCall& call){runtime_calls.push_back(call);const auto native=access(call);
            if(!native)return native;auto out=std::make_shared<IgbThreadRuntimeInitOperation>();
            out->resume_checked=[this,call,native](const std::function<void()>& guard){++runtime_polls;
                if(runtime_before){const auto state=runtime_before(call);if(state)return *state;}
                const auto state=native->resume_checked?native->resume_checked(guard):native->resume();if(runtime_after)runtime_after(call);return state;};
            out->resume=[result=out.get()]{return result->resume_checked({});};runtime_operations.push_back(out);return out;};
        services->factory=[this](const IgbThreadManagerInsertCall& call){lower_calls.push_back(call);auto out=std::make_shared<IgbThreadManagerInsertOperation>();
            out->resume_checked=[this,call](const std::function<void()>& guard){++lower_polls;if(lower_action)return lower_action(call,guard);
                if(call.kind==K::wake_thread)return MessengerCallResult{true,0,{}};
                return MessengerCallResult{false,0,"Explicit unimplemented fixture sleep/growth."};};
            out->resume=[result=out.get()]{return result->resume_checked({});};lower_operations.push_back(out);return out;};
    }
};

void inactive_access_never_reads_unused_count(){
    for(const auto slot:{0x8cu,0x7cu}){Fixture f;active(f.semaphore,0);for(unsigned i=0;i<4;++i){
        f.semaphore.storage->bytes->initialized[f.semaphore.offset+0x10+i]=0;f.semaphore.storage->bytes->initialized[f.semaphore.offset+0x18+i]=0;}
        f.services->memory->status_failure.reset();auto result=bytes(8,0xe7);IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,slot,result);
        check(!run.resume(f.services).completed && f.interrupt_calls.empty() && f.lower_calls.empty() && !result->initialized[0],
            "Inactive semaphore accessed count/max/queue or invented a failure status.");
        active(f.semaphore,1);f.services->memory->status_failure=0xfedcba98;
        check(run.resume().completed && igb_buffer_word(result,0)==0xfedcba98 && result->bytes[4]==0xe7 && !result->initialized[4] &&
            f.interrupt_calls.empty(),"Pending inactive status rechecked active or changed adjacent hidden-result storage.");
    }
}

void positive_wait_and_signed_sleep(){
    for(bool enabled:{false,true})for(const auto count:{1u,2u,0x7fffffffu}){
        Fixture f;f.interrupts->enabled=enabled;word(f.semaphore,0x18,count);auto result=bytes(4);
        IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x8c,result);
        check(run.resume(f.services).completed && word(f.semaphore,0x18)==count-1 && f.interrupts->enabled==enabled &&
            f.interrupt_calls.size()==2 && f.interrupt_calls[0].kind==IK::disable && f.interrupt_calls[1].kind==IK::restore &&
            f.interrupt_calls[1].restore_enabled==unsigned(enabled) && f.lower_calls.empty() && igb_buffer_word(result,0)==0x11223344,
            "Positive wait changed the signed count/decrement or failed to restore the captured exclusion bit.");
        word(f.semaphore,0x18,0x55555555);check(run.resume().completed && word(f.semaphore,0x18)==0x55555555 && f.interrupt_polls==2,
            "Completed wait replayed count or exclusion transitions.");
    }
    for(const auto count:{0u,0x80000000u,0xffffffffu}){
        Fixture f;word(f.semaphore,0x18,count);auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x8c,result);
        check(!run.resume(f.services).completed && word(f.semaphore,0x18)==count && f.interrupts->enabled==false && f.interrupt_calls.size()==1 &&
            f.lower_calls.size()==1 && f.lower_calls[0].kind==K::sleep_thread &&
            igb_bootstrap_address(f.lower_calls[0].queue)==igb_bootstrap_address(pointer(f.semaphore))+0x1c && !result->initialized[0],
            "Nonpositive signed wait count skipped explicit sleep or restored exclusion while scheduling was pending.");
    }
}

void sleep_reload_and_retained_continuation(){
    Fixture f;word(f.semaphore,0x18,0);auto result=bytes(4);bool ready{};unsigned completed_sleeps{};
    f.lower_action=[&](const IgbThreadManagerInsertCall& call,const std::function<void()>&){check(call.kind==K::sleep_thread,"Wait selected wake or growth.");
        if(!ready)return MessengerCallResult{false,0,"Prepared sleep suspension."};++completed_sleeps;
        word(f.semaphore,0x18,completed_sleeps==1?0:3);return MessengerCallResult{true,0,{}};};
    IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x8c,result);check(!run.resume(f.services).completed && f.lower_polls==1,"Sleep fixture did not suspend.");
    f.lower_operations[0]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{throw std::runtime_error("Replacement sleep must not run.");};
    ready=true;active(f.semaphore,0);f.services->memory->status_success=0x76543210;
    check(run.resume().completed && completed_sleeps==2 && f.lower_calls.size()==2 && f.lower_polls==3 && word(f.semaphore,0x18)==2 &&
        f.interrupts->enabled==true && igb_buffer_word(result,0)==0x76543210,
        "Resumed sleep lost its captured continuation/active branch or omitted the fresh count loop.");
}

void wait_separate_positive_test_and_decrement_read(){
    Fixture baseline;auto baseline_result=bytes(4);unsigned boundaries{};IgbSemaphoreRuntimeAccessInvocation trace(baseline.semaphore,0x8c,baseline_result);
    check(trace.resume(baseline.services,[&]{++boundaries;}).completed,"Wait guard baseline did not complete.");bool saw_wrap{},saw_sleep{};
    for(unsigned target=1;target<=boundaries;++target){Fixture f;auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x8c,result);
        unsigned visited{};bool changed{};const auto guard=[&]{if(++visited==target && f.interrupts->enabled==false && word(f.semaphore,0x18)==1){
            word(f.semaphore,0x18,0);changed=true;}};
        const auto state=run.resume(f.services,guard);if(changed && state.completed && word(f.semaphore,0x18)==0xffffffff)saw_wrap=true;
        if(changed && !state.completed && !f.lower_calls.empty() && f.lower_calls.back().kind==K::sleep_thread)saw_sleep=true;
    }
    check(saw_wrap && saw_sleep,"Wait collapsed its signed positive-test read and later decrement operand read.");
}

void signal_unsigned_maximum_and_fresh_os_count(){
    struct Case{std::uint32_t count,maximum;bool accepted;};
    for(const auto c:std::array<Case,7>{{{0,0,false},{1,1,false},{2,1,false},{0x80000000,0x7fffffff,false},
        {0x7fffffff,0x80000000,true},{0xfffffffe,0xffffffff,true},{0,1,true}}}){
        Fixture f;word(f.semaphore,0x18,c.count);word(f.semaphore,0x10,c.maximum);auto result=bytes(4);
        IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x7c,result);check(run.resume(f.services).completed &&
            word(f.semaphore,0x18)==(c.accepted?c.count+1:c.count) && igb_buffer_word(result,0)==(c.accepted?0x11223344:0xaabbccdd) &&
            f.interrupt_calls.size()==(c.accepted?2u:0u) && f.lower_calls.size()==(c.accepted?1u:0u),
            "Signal used signed/clamped maximum semantics or entered OS signaling on the failure branch.");
    }
    Fixture f;word(f.semaphore,0x18,0);auto result=bytes(4);
    f.interrupt_after=[&](const IgbInterruptCall& call){if(call.kind==IK::disable)word(f.semaphore,0x18,0xffffffff);};
    IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x7c,result);
    check(run.resume(f.services).completed && word(f.semaphore,0x18)==0 && igb_buffer_word(result,0)==0x11223344,
        "Signal reused its pre-disable limit-check operand rather than reading the actual OS increment value.");
}

void wake_then_restore_then_status(){
    for(bool enabled:{false,true}){Fixture f;f.interrupts->enabled=enabled;word(f.semaphore,0x18,0);auto result=bytes(4);bool ready{};
        f.lower_action=[&](const IgbThreadManagerInsertCall& call,const std::function<void()>&){check(call.kind==K::wake_thread &&
            igb_bootstrap_address(call.queue)==igb_bootstrap_address(pointer(f.semaphore))+0x1c && word(f.semaphore,0x18)==1 && f.interrupts->enabled==false,
            "Wake selection preceded count publication or used a different queue/exclusion state.");return MessengerCallResult{ready,0,"Prepared wake suspension."};};
        IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x7c,result);check(!run.resume(f.services).completed && f.interrupt_calls.size()==1 &&
            word(f.semaphore,0x18)==1 && !result->initialized[0],"Pending wake restored exclusion or published success too soon.");
        ready=true;active(f.semaphore,0);word(f.semaphore,0x10,0);f.services->memory->status_success.reset();
        check(!run.resume().completed && f.interrupts->enabled==enabled && f.interrupt_calls.size()==2 && f.lower_polls==2 && !result->initialized[0],
            "Completed wake rechecked active/maximum, replayed count or failed to restore before the fresh status load.");
        word(f.semaphore,0x18,0x55555555);f.services->memory->status_success=0x87654321;
        check(run.resume().completed && word(f.semaphore,0x18)==0x55555555 && igb_buffer_word(result,0)==0x87654321 && f.lower_polls==2,
            "Late signal status replayed wake/count or missed the canonical status cell.");
    }
}

void semaphore_fault_prefix_and_identity(){
    for(const auto slot:{0x8cu,0x7cu})for(unsigned failure=0;failure<4;++failure){Fixture f;word(f.semaphore,0x18,slot==0x8c?1:0);auto result=bytes(4,0xe7);
        if(failure==0)f.interrupt_before=[](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{if(call.kind==IK::restore)
            throw std::runtime_error("Prepared restore failure.");return {};};
        if(failure==1)result->released=true;if(failure==2)result->bytes.resize(3);if(failure==3)result->initialized.resize(3);
        const auto before=result->bytes,mask=result->initialized;IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,slot,result);
        rejects([&]{run.resume(f.services);},"Failed restore or hidden-result store was accepted.");
        check(run.faulted() && word(f.semaphore,0x18)==(slot==0x8c?0:1) && f.interrupts->enabled==(failure!=0) &&
            result->bytes==before && result->initialized==mask,"Semaphore fault lost count/exclusion prefix or wrote a partial hidden result.");
        result->released=false;result->bytes.resize(4);result->initialized.resize(4);f.interrupt_before={};
        const auto polls=f.interrupt_polls;rejects([&]{run.resume();},"Faulted semaphore access revived after result/restore repair.");
        check(f.interrupt_polls==polls,"Failed access replayed an interrupt operation.");
    }
    for(const auto slot:{0x8cu,0x7cu}){Fixture f;word(f.semaphore,0x18,0);auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,slot,result);unsigned caught{};
        f.lower_action=[&](const IgbThreadManagerInsertCall&,const std::function<void()>&){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}
            return MessengerCallResult{true,0,{}};};
        rejects([&]{run.resume(f.services);},"Caught scheduling callback reentry reached later semaphore operations.");
        check(run.faulted() && caught==1 && word(f.semaphore,0x18)==(slot==0x8c?0:1) && f.interrupts->enabled==false &&
            f.interrupt_calls.size()==1 && !result->initialized[0],"Caught scheduling reentry restored exclusion or changed count/status prefix.");
    }
}

void unknown_state_and_late_scheduler_callback(){
    Fixture f;f.interrupts->enabled.reset();auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(f.semaphore,0x8c,result);
    check(!run.resume(f.services).completed && word(f.semaphore,0x18)==1 && !f.interrupts->enabled && f.lower_calls.empty(),
        "Unknown exclusion state was assumed enabled/disabled or allowed count mutation.");
    f.interrupts->enabled=false;check(run.resume().completed && f.interrupts->enabled==false && word(f.semaphore,0x18)==0,
        "Late known-false exclusion state did not preserve the selected disable/restore semantics.");
    Fixture callback;word(callback.semaphore,0x18,0);auto lower=std::make_shared<IgbThreadManagerInsertOperation>();
    callback.services->factory=[lower](const IgbThreadManagerInsertCall&){return lower;};auto output=bytes(4);
    IgbSemaphoreRuntimeAccessInvocation waiting(callback.semaphore,0x7c,output);
    check(!waiting.resume(callback.services).completed && word(callback.semaphore,0x18)==1 && callback.interrupts->enabled==false &&
        !output->initialized[0],"Absent retained wake callback became successful scheduling.");
    lower->resume=[] {return MessengerCallResult{true,0,{}};};callback.services->factory={};
    check(waiting.resume().completed && callback.interrupts->enabled==true && igb_buffer_word(output,0)==0x11223344,
        "Late legacy wake continuation was discarded after factory replacement.");
    for(const auto slot:{0x8cu,0x7cu})for(bool unknown_active:{false,true}){
        Fixture bad;bad.semaphore.storage->bytes->initialized[bad.semaphore.offset+(unknown_active?0x14:0x18)]=0;auto out=bytes(4);
        IgbSemaphoreRuntimeAccessInvocation failure(bad.semaphore,slot,out);rejects([&]{failure.resume(bad.services);},
            "Unknown reached active/count byte became zero or a pending scheduler operation.");
        check(failure.faulted() && bad.interrupt_calls.size()==(!unknown_active && slot==0x8c?1u:0u) &&
            bad.interrupts->enabled==(unknown_active || slot==0x7c) && !out->initialized[0] && bad.lower_calls.empty(),
            "Unknown semaphore operand fault violated active/count/exclusion access order.");
    }
}

void actual_manager_wait_append_signal(){
    Fixture f;const auto reference=word(f.thread,4);IgbThreadManagerInsertInvocation run(pointer(f.manager),pointer(f.thread));
    check(run.resume(f.services).completed && run.completed() && !run.faulted() && word(f.thread,4)==reference+1 && word(f.list,8)==1 &&
        igb_bootstrap_read_word(f.backing,0)==igb_bootstrap_address(pointer(f.thread)) && word(f.semaphore,0x18)==1,
        "Manager failed its actual wait/count/element/signal composition or changed reference transfer.");
    check(f.runtime_calls.size()==2 && f.runtime_calls[0].virtual_slot==0x8c && f.runtime_calls[1].virtual_slot==0x7c &&
        f.runtime_calls[0].result!=f.runtime_calls[1].result && igb_buffer_word(f.runtime_calls[0].result,0)==0x11223344 &&
        igb_buffer_word(f.runtime_calls[1].result,0)==0x11223344 && f.lower_calls.size()==1 && f.lower_calls[0].kind==K::wake_thread,
        "Manager merged its hidden results or skipped the distinct semaphore virtuals.");
    const auto polls=f.runtime_polls;word(f.thread,4,0x65432100);
    check(run.resume().completed && word(f.thread,4)==0x65432100 && word(f.list,8)==1 && f.runtime_polls==polls,
        "Completed manager insertion replayed retain/list/semaphore effects.");
}

void manager_retain_before_invalid_manager_and_ignored_status(){
    for(const auto reference:{0u,0x007fffffu,0xa5ffffffu,0xffffffffu}){Fixture f;word(f.thread,4,reference);
        IgbThreadManagerInsertInvocation run({},pointer(f.thread));rejects([&]{run.resume(f.services);},"Null manager bypassed its reached semaphore member read.");
        check(run.faulted() && word(f.thread,4)==reference+1 && f.runtime_calls.empty(),
            "Manager access fault occurred before the full wrapping incoming-thread retain.");
        rejects([&]{run.resume();},"Failed manager insertion replayed its retain.");check(word(f.thread,4)==reference+1,"Faulted retain was applied twice.");
    }
    Fixture inactive;active(inactive.semaphore,0);IgbThreadManagerInsertInvocation ignored(pointer(inactive.manager),pointer(inactive.thread));
    check(ignored.resume(inactive.services).completed && word(inactive.list,8)==1 &&
        igb_buffer_word(inactive.runtime_calls[0].result,0)==0xaabbccdd && igb_buffer_word(inactive.runtime_calls[1].result,0)==0xaabbccdd &&
        inactive.interrupt_calls.empty(),"Manager treated a hidden failure status as a branch or skipped insertion/signal.");
}

void fresh_list_and_signal_after_captured_wait(){
    Fixture f;auto second_list=object(IgbRuntimeObjectKind::thread_list,0x80476ce0,0x500000,0x14);
    auto second_semaphore=object(IgbRuntimeObjectKind::semaphore,0x80470314,0x600000,0x28);auto second_backing=raw(0xa00000,16);
    word(second_list,8,1);word(second_list,0x0c,4);word(second_list,0x10,igb_bootstrap_address(second_backing));
    active(second_semaphore,0);f.services->runtime->objects->objects.push_back(second_list);f.services->runtime->objects->objects.push_back(second_semaphore);
    bind_backing(f.services->memory,second_backing);bool ready{};f.runtime_before=[&](const IgbThreadRuntimeInitCall& call)->std::optional<MessengerCallResult>{
        if(call.virtual_slot==0x8c && !ready)return MessengerCallResult{false,0,"Prepared wait suspension."};return {};};
    IgbThreadManagerInsertInvocation run(pointer(f.manager),pointer(f.thread));check(!run.resume(f.services).completed && word(f.thread,4)==0xa5000003 &&
        word(f.list,8)==0 && f.runtime_calls.size()==1,"Pending wait lost its retain prefix or started appending early.");
    word(f.manager,8,igb_bootstrap_address(pointer(second_list)));word(f.manager,0x0c,igb_bootstrap_address(pointer(second_semaphore)));ready=true;
    f.runtime_operations[0]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{throw std::runtime_error("Replacement wait must not run.");};
    check(run.resume().completed && word(f.list,8)==0 && word(second_list,8)==2 &&
        igb_bootstrap_read_word(second_backing,4)==igb_bootstrap_address(pointer(f.thread)) && word(f.semaphore,0x18)==0 &&
        f.runtime_calls[0].object.runtime_object==f.semaphore.runtime_object && f.runtime_calls[1].object.runtime_object==second_semaphore.runtime_object,
        "Resumed wait lost its receiver/callback or missed fresh manager list/signal semaphore reads.");
}

void growth_capture_and_count_before_element_prefix(){
    Fixture f;word(f.list,8,4);word(f.list,0x0c,4);auto grown=raw(0xa00000,32);bind_backing(f.services->memory,grown);bool ready{};
    f.lower_action=[&](const IgbThreadManagerInsertCall& call,const std::function<void()>&){
        if(call.kind==K::grow_list){check(call.object.runtime_object==f.list.runtime_object && call.count==5 && call.element_bytes==4,
            "Growth call lost captured list/old count or used another list-layout helper.");if(!ready)return MessengerCallResult{false,0,"Prepared growth suspension."};
            word(f.list,8,call.count);word(f.list,0x0c,8);word(f.list,0x10,igb_bootstrap_address(grown));}return MessengerCallResult{true,0,{}};};
    IgbThreadManagerInsertInvocation run(pointer(f.manager),pointer(f.thread));check(!run.resume(f.services).completed && word(f.list,8)==4 &&
        word(f.thread,4)==0xa5000003 && word(f.semaphore,0x18)==0 && f.runtime_calls.size()==1,"Pending growth changed count/element/signal prefix.");
    word(f.manager,8,0);word(f.list,0x10,0);ready=true;
    check(run.resume().completed && word(f.list,8)==5 && igb_bootstrap_read_word(grown,16)==igb_bootstrap_address(pointer(f.thread)) &&
        !known(f.backing,16),"Resumed growth recaptured manager list or used stale backing/count for the element store.");
    Fixture missing;missing.services->memory->allocations.clear();IgbThreadManagerInsertInvocation pending(pointer(missing.manager),pointer(missing.thread));
    check(!pending.resume(missing.services).completed && word(missing.list,8)==1 && missing.runtime_calls.size()==1 && word(missing.semaphore,0x18)==0,
        "Missing backing mapping was invented or lost the prior count increment.");
    word(missing.list,8,7);bind_backing(missing.services->memory,missing.backing);
    check(pending.resume().completed && word(missing.list,8)==7 && igb_bootstrap_read_word(missing.backing,0)==igb_bootstrap_address(pointer(missing.thread)),
        "Late backing replayed count increment or recaptured old element index.");
    Fixture bounded;bounded.backing.storage->bytes->bytes.resize(bounded.backing.offset+3);bounded.backing.storage->bytes->initialized.resize(bounded.backing.offset+3);
    IgbThreadManagerInsertInvocation fault(pointer(bounded.manager),pointer(bounded.thread));rejects([&]{fault.resume(bounded.services);},
        "Truncated list element destination accepted a partial pointer store.");
    check(fault.faulted() && word(bounded.list,8)==1 && bounded.runtime_calls.size()==1 && !known(bounded.backing,0,3),
        "Element-store fault rolled back count, wrote partial pointer or signaled after failure.");
}

void signed_list_count_and_wrapping_index(){
    Fixture f;word(f.list,8,0xffffffff);word(f.list,0x0c,0);word(f.list,0x10,igb_bootstrap_address(f.backing)+4);
    IgbThreadManagerInsertInvocation run(pointer(f.manager),pointer(f.thread));check(run.resume(f.services).completed && word(f.list,8)==0 &&
        igb_bootstrap_read_word(f.backing,0)==igb_bootstrap_address(pointer(f.thread)) &&
        std::none_of(f.lower_calls.begin(),f.lower_calls.end(),[](const auto& call){return call.kind==K::grow_list;}),
        "Signed negative count was clamped/treated unsigned or lost modulo32 element-address arithmetic.");
    Fixture prefix;word(prefix.list,8,0xffffffff);word(prefix.list,0x0c,0);
    IgbThreadManagerInsertInvocation before_mapping(pointer(prefix.manager),pointer(prefix.thread));
    check(before_mapping.resume(prefix.services).completed && word(prefix.list,8)==0 &&
        igb_buffer_word(prefix.backing.storage->bytes,prefix.backing.offset-4)==igb_bootstrap_address(pointer(prefix.thread)) &&
        !known(prefix.backing,0) && std::none_of(prefix.lower_calls.begin(),prefix.lower_calls.end(),
            [](const auto& call){return call.kind==K::grow_list;}),
        "Negative list index lost the represented physical prefix before the canonical allocation pointer.");
    Fixture grow;word(grow.list,8,0x7fffffff);word(grow.list,0x0c,0x7fffffff);
    IgbThreadManagerInsertInvocation pending(pointer(grow.manager),pointer(grow.thread));check(!pending.resume(grow.services).completed &&
        grow.lower_calls.size()==1 && grow.lower_calls[0].kind==K::grow_list && grow.lower_calls[0].count==0x80000000,
        "Growth did not capture wrapping requested count after signed capacity comparison.");
}

void manager_reentry_stops_nested_operations(){
    for(unsigned phase=0;phase<3;++phase){Fixture f;if(phase==0)word(f.semaphore,0x18,0);if(phase==1){word(f.list,8,4);word(f.list,0x0c,4);}
        IgbThreadManagerInsertInvocation run(pointer(f.manager),pointer(f.thread));unsigned caught{};
        f.lower_action=[&](const IgbThreadManagerInsertCall& call,const std::function<void()>&){const auto expected=phase==0?K::sleep_thread:phase==1?K::grow_list:K::wake_thread;
            check(call.kind==expected,"Reentry fixture reached the wrong nested lower endpoint.");try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}
            return MessengerCallResult{true,0,{}};};
        rejects([&]{run.resume(f.services);},"Caught manager reentry let a selected semaphore/growth child continue.");
        check(run.faulted() && caught==1 && word(f.thread,4)==0xa5000003 && word(f.list,8)==(phase==0?0u:phase==1?4u:1u) &&
            word(f.semaphore,0x18)==(phase==2?1u:0u) && f.interrupts->enabled==(phase==1) && f.runtime_calls.size()==(phase==2?2u:1u),
            "Caught manager reentry changed subsequent count/list/restore effects or lost its original prefix.");
        if(phase==2)check(igb_bootstrap_read_word(f.backing,0)==igb_bootstrap_address(pointer(f.thread)),"Signal fault lost the completed list element store.");
        else check(!known(f.backing,0),"Wait/growth fault still published a list element.");
        const auto polls=f.lower_polls;rejects([&]{run.resume();},"Faulted manager resumed after nested callback removal.");check(f.lower_polls==polls,"Faulted manager replayed its lower endpoint.");
    }
}

void actual_startup_parent_guard(){
    Fixture f;auto startup=std::make_shared<IgbThreadStartupServices>();startup->pool_handle=4;startup->manager=IgbBootstrapPointer{};
    auto pool=std::make_shared<ModelSceneData>();const auto native=make_native_igb_thread_manager_insert_factory(f.services);unsigned caught{};
    startup->factory=[&](const IgbThreadStartupCall& call){if(call.kind==IgbThreadStartupServiceKind::object_virtual && call.virtual_slot==0x60)return native(call);
        auto op=std::make_shared<IgbThreadStartupOperation>();op->resume=[&,call,out=op.get()]{
            if(call.kind==IgbThreadStartupServiceKind::pool_for_handle)out->pool_result=pool;
            else if(call.kind==IgbThreadStartupServiceKind::create_manager)out->object_result=pointer(f.manager);
            else if(call.kind==IgbThreadStartupServiceKind::create_thread)out->object_result=pointer(f.thread);
            else if(call.kind!=IgbThreadStartupServiceKind::object_virtual || call.virtual_slot!=0xdc)throw std::runtime_error("Unexpected prepared startup endpoint.");
            return MessengerCallResult{true,0,{}};};return op;};
    IgbThreadStartupInvocation run;f.lower_action=[&](const IgbThreadManagerInsertCall& call,const std::function<void()>&){
        check(call.kind==K::wake_thread,"Startup reentry fixture did not reach signal wake.");try{run.resume(startup);}catch(const std::runtime_error&){++caught;}
        return MessengerCallResult{true,0,{}};};
    rejects([&]{run.resume(startup);},"Caught real startup reentry let manager signal/temporary release continue.");
    check(caught==1 && run.faulted() && word(f.thread,4)==0xa5000003 && word(f.list,8)==1 && word(f.semaphore,0x18)==1 &&
        f.interrupts->enabled==false && igb_bootstrap_read_word(f.backing,0)==igb_bootstrap_address(pointer(f.thread)) &&
        !f.runtime_calls[1].result->initialized[0],"Parent fault restored exclusion, released temporary ownership or erased completed insertion prefix.");
    rejects([&]{run.resume(startup);},"Failed startup replayed manager insertion.");check(f.lower_polls==1,"Failed startup replayed wake.");
}

void exact_adapter_admission(){
    Fixture f;auto sem_sentinel=std::make_shared<IgbThreadRuntimeInitOperation>();const auto sem=make_native_igb_semaphore_runtime_access_factory(f.services,
        [&](const IgbThreadRuntimeInitCall&){return sem_sentinel;});IgbThreadRuntimeInitCall call;call.vtable_word=0x80470314;
    for(const auto slot:{0x5cu,0x68u,0x84u,0xa8u}){call.virtual_slot=slot;check(sem(call)==sem_sentinel,"Semaphore adapter admitted activation/setter or another virtual.");}
    call.virtual_slot=0x8c;call.vtable_word=0x80470110;check(sem(call)==sem_sentinel,"Semaphore adapter claimed another class.");
    auto sentinel=std::make_shared<IgbThreadStartupOperation>();const auto manager=make_native_igb_thread_manager_insert_factory(f.services,
        [&](const IgbThreadStartupCall&){return sentinel;});IgbThreadStartupCall other;other.kind=IgbThreadStartupServiceKind::object_virtual;other.vtable_word=0x804700a8;
    for(const auto slot:{0x5cu,0x64u,0xdcu,0x2cu}){other.virtual_slot=slot;check(manager(other)==sentinel,"Manager adapter admitted current lookup/removal/adoption/init.");}
    other.virtual_slot=0x60;other.vtable_word=0x80470110;check(manager(other)==sentinel,"Manager adapter claimed thread +60.");
}
}
int main(){try{
    inactive_access_never_reads_unused_count();positive_wait_and_signed_sleep();sleep_reload_and_retained_continuation();
    wait_separate_positive_test_and_decrement_read();signal_unsigned_maximum_and_fresh_os_count();wake_then_restore_then_status();
    semaphore_fault_prefix_and_identity();unknown_state_and_late_scheduler_callback();actual_manager_wait_append_signal();manager_retain_before_invalid_manager_and_ignored_status();
    fresh_list_and_signal_after_captured_wait();growth_capture_and_count_before_element_prefix();signed_list_count_and_wrapping_index();
    manager_reentry_stops_nested_operations();actual_startup_parent_guard();exact_adapter_admission();
    std::cout<<"igb_thread_manager_insert_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Thread manager insertion tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
