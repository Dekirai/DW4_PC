#include "dw4/igb_thread_startup.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbThreadStartupError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
const char* boundary(IgbThreadStartupServiceKind kind){
    using K=IgbThreadStartupServiceKind;
    switch(kind){
    case K::pool_for_handle:return "Thread startup requires original607F4 pool selection.";
    case K::create_manager:return "Thread startup requires original2380C manager construction.";
    case K::create_thread:return "Thread startup requires original23B90 thread construction.";
    case K::object_virtual:return "Thread startup requires its selected physical object virtual.";
    case K::destroy_object:return "Thread startup requires original66E1C after its reference decrement.";
    }
    return "Thread startup requires its reached operation.";
}
struct Call {
    IgbThreadStartupCall request;
    IgbThreadStartupFactory factory;
    std::shared_ptr<IgbThreadStartupOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbThreadStartupServices>& services,Check check){
        check();if(!factory && services)factory=services->factory;
        if(!factory)return pending(boundary(request.kind));
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending(boundary(request.kind));
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending(boundary(request.kind));
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
}
void publish_original_igb_thread_pool_handle(IgbThreadStartupServices& services)noexcept{services.pool_handle=4;}
struct IgbThreadObjectFactoryInvocation::Impl {
    enum class Phase {registration,metadata,construct,done};
    IgbThreadObjectKind kind;
    std::shared_ptr<ModelSceneObject> pool;
    IgbBootstrapPointer result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::unique_ptr<IgbReflectedFactoryInvocation> reflected;
    Phase phase=Phase::registration;
    bool running{},fault{};
    Impl(IgbThreadObjectKind k,std::shared_ptr<ModelSceneObject> p):kind(k),pool(std::move(p)){
        if(k!=IgbThreadObjectKind::manager && k!=IgbThreadObjectKind::thread)fail("Unknown thread object factory kind.");
        registration=std::make_unique<IgbRegistrationCallbacksInvocation>(k==IgbThreadObjectKind::manager?0x800238a8u:0x80023c68u);
    }
};
IgbThreadObjectFactoryInvocation::IgbThreadObjectFactoryInvocation(IgbThreadObjectKind kind,std::shared_ptr<ModelSceneObject> pool)
    :impl_(std::make_unique<Impl>(kind,std::move(pool))){}
IgbThreadObjectFactoryInvocation::~IgbThreadObjectFactoryInvocation()=default;
bool IgbThreadObjectFactoryInvocation::completed()const noexcept{return impl_->phase==Impl::Phase::done;}
bool IgbThreadObjectFactoryInvocation::faulted()const noexcept{return impl_->fault;}
IgbBootstrapPointer IgbThreadObjectFactoryInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete thread object factory has no returned object.");return impl_->result;
}
MessengerCallResult IgbThreadObjectFactoryInvocation::resume(std::shared_ptr<IgbThreadStartupServices> services){
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault){s.fault=true;fail("Thread object factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Thread object factory continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::registration:{
        const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::metadata;break;
    }
    case P::metadata:{
        const auto slot=services?(s.kind==IgbThreadObjectKind::manager?services->manager_metadata:services->thread_metadata):nullptr;
        if(!slot || !slot->has_value())return pending("Thread object factory requires its live canonical metadata global after registration.");
        s.reflected=std::make_unique<IgbReflectedFactoryInvocation>(**slot,s.pool);s.phase=P::construct;break;
    }
    case P::construct:{
        const auto status=s.reflected->resume(services?services->reflected:nullptr);check();if(!status.completed)return status;
        const auto object=s.reflected->result_instance();s.result={object.storage,object.offset};
        s.reflected.reset();check();s.phase=P::done;return {true,0,{}};
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbThreadStartupInvocation::Impl {
    enum class Phase {manager_pool,after_manager_pool,after_manager,publish_manager,thread_pool,after_thread_pool,after_thread,
        thread_virtual,manager_virtual,release,after_destroy,call,done};
    IgbBootstrapPointer manager,thread;
    std::shared_ptr<IgbThreadStartupServices> retention;
    Call call;
    Phase phase=Phase::manager_pool,after=Phase::done;
    bool running{},fault{};
};
IgbThreadStartupInvocation::IgbThreadStartupInvocation():impl_(std::make_unique<Impl>()){}
IgbThreadStartupInvocation::~IgbThreadStartupInvocation(){
    if(!completed() && impl_->retention){
        if(impl_->manager.storage)impl_->retention->retained.push_back(impl_->manager);
        if(impl_->thread.storage)impl_->retention->retained.push_back(impl_->thread);
    }
}
bool IgbThreadStartupInvocation::completed()const noexcept{return impl_->phase==Impl::Phase::done;}
bool IgbThreadStartupInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbThreadStartupInvocation::resume(std::shared_ptr<IgbThreadStartupServices> services){
    auto& s=*impl_;using P=Impl::Phase;using K=IgbThreadStartupServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Thread startup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Thread startup continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbThreadStartupCall&{
        clear();s.call.request.kind=kind;s.call.factory=services?services->factory:IgbThreadStartupFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::manager_pool:case P::thread_pool:{
        clear();
        if(!services || !services->pool_handle)return pending("Thread startup requires live pool handle805621F0.");
        const auto handle=*services->pool_handle;
        const auto after=s.phase==P::manager_pool?P::after_manager_pool:P::after_thread_pool;
        select(K::pool_for_handle,after).handle=handle;break;
    }
    case P::after_manager_pool:case P::after_thread_pool:{
        auto pool=s.call.operation->pool_result;
        const auto is_manager=s.phase==P::after_manager_pool;
        select(is_manager?K::create_manager:K::create_thread,is_manager?P::after_manager:P::after_thread).pool=std::move(pool);break;
    }
    case P::after_manager:
        s.manager=s.call.operation->object_result;if(services)s.retention=services;clear();s.phase=P::publish_manager;break;
    case P::publish_manager:
        if(!services)return pending("Thread startup requires canonical manager singleton storage805622F8.");
        s.retention=services;
        if(services->manager && services->manager->storage)services->retained.push_back(*services->manager);
        services->manager=s.manager;s.manager={};s.phase=P::thread_pool;break;
    case P::after_thread:
        s.thread=s.call.operation->object_result;if(services)s.retention=services;clear();
        igb_bootstrap_write_byte(s.thread,0x2c,1);s.phase=P::thread_virtual;break;
    case P::thread_virtual:{
        clear();const auto table=igb_bootstrap_read_word(s.thread,0);
        if(table==0)fail("Thread startup dereferences a null thread vtable at+DC.");
        auto& call=select(K::object_virtual,P::manager_virtual);call.object=s.thread;call.virtual_slot=0xdc;call.vtable_word=table;break;
    }
    case P::manager_virtual:{
        clear();
        if(!services || !services->manager)return pending("Thread startup requires live manager singleton805622F8 after thread activation.");
        const auto manager=*services->manager;const auto table=igb_bootstrap_read_word(manager,0);
        if(table==0)fail("Thread startup dereferences a null manager vtable at+60.");
        auto& call=select(K::object_virtual,P::release);call.object=manager;call.argument=s.thread;
        call.virtual_slot=0x60;call.vtable_word=table;break;
    }
    case P::release:
        clear();
        if(igb_bootstrap_address(s.thread)!=0){
            const auto reference=igb_bootstrap_read_word(s.thread,4);
            igb_bootstrap_write_word(s.thread,4,reference-1u);
            if((igb_bootstrap_read_word(s.thread,4)&0x007fffffu)==0){
                select(K::destroy_object,P::after_destroy).object=s.thread;break;
            }
        }
        s.phase=P::done;return {true,0,{}};
    case P::after_destroy:clear();s.phase=P::done;return {true,0,{}};
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbThreadStartupFactory make_native_igb_thread_startup_service_factory(
    std::shared_ptr<IgbThreadStartupServices> services,IgbThreadStartupFactory remaining){
    return [owner=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadStartupCall& call)->std::shared_ptr<IgbThreadStartupOperation>{
        using K=IgbThreadStartupServiceKind;
        if(call.kind!=K::pool_for_handle && call.kind!=K::create_manager && call.kind!=K::create_thread)return remaining?remaining(call):nullptr;
        const auto selected=owner.lock();if(!selected)fail("Thread startup adapter requires live canonical services.");
        auto operation=std::make_shared<IgbThreadStartupOperation>();const auto weak=std::weak_ptr(operation);
        if(call.kind==K::pool_for_handle){
            auto invocation=std::make_shared<IgbPoolByHandleInvocation>(call.handle);
            operation->resume=[selected,invocation,weak]{const auto status=invocation->resume(selected->pools);
                if(status.completed)weak.lock()->pool_result=invocation->result();return status;};
        }else{
            auto invocation=std::make_shared<IgbThreadObjectFactoryInvocation>(
                call.kind==K::create_manager?IgbThreadObjectKind::manager:IgbThreadObjectKind::thread,call.pool);
            operation->resume=[selected,invocation,weak]{const auto status=invocation->resume(selected);
                if(status.completed)weak.lock()->object_result=invocation->result();return status;};
        }
        return operation;
    };
}
IgbRegistrationCallbacksFactory make_native_igb_thread_startup_function_factory(
    std::shared_ptr<IgbThreadStartupServices> services,IgbRegistrationCallbacksFactory remaining){
    return [owner=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)
        ->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x80023a30u && call.function!=0x800755f8u))
            return remaining?remaining(call):nullptr;
        const auto selected=owner.lock();if(!selected)fail("Thread startup callback requires live canonical services.");
        auto invocation=std::make_shared<IgbThreadStartupInvocation>();auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        operation->resume=[selected,invocation]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
