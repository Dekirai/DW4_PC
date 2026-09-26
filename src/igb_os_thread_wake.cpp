#include "dw4/igb_os_thread_wake.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* message){throw IgbOsThreadWakeError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
struct RawAccess {
    IgbBootstrapPointer captured;std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t allocation_address{},address{};
    explicit RawAccess(IgbBootstrapPointer value):captured(std::move(value)),
        bytes(captured.storage?captured.storage->bytes:nullptr),
        allocation_address(captured.storage?captured.storage->original_address:0),address(igb_bootstrap_address(captured)){}
    const IgbBootstrapPointer& pointer()const{
        if(!captured.storage || !address || !bytes || bytes->released || captured.storage->faulted() ||
            captured.storage->bytes!=bytes || captured.storage->original_address!=allocation_address)
            fail("OS wake lost its captured physical allocation.");
        return captured;
    }
    const IgbBootstrapPointer& access(std::uint32_t relative,unsigned width)const{
        pointer();
        // These words have canonical native owners, not raw byte aliases.
        // Reject only an actually reached overlapping access; validating the
        // entire allocation or clamping priority would lose earlier stores.
        for(unsigned i=0;i<width;++i){
            const auto byte=address+relative+i;
            if((byte>=0x80565f10u && byte<0x80565f1cu) ||
                (byte>=0x800000e4u && byte<0x800000e8u))
                fail("OS wake raw access conflicts with a canonical OS word representation.");
        }
        return captured;
    }
};
std::optional<IgbBootstrapPointer> resolve(const std::shared_ptr<IgbOsThreadWakeServices>& services,
    const RawAccess& argument,std::uint32_t address){
    if(!address)return IgbBootstrapPointer{};
    std::optional<IgbBootstrapPointer> result;
    const auto visit=[&](const IgbBootstrapPointer& mapping){
        if(!mapping.storage)return;
        const auto bytes=mapping.storage->bytes;const auto offset=address-mapping.storage->original_address;
        if((!bytes || offset>=bytes->bytes.size()) && address!=igb_bootstrap_address(mapping))return;
        IgbBootstrapPointer selected{mapping.storage,offset};
        if(result && (result->storage!=selected.storage || result->offset!=selected.offset))
            fail("OS wake pointer has conflicting raw allocation mappings.");
        result=selected;
    };
    // The argument already supplies a represented allocation; permit original
    // aliases into that owner without requiring a duplicate registration.
    visit(argument.captured);
    if(services && services->os)for(const auto& mapping:services->os->mappings)visit(mapping);
    return result;
}
struct InterruptCall {
    IgbInterruptCall request;IgbInterruptFactory factory;
    std::shared_ptr<IgbInterruptOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbOsThreadWakeServices>& services,const Check& check){
        check();if(!factory && services)factory=services->interrupts;
        if(!factory)return pending("OS wake requires its selected interrupt-exclusion service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("OS wake reached an unbound interrupt-exclusion operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("OS wake requires its retained interrupt continuation.");
        const auto result=callback();check();return result;
    }
};
struct ScheduleCall {
    IgbOsThreadWakeCall request;IgbOsThreadWakeFactory factory;
    std::shared_ptr<IgbOsThreadWakeOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbOsThreadWakeServices>& services,const Check& check){
        check();if(!factory && services)factory=services->factory;
        if(!factory)return pending("OS wake requires original scheduler8026140C with argument zero.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("OS wake reached unbound original scheduling8026140C.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("OS wake requires its retained scheduler continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
}
struct IgbOsThreadWakeInvocation::Impl {
    enum class P{select_disable,disable,head,thread,next,next_thread,clear_previous,clear_tail,publish_head,state,suspend,
        priority,publish_queue,first_queue,resolve_first_queue,tail,tail_thread,append_head,append_tail_next,
        previous,clear_next,second_queue,resolve_second_queue,publish_tail,priority_again,run_bits,store_bits,store_hint,
        hint,select_schedule,schedule,select_restore,restore,done};P phase=P::select_disable;
    RawAccess queue;std::unique_ptr<RawAccess> thread,linked,run_queue;
    std::shared_ptr<IgbOsThreadWakeServices> owner;
    InterruptCall interrupt;ScheduleCall schedule;
    std::uint32_t saved_interrupt{},head{},next{},priority{},queue_address{},tail{},run_bits{},hint{};
    bool queue_accessed{},running{},fault{};
    explicit Impl(IgbBootstrapPointer pointer):queue(std::move(pointer)){}
};
IgbOsThreadWakeInvocation::IgbOsThreadWakeInvocation(IgbBootstrapPointer queue):impl_(std::make_unique<Impl>(std::move(queue))){}
IgbOsThreadWakeInvocation::~IgbOsThreadWakeInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    const auto retain=[&](const RawAccess& raw){if(raw.captured.storage)s.owner->retained.push_back(raw.captured.storage);
        if(raw.bytes)s.owner->retained.push_back(raw.bytes);};
    retain(s.queue);if(s.thread)retain(*s.thread);if(s.linked)retain(*s.linked);if(s.run_queue)retain(*s.run_queue);
}}
bool IgbOsThreadWakeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbOsThreadWakeInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbOsThreadWakeInvocation::resume(std::shared_ptr<IgbOsThreadWakeServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("OS wake requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    unsigned completed_dequeues{};
    const auto check=[&]{if(s.fault)fail("OS wake was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("OS wake was invalidated by its enclosing callback.");};
    const auto callback_check=[&]{check();if(s.queue_accessed)s.queue.pointer();
        if(s.thread)s.thread->pointer();if(s.linked)s.linked->pointer();if(s.run_queue)s.run_queue->pointer();};
    const auto capture=[&](std::unique_ptr<RawAccess>& target,std::uint32_t address)->bool{
        const auto pointer=resolve(services,s.queue,address);if(!pointer)return false;
        target=std::make_unique<RawAccess>(*pointer);return true;
    };
    try{for(;;){check();switch(s.phase){
    case P::select_disable:s.interrupt.request.kind=IgbInterruptServiceKind::disable;
        if(services)s.interrupt.factory=services->interrupts;s.phase=P::disable;break;
    case P::disable:{const auto result=s.interrupt.poll(services,callback_check);if(!result.completed)return result;
        s.saved_interrupt=result.value;s.interrupt.clear();callback_check();s.phase=P::head;break;}
    case P::head:
        if(completed_dequeues==256)return pending("OS wake yielded at its original loop head after a bounded dequeue batch.");
        s.head=igb_bootstrap_read_word(s.queue.access(0,4),0);s.queue_accessed=true;
        s.thread.reset();s.linked.reset();s.run_queue.reset();s.phase=s.head?P::thread:P::hint;break;
    case P::thread:
        if(!capture(s.thread,s.head))return pending("OS wake requires its captured queue-head thread allocation.");
        s.phase=P::next;break;
    case P::next:s.next=igb_bootstrap_read_word(s.thread->access(0x2e0,4),0x2e0);s.phase=s.next?P::next_thread:P::clear_tail;break;
    case P::next_thread:
        if(!capture(s.linked,s.next))return pending("OS wake requires its captured next-thread allocation.");
        s.phase=P::clear_previous;break;
    case P::clear_previous:igb_bootstrap_write_word(s.linked->access(0x2e4,4),0x2e4,0);s.linked.reset();s.phase=P::publish_head;break;
    case P::clear_tail:igb_bootstrap_write_word(s.queue.access(4,4),4,0);s.phase=P::publish_head;break;
    case P::publish_head:igb_bootstrap_write_word(s.queue.access(0,4),0,s.next);s.phase=P::state;break;
    case P::state:igb_bootstrap_write_halfword(s.thread->access(0x2c8,2),0x2c8,1);s.phase=P::suspend;break;
    case P::suspend:
        if(std::bit_cast<std::int32_t>(igb_bootstrap_read_word(s.thread->access(0x2cc,4),0x2cc))>0){++completed_dequeues;s.phase=P::head;}
        else s.phase=P::priority;break;
    case P::priority:s.priority=igb_bootstrap_read_word(s.thread->access(0x2d0,4),0x2d0);
        s.queue_address=0x80514db8u+(s.priority<<3u);s.phase=P::publish_queue;break;
    case P::publish_queue:igb_bootstrap_write_word(s.thread->access(0x2dc,4),0x2dc,s.queue_address);s.phase=P::first_queue;break;
    case P::first_queue:s.queue_address=igb_bootstrap_read_word(s.thread->access(0x2dc,4),0x2dc);s.phase=P::resolve_first_queue;break;
    case P::resolve_first_queue:
        if(!capture(s.run_queue,s.queue_address))return pending("OS wake requires its freshly selected runqueue allocation.");
        s.phase=P::tail;break;
    case P::tail:s.tail=igb_bootstrap_read_word(s.run_queue->access(4,4),4);s.phase=s.tail?P::tail_thread:P::append_head;break;
    case P::tail_thread:
        if(!capture(s.linked,s.tail))return pending("OS wake requires its captured runqueue tail-thread allocation.");
        s.phase=P::append_tail_next;break;
    case P::append_head:igb_bootstrap_write_word(s.run_queue->access(0,4),0,s.head);s.phase=P::previous;break;
    case P::append_tail_next:igb_bootstrap_write_word(s.linked->access(0x2e0,4),0x2e0,s.head);s.linked.reset();s.phase=P::previous;break;
    case P::previous:igb_bootstrap_write_word(s.thread->access(0x2e4,4),0x2e4,s.tail);s.phase=P::clear_next;break;
    case P::clear_next:igb_bootstrap_write_word(s.thread->access(0x2e0,4),0x2e0,0);s.phase=P::second_queue;break;
    case P::second_queue:s.queue_address=igb_bootstrap_read_word(s.thread->access(0x2dc,4),0x2dc);s.phase=P::resolve_second_queue;break;
    case P::resolve_second_queue:
        if(!capture(s.run_queue,s.queue_address))return pending("OS wake requires its reloaded runqueue tail destination.");
        s.phase=P::publish_tail;break;
    case P::publish_tail:igb_bootstrap_write_word(s.run_queue->access(4,4),4,s.head);s.phase=P::priority_again;break;
    case P::priority_again:s.priority=igb_bootstrap_read_word(s.thread->access(0x2d0,4),0x2d0);s.phase=P::run_bits;break;
    case P::run_bits:
        if(!services || !services->scheduler || !services->scheduler->run_bits)
            return pending("OS wake requires fresh canonical runbits80565F10 after enqueue.");
        s.run_bits=*services->scheduler->run_bits;s.phase=P::store_bits;break;
    case P::store_bits:{
        if(!services || !services->scheduler)return pending("OS wake requires canonical runbits storage for publication.");
        const auto shift=(31u-s.priority)&63u;
        services->scheduler->run_bits=s.run_bits|(shift<32u?(1u<<shift):0u);s.phase=P::store_hint;break;}
    case P::store_hint:
        if(!services || !services->scheduler)return pending("OS wake requires canonical runhint storage for publication.");
        services->scheduler->run_hint=1;++completed_dequeues;s.phase=P::head;break;
    case P::hint:
        if(!services || !services->scheduler || !services->scheduler->run_hint)
            return pending("OS wake requires fresh canonical runhint80565F14 after the queue became empty.");
        s.hint=*services->scheduler->run_hint;s.phase=s.hint?P::select_schedule:P::select_restore;break;
    case P::select_schedule:s.schedule.request.argument=0;if(services)s.schedule.factory=services->factory;s.phase=P::schedule;break;
    case P::schedule:{const auto result=s.schedule.poll(services,callback_check);if(!result.completed)return result;
        s.schedule.clear();callback_check();s.phase=P::select_restore;break;}
    case P::select_restore:s.interrupt.request.kind=IgbInterruptServiceKind::restore;s.interrupt.request.restore_enabled=s.saved_interrupt;
        if(services)s.interrupt.factory=services->interrupts;s.phase=P::restore;break;
    case P::restore:{const auto result=s.interrupt.poll(services,callback_check);if(!result.completed)return result;
        s.interrupt.clear();callback_check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbThreadManagerInsertFactory make_native_igb_os_thread_wake_factory(std::shared_ptr<IgbOsThreadWakeServices> services,
    IgbThreadManagerInsertFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadManagerInsertCall& call)->std::shared_ptr<IgbThreadManagerInsertOperation>{
        if(call.kind!=IgbThreadManagerInsertServiceKind::wake_thread)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbOsThreadWakeInvocation>(call.queue);
        auto operation=std::make_shared<IgbThreadManagerInsertOperation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};return operation;
    };
}
} // namespace dw4
