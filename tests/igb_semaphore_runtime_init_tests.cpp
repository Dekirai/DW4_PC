#include "dw4/igb_semaphore_runtime_init.hpp"
#include "dw4/igb_memory_field_construct.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Initial execution-exclusion
// state is prepared explicitly; this does not prove host scheduling or wait/signal.
namespace {
using namespace dw4;
using IK=IgbInterruptServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t fill=0xcd,std::uint8_t known=0){
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,fill);result->initialized.assign(size,known);return result;
}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
IgbObjectInstance semaphore(){
    IgbObjectInstance result;result.offset=0x10;result.storage=std::make_shared<IgbObjectStorage>();
    result.storage->original_address=0x200000;result.storage->bytes=bytes(0x40);
    result.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::semaphore);
    result.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{result.storage,result.offset};
    IgbObjectBinding binding;binding.offset=result.offset;binding.runtime_object=result.runtime_object;
    result.storage->bindings.push_back(binding);igb_bootstrap_write_word(pointer(result),0,0x80470314);
    igb_object_set_reference_word(result,0xa5000003);return result;
}
bool word_known(const IgbObjectInstance& object,unsigned relative){
    const auto memory=object.storage->bytes;const auto at=object.offset+relative;
    return at+4<=memory->initialized.size() && std::all_of(memory->initialized.begin()+at,memory->initialized.begin()+at+4,[](auto value){return value!=0;});
}
struct Fixture {
    std::shared_ptr<IgbSemaphoreRuntimeInitServices> services=std::make_shared<IgbSemaphoreRuntimeInitServices>();
    std::shared_ptr<IgbExecutionInterruptState> interrupts=std::make_shared<IgbExecutionInterruptState>();
    std::shared_ptr<IgbMemoryFieldConstructServices> status=std::make_shared<IgbMemoryFieldConstructServices>();
    IgbObjectInstance object=semaphore();
    std::shared_ptr<IgbScratchBuffer> result=bytes(8,0xe7);
    std::vector<IgbInterruptCall> calls;
    std::vector<std::shared_ptr<IgbInterruptOperation>> operations;
    std::function<std::optional<MessengerCallResult>(const IgbInterruptCall&)> before;
    std::function<void(const IgbInterruptCall&,const MessengerCallResult&)> after;
    unsigned polls{};
    Fixture(){
        interrupts->enabled=true;status->status_success=0;
        services->status_success=std::shared_ptr<std::optional<std::uint32_t>>(status,&status->status_success);
        igb_bootstrap_write_word(pointer(object),0x0c,0x81234567);
        const auto native=make_native_igb_interrupt_factory(interrupts);
        services->interrupts=[this,native](const IgbInterruptCall& call){
            calls.push_back(call);const auto selected=native(call);auto operation=std::make_shared<IgbInterruptOperation>();
            operation->resume=[this,call,selected]{++polls;if(before){const auto pending=before(call);if(pending)return *pending;}
                const auto value=selected->resume();if(after)after(call,value);return value;};operations.push_back(operation);return operation;
        };
    }
    std::uint32_t word(unsigned relative)const{return igb_bootstrap_read_word(pointer(object),relative);}
    void completed_prefix(std::uint32_t count)const{
        check(word(0x20)==0 && word(0x1c)==0 && word(0x18)==count && igb_bootstrap_read_byte(pointer(object),0x14)==1,
            "Semaphore activation lost queue/count stores or active publication.");
    }
};

void interrupt_state_transitions(){
    for(const bool enabled:{false,true})for(const auto restore:{0u,1u,0x80000000u,0xffffffffu}){
        auto state=std::make_shared<IgbExecutionInterruptState>();state->enabled=enabled;
        const auto factory=make_native_igb_interrupt_factory(state);const auto disable=factory({IK::disable,0});
        check(disable && disable->resume().completed && disable->resume().value==std::uint32_t(enabled) && state->enabled==false,
            "Disable did not return and clear the actual preceding interrupt bit.");
        state->enabled=true;check(disable->resume().value==std::uint32_t(enabled) && state->enabled==true,
            "Completed disable replayed the state transition.");
        const auto resume=factory({IK::restore,restore});state->enabled=false;
        const auto status=resume->resume();check(status.completed && status.value==0 && state->enabled==(restore!=0),
            "Restore did not use its full captured nonzero argument and fresh previous bit.");
        state->enabled=true;check(resume->resume().value==0 && state->enabled==true,"Completed restore changed state or returned a fresh bit.");
    }
    auto state=std::make_shared<IgbExecutionInterruptState>();const auto factory=make_native_igb_interrupt_factory(state);
    const auto operation=factory({IK::disable,0});check(!operation->resume().completed && !state->enabled,
        "Unknown original interrupt state was assumed enabled or disabled.");
    state->enabled=false;check(operation->resume().completed && operation->resume().value==0 && state->enabled==false,
        "Known false interrupt state remained unavailable.");
    const auto restore=factory({IK::restore,1});state->enabled.reset();check(!restore->resume().completed,"Restore fabricated an unknown former bit.");
    state->enabled=true;check(restore->resume().completed && restore->resume().value==1 && state->enabled==true,"Late restore state lost its true former bit.");
}

void activation_counts_active_and_padding(){
    for(const bool enabled:{false,true})for(const auto count:{0u,1u,0x80000000u,0xffffffffu})for(const auto active:{0u,1u,0x7fu}){
        Fixture f;f.interrupts->enabled=enabled;igb_bootstrap_write_word(pointer(f.object),0x0c,count);
        igb_bootstrap_write_byte(pointer(f.object),0x14,static_cast<std::uint8_t>(active));f.status->status_success=0x89abcdef;
        const auto before=f.object.storage->bytes->bytes,known=f.object.storage->bytes->initialized;
        IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);const auto outcome=run.resume(f.services);
        check(outcome.completed && run.completed() && !run.faulted() && f.interrupts->enabled==enabled,
            "Semaphore activation changed the prior interrupt bit, clamped count or skipped a prior active value.");
        f.completed_prefix(count);check(f.calls.size()==2 && f.calls[0].kind==IK::disable && f.calls[1].kind==IK::restore &&
            f.calls[1].restore_enabled==std::uint32_t(enabled) && f.polls==2,"Activation changed disable/restore order or the captured previous bit.");
        check(igb_buffer_word(f.result,0)==0x89abcdef && f.result->bytes[4]==0xe7 && !f.result->initialized[4],
            "Hidden result did not copy the full canonical status or wrote adjacent bytes.");
        for(unsigned at=0;at<before.size();++at){const bool changed=at==f.object.offset+0x14 ||
            (at>=f.object.offset+0x18 && at<f.object.offset+0x24);
            if(!changed)check(f.object.storage->bytes->bytes[at]==before[at] && f.object.storage->bytes->initialized[at]==known[at],
                "Activation read or changed maxCount, padding, common header or allocation boundary.");}
        const auto completed=f.object.storage->bytes->bytes;f.status->status_success=3;f.interrupts->enabled=!enabled;
        check(run.resume().completed && f.polls==2 && f.object.storage->bytes->bytes==completed && igb_buffer_word(f.result,0)==0x89abcdef &&
            f.interrupts->enabled==!enabled,"Completed activation replayed stores, helpers or status publication.");
    }
}

void suspended_helpers_capture_count_and_fresh_status(){
    Fixture f;bool allow_disable{},allow_restore{};const auto original_result=f.result;
    f.before=[&](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==IK::disable && !allow_disable)return MessengerCallResult{false,0x91,"Prepared disable suspension."};
        if(call.kind==IK::restore && !allow_restore)return MessengerCallResult{false,0x92,"Prepared restore suspension."};return {};
    };
    IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);check(!run.resume(f.services).completed && f.polls==1 && f.calls.size()==1 &&
        f.interrupts->enabled==true && !word_known(f.object,0x20),"Pending disable wrote queue storage or changed exclusion state.");
    igb_bootstrap_write_word(pointer(f.object),0x0c,0xffffffff);allow_disable=true;
    check(!run.resume(f.services).completed && f.calls.size()==2 && f.polls==3 && f.interrupts->enabled==false &&
        f.word(0x20)==0 && f.word(0x1c)==0 && f.word(0x18)==0x81234567 && !f.object.storage->bytes->initialized[f.object.offset+0x14],
        "Pending restore lost the captured count or published active too early.");
    f.result=bytes(4,0x48,1);f.status->status_success=0x76543210;allow_restore=true;f.services->interrupts={};
    check(run.resume(f.services).completed && f.calls.size()==2 && f.polls==4 && f.interrupts->enabled==true &&
        igb_buffer_word(original_result,0)==0x76543210 && igb_buffer_word(f.result,0)==0x48484848,
        "Resumed restore lost its selected callback, fresh status or captured result owner.");
    f.completed_prefix(0x81234567);
}

void late_interrupt_factories_and_continuations(){
    Fixture f;const auto original_factory=f.services->interrupts;f.services->interrupts={};
    IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);
    check(!run.resume(f.services).completed && f.calls.empty() && f.interrupts->enabled==true && !word_known(f.object,0x20),
        "Missing interrupt factory became success or executed queue stores.");
    igb_bootstrap_write_word(pointer(f.object),0x0c,0xffffffff);f.services->interrupts=original_factory;
    check(run.resume(f.services).completed && f.word(0x18)==0x81234567,"Late interrupt factory recaptured initial count.");
    Fixture missing;bool available{};unsigned selections{};const auto ready=missing.services->interrupts;
    missing.services->interrupts=[&](const IgbInterruptCall& call){++selections;
        if(call.kind==IK::disable && !available)return std::shared_ptr<IgbInterruptOperation>{};return ready(call);};
    IgbSemaphoreRuntimeInitInvocation waiting(missing.object,missing.result);
    check(!waiting.resume(missing.services).completed && selections==1 && missing.calls.empty(),"Missing selected interrupt operation became a no-op.");
    // A replacement service is available for the subsequent restore. The
    // pending disable must still reselect through its already captured factory.
    missing.services->interrupts=ready;available=true;
    check(waiting.resume(missing.services).completed && selections==2 && missing.calls.size()==2 && missing.polls==2,
        "Late interrupt endpoint discarded its selected factory or changed subsequent service selection.");
    Fixture empty;const auto eventual=empty.services->interrupts;auto operation=std::make_shared<IgbInterruptOperation>();
    empty.services->interrupts=[&](const IgbInterruptCall& call){return call.kind==IK::disable?operation:eventual(call);};
    IgbSemaphoreRuntimeInitInvocation callback(empty.object,empty.result);
    check(!callback.resume(empty.services).completed && empty.calls.empty(),"Absent selected interrupt callback was treated as complete.");
    const auto native=make_native_igb_interrupt_factory(empty.interrupts)({IK::disable,0});operation->resume=[native]{return native->resume();};
    check(callback.resume(empty.services).completed && empty.calls.size()==1 && empty.calls[0].kind==IK::restore,
        "Late callback did not continue the retained disable operation.");
}

void unknown_status_follows_active_publication(){
    Fixture f;f.status->status_success.reset();IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);
    check(!run.resume(f.services).completed && f.interrupts->enabled==true && f.calls.size()==2 && !f.result->initialized[0],
        "Unknown success status was a completed zero or changed interrupt restoration.");f.completed_prefix(0x81234567);
    igb_bootstrap_write_word(pointer(f.object),0x18,0xaabbccdd);f.services->interrupts={};
    check(!run.resume(f.services).completed && f.word(0x18)==0xaabbccdd && f.polls==2,
        "Pending success load replayed initialization stores or interrupt helpers.");
    f.status->status_success=0xfedcba98;
    check(run.resume(f.services).completed && igb_buffer_word(f.result,0)==0xfedcba98 && f.word(0x18)==0xaabbccdd && f.polls==2,
        "Late success publication recaptured count or missed the actual canonical alias.");
    check(f.services->status_success.get()==&f.status->status_success,"Semaphore success cell copied the memory-construct canonical state.");
}

void queue_store_order_and_failure_prefix(){
    for(unsigned stop_after=0;stop_after<3;++stop_after){
        Fixture f;IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);bool injected{};
        const auto inspect=[&]{
            const bool tail=word_known(f.object,0x20),head=word_known(f.object,0x1c),count=word_known(f.object,0x18);
            check(!head || tail,"Queue head store preceded its tail store.");check(!count || (tail && head),"Count store preceded queue initialization.");
            const unsigned stores=unsigned(tail)+unsigned(head)+unsigned(count);
            if(f.interrupts->enabled==false && stores==stop_after){injected=true;throw std::runtime_error("Prepared failure at the next original store boundary.");}
        };
        rejects([&]{run.resume(f.services,inspect);},"Injected store-boundary failure was ignored.");
        check(injected && run.faulted() && f.interrupts->enabled==false && f.calls.size()==1 && f.calls[0].kind==IK::disable &&
            !f.object.storage->bytes->initialized[f.object.offset+0x14] && !f.result->initialized[0],
            "Queue/count failure restored interrupts, published active or wrote status.");
        check(word_known(f.object,0x20)==(stop_after>=1) && word_known(f.object,0x1c)==(stop_after>=2) && !word_known(f.object,0x18),
            "Queue/count failure changed the completed physical store prefix.");
        rejects([&]{run.resume(f.services);},"Faulted store sequence restarted after its injection was removed.");
        check(f.polls==1 && f.interrupts->enabled==false,"Failed queue initialization replayed disable or automatically restored interrupts.");
    }
    Fixture truncated;truncated.object.storage->bytes->bytes.resize(truncated.object.offset+0x23);
    truncated.object.storage->bytes->initialized.resize(truncated.object.offset+0x23);
    IgbSemaphoreRuntimeInitInvocation tail(truncated.object,truncated.result);
    rejects([&]{tail.resume(truncated.services);},"Truncated queue-tail word accepted a partial physical store.");
    check(tail.faulted() && truncated.interrupts->enabled==false && truncated.calls.size()==1 &&
        !word_known(truncated.object,0x20) && !word_known(truncated.object,0x1c),"Failed tail word wrote part of its value or restored interrupts.");
}

void restore_and_hidden_result_failure_prefix(){
    Fixture restore;restore.before=[](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==IK::restore)throw std::runtime_error("Prepared restore failure.");return {};
    };
    IgbSemaphoreRuntimeInitInvocation run(restore.object,restore.result);
    rejects([&]{run.resume(restore.services);},"Throwing restore endpoint was swallowed.");
    check(run.faulted() && restore.interrupts->enabled==false && restore.word(0x20)==0 && restore.word(0x1c)==0 &&
        restore.word(0x18)==0x81234567 && !restore.object.storage->bytes->initialized[restore.object.offset+0x14],
        "Restore failure lost OS-initialization stores or published active early.");
    for(unsigned invalid=0;invalid<4;++invalid){Fixture f;const auto original=f.result;
        if(invalid==0)f.result.reset();if(invalid==1)original->bytes.resize(3);if(invalid==2)original->initialized.resize(3);if(invalid==3)original->released=true;
        const auto before=original->bytes,known=original->initialized;
        IgbSemaphoreRuntimeInitInvocation bad(f.object,f.result);rejects([&]{bad.resume(f.services);},"Invalid hidden result destination was accepted.");
        f.completed_prefix(0x81234567);check(bad.faulted() && f.interrupts->enabled==true && f.calls.size()==2 &&
            original->bytes==before && original->initialized==known,"Result-word failure happened before active/status or wrote a partial word.");
        original->bytes.resize(8);original->initialized.resize(8);original->released=false;
        rejects([&]{bad.resume(f.services);},"Invalid-result failure revived after destination repair.");check(f.polls==2,"Failed status write replayed interrupt helpers.");
    }
    for(const auto release_at:{IK::disable,IK::restore}){Fixture f;
        f.after=[&](const IgbInterruptCall& call,const MessengerCallResult& status){if(call.kind==release_at && status.completed)f.result->released=true;};
        IgbSemaphoreRuntimeInitInvocation bad(f.object,f.result);
        rejects([&]{bad.resume(f.services);},"Callback-released result destination was accepted.");f.completed_prefix(0x81234567);
        check(bad.faulted() && f.interrupts->enabled==true && f.calls.size()==2 && !f.result->initialized[0],
            "Result release was checked before its actual store or changed the completed activation prefix.");
    }
}

void callback_reentry_and_receiver_identity(){
    for(const auto when:{IK::disable,IK::restore})for(unsigned mutation=0;mutation<5;++mutation){
        Fixture f;const auto original=f.object.storage->bytes;const auto bindings=f.object.storage->bindings;
        const auto address=f.object.storage->original_address;const auto physical=f.object.runtime_object->physical_storage;
        IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);unsigned caught{};
        f.after=[&](const IgbInterruptCall& call,const MessengerCallResult& result){if(call.kind!=when || !result.completed)return;
            if(mutation==0){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}}
            if(mutation==1)f.object.storage->bindings.clear();
            if(mutation==2)f.object.storage->bytes=bytes(0x40);
            if(mutation==3)f.object.storage->original_address+=0x1000;
            if(mutation==4)f.object.runtime_object->physical_storage.reset();
        };
        rejects([&]{run.resume(f.services);},"Interrupt callback invalidation allowed later native stores.");
        check(run.faulted() && (mutation!=0 || caught==1) && f.calls.size()==(when==IK::disable?1u:2u) &&
            f.interrupts->enabled==(when==IK::restore) && !original->initialized[f.object.offset+0x14] && !f.result->initialized[0],
            "Callback identity failure changed the reached interrupt or active/status prefix.");
        if(when==IK::restore)check(igb_buffer_word(original,f.object.offset+0x20)==0 && igb_buffer_word(original,f.object.offset+0x1c)==0 &&
            igb_buffer_word(original,f.object.offset+0x18)==0x81234567,"Restore callback fault erased completed queue/count stores.");
        else check(!original->initialized[f.object.offset+0x20],"Disable callback fault reached queue stores.");
        f.object.storage->bytes=original;f.object.storage->bindings=bindings;f.object.storage->original_address=address;
        f.object.runtime_object->physical_storage=physical;f.after={};const auto polls=f.polls;
        rejects([&]{run.resume(f.services);},"Faulted activation restarted after restoring its receiver identity.");
        check(f.polls==polls,"Receiver repair replayed a completed interrupt operation.");
    }
}

void borrowed_guard_reentry_stops_immediate_effects(){
    Fixture baseline;unsigned boundaries{};IgbSemaphoreRuntimeInitInvocation normal(baseline.object,baseline.result);
    check(normal.resume(baseline.services,[&]{++boundaries;}).completed && boundaries>0,"Borrowed-check fixture did not complete an ordinary activation.");
    for(unsigned boundary=1;boundary<=boundaries;++boundary){
        Fixture f;unsigned reached{},caught{};bool captured{};
        std::vector<std::uint8_t> object_bytes,object_known,result_bytes,result_known;
        std::optional<bool> interrupt_state;std::size_t selected_calls{};
        IgbSemaphoreRuntimeInitInvocation run(f.object,f.result);
        const auto parent=[&]{if(++reached!=boundary)return;
            object_bytes=f.object.storage->bytes->bytes;object_known=f.object.storage->bytes->initialized;
            result_bytes=f.result->bytes;result_known=f.result->initialized;interrupt_state=f.interrupts->enabled;
            selected_calls=f.calls.size();captured=true;
            try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}
        };
        rejects([&]{run.resume(f.services,parent);},"Borrowed parent check swallowed recursive invalidation and completed activation.");
        check(captured && caught==1 && run.faulted() && f.object.storage->bytes->bytes==object_bytes &&
            f.object.storage->bytes->initialized==object_known && f.result->bytes==result_bytes && f.result->initialized==result_known &&
            f.interrupts->enabled==interrupt_state && f.calls.size()==selected_calls,
            "Swallowed borrowed-check recursion allowed an immediate store or interrupt operation before the next guard.");
        const auto polls=f.polls;rejects([&]{run.resume(f.services);},"Borrowed-check failure resumed after removing the parent callback.");
        check(f.polls==polls,"Borrowed-check repair replayed an interrupt operation.");
    }
}

void invalid_receiver_or_count_fails_before_disable(){
    for(unsigned invalid=0;invalid<7;++invalid){Fixture f;auto target=f.object;
        if(invalid==0)target={};
        if(invalid==1)target.runtime_object.reset();
        if(invalid==2)target.storage->bindings.clear();
        if(invalid==3)target.runtime_object->physical_storage.reset();
        if(invalid==4)target.runtime_object->physical_storage->offset+=4;
        if(invalid==5)target.storage->bytes->initialized[target.offset+0x0d]=0;
        if(invalid==6)target.storage->bytes->released=true;
        IgbSemaphoreRuntimeInitInvocation run(target,f.result);
        rejects([&]{run.resume(f.services);},"Semaphore activation accepted invalid reached receiver/count storage.");
        check(run.faulted() && f.calls.empty() && f.interrupts->enabled==true && !f.result->initialized[0],
            "Initial receiver/count failure disabled interrupts or published a result.");
    }
}

void manager_reentry_stops_nested_activation(){
    for(const auto when:{IK::disable,IK::restore}){
        Fixture f;IgbObjectInstance manager;manager.offset=0x10;manager.storage=std::make_shared<IgbObjectStorage>();
        manager.storage->original_address=0x300000;manager.storage->bytes=bytes(0x30);
        manager.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::thread_manager);
        manager.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{manager.storage,manager.offset};
        IgbObjectBinding binding;binding.offset=manager.offset;binding.runtime_object=manager.runtime_object;manager.storage->bindings.push_back(binding);
        igb_bootstrap_write_word(pointer(manager),0,0x804700a8);igb_object_set_reference_word(manager,0xa5000003);
        igb_bootstrap_write_word(pointer(manager),0x0c,igb_bootstrap_address(pointer(f.object)));
        const auto manager_bytes=manager.storage->bytes->bytes,manager_known=manager.storage->bytes->initialized;
        auto services=std::make_shared<IgbThreadRuntimeInitServices>();services->objects=std::make_shared<IgbObjectFieldConstructServices>();
        services->objects->objects.push_back(f.object);std::shared_ptr<IgbScratchBuffer> hidden_result;unsigned selections{},caught{};
        const auto activation=make_native_igb_semaphore_runtime_init_factory(f.services);
        services->factory=[&](const IgbThreadRuntimeInitCall& call){++selections;hidden_result=call.result;
            check(call.object.storage==f.object.storage && call.object.runtime_object==f.object.runtime_object && call.virtual_slot==0x5c,
                "Real manager selected a different semaphore or activation virtual.");return activation(call);};
        IgbThreadManagerInitInvocation run(manager);
        f.after=[&](const IgbInterruptCall& call,const MessengerCallResult& result){if(call.kind==when && result.completed){
            try{run.resume(services);}catch(const std::runtime_error&){++caught;}
        }};
        rejects([&]{run.resume(services);},"Caught recursive manager call allowed its nested activation to complete.");
        check(run.faulted() && caught==1 && selections==1 && hidden_result &&
            std::all_of(hidden_result->initialized.begin(),hidden_result->initialized.end(),[](auto value){return value==0;}) &&
            !f.object.storage->bytes->initialized[f.object.offset+0x14] && f.interrupts->enabled==(when==IK::restore) &&
            f.calls.size()==(when==IK::disable?1u:2u),
            "Manager invalidation reached later activation, restore or hidden-result effects.");
        if(when==IK::disable)check(!word_known(f.object,0x20) && !word_known(f.object,0x1c) && !word_known(f.object,0x18),
            "Manager recursion during disable still initialized queue/count storage.");
        else check(f.word(0x20)==0 && f.word(0x1c)==0 && f.word(0x18)==0x81234567,
            "Manager recursion during restore erased completed queue/count stores.");
        check(manager.storage->bytes->bytes==manager_bytes && manager.storage->bytes->initialized==manager_known,
            "Failed nested activation changed the manager receiver.");
        f.after={};const auto polls=f.polls;rejects([&]{run.resume(services);},"Manager fault resumed after removing the recursive endpoint.");
        check(f.polls==polls && selections==1,"Faulted manager replayed its retained activation.");
    }
}

void exact_adapter_and_retained_pending_result(){
    Fixture f;unsigned delegated{};auto sentinel=std::make_shared<IgbThreadRuntimeInitOperation>();
    const auto factory=make_native_igb_semaphore_runtime_init_factory(f.services,[&](const auto&){++delegated;return sentinel;});
    IgbThreadRuntimeInitCall call;call.object=f.object;call.result=f.result;call.vtable_word=0x80470314;call.virtual_slot=0x5c;
    const auto operation=factory(call);check(operation && operation!=sentinel && operation->resume().completed,"Exact semaphore runtime +5C was not admitted.");
    for(const auto slot:{0x58u,0x64u,0x68u,0x7cu,0x8cu}){call.virtual_slot=slot;check(factory(call)==sentinel,"Activation adapter claimed other semaphore behavior.");}
    call.virtual_slot=0x5c;call.vtable_word=0x804700a8;check(factory(call)==sentinel && delegated==6,"Activation adapter consumed another runtime class.");
    Fixture pending;pending.status->status_success.reset();const auto old_result=pending.result;
    {IgbSemaphoreRuntimeInitInvocation abandoned(pending.object,pending.result);check(!abandoned.resume(pending.services).completed,"Retention fixture unexpectedly completed.");}
    const auto retained=[&](const std::shared_ptr<void>& value){return std::find(pending.services->retained.begin(),pending.services->retained.end(),value)!=pending.services->retained.end();};
    check(retained(pending.object.storage) && retained(pending.object.storage->bytes) && retained(old_result),
        "Abandoned status continuation discarded the receiver allocation or captured hidden result.");
}
}
int main(){try{
    interrupt_state_transitions();activation_counts_active_and_padding();suspended_helpers_capture_count_and_fresh_status();late_interrupt_factories_and_continuations();
    unknown_status_follows_active_publication();queue_store_order_and_failure_prefix();restore_and_hidden_result_failure_prefix();
    callback_reentry_and_receiver_identity();borrowed_guard_reentry_stops_immediate_effects();invalid_receiver_or_count_fails_before_disable();
    manager_reentry_stops_nested_activation();exact_adapter_and_retained_pending_result();
    std::cout<<"igb_semaphore_runtime_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Semaphore activation tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
