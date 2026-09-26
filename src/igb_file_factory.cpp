#include "dw4/igb_file_factory.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbFileFactoryError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
const char* boundary(IgbFileFactoryServiceKind kind) {
    using K=IgbFileFactoryServiceKind;
    switch(kind) {
    case K::register_file_type:return "IGB file factory requires registration8002F178, including80066188 callbacks.";
    case K::redirect_type:return "Reflected factory requires its captured metadata+3C redirect callback.";
    case K::named_pool:return "Reflected factory requires named-pool selection80060834.";
    case K::current_pool:return "Reflected factory requires current-pool selection80058C80.";
    case K::allocate:return "Reflected factory requires the captured pool allocation virtual+D8.";
    }
    return "Reflected factory requires its reached operation.";
}
struct Call {
    IgbFileFactoryCall request;
    IgbFileFactoryFactory factory;
    std::shared_ptr<IgbFileFactoryOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear() {callback={};checked_callback={};factory={};operation.reset();request={};}
    void select(IgbFileFactoryCall value,const std::shared_ptr<IgbFileFactoryServices>& services) {
        clear();request=std::move(value);factory=services?services->factory:IgbFileFactoryFactory{};
    }
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbFileFactoryServices>& services,Check&& check) {
        if(!factory && services)factory=services->factory;
        if(!factory)return pending(boundary(request.kind));
        if(!operation) {operation=factory(request);check();}
        if(!operation)return pending(boundary(request.kind));
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending(boundary(request.kind));
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
}

struct IgbReflectedFactoryInvocation::Impl {
    enum class Phase {entry,after_redirect,pool,after_named,after_current,allocation,after_allocation,initialize,call,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> original_pool,pool;
    std::shared_ptr<IgbObjectStorage> allocation;
    std::shared_ptr<IgbFileFactoryServices> allocation_services;
    IgbObjectInstance result;
    std::unique_ptr<IgbObjectInitInvocation> initializer;
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    std::uint32_t prefix{};
    unsigned redirects{};
    bool running{},fault{};
};
IgbReflectedFactoryInvocation::IgbReflectedFactoryInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,
    std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()) {
    impl_->metadata=std::move(metadata);impl_->original_pool=std::move(pool);
}
IgbReflectedFactoryInvocation::~IgbReflectedFactoryInvocation() {
    if(!completed() && impl_->allocation && impl_->allocation_services)
        impl_->allocation_services->unfinished_allocations_.push_back(impl_->allocation);
}
bool IgbReflectedFactoryInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbReflectedFactoryInvocation::faulted() const noexcept {return impl_->fault;}
IgbObjectInstance IgbReflectedFactoryInvocation::result_instance() const {
    if(!completed() || faulted())fail("Incomplete reflected construction has no returned instance.");return impl_->result;
}
std::shared_ptr<LoaderRequestFile> IgbReflectedFactoryInvocation::result() const {
    const auto object=result_instance();
    if(object.field || object.metadata || object.fields || object.types || object.runtime_object)fail("A reflected non-file instance cannot be returned through the file adapter.");return object.file;
}
MessengerCallResult IgbReflectedFactoryInvocation::resume(std::shared_ptr<IgbFileFactoryServices> services,
    const std::function<void()>& enclosing_check) {
    auto& s=*impl_;using P=Impl::Phase;using K=IgbFileFactoryServiceKind;
    if(s.running || s.fault) {s.fault=true;fail("Reflected factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Reflected factory continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Reflected factory continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbFileFactoryCall& {
        clear();IgbFileFactoryCall call;call.kind=kind;call.metadata=s.metadata;
        s.call.select(std::move(call),services);check();s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto finish=[&]{clear();s.initializer.reset();check();s.phase=P::done;return MessengerCallResult{true,0,{}};};
    try {
        for(;;){check();switch(s.phase) {
        case P::entry:
            if(!s.metadata)fail("Reflected factory dereferences a null metadata owner.");
            if(!s.metadata->factory_redirect)return pending("Reflected factory requires represented metadata+3C redirect state.");
            if(*s.metadata->factory_redirect!=0) {
                if(++s.redirects>256)fail("Reflected metadata redirects exceed the bounded native profile.");
                select(K::redirect_type,P::after_redirect).redirect=*s.metadata->factory_redirect;
            }else if(s.metadata->flag_1a==1)return finish();
            else {s.pool=s.original_pool;s.phase=P::pool;}
            break;
        case P::after_redirect:
            s.metadata=std::move(s.call.operation->metadata_result);clear();s.phase=P::entry;break;
        case P::pool:
            if(s.pool) {s.phase=P::allocation;break;}
            if(!s.metadata->default_pool_name)return pending("Reflected factory requires represented metadata+60 pool-name state.");
            if(const auto name=*s.metadata->default_pool_name;name && !name->empty() && name->front()!=0)
                select(K::named_pool,P::after_named).name=name;
            else select(K::current_pool,P::after_current);
            break;
        case P::after_named:
            s.pool=std::move(s.call.operation->object_result);clear();
            if(s.pool)s.phase=P::allocation;else select(K::current_pool,P::after_current);
            break;
        case P::after_current:s.pool=std::move(s.call.operation->object_result);clear();s.phase=P::allocation;break;
        case P::allocation:
            if(!s.metadata->allocation_prefix)return pending("Reflected factory requires represented metadata+20 allocation prefix.");
            s.prefix=*s.metadata->allocation_prefix;
            if(!s.pool)fail("Reflected factory dereferences a null selected allocation pool.");
            {const auto bytes=s.prefix+s.metadata->native_size;const auto pool=s.pool;
                s.allocation_services=services;
                auto& call=select(K::allocate,P::after_allocation);call.pool=pool;call.bytes=bytes;}break;
        case P::after_allocation: {
            s.allocation=std::move(s.call.operation->allocation_result);
            const auto base=s.allocation?s.allocation->original_address:0u;
            clear();
            if(base+s.prefix==0)return finish();
            // The nonzero result of original pointer addition does not prove
            // physical storage. The initializer reaches its own byte/binding
            // checks, including a null allocation plus a nonzero prefix.
            s.initializer=std::make_unique<IgbObjectInitInvocation>(s.allocation,s.prefix,s.metadata);
            s.phase=P::initialize;break;
        }
        case P::initialize: {
            const auto result=s.initializer->resume(services?services->initializer:nullptr,check);check();if(!result.completed)return result;
            s.result=s.initializer->result_instance();return finish();
        }
        case P::call: {
            if(s.call.request.kind==K::allocate && !s.allocation_services && services)s.allocation_services=services;
            const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;
        }
        case P::done:return {true,0,{}};
        }}
    }catch(...){s.fault=true;throw;}
}

struct IgbFileFactoryInvocation::Impl {
    enum class Phase {entry,registration,metadata,construct,done};
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<LoaderRequestFile> result;
    std::unique_ptr<IgbReflectedFactoryInvocation> factory;
    Call call;
    Phase phase=Phase::entry;
    bool running{},fault{};
};
IgbFileFactoryInvocation::IgbFileFactoryInvocation(std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){impl_->pool=std::move(pool);}
IgbFileFactoryInvocation::~IgbFileFactoryInvocation()=default;
bool IgbFileFactoryInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbFileFactoryInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<LoaderRequestFile> IgbFileFactoryInvocation::result() const {
    if(!completed() || faulted())fail("Incomplete file factory has no returned file.");return impl_->result;
}
MessengerCallResult IgbFileFactoryInvocation::resume(std::shared_ptr<IgbFileFactoryServices> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault){s.fault=true;fail("File factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&]{if(s.fault)fail("File factory continuation was invalidated.");};
    try {
        for(;;){check();switch(s.phase){
        case P::entry: {
            IgbFileFactoryCall call;call.kind=IgbFileFactoryServiceKind::register_file_type;
            s.call.select(std::move(call),services);check();s.phase=P::registration;break;
        }
        case P::registration: {
            const auto result=s.call.poll(services,check);if(!result.completed)return result;
            s.call.clear();check();s.phase=P::metadata;break;
        }
        case P::metadata:
            if(!services || !services->file_metadata)return pending("File factory requires its live80561A04 metadata global after registration.");
            s.factory=std::make_unique<IgbReflectedFactoryInvocation>(*services->file_metadata,s.pool);s.phase=P::construct;break;
        case P::construct: {
            const auto result=s.factory->resume(services);check();if(!result.completed)return result;
            s.result=s.factory->result();s.factory.reset();check();s.phase=P::done;return {true,0,{}};
        }
        case P::done:return {true,0,{}};
        }}
    }catch(...){s.fault=true;throw;}
}

LoaderDependencyFactory make_native_igb_file_factory(std::shared_ptr<IgbFileFactoryServices> services,LoaderDependencyFactory remaining) {
    return [weak_services=std::weak_ptr(services),remaining=std::move(remaining)](const LoaderDependencyCall& call)
        ->std::shared_ptr<LoaderDependencyOperation> {
        if(call.kind!=LoaderDependencyServiceKind::create_igb_file)return remaining?remaining(call):nullptr;
        const auto selected=weak_services.lock();if(!selected)fail("IGB dependency file factory requires its live canonical services.");
        auto invocation=std::make_shared<IgbFileFactoryInvocation>(call.object);
        auto operation=std::make_shared<LoaderDependencyOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[invocation,selected,weak] {const auto result=invocation->resume(selected);
            if(result.completed)weak.lock()->file_result=invocation->result();return result;};return operation;
    };
}
LoaderRequestFactory make_native_igb_request_file_factory(std::shared_ptr<IgbFileFactoryServices> services,LoaderRequestFactory remaining) {
    return [weak_services=std::weak_ptr(services),remaining=std::move(remaining)](const LoaderRequestCall& call)
        ->std::shared_ptr<LoaderRequestOperation> {
        if(call.kind!=LoaderRequestServiceKind::create_igb_file)return remaining?remaining(call):nullptr;
        const auto selected=weak_services.lock();if(!selected)fail("IGB ordinary file factory requires its live canonical services.");
        auto invocation=std::make_shared<IgbFileFactoryInvocation>(call.object);
        auto operation=std::make_shared<LoaderRequestOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[invocation,selected,weak] {const auto result=invocation->resume(selected);
            if(result.completed)weak.lock()->file_result=invocation->result();return result;};return operation;
    };
}
IgbObjectInitFactory make_native_igb_file_init_factory(std::shared_ptr<IgbFileFactoryServices> services,
    std::shared_ptr<IgbFilePoolsServices> pools,IgbObjectInitFactory remaining) {
    return [weak_services=std::weak_ptr(services),weak_pools=std::weak_ptr(pools),remaining=std::move(remaining)]
        (const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation> {
        if(call.kind!=IgbObjectInitServiceKind::object_virtual || call.vtable_word!=0x804724c4u)
            return remaining?remaining(call):nullptr;
        if(call.virtual_slot==0x0c || call.virtual_slot==0x24 || call.virtual_slot==0x2c) {
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[] {return MessengerCallResult{true,0,{}};};return operation;
        }
        if(call.virtual_slot==0x58) {
            const auto selected=weak_services.lock();if(!selected)fail("IGB metadata getter requires its live canonical global owner.");
            auto operation=std::make_shared<IgbObjectInitOperation>();const auto weak=std::weak_ptr(operation);
            operation->resume=[selected,weak] {
                if(!selected->file_metadata)return pending("IGB metadata getter requires represented80561A04 global storage.");
                weak.lock()->metadata_result=*selected->file_metadata;return MessengerCallResult{true,0,{}};
            };return operation;
        }
        if(call.virtual_slot==0x34) {
            if(!call.object.file || call.object.field || call.object.metadata || call.object.fields || call.object.types || call.object.runtime_object)fail("IGB file pool initialization requires a file layout binding.");
            const auto selected=weak_pools.lock();if(!selected)fail("IGB post-default pools require their live canonical services.");
            auto invocation=std::make_shared<IgbFilePoolsInvocation>(call.object.file);
            auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[selected,invocation] {return invocation->resume(selected);};return operation;
        }
        return remaining?remaining(call):nullptr;
    };
}
IgbFileFactoryFactory make_native_igb_factory_pool_factory(std::shared_ptr<IgbFilePoolsServices> services,
    IgbFileFactoryFactory remaining) {
    return [weak_services=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFileFactoryCall& call)
        ->std::shared_ptr<IgbFileFactoryOperation> {
        if(call.kind!=IgbFileFactoryServiceKind::current_pool)return remaining?remaining(call):nullptr;
        const auto selected=weak_services.lock();if(!selected)fail("Reflected current-pool adapter requires its live canonical services.");
        auto invocation=std::make_shared<IgbCurrentPoolInvocation>();
        auto operation=std::make_shared<IgbFileFactoryOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[invocation,selected,weak] {
            const auto result=invocation->resume(selected);
            if(result.completed)weak.lock()->object_result=invocation->result();return result;
        };return operation;
    };
}
} // namespace dw4
