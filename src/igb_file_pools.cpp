#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_stream.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbFilePoolsError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
}
struct IgbPoolByHandleInvocation::Impl {
    enum class Phase {selector,owner,dispatch,pool,flags,prepare,reload,search_selector,search,previous,call,done};
    Phase phase=Phase::selector;
    std::int32_t selector{},index{};
    std::uint32_t handle{};
    std::shared_ptr<IgbMemoryPoolRegistry> manager;
    std::vector<std::shared_ptr<IgbMemoryPoolRegistry>> callers;
    std::shared_ptr<ModelSceneObject> result;
    IgbFilePoolsCall request;
    IgbFilePoolsFactory factory;
    std::shared_ptr<IgbFilePoolsOperation> operation;
    std::function<MessengerCallResult()> callback;
    bool running{},fault{};
};
IgbPoolByHandleInvocation::IgbPoolByHandleInvocation(std::uint32_t handle):impl_(std::make_unique<Impl>()) {impl_->handle=handle;}
IgbPoolByHandleInvocation::~IgbPoolByHandleInvocation()=default;
bool IgbPoolByHandleInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbPoolByHandleInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<ModelSceneObject> IgbPoolByHandleInvocation::result() const {
    if(!completed() || faulted())fail("Incomplete pool-by-handle lookup has no result.");return impl_->result;
}
MessengerCallResult IgbPoolByHandleInvocation::resume(std::shared_ptr<IgbFilePoolsServices> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) {s.fault=true;fail("Pool-by-handle lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running {bool& v;Running(bool& v):v(v){v=true;}~Running(){v=false;}} running(s.running);
    const auto check=[&] {if(s.fault)fail("Pool-by-handle continuation was invalidated.");};
    const auto active=[&](std::int32_t index)->std::optional<std::shared_ptr<IgbMemoryPoolRegistry>> {
        if(!services || !services->memory_pools || index<0 || index>=3)return {};
        return services->memory_pools->active[static_cast<std::size_t>(index)];
    };
    try {
        for(;;) {check();switch(s.phase) {
        case P::selector:
            if(!services || !services->memory_pools || !services->memory_pools->selector)
                return pending("Pool-by-handle lookup requires selector global805621DC.");
            s.selector=*services->memory_pools->selector;s.phase=P::owner;break;
        case P::owner:
            if(s.selector==0) {
                if(!services || !services->memory_pools || !services->memory_pools->fallback)
                    return pending("Pool-by-handle lookup requires fallback global805621C8.");
                s.manager=*services->memory_pools->fallback;
            }else if(s.selector>0 && s.selector<4) {
                const auto owner=active(s.selector-1);
                if(!owner)return pending("Pool-by-handle lookup requires its selected804F2294 entry.");
                s.manager=*owner;
            }
            s.phase=P::dispatch;break;
        case P::dispatch:
            if(!s.manager)fail("Pool-by-handle lookup dereferences a null selected manager.");
            if(s.manager->original_pool_access_bound)s.phase=P::pool;
            else {
                s.request={};s.request.kind=IgbFilePoolsServiceKind::pool_virtual;s.request.manager=s.manager;s.request.handle=s.handle;
                s.factory=services?services->factory:IgbFilePoolsFactory{};s.phase=P::call;
            }
            break;
        case P::pool:
            if(s.handle>=46u){s.result.reset();s.phase=P::done;break;}
            if(!s.manager->pools[s.handle])return pending("Pool-by-handle access requires its captured physical manager slot.");
            s.result=*s.manager->pools[s.handle];s.phase=s.result?P::done:P::flags;break;
        case P::flags:
            s.phase=(igb_buffer_word(s.manager->inline_bytes,0xa0u+s.handle*0x90u)&0x04000000u)!=0?P::prepare:P::search_selector;
            break;
        case P::prepare:
            s.request={};s.request.kind=IgbFilePoolsServiceKind::prepare_pool;s.request.manager=s.manager;s.request.handle=s.handle;
            s.factory=services?services->factory:IgbFilePoolsFactory{};s.phase=P::call;break;
        case P::reload:
            if(!s.manager->pools[s.handle])return pending("Pool-by-handle access requires its reloaded physical manager slot.");
            s.result=*s.manager->pools[s.handle];s.phase=P::done;break;
        case P::search_selector:
            if(!services || !services->memory_pools || !services->memory_pools->selector)
                return pending("Pool-by-handle fallback requires a fresh selector global805621DC.");
            s.selector=*services->memory_pools->selector;
            if(s.selector<=1)s.phase=P::done;
            else {s.index=s.selector-1;s.phase=P::search;}
            break;
        case P::search: {
            const auto candidate=active(s.index);
            if(!candidate)return pending("Pool-by-handle fallback reaches an unrepresented physical active-manager entry.");
            if(*candidate==s.manager) {--s.index;s.phase=s.index<0?P::done:P::previous;}
            else if(--s.index<0)s.phase=P::done;
            break;
        }
        case P::previous: {
            const auto candidate=active(s.index);
            if(!candidate)return pending("Pool-by-handle fallback requires the preceding active-manager entry.");
            if(*candidate==s.manager) {s.phase=P::done;break;}
            if(s.callers.size()>=64)fail("Pool-by-handle fallback exceeds the bounded native call depth.");
            s.callers.push_back(s.manager);s.manager=*candidate;s.phase=P::dispatch;break;
        }
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(s.request.kind==IgbFilePoolsServiceKind::prepare_pool?
                "Pool-by-handle access requires manager+D0 pool preparation8005E7AC.":"Pool-by-handle lookup requires captured manager virtual+5C.");
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending("The reached pool-manager operation is not bound.");
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending("The reached pool-manager operation has no continuation.");
            {const auto progress=s.callback();check();if(!progress.completed)return progress;}
            {
                const bool prepare=s.request.kind==IgbFilePoolsServiceKind::prepare_pool;
                if(!prepare)s.result=s.operation->object_result;
                // Complete the selected call's native lifetime before the
                // original caller performs any fresh state read.
                s.callback={};s.operation.reset();s.factory={};s.request={};check();
                s.phase=prepare?P::reload:P::done;
            }
            break;
        case P::done:return {true,0,{}};
        }}
    }catch(...) {s.fault=true;throw;}
}

struct IgbCurrentPoolInvocation::Impl {IgbPoolByHandleInvocation lookup{2};};
IgbCurrentPoolInvocation::IgbCurrentPoolInvocation():impl_(std::make_unique<Impl>()){}
IgbCurrentPoolInvocation::~IgbCurrentPoolInvocation()=default;
bool IgbCurrentPoolInvocation::completed() const noexcept{return impl_->lookup.completed();}
bool IgbCurrentPoolInvocation::faulted() const noexcept{return impl_->lookup.faulted();}
std::shared_ptr<ModelSceneObject> IgbCurrentPoolInvocation::result() const{return impl_->lookup.result();}
MessengerCallResult IgbCurrentPoolInvocation::resume(std::shared_ptr<IgbFilePoolsServices> services){
    return impl_->lookup.resume(std::move(services));
}

struct IgbFilePoolsInvocation::Impl {
    enum class Phase {entry,lookup,retain,release,publish,call,done};
    Phase phase=Phase::entry;
    std::shared_ptr<LoaderRequestFile> file;
    std::unique_ptr<IgbCurrentPoolInvocation> lookup;
    std::shared_ptr<ModelSceneObject> incoming,old;
    IgbFilePoolsCall request;
    IgbFilePoolsFactory factory;
    std::shared_ptr<IgbFilePoolsOperation> operation;
    std::function<MessengerCallResult()> callback;
    unsigned index{};
    bool running{},fault{},started{},retained{};
};
IgbFilePoolsInvocation::IgbFilePoolsInvocation(std::shared_ptr<LoaderRequestFile> file)
    :impl_(std::make_unique<Impl>()) {impl_->file=std::move(file);}
IgbFilePoolsInvocation::~IgbFilePoolsInvocation() {
    auto& s=*impl_;
    if(!completed() && s.started && s.file) {
        s.file->pool_initialization_fault_=true;
        if(s.retained && s.incoming)s.file->unpublished_pools_.push_back(s.incoming);
        if(s.request.object)s.file->unpublished_pools_.push_back(s.request.object);
    }
}
bool IgbFilePoolsInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbFilePoolsInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult IgbFilePoolsInvocation::resume(std::shared_ptr<IgbFilePoolsServices> services) {
    auto& s=*impl_;using P=Impl::Phase;
    if(s.running || s.fault) {s.fault=true;if(s.file)s.file->pool_initialization_fault_=true;fail("IGB file pool initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running {bool& v;Running(bool& v):v(v){v=true;}~Running(){v=false;}} running(s.running);
    const auto check=[&] {if(s.fault || (s.started && s.file && s.file->pool_initialization_fault_))fail("IGB file pool initialization was invalidated.");};
    const auto field=[&]() ->std::shared_ptr<ModelSceneObject>& {
        if(!s.file)fail("IGB file pool initialization dereferences a null file.");
        return s.index==0?s.file->pool_118:s.index==1?s.file->pool_11c:s.file->pool_120;
    };
    try {
        for(;;) {check();switch(s.phase) {
        case P::entry:
            if(s.file && s.file->pool_initialization_fault_)fail("An invalidated file pool prefix cannot restart.");
            s.started=true;s.phase=P::lookup;break;
        case P::lookup: {
            if(!s.lookup)s.lookup=std::make_unique<IgbCurrentPoolInvocation>();
            const auto progress=s.lookup->resume(services);check();if(!progress.completed)return progress;
            s.incoming=s.lookup->result();s.lookup.reset();check();s.phase=P::retain;break;
        }
        case P::retain:
            if(s.incoming)++s.incoming->reference_word;
            s.retained=true;s.old=field();s.phase=P::release;break;
        case P::release:
            if(s.old) {
                --s.old->reference_word;
                if((s.old->reference_word&0x7fffffu)==0) {
                    s.request={};s.request.kind=IgbFilePoolsServiceKind::destroy_object;s.request.object=s.old;
                    s.factory=services?services->factory:IgbFilePoolsFactory{};s.phase=P::call;break;
                }
            }
            s.phase=P::publish;break;
        case P::publish: {
            auto& target=field();
            if(target && target!=s.old)s.file->unpublished_pools_.push_back(target);
            target=s.incoming;s.retained=false;
            s.incoming.reset();s.old.reset();s.operation.reset();s.callback={};s.factory={};s.request={};check();
            s.phase=++s.index==3?P::done:P::lookup;break;
        }
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending("IGB file pool replacement requires final destruction80066E1C.");
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending("The reached pool destructor80066E1C is not bound.");
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending("The reached pool destructor has no continuation.");
            {const auto progress=s.callback();check();if(!progress.completed)return progress;}
            s.callback={};s.operation.reset();s.factory={};s.request={};check();
            s.phase=P::publish;break;
        case P::done:return {true,0,{}};
        }}
    }catch(...) {s.fault=true;if(s.file)s.file->pool_initialization_fault_=true;throw;}
}
} // namespace dw4
