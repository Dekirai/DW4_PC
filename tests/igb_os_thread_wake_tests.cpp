#include "dw4/igb_os_thread_wake.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// The original queue operation is composed here. Initial OS/global cells and
// the lower 8026140C scheduler are explicit prepared test inputs, not OS startup
// or a host context-switch implementation.
namespace {
using namespace dw4;
using IK=IgbInterruptServiceKind;
using LK=IgbThreadManagerInsertServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
template<class F>void rejects_canonical_alias(F action,const char* message){bool caught{};try{action();}catch(const IgbOsThreadWakeError& error){
    const std::string detail=error.what();caught=detail.find("canonical")!=std::string::npos && detail.find("conflict")!=std::string::npos;}check(caught,message);}
auto bytes(std::size_t size){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(size,0xcd);out->initialized.assign(size,0);return out;}
IgbBootstrapPointer raw(std::uint32_t guest,std::size_t size,std::uint32_t prefix=0x10){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=guest-prefix;storage->bytes=bytes(size+prefix);return {storage,prefix};}
std::uint32_t address(IgbBootstrapPointer value){return igb_bootstrap_address(value);}
std::uint32_t word(IgbBootstrapPointer value,unsigned at){return igb_bootstrap_read_word(value,at);}
void word(IgbBootstrapPointer value,unsigned at,std::uint32_t data){igb_bootstrap_write_word(value,at,data);}
std::uint16_t half(IgbBootstrapPointer value,unsigned at){return static_cast<std::uint16_t>((igb_bootstrap_read_byte(value,at)<<8u)|igb_bootstrap_read_byte(value,at+1));}
bool known(IgbBootstrapPointer value,unsigned at,unsigned count=4){const auto b=value.storage->bytes;const auto offset=value.offset+at;
    return offset+count<=b->bytes.size() && offset+count<=b->initialized.size() &&
        std::all_of(b->initialized.begin()+offset,b->initialized.begin()+offset+count,[](auto v){return v!=0;});}
void unknown(IgbBootstrapPointer value,unsigned at,unsigned count=4){auto& mask=value.storage->bytes->initialized;
    std::fill(mask.begin()+value.offset+at,mask.begin()+value.offset+at+count,0);}
void resize(IgbBootstrapPointer value,unsigned extent){value.storage->bytes->bytes.resize(value.offset+extent);value.storage->bytes->initialized.resize(value.offset+extent);}
IgbBootstrapPointer thread(std::uint32_t guest,std::uint32_t next,std::uint32_t suspend,std::uint32_t priority){
    auto out=raw(guest,0x318);igb_bootstrap_write_halfword(out,0x2c8,0x9876);igb_bootstrap_write_halfword(out,0x2ca,0x1234);
    word(out,0x2cc,suspend);word(out,0x2d0,priority);word(out,0x2dc,0xabcdef00);word(out,0x2e0,next);word(out,0x2e4,0x76543210);return out;}
struct Snapshot {
    std::vector<std::uint8_t> data,mask;
    explicit Snapshot(IgbBootstrapPointer p):data(p.storage->bytes->bytes),mask(p.storage->bytes->initialized){}
    bool same(IgbBootstrapPointer p)const{return data==p.storage->bytes->bytes && mask==p.storage->bytes->initialized;}
};
struct Fixture {
    std::shared_ptr<IgbOsThreadWakeServices> services=std::make_shared<IgbOsThreadWakeServices>();
    std::shared_ptr<IgbExecutionInterruptState> interrupts=std::make_shared<IgbExecutionInterruptState>();
    IgbBootstrapPointer queue=raw(0x100010,8);
    std::vector<IgbInterruptCall> calls;
    std::vector<std::shared_ptr<IgbInterruptOperation>> interrupt_operations;
    std::vector<std::shared_ptr<IgbOsThreadWakeOperation>> scheduler_operations;
    std::function<std::optional<MessengerCallResult>(const IgbInterruptCall&)> interrupt_before;
    std::function<void(const IgbInterruptCall&)> interrupt_after;
    std::function<MessengerCallResult(const std::function<void()>&)> schedule;
    unsigned irq_polls{},schedule_selections{},schedule_polls{};
    Fixture(){services->os=std::make_shared<IgbOsThreadState>();services->scheduler=std::make_shared<IgbOsSchedulerState>();
        interrupts->enabled=true;services->scheduler->run_hint=0;word(queue,0,0);
        const auto native=make_native_igb_interrupt_factory(interrupts);
        services->interrupts=[this,native](const IgbInterruptCall& call){calls.push_back(call);const auto selected=native(call);
            auto out=std::make_shared<IgbInterruptOperation>();out->resume=[this,selected,call]{++irq_polls;
                if(interrupt_before){const auto state=interrupt_before(call);if(state)return *state;}
                const auto state=selected->resume();if(interrupt_after)interrupt_after(call);return state;};interrupt_operations.push_back(out);return out;};
        services->factory=[this](const IgbOsThreadWakeCall& call){++schedule_selections;
            check(call.argument==0 && interrupts->enabled==false,"Wake selected scheduling with a different argument or enabled interrupts.");
            auto out=std::make_shared<IgbOsThreadWakeOperation>();out->resume_checked=[this](const std::function<void()>& guard){++schedule_polls;
                if(schedule)return schedule(guard);return MessengerCallResult{false,0,"Explicit prepared scheduler boundary."};};
            out->resume=[]()->MessengerCallResult{throw std::runtime_error("Wake ignored the preferred checked scheduler continuation.");};
            scheduler_operations.push_back(out);return out;};
    }
    void complete_scheduler(){schedule=[](const std::function<void()>& guard){if(guard)guard();return MessengerCallResult{true,0,{}};};}
    void bind(IgbBootstrapPointer value){services->os->mappings.push_back(value);}
};

void empty_queue_exclusion_and_unused_state(){
    for(bool enabled:{false,true}){Fixture f;f.interrupts->enabled=enabled;resize(f.queue,4);Snapshot before(f.queue);
        f.services->os.reset();f.services->factory={};IgbOsThreadWakeInvocation run(f.queue);
        check(run.resume(f.services).completed && before.same(f.queue) && f.calls.size()==2 && f.calls[0].kind==IK::disable &&
            f.calls[1].kind==IK::restore && f.calls[1].restore_enabled==unsigned(enabled) && f.interrupts->enabled==enabled &&
            !f.services->scheduler->run_bits && !f.services->scheduler->disable_count && f.schedule_selections==0,
            "Empty wake accessed tail/current thread/runbits/disable count or did not restore the captured bit.");
        word(f.queue,0,0xdeadbeef);check(run.resume().completed && word(f.queue,0)==0xdeadbeef && f.irq_polls==2,
            "Completed empty wake reread its queue or replayed exclusion callbacks.");
    }
    Fixture f;f.services->scheduler->run_hint.reset();IgbOsThreadWakeInvocation run(f.queue);
    check(!run.resume(f.services).completed && f.calls.size()==1 && f.interrupts->enabled==false,
        "Unknown full-word run hint became zero or restored interrupts.");
    word(f.queue,0,0xdeadbeef);f.services->scheduler->run_hint=0;
    check(run.resume().completed && f.calls.size()==2 && word(f.queue,0)==0xdeadbeef,
        "Late run hint repeated the completed empty-head read.");
}

void nonzero_hint_retains_scheduler_and_captured_bit(){
    for(const auto hint:{1u,0x80000000u,0xffffffffu}){Fixture f;f.services->scheduler->run_hint=hint;bool ready{};
        f.schedule=[&](const std::function<void()>& guard){if(guard)guard();return MessengerCallResult{ready,0,"Prepared suspended scheduler."};};
        IgbOsThreadWakeInvocation run(f.queue);check(!run.resume(f.services).completed && f.schedule_selections==1 && f.schedule_polls==1 && f.calls.size()==1,
            "A nonzero full-word hint skipped scheduling or restored exclusion before its continuation.");
        f.services->scheduler->run_hint=0;word(f.queue,0,0xdeadbeef);
        f.scheduler_operations[0]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{throw std::runtime_error("Replacement continuation ran.");};
        f.services->factory=[](const IgbOsThreadWakeCall&)->std::shared_ptr<IgbOsThreadWakeOperation>{throw std::runtime_error("Replacement scheduler factory ran.");};
        ready=true;check(run.resume().completed && f.schedule_polls==2 && f.schedule_selections==1 && f.calls.size()==2 &&
            f.calls[1].restore_enabled==1 && word(f.queue,0)==0xdeadbeef && f.interrupts->enabled==true,
            "Pending wake reselected its scheduler, reloaded head/hint, or lost the original exclusion bit.");
    }
    Fixture f;f.services->scheduler->run_hint=1;const auto factory=f.services->factory;f.services->factory={};IgbOsThreadWakeInvocation run(f.queue);
    check(!run.resume(f.services).completed && f.calls.size()==1,"Absent scheduler fabricated successful wake.");
    f.services->factory=factory;f.complete_scheduler();check(run.resume().completed && f.schedule_polls==1 && f.calls.size()==2,
        "Late scheduler factory did not resume its reached boundary.");
}

void disable_precedes_reached_queue_validation(){
    Fixture f;f.interrupts->enabled.reset();IgbOsThreadWakeInvocation invalid({});
    check(!invalid.resume(f.services).completed && !invalid.faulted() && f.calls.size()==1,
        "Invalid queue was dereferenced before unknown interrupt state could suspend.");
    f.interrupts->enabled=true;rejects([&]{invalid.resume();},"Known exclusion accepted a null queue head read.");
    check(invalid.faulted() && f.interrupts->enabled==false && f.calls.size()==1,"Null queue failure restored interrupts or lost disable prefix.");
    Fixture late;bool ready{};late.interrupt_before=[&](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==IK::disable && !ready)return MessengerCallResult{false,0,"Prepared disable suspension."};return {};};
    auto t=thread(0x200010,0,1,0);late.bind(t);IgbOsThreadWakeInvocation captured(late.queue);
    check(!captured.resume(late.services).completed && late.interrupts->enabled==true,"Pending disable read or changed queue state.");
    word(late.queue,0,address(t));word(late.queue,4,address(t));ready=true;
    check(captured.resume().completed && half(t,0x2c8)==1 && word(late.queue,0)==0 && late.calls.size()==2 && late.irq_polls==3,
        "Wake captured queue head before the original disable returned.");
}

void fifo_dequeue_runqueue_append_and_signed_suspension(){
    Fixture f;auto a=thread(0x200010,0x300010,1,0),b=thread(0x300010,0x400010,0xffffffff,3),c=thread(0x400010,0,0,3);
    auto previous=thread(0x500010,0,0,3);auto runqueue=raw(0x80514dd0,8);word(runqueue,0,address(previous));word(runqueue,4,address(previous));
    word(f.queue,0,address(a));word(f.queue,4,address(c));for(const auto p:{a,b,c,previous,runqueue})f.bind(p);
    f.services->scheduler->run_bits=0x00000004;f.complete_scheduler();IgbOsThreadWakeInvocation run(f.queue);
    check(run.resume(f.services).completed && word(f.queue,0)==0 && word(f.queue,4)==0 && half(a,0x2c8)==1 &&
        half(b,0x2c8)==1 && half(c,0x2c8)==1,"Wake did not drain all original waiting nodes and set their halfword states.");
    check(word(a,0x2e0)==address(b) && word(a,0x2e4)==0x76543210 && word(a,0x2dc)==0xabcdef00,
        "Positive suspended thread had unused links/queue sanitized or was appended to a runqueue.");
    check(word(runqueue,0)==address(previous) && word(runqueue,4)==address(c) && word(previous,0x2e0)==address(b) &&
        word(b,0x2e4)==address(previous) && word(b,0x2e0)==address(c) && word(c,0x2e4)==address(b) && word(c,0x2e0)==0 &&
        word(b,0x2dc)==address(runqueue) && word(c,0x2dc)==address(runqueue),"Runqueue append lost FIFO links, existing tail, or captured predecessor.");
    check(f.services->scheduler->run_bits==0x10000004 && f.services->scheduler->run_hint==1 && f.schedule_polls==1 &&
        half(a,0x2ca)==0x1234 && half(b,0x2ca)==0x1234 && half(c,0x2ca)==0x1234,
        "Wake modified adjacent halfword, lost runbits, or scheduled before all dequeues.");
    for(const auto suspend:{1u,0x7fffffffu}){Fixture skipped;auto t=thread(0x210010,0,suspend,0);unknown(t,0x2d0);
        skipped.bind(t);word(skipped.queue,0,address(t));IgbOsThreadWakeInvocation call(skipped.queue);
        check(call.resume(skipped.services).completed && !skipped.services->scheduler->run_bits && skipped.schedule_polls==0 && !known(t,0x2d0),
            "Signed positive suspend count read priority or required runqueue state.");}
}

void priority_wrap_and_original_slw(){
    struct Case{std::uint32_t priority,queue,bit;};
    constexpr std::array<Case,8> cases{{{0,0x80514db8,0x80000000},{31,0x80514eb0,1},{32,0x80514eb8,0},
        {63,0x80514fb0,0},{64,0x80514fb8,0x80000000},{0xffffffff,0x80514db0,0},
        {0x20000000,0x80514db8,0x80000000},{0x80000000,0x80514db8,0x80000000}}};
    for(const auto c:cases){Fixture f;auto t=thread(0x200010,0,0x80000000,c.priority);auto q=raw(c.queue,8);
        word(q,4,0);word(f.queue,0,address(t));f.bind(t);f.bind(q);f.services->scheduler->run_bits=0x102;f.complete_scheduler();
        IgbOsThreadWakeInvocation run(f.queue);check(run.resume(f.services).completed && word(t,0x2dc)==c.queue &&
            word(q,0)==address(t) && word(q,4)==address(t) && f.services->scheduler->run_bits==(0x102|c.bit) &&
            f.services->scheduler->run_hint==1,"Wake clamped priority, used unsigned suspend, or replaced PPC SLW with host shift semantics.");}
}

void late_mappings_and_fresh_priority(){
    Fixture f;auto t=thread(0x200010,0,0,2);auto q=raw(0x80514dc8,8);word(q,4,0);f.bind(t);word(f.queue,0,address(t));
    f.services->scheduler->run_bits=4;IgbOsThreadWakeInvocation run(f.queue);
    check(!run.resume(f.services).completed && word(f.queue,0)==0 && word(f.queue,4)==0 && half(t,0x2c8)==1 &&
        word(t,0x2dc)==address(q) && word(t,0x2e4)==0x76543210 && f.calls.size()==1,
        "Missing runqueue mapping failed to preserve the completed dequeue/state/queue-member prefix.");
    word(t,0x2d0,7);f.bind(q);f.complete_scheduler();check(run.resume().completed && word(q,0)==address(t) && word(q,4)==address(t) &&
        word(t,0x2dc)==address(q) && f.services->scheduler->run_bits==0x01000004,
        "Late runqueue binding recaptured the initial priority or missed the final fresh priority read.");

    Fixture missing;auto a=thread(0x300010,0x400010,1,0),b=thread(0x400010,0,1,0);missing.bind(a);word(missing.queue,0,address(a));
    word(missing.queue,4,address(b));IgbOsThreadWakeInvocation pending(missing.queue);
    check(!pending.resume(missing.services).completed && word(missing.queue,0)==address(a) && half(a,0x2c8)==0x9876,
        "Missing next-thread mapping published head/state before the previous-link store.");
    word(a,0x2e0,0xdeadbeef);word(missing.queue,0,0xdeadbeef);missing.bind(b);
    check(pending.resume().completed && word(missing.queue,0)==0 && word(missing.queue,4)==0 &&
        half(a,0x2c8)==1 && half(b,0x2c8)==1 && word(b,0x2e4)==0 && word(a,0x2e0)==0xdeadbeef,
        "Missing-link resume recaptured next/head or failed the following fresh queue-head iteration.");

    Fixture bits;auto x=thread(0x500010,0,0,5),rq=raw(0x80514de0,8);word(rq,4,0);bits.bind(x);bits.bind(rq);word(bits.queue,0,address(x));
    IgbOsThreadWakeInvocation held(bits.queue);check(!held.resume(bits.services).completed && word(rq,0)==address(x) &&
        word(rq,4)==address(x) && word(x,0x2e0)==0 && bits.services->scheduler->run_hint==0 && bits.schedule_polls==0,
        "Unknown runbits did not suspend after queue append and before runhint publication.");
    word(x,0x2d0,0);bits.services->scheduler->run_bits=2;bits.complete_scheduler();
    check(held.resume().completed && bits.services->scheduler->run_bits==0x04000002,
        "Pending runbits recaptured the already loaded final priority.");
}

void fresh_runqueue_members_and_alias_order(){
    Fixture f;auto t=thread(0x200010,0,0,0);auto first=raw(0x600010,8),second=raw(0x700010,8);word(first,4,0);
    f.bind(t);f.bind(first);f.bind(second);word(f.queue,0,address(t));f.services->scheduler->run_bits=0;f.complete_scheduler();
    bool changed_first{},changed_second{};IgbOsThreadWakeInvocation run(f.queue);
    const auto guard=[&]{if(!changed_first && word(t,0x2dc)==0x80514db8){word(t,0x2dc,address(first));changed_first=true;}
        if(changed_first && !changed_second && known(first,0) && word(first,0)==address(t)){
            word(t,0x2dc,address(second));changed_second=true;}};
    check(run.resume(f.services,guard).completed && changed_first && changed_second && word(first,0)==address(t) && word(first,4)==0 &&
        !known(second,0) && word(second,4)==address(t) && word(t,0x2dc)==address(second),
        "Wake reused a prior +2DC value instead of its two original fresh runqueue-member loads.");

    Fixture alias;auto t2=thread(0x210010,0,1,0);IgbBootstrapPointer queue{t2.storage,t2.offset+0x2c8};
    // The state halfword aliases the high half of the input head. Clearing head
    // then storing state1 makes the next fresh head exactly00010000.
    word(queue,0,address(t2));word(queue,4,1);word(t2,0x2e0,0);alias.queue=queue;
    auto alias_runqueue=raw(0x80514db8,8);word(alias_runqueue,4,0);alias.bind(alias_runqueue);alias.services->scheduler->run_bits=0;
    IgbOsThreadWakeInvocation overlap(queue);check(!overlap.resume(alias.services).completed && word(queue,0)==0x00010000 &&
        word(queue,4)==0 && half(t2,0x2c8)==1 && word(alias_runqueue,0)==address(t2) && word(alias_runqueue,4)==address(t2) &&
        alias.services->scheduler->run_bits==0x80000000 && alias.calls.size()==1 && alias.schedule_polls==0,
        "Aliased dequeue/state stores were reordered or the following head was replaced by a cached empty result.");
}

void cyclic_reenqueue_yields_without_fabricated_return(){
    Fixture f;f.queue=raw(0x80514db8,8);auto t=thread(0x200010,0,0,0);f.bind(t);
    word(f.queue,0,address(t));word(f.queue,4,address(t));f.services->scheduler->run_bits=0;
    IgbOsThreadWakeInvocation run(f.queue);check(!run.resume(f.services).completed && !run.faulted() &&
        word(f.queue,0)==address(t) && word(f.queue,4)==address(t) && half(t,0x2c8)==1 && word(t,0x2e4)==0 &&
        f.services->scheduler->run_bits==0x80000000 && f.services->scheduler->run_hint==1 &&
        f.calls.size()==1 && f.irq_polls==1 && f.schedule_polls==0 && f.interrupts->enabled==false,
        "Self-reenqueuing wake hung, faulted, restored exclusion, or fabricated a scheduler/return at its work bound.");
    igb_bootstrap_write_halfword(t,0x2c8,0x9876);word(t,0x2e4,0x76543210);
    check(!run.resume().completed && !run.faulted() && half(t,0x2c8)==1 && word(t,0x2e4)==0 &&
        f.calls.size()==1 && f.irq_polls==1 && f.schedule_polls==0,
        "Resuming a bounded cyclic wake repeated disable or stopped making original dequeue progress.");
    word(t,0x2cc,1);check(!run.resume().completed && word(f.queue,0)==0 && word(f.queue,4)==0 && f.schedule_polls==1,
        "Breaking the captured cycle did not reach the fresh runhint scheduler boundary.");
    f.complete_scheduler();check(run.resume().completed && f.calls.size()==2 && f.interrupts->enabled==true,
        "A bounded wake could not finish after its original queue became empty.");
}

void ordered_unknown_reads_and_atomic_write_faults(){
    for(const unsigned at:{0x2e0u,0x2ccu,0x2d0u}){Fixture f;auto t=thread(0x200010,0,0,0);f.bind(t);word(f.queue,0,address(t));word(f.queue,4,address(t));
        unknown(t,at);Snapshot before(t);IgbOsThreadWakeInvocation run(f.queue);rejects([&]{run.resume(f.services);},"Unknown reached OS thread word was fabricated.");
        check(run.faulted() && f.calls.size()==1 && f.interrupts->enabled==false && f.schedule_polls==0,
            "Unknown thread word did not permanently preserve the disabled prefix.");
        if(at==0x2e0)check(before.same(t) && word(f.queue,0)==address(t) && word(f.queue,4)==address(t),
            "Unknown next pointer changed dequeue or state before its load.");
        else check(word(f.queue,0)==0 && word(f.queue,4)==0 && half(t,0x2c8)==1 && half(t,0x2ca)==0x1234 &&
            word(t,0x2dc)==0xabcdef00,"Unknown suspend/priority word lost the completed dequeue/state prefix.");
        word(t,at,0);rejects([&]{run.resume();},"Unknown-byte failure resumed after its input was repaired.");}

    Fixture next;auto t=thread(0x200010,0x300010,1,0),n=thread(0x300010,0,1,0);next.bind(t);next.bind(n);
    resize(n,0x2e7);Snapshot truncated(n);word(next.queue,0,address(t));word(next.queue,4,address(n));IgbOsThreadWakeInvocation linked(next.queue);
    rejects([&]{linked.resume(next.services);},"Truncated next.prev store was accepted.");check(truncated.same(n) && word(next.queue,0)==address(t) &&
        word(next.queue,4)==address(n) && half(t,0x2c8)==0x9876,"Failed next.prev word partially wrote or published later head/state stores.");

    Fixture state;auto s=thread(0x400010,0,1,0);state.bind(s);word(state.queue,0,address(s));bool shrunk{};
    IgbOsThreadWakeInvocation halfword(state.queue);const auto shrink=[&]{if(!shrunk && word(state.queue,0)==0){resize(s,0x2c9);shrunk=true;}};
    rejects([&]{halfword.resume(state.services,shrink);},"Truncated state halfword was accepted.");
    check(shrunk && word(state.queue,0)==0 && word(state.queue,4)==0 && s.storage->bytes->bytes[s.offset+0x2c8]==0x98 &&
        s.storage->bytes->initialized[s.offset+0x2c8]==1 && state.calls.size()==1,"Failed state STH wrote its first byte or rolled back dequeues.");

    Fixture tail;auto u=thread(0x500010,0,0,0),q=raw(0x80514db8,8);word(q,4,0);tail.bind(u);tail.bind(q);word(tail.queue,0,address(u));
    bool tail_shrunk{};IgbOsThreadWakeInvocation publish(tail.queue);const auto shorten=[&]{if(!tail_shrunk && known(q,0) && word(q,0)==address(u)){
        resize(q,7);tail_shrunk=true;}};
    rejects([&]{publish.resume(tail.services,shorten);},"Truncated runqueue tail publication was accepted.");
    check(tail_shrunk && word(q,0)==address(u) && word(u,0x2e4)==0 && word(u,0x2e0)==0 &&
        q.storage->bytes->bytes[q.offset+4]==0 && q.storage->bytes->bytes[q.offset+5]==0 && q.storage->bytes->bytes[q.offset+6]==0 &&
        !tail.services->scheduler->run_bits && tail.services->scheduler->run_hint==0 && tail.calls.size()==1,
        "Failed tail STW partially wrote or lost earlier append links or reached global publication.");
}

void raw_mapping_prefix_conflict_and_captured_owners(){
    Fixture f;auto t=thread(0x200010,0,0,0);auto runqueue=raw(0x80514db8,8);word(runqueue,4,0);f.bind(t);
    // Logical mapping starts four bytes after its queue; the represented
    // allocation prefix is still the original queue-head address.
    f.bind({runqueue.storage,runqueue.offset+4});word(f.queue,0,address(t));f.services->scheduler->run_bits=0;f.complete_scheduler();
    IgbOsThreadWakeInvocation prefix(f.queue);check(prefix.resume(f.services).completed && word(runqueue,0)==address(t) && word(runqueue,4)==address(t),
        "Raw runqueue lookup excluded represented bytes before the mapping's logical offset.");

    Fixture conflict;auto a=thread(0x300010,0,1,0),duplicate=thread(0x300010,0,1,0);conflict.bind(a);conflict.bind(duplicate);
    word(conflict.queue,0,address(a));Snapshot untouched(a);IgbOsThreadWakeInvocation clash(conflict.queue);
    rejects([&]{clash.resume(conflict.services);},"Conflicting raw thread mappings selected an arbitrary owner.");
    check(clash.faulted() && untouched.same(a) && word(conflict.queue,0)==address(a) && conflict.calls.size()==1,
        "Raw mapping conflict published dequeue/state changes.");

    Fixture retained;auto original=thread(0x400010,0,0,0),replacement=thread(0x400010,0,0,0),rq=raw(0x80514db8,8);
    word(rq,4,0);retained.bind(original);word(retained.queue,0,address(original));IgbOsThreadWakeInvocation selected(retained.queue);
    check(!selected.resume(retained.services).completed && half(original,0x2c8)==1,"Owner retention fixture did not reach missing runqueue.");
    retained.services->os->mappings={replacement,rq};retained.services->scheduler->run_bits=0;retained.complete_scheduler();
    check(selected.resume().completed && word(original,0x2e4)==0 && word(original,0x2e0)==0 && half(replacement,0x2c8)==0x9876 &&
        word(replacement,0x2dc)==0xabcdef00,"Replacing external lookup records redirected an already captured thread owner.");
    for(unsigned failure=0;failure<3;++failure){Fixture fault;auto x=thread(0x500010,0,0,0),q=raw(0x80514db8,8);word(q,4,0);
        fault.bind(x);word(fault.queue,0,address(x));IgbOsThreadWakeInvocation run(fault.queue);check(!run.resume(fault.services).completed,
            "Captured-owner fault fixture did not reach missing runqueue.");const auto owner=x.storage->bytes;const auto guest=x.storage->original_address;
        if(failure==0)x.storage->bytes=bytes(owner->bytes.size());if(failure==1)owner->released=true;if(failure==2)x.storage->original_address+=0x1000;
        fault.bind(q);rejects([&]{run.resume();},"Changed captured thread storage was accepted at the next write.");
        check(run.faulted() && fault.calls.size()==1 && fault.services->scheduler->run_hint==0,
            "Captured-owner fault reached restore/global publication.");
        x.storage->bytes=owner;owner->released=false;x.storage->original_address=guest;rejects([&]{run.resume();},"Captured-owner fault revived after restoration.");}
}

void canonical_cells_reject_only_reached_raw_aliases(){
    // These cells have one canonical optional-word representation. A raw span
    // is not a second value for them, even if its bytes happen to agree today.
    for(const auto cell:{0x80565f10u,0x80565f14u,0x80565f18u,0x800000e4u})for(bool represented:{false,true}){
        Fixture f;auto queue=raw(cell,8);word(queue,0,0);Snapshot before(queue);
        if(represented){f.services->scheduler->run_bits=0;f.services->scheduler->run_hint=0;f.services->scheduler->disable_count=0;
            f.services->os->current_thread_address=0;}else f.services->scheduler->run_hint.reset();
        IgbOsThreadWakeInvocation run(queue);rejects_canonical_alias([&]{run.resume(f.services);},
            "Raw canonical-global head read lacked an explicit representation-overlap diagnostic.");
        check(run.faulted() && before.same(queue) && f.calls.size()==1 && f.interrupts->enabled==false && f.schedule_polls==0 &&
            bool(f.services->scheduler->run_bits)==represented && bool(f.services->scheduler->run_hint)==represented &&
            bool(f.services->scheduler->disable_count)==represented && bool(f.services->os->current_thread_address)==represented,
            "Raw global conflict changed canonical knownness, raw bytes, or the completed disable prefix.");
        rejects([&]{run.resume();},"Raw canonical-cell representation fault was resumable.");
    }
    struct Case{std::uint32_t priority,queue;};
    for(const auto c:std::array<Case,2>{{{0xa22b,0x80565f10},{0xa22c,0x80565f18}}}){
        Fixture f;auto t=thread(0x200010,0,0,c.priority);auto queue=raw(c.queue,8);word(queue,4,0);
        word(f.queue,0,address(t));word(f.queue,4,address(t));f.bind(t);f.bind(queue);Snapshot raw_before(queue);
        f.services->scheduler->run_bits=0x12345678;f.services->scheduler->disable_count=7;IgbOsThreadWakeInvocation run(f.queue);
        rejects_canonical_alias([&]{run.resume(f.services);},"Malformed priority created an independent raw copy of canonical scheduler globals.");
        check(run.faulted() && word(f.queue,0)==0 && word(f.queue,4)==0 && half(t,0x2c8)==1 && word(t,0x2dc)==c.queue &&
            word(t,0x2e4)==0x76543210 && raw_before.same(queue) && f.services->scheduler->run_bits==0x12345678 &&
            f.services->scheduler->run_hint==0 && f.services->scheduler->disable_count==7 && f.calls.size()==1,
            "Canonical alias was rejected before dequeue/state/+2DC or allowed later append/global/restore effects.");
    }

    Fixture ordered;auto t=thread(0x300010,0,0,0xa22c);auto queue=raw(0x80565f18,8);ordered.bind(t);ordered.bind(queue);
    word(ordered.queue,0,address(t));IgbOsThreadWakeInvocation before_store(ordered.queue);bool unknown_tail{};
    try{before_store.resume(ordered.services);}catch(const std::runtime_error& error){unknown_tail=std::string(error.what()).find("canonical")==std::string::npos;}
    check(unknown_tail && before_store.faulted() && word(t,0x2dc)==0x80565f18 && !known(queue,0) && !known(queue,4),
        "An unaccessed conflicting runqueue head was rejected before the original unknown nonoverlapping tail read.");

    for(const auto cell:{0x80565f10u,0x80565f18u,0x800000e4u}){
        Fixture f;auto t=thread(cell-0x2c8u,0,1,0);f.bind(t);word(f.queue,0,address(t));word(f.queue,4,address(t));Snapshot before(t);
        IgbOsThreadWakeInvocation run(f.queue);rejects_canonical_alias([&]{run.resume(f.services);},
            "State halfword overlapping part of a canonical word was accepted.");
        check(run.faulted() && word(f.queue,0)==0 && word(f.queue,4)==0 && before.same(t) && half(t,0x2c8)==0x9876 &&
            f.calls.size()==1 && f.interrupts->enabled==false,
            "Conflicting halfword wrote one byte or discarded the preceding dequeue word stores.");
    }
    for(const auto cell:{0x80565f10u,0x800000e4u}){
        Fixture f;auto queue=raw(cell-4,8);word(queue,0,0);Snapshot before(queue);
        f.bind(raw(cell,16));f.services->os->current_thread_address=0x12345678;f.services->scheduler->run_bits=0x87654321;
        IgbOsThreadWakeInvocation run(queue);check(run.resume(f.services).completed && before.same(queue) &&
            f.services->os->current_thread_address==0x12345678 && f.services->scheduler->run_bits==0x87654321 &&
            f.calls.size()==2 && f.interrupts->enabled==true,
            "Unused raw mapping/tail overlap eagerly rejected the original empty-head path.");
    }
}

void callback_failures_and_caught_reentry(){
    for(unsigned where=0;where<3;++where){Fixture f;f.services->scheduler->run_hint=1;IgbOsThreadWakeInvocation run(f.queue);
        const auto recurse=[&]{try{run.resume(f.services);}catch(const std::runtime_error&){};};
        if(where==0)f.interrupt_after=[&](const IgbInterruptCall& call){if(call.kind==IK::disable)recurse();};
        if(where==1)f.schedule=[&](const std::function<void()>&){recurse();return MessengerCallResult{true,0,{}};};
        if(where==2)f.complete_scheduler();
        bool called{};const auto guard=[&]{if(where==2 && !called && f.interrupts->enabled==false){called=true;recurse();}};
        rejects([&]{run.resume(f.services,guard);},"Caught recursive wake completed the outer operation.");
        check(run.faulted() && f.calls.size()==1 && f.interrupts->enabled==false && (where!=2 || called),
            "Caught callback/enclosing recursion allowed a later native restore.");
        rejects([&]{run.resume();},"Recursive wake failure did not remain permanent.");}
    Fixture restore;restore.interrupt_before=[](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{
        if(call.kind==IK::restore)throw std::runtime_error("Prepared restore failure.");return {};};
    IgbOsThreadWakeInvocation run(restore.queue);rejects([&]{run.resume(restore.services);},"Restore exception was swallowed.");
    check(run.faulted() && restore.interrupts->enabled==false && restore.calls.size()==2,"Restore failure fabricated enabled state or completion.");
    for(unsigned invalid=0;invalid<3;++invalid){Fixture f;f.services->scheduler->run_hint=1;const auto owner=f.queue.storage->bytes;
        const auto original_address=f.queue.storage->original_address;IgbOsThreadWakeInvocation wake(f.queue);
        f.schedule=[&](const std::function<void()>&){if(invalid==0)f.queue.storage->bytes=bytes(owner->bytes.size());
            if(invalid==1)owner->released=true;if(invalid==2)f.queue.storage->original_address+=0x1000;return MessengerCallResult{true,0,{}};};
        rejects([&]{wake.resume(f.services);},"Scheduler replaced or released its captured queue allocation without faulting.");
        check(wake.faulted() && f.calls.size()==1 && f.interrupts->enabled==false,"Invalid queue identity returned through scheduler to restore.");
        f.queue.storage->bytes=owner;owner->released=false;f.queue.storage->original_address=original_address;
        rejects([&]{wake.resume();},"Queue identity fault resumed after external repair.");}
}

IgbObjectInstance runtime_object(IgbRuntimeObjectKind kind,std::uint32_t table,std::uint32_t guest,unsigned size){
    auto p=raw(guest,size);IgbObjectInstance out;out.storage=p.storage;out.offset=p.offset;out.runtime_object=std::make_shared<IgbRuntimeObject>(kind);
    out.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{out.storage,out.offset};IgbObjectBinding binding;binding.offset=out.offset;
    binding.runtime_object=out.runtime_object;out.storage->bindings.push_back(binding);word(p,0,table);igb_object_set_reference_word(out,0xa5000002);return out;}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}

void actual_signal_nested_exclusion_and_parent_fault(){
    for(bool enabled:{false,true}){Fixture f;f.interrupts->enabled=enabled;auto semaphore=runtime_object(IgbRuntimeObjectKind::semaphore,0x80470314,0x300010,0x28);
        const auto sem=pointer(semaphore);igb_bootstrap_write_byte(sem,0x14,1);word(sem,0x10,3);word(sem,0x18,1);word(sem,0x1c,0);
        auto services=std::make_shared<IgbThreadManagerInsertServices>();services->memory=std::make_shared<IgbMemoryFieldConstructServices>();
        services->memory->status_success=0x12345678;services->interrupts=f.services->interrupts;services->factory=make_native_igb_os_thread_wake_factory(f.services);
        unsigned disables{};f.interrupt_before=[&](const IgbInterruptCall& call)->std::optional<MessengerCallResult>{if(call.kind==IK::disable){++disables;
            if(disables==2)check(word(sem,0x18)==2,"Nested wake disabled interrupts before the outer signal increment.");}return {};};
        f.services->scheduler->run_hint.reset();auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(semaphore,0x7c,result);
        check(!run.resume(services).completed && word(sem,0x18)==2 && f.calls.size()==2 && !result->initialized[0] && f.interrupts->enabled==false,
            "Unknown inner run hint lost the outer count/two-disable prefix or published status.");
        f.services->scheduler->run_hint=0;check(run.resume().completed && f.calls.size()==4 && f.calls[2].kind==IK::restore &&
            f.calls[2].restore_enabled==0 && f.calls[3].kind==IK::restore && f.calls[3].restore_enabled==unsigned(enabled) &&
            f.interrupts->enabled==enabled && igb_buffer_word(result,0)==0x12345678,"Nested wake/signal did not restore inner0 before the captured outer bit.");}

    Fixture f;auto semaphore=runtime_object(IgbRuntimeObjectKind::semaphore,0x80470314,0x300010,0x28);const auto sem=pointer(semaphore);
    igb_bootstrap_write_byte(sem,0x14,1);word(sem,0x10,2);word(sem,0x18,0);word(sem,0x1c,0);f.services->scheduler->run_hint=1;
    auto services=std::make_shared<IgbThreadManagerInsertServices>();services->memory=std::make_shared<IgbMemoryFieldConstructServices>();
    services->memory->status_success=7;services->interrupts=f.services->interrupts;services->factory=make_native_igb_os_thread_wake_factory(f.services);
    auto result=bytes(4);IgbSemaphoreRuntimeAccessInvocation run(semaphore,0x7c,result);
    f.schedule=[&](const std::function<void()>& guard){try{run.resume(services);}catch(const std::runtime_error&){};
        if(guard){try{guard();}catch(const std::runtime_error&){};}return MessengerCallResult{true,0,{}};};
    rejects([&]{run.resume(services);},"Caught parent signal recursion returned through the inner scheduler.");
    check(run.faulted() && word(sem,0x18)==1 && f.calls.size()==2 && f.interrupts->enabled==false && !result->initialized[0],
        "Caught parent fault permitted inner/outer restores or hidden-status publication.");
}

void actual_manager_to_wake_caught_parent_fault(){
    Fixture f;f.services->scheduler->run_hint=1;
    auto manager=runtime_object(IgbRuntimeObjectKind::thread_manager,0x804700a8,0x100010,0x10);
    auto incoming=runtime_object(IgbRuntimeObjectKind::thread,0x80470110,0x200010,0x388);
    auto semaphore=runtime_object(IgbRuntimeObjectKind::semaphore,0x80470314,0x300010,0x28);
    auto list=runtime_object(IgbRuntimeObjectKind::thread_list,0x80476ce0,0x400010,0x14);auto backing=raw(0x500010,8);
    const auto sem=pointer(semaphore);word(pointer(manager),8,address(pointer(list)));word(pointer(manager),0x0c,address(sem));
    word(pointer(list),8,0);word(pointer(list),0x0c,2);word(pointer(list),0x10,address(backing));
    igb_bootstrap_write_byte(sem,0x14,1);word(sem,0x10,1);word(sem,0x18,1);word(sem,0x1c,0);
    auto services=std::make_shared<IgbThreadManagerInsertServices>();services->runtime=std::make_shared<IgbThreadRuntimeInitServices>();
    services->runtime->objects=std::make_shared<IgbObjectFieldConstructServices>();services->runtime->objects->objects={manager,incoming,semaphore,list};
    services->memory=std::make_shared<IgbMemoryFieldConstructServices>();services->memory->status_success=0x12345678;
    IgbPointerListBacking mapped;mapped.bytes=backing.storage->bytes;mapped.address=address(backing);mapped.offset=backing.offset;
    igb_bind_memory_field_allocation(services->memory,mapped);services->interrupts=f.services->interrupts;
    services->factory=make_native_igb_os_thread_wake_factory(f.services);
    services->runtime->factory=make_native_igb_semaphore_runtime_access_factory(services);
    IgbThreadManagerInsertInvocation run(pointer(manager),pointer(incoming));
    f.schedule=[&](const std::function<void()>& guard){try{run.resume(services);}catch(const std::runtime_error&){};
        if(guard){try{guard();}catch(const std::runtime_error&){};}return MessengerCallResult{true,0,{}};};
    rejects([&]{run.resume(services);},"Caught manager recursion returned through native signal/wake to restore.");
    check(run.faulted() && !run.completed() && word(pointer(list),8)==1 && word(backing,0)==address(pointer(incoming)) &&
        word(pointer(incoming),4)==0xa5000003 && word(sem,0x18)==1 && f.calls.size()==4 &&
        f.calls[0].kind==IK::disable && f.calls[1].kind==IK::restore && f.calls[2].kind==IK::disable &&
        f.calls[3].kind==IK::disable && f.interrupts->enabled==false && f.schedule_polls==1,
        "Manager fault lost its completed retain/list/count prefix or allowed later inner/outer restoration.");
    rejects([&]{run.resume();},"Faulted native manager/wake chain resumed after caught recursion.");
}

void adapter_limits(){
    Fixture f;unsigned delegated{};auto adapter=make_native_igb_os_thread_wake_factory(f.services,[&](const IgbThreadManagerInsertCall&){++delegated;
        return std::shared_ptr<IgbThreadManagerInsertOperation>{};});IgbThreadManagerInsertCall call;call.kind=LK::wake_thread;call.queue=f.queue;
    const auto operation=adapter(call);check(operation && operation->resume_checked && operation->resume_checked({}).completed && delegated==0,
        "Wake adapter did not admit the exact raw wake request with a checked continuation.");
    call.kind=LK::sleep_thread;check(!adapter(call) && delegated==1,"Wake adapter fabricated sleep completion.");
    call.kind=LK::grow_list;check(!adapter(call) && delegated==2,"Wake adapter intercepted data-list growth.");
}
}
int main(){try{
    empty_queue_exclusion_and_unused_state();nonzero_hint_retains_scheduler_and_captured_bit();disable_precedes_reached_queue_validation();
    fifo_dequeue_runqueue_append_and_signed_suspension();priority_wrap_and_original_slw();late_mappings_and_fresh_priority();
    fresh_runqueue_members_and_alias_order();cyclic_reenqueue_yields_without_fabricated_return();ordered_unknown_reads_and_atomic_write_faults();
    raw_mapping_prefix_conflict_and_captured_owners();canonical_cells_reject_only_reached_raw_aliases();callback_failures_and_caught_reentry();actual_signal_nested_exclusion_and_parent_fault();
    actual_manager_to_wake_caught_parent_fault();adapter_limits();
    std::cout<<"OS thread wake checks: "<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"OS thread wake failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
