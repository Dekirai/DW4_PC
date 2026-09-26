#include "dw4/igb_gamecube_thread_init.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbGamecubeThreadInitError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
struct Physical {
    IgbObjectInstance object;std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t guest{};
    explicit Physical(IgbObjectInstance value):object(std::move(value)),bytes(object.storage?object.storage->bytes:nullptr),guest(address(object)){}
    IgbBootstrapPointer pointer(IgbRuntimeObjectKind kind)const{
        igb_object_validate_instance(object);
        if(!object.runtime_object || object.runtime_object->kind!=kind || !bytes || bytes->released ||
            object.storage->bytes!=bytes || address(object)!=guest)
            fail("Thread runtime operation lost its captured physical instance identity.");
        return {object.storage,object.offset};
    }
};
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.file==b.file && a.metadata==b.metadata && a.field==b.field && a.fields==b.fields && a.types==b.types && a.runtime_object==b.runtime_object;}
std::optional<IgbObjectInstance> resolve(const std::shared_ptr<IgbGamecubeThreadInitServices>& services,std::uint32_t guest){
    if(!guest)return IgbObjectInstance{};
    std::optional<IgbObjectInstance> result;
    if(services && services->runtime && services->runtime->objects)for(const auto& object:services->runtime->objects->objects){
        if(address(object)!=guest)continue;igb_object_validate_instance(object);
        if(result && !same(*result,object))fail("Thread semaphore pointer has conflicting native object identities.");result=object;
    }
    return result;
}
void result_word(const std::shared_ptr<IgbScratchBuffer>& result,std::uint32_t value){
    if(!result || result->released || result->bytes.size()<4 || result->initialized.size()<4)
        fail("Thread semaphore setter writes an unavailable hidden result word.");
    for(unsigned i=0;i<4;++i){result->bytes[i]=static_cast<std::uint8_t>(value>>(24u-8u*i));result->initialized[i]=1;}
}
struct RuntimeCall {
    IgbThreadRuntimeInitCall request;IgbThreadRuntimeInitFactory factory;
    std::shared_ptr<IgbThreadRuntimeInitOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbGamecubeThreadInitServices>& services,const Check& check){
        check();if(!factory && services && services->runtime)factory=services->runtime->factory;
        if(!factory)return pending("Thread initialization requires its selected object virtual service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread initialization reached an unbound object virtual.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Thread initialization requires its retained object virtual continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
struct MemoryCall {
    IgbMemoryFieldConstructCall request;IgbMemoryFieldConstructFactory factory;
    std::shared_ptr<IgbMemoryFieldConstructOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbGamecubeThreadInitServices>& services,const Check& check){
        check();if(!factory && services && services->memory)factory=services->memory->factory;
        if(!factory)return pending("Thread initialization requires its selected tagged-pool or aligned-allocation service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread initialization reached an unbound tagged-pool or aligned-allocation service.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Thread initialization requires its retained memory continuation.");
        const auto result=callback();check();return result;
    }
};
}

struct IgbSemaphoreInitialCountInvocation::Impl {
    enum class P{active,count,status,result,done};P phase=P::active;
    Physical semaphore;std::shared_ptr<IgbScratchBuffer> result;
    std::shared_ptr<IgbGamecubeThreadInitServices> owner;
    std::uint32_t count{},status{};bool active{},running{},fault{};
    Impl(IgbObjectInstance object,std::uint32_t value,std::shared_ptr<IgbScratchBuffer> out)
        :semaphore(std::move(object)),result(std::move(out)),count(value){}
};
IgbSemaphoreInitialCountInvocation::IgbSemaphoreInitialCountInvocation(IgbObjectInstance object,std::uint32_t count,
    std::shared_ptr<IgbScratchBuffer> result):impl_(std::make_unique<Impl>(std::move(object),count,std::move(result))){}
IgbSemaphoreInitialCountInvocation::~IgbSemaphoreInitialCountInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.semaphore.object.storage)s.owner->retained.push_back(s.semaphore.object.storage);if(s.result)s.owner->retained.push_back(s.result);
}}
bool IgbSemaphoreInitialCountInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbSemaphoreInitialCountInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbSemaphoreInitialCountInvocation::resume(std::shared_ptr<IgbGamecubeThreadInitServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Semaphore count setter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Semaphore count setter was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Semaphore count setter was invalidated by its enclosing callback.");};
    try{for(;;){check();switch(s.phase){
    case P::active:s.active=igb_bootstrap_read_byte(s.semaphore.pointer(IgbRuntimeObjectKind::semaphore),0x14)!=0;
        s.phase=s.active?P::status:P::count;break;
    case P::count:igb_bootstrap_write_word(s.semaphore.pointer(IgbRuntimeObjectKind::semaphore),0x0c,s.count);s.phase=P::status;break;
    case P::status:{const auto value=services && services->memory?
            (s.active?services->memory->status_failure:services->memory->status_success):std::optional<std::uint32_t>{};
        if(!value)return pending("Semaphore count setter requires its fresh canonical success/failure status.");
        s.status=*value;s.phase=P::result;break;}
    case P::result:result_word(s.result,s.status);s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbGamecubeThreadInitInvocation::Impl {
    enum class P{table,alignment,size,tag,pool,allocate,allocation,stack,semaphore,resolve,semaphore_table,set_count,done};P phase=P::table;
    Physical thread;std::unique_ptr<Physical> semaphore;
    std::shared_ptr<IgbGamecubeThreadInitServices> owner;std::shared_ptr<ModelSceneObject> pool;
    RuntimeCall runtime;MemoryCall memory;IgbPointerListBacking backing;
    std::uint32_t table{},alignment{},bytes{},semaphore_address{};bool running{},fault{};
    explicit Impl(IgbObjectInstance value):thread(std::move(value)){}
};
IgbGamecubeThreadInitInvocation::IgbGamecubeThreadInitInvocation(IgbObjectInstance object):impl_(std::make_unique<Impl>(std::move(object))){}
IgbGamecubeThreadInitInvocation::~IgbGamecubeThreadInitInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.thread.object.storage)s.owner->retained.push_back(s.thread.object.storage);if(s.pool)s.owner->retained.push_back(s.pool);
    if(s.backing.bytes)s.owner->retained.push_back(s.backing.bytes);
    if(s.semaphore && s.semaphore->object.storage)s.owner->retained.push_back(s.semaphore->object.storage);
    if(s.runtime.request.result)s.owner->retained.push_back(s.runtime.request.result);
}}
bool IgbGamecubeThreadInitInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadInitInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbGamecubeThreadInitInvocation::resume(std::shared_ptr<IgbGamecubeThreadInitServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbMemoryFieldConstructServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Thread initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Thread initialization was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Thread initialization was invalidated by its enclosing callback.");s.thread.pointer(IgbRuntimeObjectKind::thread);};
    try{for(;;){check();switch(s.phase){
    case P::table:
        s.table=igb_bootstrap_read_word(s.thread.pointer(IgbRuntimeObjectKind::thread),0);
        if(!s.table)fail("Thread initialization dereferences a null vtable at+A8.");
        s.runtime.request.object=s.thread.object;s.runtime.request.vtable_word=s.table;s.runtime.request.virtual_slot=0xa8;
        if(services && services->runtime)s.runtime.factory=services->runtime->factory;s.phase=P::alignment;break;
    case P::alignment:{const auto result=s.runtime.poll(services,check);if(!result.completed)return result;
        s.alignment=static_cast<std::uint16_t>(result.value);s.runtime.clear();check();s.phase=P::size;break;}
    case P::size:s.bytes=igb_bootstrap_read_word(s.thread.pointer(IgbRuntimeObjectKind::thread),0x24);s.phase=P::tag;break;
    case P::tag:
        s.memory.request.kind=K::tagged_pool;s.memory.request.object=s.thread.object;
        s.memory.request.handle=igb_bootstrap_read_byte(s.thread.pointer(IgbRuntimeObjectKind::thread),4);
        if(services && services->memory)s.memory.factory=services->memory->factory;s.phase=P::pool;break;
    case P::pool:{const auto result=s.memory.poll(services,check);if(!result.completed)return result;
        s.pool=s.memory.operation->pool_result;s.memory.clear();check();s.phase=P::allocate;break;}
    case P::allocate:
        if(!s.pool)fail("Thread initialization dereferences a null stack pool at+DC.");
        s.memory.request.kind=K::allocate;s.memory.request.object=s.thread.object;s.memory.request.pool=s.pool;
        s.memory.request.bytes=s.bytes;s.memory.request.alignment=s.alignment;s.memory.request.virtual_slot=0xdc;
        if(services && services->memory)s.memory.factory=services->memory->factory;s.phase=P::allocation;break;
    case P::allocation:{const auto result=s.memory.poll(services,check);if(!result.completed)return result;
        s.backing=s.memory.operation->backing_result;
        if(!s.backing.address && (s.backing.bytes || s.backing.fields || s.backing.types))fail("Null thread stack allocation has a nonnull physical mapping.");
        if(s.backing.fields || s.backing.types)fail("Thread stack allocation has an unrelated typed pointer-list mapping.");
        s.memory.clear();check();s.phase=P::stack;break;}
    case P::stack:
        igb_bootstrap_write_word(s.thread.pointer(IgbRuntimeObjectKind::thread),0x20,s.backing.address);
        if(services && s.backing.bytes)services->retained.push_back(s.backing.bytes);s.phase=P::semaphore;break;
    case P::semaphore:s.semaphore_address=igb_bootstrap_read_word(s.thread.pointer(IgbRuntimeObjectKind::thread),0x380);s.phase=P::resolve;break;
    case P::resolve:{const auto object=resolve(services,s.semaphore_address);
        if(!object)return pending("Thread initialization requires its captured done-semaphore object binding.");
        s.semaphore=std::make_unique<Physical>(*object);s.phase=P::semaphore_table;break;}
    case P::semaphore_table:
        s.table=igb_bootstrap_read_word(s.semaphore->pointer(IgbRuntimeObjectKind::semaphore),0);
        if(!s.table)fail("Thread initialization dereferences a null done-semaphore vtable at+68.");
        s.runtime.request.object=s.semaphore->object;s.runtime.request.vtable_word=s.table;s.runtime.request.virtual_slot=0x68;s.runtime.request.argument=0;
        s.runtime.request.result=std::make_shared<IgbScratchBuffer>();s.runtime.request.result->bytes.assign(4,0xcc);s.runtime.request.result->initialized.assign(4,0);
        if(services && services->runtime)s.runtime.factory=services->runtime->factory;s.phase=P::set_count;break;
    case P::set_count:{const auto child_check=[&]{check();s.semaphore->pointer(IgbRuntimeObjectKind::semaphore);};
        const auto result=s.runtime.poll(services,child_check);if(!result.completed)return result;
        s.runtime.clear();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbObjectInitFactory make_native_igb_gamecube_thread_init_factory(std::shared_ptr<IgbGamecubeThreadInitServices> services,IgbObjectInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation>{
        if(call.kind!=IgbObjectInitServiceKind::object_virtual || call.vtable_word!=0x80470110 ||
            (call.virtual_slot!=0x58 && call.virtual_slot!=0x0c && call.virtual_slot!=0x24 && call.virtual_slot!=0x34 && call.virtual_slot!=0x2c))
            return remaining?remaining(call):nullptr;
        auto operation=std::make_shared<IgbObjectInitOperation>();
        if(call.virtual_slot!=0x58 && call.virtual_slot!=0x2c){operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;}
        const auto selected=weak.lock();
        if(call.virtual_slot==0x58){operation->resume=[selected,out=operation.get(),done=false]()mutable{
            if(done)return MessengerCallResult{true,0,{}};
            const auto slot=igb_gamecube_thread_metadata_slot(selected?selected->registration:nullptr);
            if(!slot || !*slot)return pending("Thread plain933F8 getter requires canonical80562074.");
            out->metadata_result=**slot;done=true;return MessengerCallResult{true,0,{}};
        };}else{
            auto invocation=std::make_shared<IgbGamecubeThreadInitInvocation>(call.object);
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
            operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};
        }return operation;
    };
}
IgbThreadRuntimeInitFactory make_native_igb_gamecube_thread_runtime_factory(std::shared_ptr<IgbGamecubeThreadInitServices> services,IgbThreadRuntimeInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadRuntimeInitCall& call)->std::shared_ptr<IgbThreadRuntimeInitOperation>{
        const bool alignment=call.vtable_word==0x80470110 && call.virtual_slot==0xa8;
        const bool setter=call.vtable_word==0x80470314 && call.virtual_slot==0x68;
        if(!alignment && !setter)return remaining?remaining(call):nullptr;
        auto operation=std::make_shared<IgbThreadRuntimeInitOperation>();
        if(alignment){operation->resume=[] {return MessengerCallResult{true,32,{}};};return operation;}
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbSemaphoreInitialCountInvocation>(call.object,call.argument,call.result);
        operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};return operation;
    };
}
} // namespace dw4
