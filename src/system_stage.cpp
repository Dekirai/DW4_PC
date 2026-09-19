#include "dw4/system_stage.hpp"

#include <cstring>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw SystemStageError(reason);}
MessengerCallResult pending(const char* reason) {return {false,0,reason};}
template<class T> void release(std::shared_ptr<T>& value) {
    if(value && value.use_count()==1)
        fail("System stage reaches a final object destructor whose effects are not represented.");
    value.reset();
}
std::shared_ptr<ModelSceneObject> physical(const std::shared_ptr<ModelSceneList::Storage>& backing,std::int32_t index) {
    if(index<0 || index>=262144 || !backing || static_cast<std::size_t>(index)>=backing->size())
        fail("System stage reaches an unavailable physical directory info slot.");
    return (*backing)[static_cast<std::size_t>(index)];
}
const char* boundary(SystemStageServiceKind kind) {
    switch(kind) {
    case SystemStageServiceKind::object_pool:return "System stage requires original object-pool lookup80068430.";
    case SystemStageServiceKind::named_pool:return "System stage requires original named-pool resolution800607F4.";
    case SystemStageServiceKind::prepare_loading:return "System stage requires original loader preparation8006F794.";
    case SystemStageServiceKind::load_directory:return "System stage requires original directory request8006FD34.";
    case SystemStageServiceKind::create_string_pool:return "System stage requires the original string-pool allocation/initialization.";
    case SystemStageServiceKind::register_info:return "System stage requires original context info registration8028A7DC.";
    case SystemStageServiceKind::release_claim:return "System stage requires original directory-claim release80070140.";
    case SystemStageServiceKind::finish_loading:return "System stage requires original loader cleanup8006F8CC.";
    }
    return "System stage requires its reached original operation.";
}
}
struct SystemStageInvocation::Impl {
    enum class Phase {entry,lookup_pool,after_object_pool,after_named_pool,assign_pool,prepare,
        after_prepare,after_load,ensure_string_pool,after_string_pool,clear_string,
        inspect_directory,visit_info,after_info,release_claim,after_release_claim,
        clear_directory,finish_loading,after_finish,call,done};
    std::shared_ptr<ModelSceneSystem> system;
    std::shared_ptr<SystemStageState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    SystemStageRequest request;
    SystemStageFactory factory;
    std::shared_ptr<SystemStageOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<SystemStageDirectory> directory;
    std::shared_ptr<ModelSceneList> infos;
    Phase phase=Phase::entry,after=Phase::done;
    std::int32_t index{};
    unsigned pool_index{};
    bool common{},clear_stage_name{},retain_pool{},retain_directory{},owns{},running{},fault{};
};
SystemStageInvocation::SystemStageInvocation(std::shared_ptr<ModelSceneSystem> system,
    std::shared_ptr<SystemStageState> state):impl_(std::make_unique<Impl>()) {
    impl_->system=std::move(system);impl_->state=std::move(state);
}
SystemStageInvocation::~SystemStageInvocation() {
    if(impl_->owns && !completed())impl_->state->fault_=true;
}
bool SystemStageInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool SystemStageInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult SystemStageInvocation::resume(std::shared_ptr<SystemStageServices> services) {
    auto& s=*impl_;
    if(s.fault || s.running) {
        s.fault=true;if(s.owns)s.state->fault_=true;
        fail("System stage requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || s.state->fault_)fail("A recursive or invalidated operation faulted the system stage.");};
    const auto finish=[&] {
        check();
        s.request={};s.operation.reset();s.callback={};s.factory={};
        check();s.phase=Impl::Phase::done;s.state->active_.reset();s.token.reset();s.owns=false;
        return MessengerCallResult{true,0,{}};
    };
    const auto select=[&](SystemStageServiceKind kind,Impl::Phase after) {
        s.request={};s.request.kind=kind;s.request.system=s.system;
        s.factory=services ? services->factory:SystemStageFactory{};
        s.operation.reset();s.callback={};s.after=after;s.phase=Impl::Phase::call;
    };
    const auto loader=[&] {
        if(!services->loader)fail("System stage dereferences a represented null loader singleton.");
        return services->loader;
    };
    try {
        if(!s.system || !s.state)fail("System stage requires its original system and stage field owners.");
        if(!s.owns) {
            if(s.state->fault_)fail("A faulted system stage cannot restart its published prefix.");
            if(!s.state->active_.expired())fail("Another pending invocation owns this system stage.");
            s.state->active_=s.token;s.owns=true;
        }
        check();
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            if(s.state->next_stage) {
                s.common=std::strcmp(s.state->next_stage->c_str(),"ndmw.igb")==0;
                s.phase=Impl::Phase::lookup_pool;
            }else if(s.state->boot_directory)s.phase=Impl::Phase::inspect_directory;
            else return finish();
            break;
        case Impl::Phase::lookup_pool:
            select(SystemStageServiceKind::object_pool,Impl::Phase::after_object_pool);break;
        case Impl::Phase::after_object_pool:
            if(!services)return pending("System stage requires represented global pool-selection state.");
            s.pool=std::move(s.operation->object_result);s.operation.reset();
            if(services->use_object_pool!=0)s.phase=Impl::Phase::assign_pool;
            else {
                s.pool.reset();
                const auto handle=s.common ? services->common_pool_handle:services->stage_pool_handle;
                select(SystemStageServiceKind::named_pool,Impl::Phase::after_named_pool);
                s.request.handle=handle;
            }
            break;
        case Impl::Phase::after_named_pool:
            s.pool=std::move(s.operation->object_result);s.operation.reset();s.phase=Impl::Phase::assign_pool;break;
        case Impl::Phase::assign_pool: {
            if(!services)return pending("System stage requires its live loader singleton for pool publication.");
            // The original retains the selected pool before releasing the old
            // field. Keep that acquired reference if the later access faults.
            s.retain_pool=bool(s.pool);
            const auto owner=loader();
            auto& field=s.pool_index==0 ? owner->pool_40:owner->pool_3c;
            release(field);field=std::move(s.pool);s.retain_pool=false;
            s.phase=++s.pool_index<2 ? Impl::Phase::lookup_pool:Impl::Phase::prepare;break;
        }
        case Impl::Phase::prepare:
            if(!services)return pending("System stage requires its live loader singleton for preparation.");
            {const auto owner=loader();select(SystemStageServiceKind::prepare_loading,Impl::Phase::after_prepare);s.request.loader=owner;}
            break;
        case Impl::Phase::after_prepare:
            if(!services)return pending("System stage requires its live loader singleton for a directory request.");
            {const auto name=s.state->next_stage;const auto owner=loader();
            select(SystemStageServiceKind::load_directory,Impl::Phase::after_load);s.request.loader=owner;s.request.filename=name;}
            break;
        case Impl::Phase::after_load:
            s.directory=std::move(s.operation->directory_result);s.operation.reset();
            s.retain_directory=bool(s.directory);
            release(s.state->boot_directory);s.state->boot_directory=std::move(s.directory);s.retain_directory=false;
            s.clear_stage_name=false;s.phase=Impl::Phase::ensure_string_pool;break;
        case Impl::Phase::ensure_string_pool:
            if(!services)return pending("System stage requires its represented string-pool singleton.");
            if(services->string_pool)s.phase=Impl::Phase::clear_string;
            else select(SystemStageServiceKind::create_string_pool,Impl::Phase::after_string_pool);
            break;
        case Impl::Phase::after_string_pool:
            if(!services)return pending("System stage requires its live string-pool singleton for publication.");
            // Original creation publishes its returned pointer, including null,
            // without releasing a concurrently replaced singleton.
            if(services->string_pool)
                s.state->unpublished_objects_.push_back(std::move(services->string_pool));
            services->string_pool=std::move(s.operation->object_result);s.operation.reset();
            s.phase=Impl::Phase::clear_string;break;
        case Impl::Phase::clear_string:
            // 80054094(pool,null) returns null without dereferencing the pool.
            // Native immutable string storage has a known host destructor.
            if(s.clear_stage_name) {s.state->stage_name.reset();s.phase=Impl::Phase::clear_directory;}
            else {s.state->next_stage.reset();return finish();}
            break;
        case Impl::Phase::inspect_directory:
            if(!s.state->boot_directory) {
                s.clear_stage_name=true;s.phase=Impl::Phase::ensure_string_pool;break;
            }
            // 80070130 is a pure leaf. Its loader argument is unused.
            if(s.state->boot_directory->loading!=0)return finish();
            s.infos=s.state->boot_directory->infos;
            if(!s.infos)fail("System stage dereferences a null completed-directory info list.");
            s.index=0;s.phase=Impl::Phase::visit_info;break;
        case Impl::Phase::visit_info:
            if(s.index>=s.infos->count) {s.infos.reset();s.phase=Impl::Phase::release_claim;break;}
            {const auto backing=s.infos->backing;const auto context=s.system->context;
            const auto info=physical(backing,s.index);
            select(SystemStageServiceKind::register_info,Impl::Phase::after_info);
            s.request.context=context;s.request.info=info;}
            break;
        case Impl::Phase::after_info:
            s.operation.reset();++s.index;s.phase=Impl::Phase::visit_info;break;
        case Impl::Phase::release_claim:
            if(!services)return pending("System stage requires its live loader for directory-claim release.");
            {const auto directory=s.state->boot_directory;const auto owner=loader();
            if(!directory)fail("System stage dereferences a null live directory filename after registration.");
            select(SystemStageServiceKind::release_claim,Impl::Phase::after_release_claim);
            s.request.loader=owner;s.request.filename=directory->filename;}
            break;
        case Impl::Phase::after_release_claim:
            s.operation.reset();s.phase=Impl::Phase::clear_directory;break;
        case Impl::Phase::clear_directory:
            release(s.state->boot_directory);
            if(s.clear_stage_name)return finish();
            s.phase=Impl::Phase::finish_loading;break;
        case Impl::Phase::finish_loading:
            if(!services)return pending("System stage requires its live loader for completion cleanup.");
            {const auto owner=loader();select(SystemStageServiceKind::finish_loading,Impl::Phase::after_finish);s.request.loader=owner;}
            break;
        case Impl::Phase::after_finish:s.operation.reset();return finish();
        case Impl::Phase::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {
                s.operation=s.factory(s.request);check();
                if(!s.operation || !s.operation->resume)fail("A system stage factory returned no reached operation.");
                s.callback=s.operation->resume;
            }
            {const auto result=s.callback();check();if(!result.completed)return result;}
            s.callback={};s.factory={};s.request={};s.phase=s.after;break;
        case Impl::Phase::done:return {true,0,{}};
        }
        }
    }catch(...) {
        s.fault=true;
        if(s.owns) {
            s.state->fault_=true;
            if(s.retain_pool && s.pool)s.state->unpublished_objects_.push_back(std::move(s.pool));
            if(s.retain_directory && s.directory)s.state->unpublished_directories_.push_back(std::move(s.directory));
        }
        throw;
    }
}
} // namespace dw4
