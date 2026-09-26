#include "dw4/igb_semaphore_runtime_init.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbSemaphoreRuntimeInitError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& flag):value(flag){value=true;}~Running(){value=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
}

IgbInterruptFactory make_native_igb_interrupt_factory(std::shared_ptr<IgbExecutionInterruptState> state){
    return [weak=std::weak_ptr(state)](const IgbInterruptCall& call){
        auto operation=std::make_shared<IgbInterruptOperation>();
        operation->resume=[selected=weak.lock(),call,done=false,previous=std::uint32_t{}]()mutable{
            if(done)return MessengerCallResult{true,previous,{}};
            if(!selected || !selected->enabled)return pending("Interrupt operation requires explicit original execution-exclusion state.");
            previous=*selected->enabled?1u:0u;
            *selected->enabled=call.kind==IgbInterruptServiceKind::restore && call.restore_enabled!=0;
            done=true;return MessengerCallResult{true,previous,{}};
        };return operation;
    };
}

struct IgbSemaphoreRuntimeInitInvocation::Impl {
    enum class P{count,disable,queue_tail,queue_head,count_store,restore,active,status,result,done};P phase=P::count;
    IgbObjectInstance object;
    std::shared_ptr<IgbScratchBuffer> bytes,result;
    std::shared_ptr<IgbSemaphoreRuntimeInitServices> owner;
    std::uint32_t receiver_address{},count{},previous_enabled{},status{};
    IgbInterruptCall request;IgbInterruptFactory factory;
    std::shared_ptr<IgbInterruptOperation> operation;std::function<MessengerCallResult()> callback;
    bool selected{},running{},fault{};
    Impl(IgbObjectInstance value,std::shared_ptr<IgbScratchBuffer> output):object(std::move(value)),
        bytes(object.storage?object.storage->bytes:nullptr),result(std::move(output)),receiver_address(address(object)){}
};
IgbSemaphoreRuntimeInitInvocation::IgbSemaphoreRuntimeInitInvocation(IgbObjectInstance object,std::shared_ptr<IgbScratchBuffer> result)
    :impl_(std::make_unique<Impl>(std::move(object),std::move(result))){}
IgbSemaphoreRuntimeInitInvocation::~IgbSemaphoreRuntimeInitInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.object.storage)s.owner->retained.push_back(s.object.storage);
    if(s.bytes)s.owner->retained.push_back(s.bytes);
    if(s.result)s.owner->retained.push_back(s.result);
    if(s.operation)s.owner->retained.push_back(s.operation);
}}
bool IgbSemaphoreRuntimeInitInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbSemaphoreRuntimeInitInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbSemaphoreRuntimeInitInvocation::resume(std::shared_ptr<IgbSemaphoreRuntimeInitServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Semaphore initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{
        if(s.fault)fail("Semaphore initialization was invalidated by a callback.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Semaphore initialization was invalidated by its enclosing callback.");
    };
    const auto physical=[&]{
        check();igb_object_validate_instance(s.object);
        if(!s.object.runtime_object || s.object.runtime_object->kind!=IgbRuntimeObjectKind::semaphore ||
            !s.object.storage->bytes || s.object.storage->bytes->released || s.object.storage->bytes!=s.bytes ||
            address(s.object)!=s.receiver_address)
            fail("Semaphore initialization changed or lost its captured physical runtime identity.");
        return IgbBootstrapPointer{s.object.storage,s.object.offset};
    };
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.selected=false;check();};
    const auto interrupt=[&](IgbInterruptServiceKind kind,std::uint32_t restore){
        physical();
        if(!s.selected){s.request={kind,restore};if(services)s.factory=services->interrupts;s.selected=true;}
        if(!s.factory && services)s.factory=services->interrupts;
        if(!s.factory)return pending("Semaphore initialization requires its selected interrupt service.");
        if(!s.operation){s.operation=s.factory(s.request);check();physical();}
        if(!s.operation)return pending("Semaphore initialization reached an unbound interrupt service.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Semaphore initialization requires its retained interrupt continuation.");
        const auto result=s.callback();check();physical();return result;
    };
    try{for(;;){check();switch(s.phase){
    case P::count:s.count=igb_bootstrap_read_word(physical(),0x0c);s.phase=P::disable;break;
    case P::disable:{const auto result=interrupt(IgbInterruptServiceKind::disable,0);if(!result.completed)return result;
        s.previous_enabled=result.value;clear();s.phase=P::queue_tail;break;}
    case P::queue_tail:igb_bootstrap_write_word(physical(),0x20,0);s.phase=P::queue_head;break;
    case P::queue_head:igb_bootstrap_write_word(physical(),0x1c,0);s.phase=P::count_store;break;
    case P::count_store:igb_bootstrap_write_word(physical(),0x18,s.count);s.phase=P::restore;break;
    case P::restore:{const auto result=interrupt(IgbInterruptServiceKind::restore,s.previous_enabled);if(!result.completed)return result;
        clear();s.phase=P::active;break;}
    case P::active:igb_bootstrap_write_byte(physical(),0x14,1);s.phase=P::status;break;
    case P::status:
        if(!services || !services->status_success || !*services->status_success)
            return pending("Semaphore initialization requires the fresh canonical success status805622D0.");
        s.status=**services->status_success;s.phase=P::result;break;
    case P::result:
        if(!s.result || s.result->released || s.result->bytes.size()<4 || s.result->initialized.size()<4)
            fail("Semaphore initialization writes an unavailable hidden result destination.");
        for(unsigned i=0;i<4;++i){s.result->bytes[i]=static_cast<std::uint8_t>(s.status>>(24u-8u*i));s.result->initialized[i]=1;}
        s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbThreadRuntimeInitFactory make_native_igb_semaphore_runtime_init_factory(
    std::shared_ptr<IgbSemaphoreRuntimeInitServices> services,IgbThreadRuntimeInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadRuntimeInitCall& call)
        ->std::shared_ptr<IgbThreadRuntimeInitOperation>{
        if(call.vtable_word!=0x80470314 || call.virtual_slot!=0x5c)return remaining?remaining(call):nullptr;
        auto invocation=std::make_shared<IgbSemaphoreRuntimeInitInvocation>(call.object,call.result);
        auto operation=std::make_shared<IgbThreadRuntimeInitOperation>();const auto selected=weak.lock();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};
        operation->resume_checked=[invocation,selected](const std::function<void()>& enclosing_check){
            return invocation->resume(selected,enclosing_check);
        };return operation;
    };
}
} // namespace dw4
