#include "dw4/igb_thread_manager_insert.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* text){throw IgbThreadManagerInsertError(text);}
MessengerCallResult pending(const char* text){return {false,0,text};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
struct RawPhysical {
    IgbBootstrapPointer pointer;std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t guest{};
    explicit RawPhysical(IgbBootstrapPointer value):pointer(std::move(value)),
        bytes(pointer.storage?pointer.storage->bytes:nullptr),guest(igb_bootstrap_address(pointer)){}
    void validate()const{
        if(!pointer.storage || !guest || !bytes || bytes->released || pointer.storage->faulted() ||
            pointer.storage->bytes!=bytes || igb_bootstrap_address(pointer)!=guest)
            fail("Thread manager operation lost its captured physical allocation.");
    }
};
struct RuntimePhysical:RawPhysical {
    IgbObjectInstance instance;IgbRuntimeObjectKind kind;bool bound{};
    RuntimePhysical(IgbBootstrapPointer value,IgbRuntimeObjectKind expected):RawPhysical(std::move(value)),kind(expected){}
    RuntimePhysical(IgbObjectInstance value,IgbRuntimeObjectKind expected):RawPhysical({value.storage,value.offset}),
        instance(std::move(value)),kind(expected),bound(true){}
    const IgbBootstrapPointer& access(){
        validate();if(!bound){instance.storage=pointer.storage;instance.offset=pointer.offset;
            instance.runtime_object=igb_bootstrap_runtime_object(pointer);bound=true;}
        igb_object_validate_instance(instance);
        if(!instance.runtime_object || instance.runtime_object->kind!=kind)
            fail("Thread manager operation requires its captured runtime object kind.");
        return pointer;
    }
};
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.file==b.file && a.field==b.field && a.metadata==b.metadata && a.fields==b.fields && a.types==b.types && a.runtime_object==b.runtime_object;}
std::optional<IgbObjectInstance> resolve_object(const std::shared_ptr<IgbThreadManagerInsertServices>& services,std::uint32_t guest){
    if(!guest)return IgbObjectInstance{};std::optional<IgbObjectInstance> result;
    if(services && services->runtime && services->runtime->objects)for(const auto& object:services->runtime->objects->objects){
        if(address(object)!=guest)continue;
        if(result && !same(*result,object))fail("Thread manager pointer has conflicting runtime object mappings.");result=object;
    }return result;
}
struct ElementPhysical {
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t guest{},offset{};
    void store(std::uint32_t value)const{
        // Validate the complete original stwx before publishing any byte.
        if(!guest || !bytes || bytes->released || offset>bytes->bytes.size() ||
            4>bytes->bytes.size()-offset || offset>bytes->initialized.size() ||
            4>bytes->initialized.size()-offset)
            fail("Thread-list element store reaches unavailable physical storage.");
        for(unsigned i=0;i<4;++i){bytes->bytes[offset+i]=static_cast<std::uint8_t>(value>>(24u-i*8u));
            bytes->initialized[offset+i]=1;}
    }
};
std::optional<ElementPhysical> resolve_element(const std::shared_ptr<IgbThreadManagerInsertServices>& services,std::uint32_t guest){
    if(!guest)return ElementPhysical{};std::optional<ElementPhysical> result;
    if(services && services->memory)for(const auto& mapping:services->memory->allocations){
        // address denotes bytes[offset]. A negative index may reach the
        // represented prefix before that logical pointer, with PPC32 wrap.
        if(!mapping.bytes)continue;
        const auto offset=guest-(mapping.address-mapping.offset);
        if(offset>=mapping.bytes->bytes.size() && guest!=mapping.address)continue;
        ElementPhysical selected{mapping.bytes,guest,offset};
        if(result && (result->bytes!=selected.bytes || result->offset!=selected.offset))
            fail("Thread-list element has conflicting physical backing mappings.");
        result=std::move(selected);
    }return result;
}
std::shared_ptr<IgbScratchBuffer> hidden_result(){auto result=std::make_shared<IgbScratchBuffer>();
    result->bytes.assign(4,0xcc);result->initialized.assign(4,0);return result;}
void store_result(const std::shared_ptr<IgbScratchBuffer>& result,std::uint32_t value){
    if(!result || result->released || result->bytes.size()<4 || result->initialized.size()<4)
        fail("Semaphore access reaches an unavailable hidden result word.");
    for(unsigned i=0;i<4;++i){result->bytes[i]=static_cast<std::uint8_t>(value>>(24u-i*8u));result->initialized[i]=1;}
}
struct LowerCall {
    IgbThreadManagerInsertCall request;IgbThreadManagerInsertFactory factory;
    std::shared_ptr<IgbThreadManagerInsertOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbThreadManagerInsertServices>& services,const Check& check){
        check();if(!factory && services)factory=services->factory;
        if(!factory)return pending("Thread manager operation requires its selected growth or scheduler service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread manager operation reached unbound growth or scheduling.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Thread manager operation requires its retained lower continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
struct InterruptCall {
    IgbInterruptCall request;IgbInterruptFactory factory;std::shared_ptr<IgbInterruptOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbThreadManagerInsertServices>& services,const Check& check){
        check();if(!factory && services)factory=services->interrupts;
        if(!factory)return pending("Semaphore access requires its selected interrupt-exclusion operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Semaphore access reached an unbound interrupt-exclusion operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Semaphore access requires its retained interrupt continuation.");
        const auto result=callback();check();return result;
    }
};
struct RuntimeCall {
    IgbThreadRuntimeInitCall request;IgbThreadRuntimeInitFactory factory;
    std::shared_ptr<IgbThreadRuntimeInitOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbThreadManagerInsertServices>& services,const Check& check){
        check();if(!factory && services && services->runtime)factory=services->runtime->factory;
        if(!factory)return pending("Thread insertion requires its captured semaphore virtual.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread insertion reached an unbound semaphore virtual.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Thread insertion requires its retained semaphore continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
}

struct IgbSemaphoreRuntimeAccessInvocation::Impl {
    enum class P{active,signal_count,signal_max,select_disable,disable,wait_test,wait_read,wait_store,
        select_sleep,sleep,signal_read,signal_store,select_wake,wake,select_restore,restore,status,result,done};
    P phase=P::active;RuntimePhysical semaphore;std::shared_ptr<IgbScratchBuffer> result;
    std::shared_ptr<IgbThreadManagerInsertServices> owner;InterruptCall interrupt;LowerCall lower;
    std::uint32_t slot{},count{},saved_interrupt{},status{};bool success{},running{},fault{};
    Impl(IgbObjectInstance object,std::uint32_t value,std::shared_ptr<IgbScratchBuffer> out)
        :semaphore(std::move(object),IgbRuntimeObjectKind::semaphore),result(std::move(out)),slot(value){
        if(slot!=0x8c && slot!=0x7c)fail("Unknown concrete semaphore access slot.");}
};
IgbSemaphoreRuntimeAccessInvocation::IgbSemaphoreRuntimeAccessInvocation(IgbObjectInstance object,std::uint32_t slot,
    std::shared_ptr<IgbScratchBuffer> result):impl_(std::make_unique<Impl>(std::move(object),slot,std::move(result))){}
IgbSemaphoreRuntimeAccessInvocation::~IgbSemaphoreRuntimeAccessInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.semaphore.pointer.storage)s.owner->retained.push_back(s.semaphore.pointer.storage);
    if(s.semaphore.bytes)s.owner->retained.push_back(s.semaphore.bytes);if(s.result)s.owner->retained.push_back(s.result);}}
bool IgbSemaphoreRuntimeAccessInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbSemaphoreRuntimeAccessInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbSemaphoreRuntimeAccessInvocation::resume(std::shared_ptr<IgbThreadManagerInsertServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbThreadManagerInsertServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Semaphore access requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Semaphore access was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Semaphore access was invalidated by its enclosing callback.");s.semaphore.access();};
    const auto queue=[&](K kind){s.lower.clear();check();s.lower.request.kind=kind;s.lower.request.object=s.semaphore.instance;
        s.lower.request.queue={s.semaphore.pointer.storage,s.semaphore.pointer.offset+0x1cu};
        if(services)s.lower.factory=services->factory;};
    try{for(;;){check();switch(s.phase){
    case P::active:
        if(igb_bootstrap_read_byte(s.semaphore.access(),0x14)==0){s.success=false;s.phase=P::status;}
        else s.phase=s.slot==0x8c?P::select_disable:P::signal_count;break;
    case P::signal_count:s.count=igb_bootstrap_read_word(s.semaphore.access(),0x18);s.phase=P::signal_max;break;
    case P::signal_max:
        if(s.count>=igb_bootstrap_read_word(s.semaphore.access(),0x10)){s.success=false;s.phase=P::status;}
        else s.phase=P::select_disable;break;
    case P::select_disable:s.interrupt.clear();check();s.interrupt.request.kind=IgbInterruptServiceKind::disable;
        if(services)s.interrupt.factory=services->interrupts;s.phase=P::disable;break;
    case P::disable:{const auto result=s.interrupt.poll(services,check);if(!result.completed)return result;
        s.saved_interrupt=result.value;s.interrupt.clear();check();s.phase=s.slot==0x8c?P::wait_test:P::signal_read;break;}
    case P::wait_test:s.count=igb_bootstrap_read_word(s.semaphore.access(),0x18);
        s.phase=std::bit_cast<std::int32_t>(s.count)>0?P::wait_read:P::select_sleep;break;
    case P::wait_read:s.count=igb_bootstrap_read_word(s.semaphore.access(),0x18);s.phase=P::wait_store;break;
    case P::wait_store:igb_bootstrap_write_word(s.semaphore.access(),0x18,s.count-1u);s.phase=P::select_restore;break;
    case P::select_sleep:queue(K::sleep_thread);s.phase=P::sleep;break;
    case P::sleep:{const auto result=s.lower.poll(services,check);if(!result.completed)return result;
        s.lower.clear();check();s.phase=P::wait_test;break;}
    case P::signal_read:s.count=igb_bootstrap_read_word(s.semaphore.access(),0x18);s.phase=P::signal_store;break;
    case P::signal_store:igb_bootstrap_write_word(s.semaphore.access(),0x18,s.count+1u);s.phase=P::select_wake;break;
    case P::select_wake:queue(K::wake_thread);s.phase=P::wake;break;
    case P::wake:{const auto result=s.lower.poll(services,check);if(!result.completed)return result;
        s.lower.clear();check();s.phase=P::select_restore;break;}
    case P::select_restore:s.interrupt.clear();check();s.interrupt.request.kind=IgbInterruptServiceKind::restore;
        s.interrupt.request.restore_enabled=s.saved_interrupt;if(services)s.interrupt.factory=services->interrupts;s.phase=P::restore;break;
    case P::restore:{const auto result=s.interrupt.poll(services,check);if(!result.completed)return result;
        s.interrupt.clear();check();s.success=true;s.phase=P::status;break;}
    case P::status:{const auto value=services && services->memory?
            (s.success?services->memory->status_success:services->memory->status_failure):std::optional<std::uint32_t>{};
        if(!value)return pending("Semaphore access requires its fresh canonical success/failure status.");s.status=*value;s.phase=P::result;break;}
    case P::result:store_result(s.result,s.status);s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbThreadManagerInsertInvocation::Impl {
    enum class P{reference,retain,wait_pointer,wait_resolve,wait_table,wait,list_pointer,list_resolve,count,capacity,
        increment,select_growth,growth,backing,element,store,signal_pointer,signal_resolve,signal_table,signal,done};
    P phase=P::reference;RuntimePhysical manager,thread;std::unique_ptr<RuntimePhysical> semaphore,list;
    std::optional<ElementPhysical> element;
    std::shared_ptr<IgbThreadManagerInsertServices> owner;RuntimeCall runtime;LowerCall lower;
    std::uint32_t reference{},pointer{},old_count{},backing{},element_address{};bool running{},fault{};
    Impl(IgbBootstrapPointer m,IgbBootstrapPointer t):manager(std::move(m),IgbRuntimeObjectKind::thread_manager),
        thread(std::move(t),IgbRuntimeObjectKind::thread){}
};
IgbThreadManagerInsertInvocation::IgbThreadManagerInsertInvocation(IgbBootstrapPointer manager,IgbBootstrapPointer thread)
    :impl_(std::make_unique<Impl>(std::move(manager),std::move(thread))){}
IgbThreadManagerInsertInvocation::~IgbThreadManagerInsertInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    const auto retain=[&](const RawPhysical& value){if(value.pointer.storage)s.owner->retained.push_back(value.pointer.storage);
        if(value.bytes)s.owner->retained.push_back(value.bytes);};
    retain(s.manager);retain(s.thread);if(s.semaphore)retain(*s.semaphore);if(s.list)retain(*s.list);
    if(s.element && s.element->bytes)s.owner->retained.push_back(s.element->bytes);
    if(s.runtime.request.result)s.owner->retained.push_back(s.runtime.request.result);}}
bool IgbThreadManagerInsertInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadManagerInsertInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbThreadManagerInsertInvocation::resume(std::shared_ptr<IgbThreadManagerInsertServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Thread insertion requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Thread insertion was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Thread insertion was invalidated by its enclosing callback.");};
    const auto callback_check=[&]{check();s.thread.access();s.manager.access();if(s.semaphore)s.semaphore->access();if(s.list)s.list->access();};
    const auto select_semaphore=[&](std::uint32_t slot){const auto table=igb_bootstrap_read_word(s.semaphore->access(),0);
        if(!table)fail("Thread insertion dereferences a null semaphore vtable.");
        s.runtime.request.object=s.semaphore->instance;s.runtime.request.vtable_word=table;s.runtime.request.virtual_slot=slot;
        s.runtime.request.result=hidden_result();if(services && services->runtime)s.runtime.factory=services->runtime->factory;};
    try{for(;;){check();switch(s.phase){
    case P::reference:s.reference=igb_bootstrap_read_word(s.thread.access(),4);s.phase=P::retain;break;
    case P::retain:igb_bootstrap_write_word(s.thread.access(),4,s.reference+1u);s.phase=P::wait_pointer;break;
    case P::wait_pointer:s.pointer=igb_bootstrap_read_word(s.manager.access(),0x0c);s.phase=P::wait_resolve;break;
    case P::wait_resolve:case P::signal_resolve:{const auto object=resolve_object(services,s.pointer);
        if(!object)return pending("Thread insertion requires its captured semaphore pointer mapping.");
        s.semaphore=std::make_unique<RuntimePhysical>(*object,IgbRuntimeObjectKind::semaphore);
        s.phase=s.phase==P::wait_resolve?P::wait_table:P::signal_table;break;}
    case P::wait_table:select_semaphore(0x8c);s.phase=P::wait;break;
    case P::wait:{const auto result=s.runtime.poll(services,callback_check);if(!result.completed)return result;
        s.runtime.clear();callback_check();s.semaphore.reset();check();s.phase=P::list_pointer;break;}
    case P::list_pointer:s.pointer=igb_bootstrap_read_word(s.manager.access(),8);s.phase=P::list_resolve;break;
    case P::list_resolve:{const auto object=resolve_object(services,s.pointer);
        if(!object)return pending("Thread insertion requires its captured list pointer mapping.");
        s.list=std::make_unique<RuntimePhysical>(*object,IgbRuntimeObjectKind::thread_list);s.phase=P::count;break;}
    case P::count:s.old_count=igb_bootstrap_read_word(s.list->access(),8);s.phase=P::capacity;break;
    case P::capacity:s.phase=std::bit_cast<std::int32_t>(s.old_count)<
        std::bit_cast<std::int32_t>(igb_bootstrap_read_word(s.list->access(),0x0c))?P::increment:P::select_growth;break;
    case P::increment:igb_bootstrap_write_word(s.list->access(),8,s.old_count+1u);s.phase=P::backing;break;
    case P::select_growth:s.lower.request.kind=IgbThreadManagerInsertServiceKind::grow_list;s.lower.request.object=s.list->instance;
        s.lower.request.count=s.old_count+1u;s.lower.request.element_bytes=4;if(services)s.lower.factory=services->factory;s.phase=P::growth;break;
    case P::growth:{const auto result=s.lower.poll(services,callback_check);if(!result.completed)return result;
        s.lower.clear();callback_check();s.phase=P::backing;break;}
    case P::backing:s.backing=igb_bootstrap_read_word(s.list->access(),0x10);s.element_address=s.backing+(s.old_count<<2u);s.phase=P::element;break;
    case P::element:{const auto pointer=resolve_element(services,s.element_address);
        if(!pointer)return pending("Thread insertion requires its captured physical element destination.");
        s.element=*pointer;s.phase=P::store;break;}
    case P::store:s.list->access();s.thread.access();s.element->store(s.thread.guest);s.phase=P::signal_pointer;break;
    case P::signal_pointer:s.pointer=igb_bootstrap_read_word(s.manager.access(),0x0c);s.phase=P::signal_resolve;break;
    case P::signal_table:select_semaphore(0x7c);s.phase=P::signal;break;
    case P::signal:{const auto result=s.runtime.poll(services,callback_check);if(!result.completed)return result;
        s.runtime.clear();callback_check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbThreadRuntimeInitFactory make_native_igb_semaphore_runtime_access_factory(std::shared_ptr<IgbThreadManagerInsertServices> services,
    IgbThreadRuntimeInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadRuntimeInitCall& call)->std::shared_ptr<IgbThreadRuntimeInitOperation>{
        if(call.vtable_word!=0x80470314 || (call.virtual_slot!=0x8c && call.virtual_slot!=0x7c))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbSemaphoreRuntimeAccessInvocation>(call.object,call.virtual_slot,call.result);
        auto operation=std::make_shared<IgbThreadRuntimeInitOperation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};return operation;
    };
}
IgbThreadStartupFactory make_native_igb_thread_manager_insert_factory(std::shared_ptr<IgbThreadManagerInsertServices> services,
    IgbThreadStartupFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadStartupCall& call)->std::shared_ptr<IgbThreadStartupOperation>{
        if(call.kind!=IgbThreadStartupServiceKind::object_virtual || call.vtable_word!=0x804700a8 || call.virtual_slot!=0x60)
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbThreadManagerInsertInvocation>(call.object,call.argument);
        auto operation=std::make_shared<IgbThreadStartupOperation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};return operation;
    };
}
} // namespace dw4
