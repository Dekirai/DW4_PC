#include "dw4/igb_thread_runtime_init.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbThreadRuntimeInitError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){
    return a.storage==b.storage && a.offset==b.offset && a.file==b.file && a.field==b.field &&
        a.metadata==b.metadata && a.fields==b.fields && a.types==b.types && a.runtime_object==b.runtime_object;
}
std::optional<IgbObjectInstance> resolve(const std::shared_ptr<IgbThreadRuntimeInitServices>& services,
    const IgbObjectInstance& manager,std::uint32_t pointer){
    if(!pointer)return IgbObjectInstance{};
    std::optional<IgbObjectInstance> result;
    const auto consider=[&](const IgbObjectInstance& object){if(address(object)==pointer){
        igb_object_validate_instance(object);
        if(result && !same(*result,object))fail("Manager semaphore pointer has conflicting native identities.");
        result=object;
    }};
    consider(manager);
    if(services && services->objects)for(const auto& object:services->objects->objects)consider(object);
    return result;
}
}
struct IgbThreadManagerInitInvocation::Impl {
    enum class P{member,resolve,table,select,call,done};P phase=P::member;
    IgbObjectInstance manager,semaphore;
    std::uint32_t pointer{},table{};
    std::shared_ptr<IgbThreadRuntimeInitServices> owner;
    IgbThreadRuntimeInitCall request;
    IgbThreadRuntimeInitFactory factory;
    std::shared_ptr<IgbThreadRuntimeInitOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    bool running{},fault{};
};
IgbThreadManagerInitInvocation::IgbThreadManagerInitInvocation(IgbObjectInstance manager):impl_(std::make_unique<Impl>()){
    impl_->manager=std::move(manager);
}
IgbThreadManagerInitInvocation::~IgbThreadManagerInitInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.owner){
        if(s.manager.storage)s.owner->retained.push_back(s.manager.storage);
        if(s.semaphore.storage)s.owner->retained.push_back(s.semaphore.storage);
        if(s.request.result)s.owner->retained.push_back(s.request.result);
    }
}
bool IgbThreadManagerInitInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbThreadManagerInitInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbThreadManagerInitInvocation::resume(std::shared_ptr<IgbThreadRuntimeInitServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Manager initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Manager initialization was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::member:
        igb_object_validate_instance(s.manager);
        s.pointer=igb_bootstrap_read_word({s.manager.storage,s.manager.offset},0x0c);s.phase=P::resolve;break;
    case P::resolve:{
        const auto object=resolve(services,s.manager,s.pointer);
        if(!object)return pending("Manager initialization requires its captured semaphore's physical object binding.");
        s.semaphore=*object;s.phase=P::table;break;
    }
    case P::table:
        s.table=igb_bootstrap_read_word({s.semaphore.storage,s.semaphore.offset},0);
        if(!s.table)fail("Manager initialization dereferences a null semaphore vtable at+5C.");
        s.phase=P::select;break;
    case P::select:
        s.request.object=s.semaphore;s.request.vtable_word=s.table;s.request.virtual_slot=0x5c;
        s.request.result=std::make_shared<IgbScratchBuffer>();
        s.request.result->bytes.assign(4,0xcc);s.request.result->initialized.assign(4,0);
        if(services)s.factory=services->factory;s.phase=P::call;break;
    case P::call:
        if(!s.factory && services)s.factory=services->factory;
        if(!s.factory)return pending("Manager initialization requires semaphore virtual+5C with its stack-result destination.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Manager initialization reached an unbound semaphore virtual+5C.");
        if(!s.callback && !s.checked_callback){
            s.checked_callback=s.operation->resume_checked;
            if(!s.checked_callback)s.callback=s.operation->resume;
        }
        if(!s.callback && !s.checked_callback)return pending("Manager initialization requires its retained semaphore continuation.");
        {const auto status=s.checked_callback?s.checked_callback(check):s.callback();check();if(!status.completed)return status;}
        s.callback={};s.checked_callback={};s.operation.reset();s.factory={};check();s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbObjectInitFactory make_native_igb_thread_runtime_init_factory(
    std::shared_ptr<IgbThreadRuntimeInitServices> services,IgbObjectInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation>{
        if(call.kind!=IgbObjectInitServiceKind::object_virtual)return remaining?remaining(call):nullptr;
        const bool manager=call.vtable_word==0x804700a8u,list=call.vtable_word==0x80476ce0u,
            semaphore=call.vtable_word==0x80470314u;
        if(!manager && !list && !semaphore)return remaining?remaining(call):nullptr;
        const auto slot=call.virtual_slot;
        if(slot!=0x0c && slot!=0x24 && slot!=0x2c && slot!=0x34 && slot!=0x58)return remaining?remaining(call):nullptr;
        // The semaphore's four literal BLRs and plain93530 global getter
        // never access their receiver. Keep the existing other admissions.
        if(!semaphore){
            igb_object_validate_instance(call.object);
            if(!call.object.runtime_object || call.object.runtime_object->kind!=
                (manager?IgbRuntimeObjectKind::thread_manager:IgbRuntimeObjectKind::thread_list))
                fail("Thread runtime initializer requires its matching native object layout.");
        }
        auto operation=std::make_shared<IgbObjectInitOperation>();
        if(slot!=0x58 && !(manager && slot==0x2c)){
            operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;
        }
        const auto selected=weak.lock();if(!selected)fail("Thread runtime initialization services expired before selection.");
        if(slot==0x58){
            operation->resume=[selected,manager,semaphore,out=operation.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                const auto canonical=manager?igb_platform_thread_metadata_slot(selected->manager_registration):
                    semaphore?igb_platform_semaphore_metadata_slot(selected->semaphore_registration):
                    (selected->dependencies?selected->dependencies->thread_list:nullptr);
                if(!canonical || !*canonical)return pending("Thread runtime metadata getter requires its represented canonical global.");
                out->metadata_result=**canonical;done=true;return MessengerCallResult{true,0,{}};
            };
        }else{
            auto invocation=std::make_shared<IgbThreadManagerInitInvocation>(call.object);
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
        }
        return operation;
    };
}
} // namespace dw4
