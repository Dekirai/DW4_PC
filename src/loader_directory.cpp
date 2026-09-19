#include "dw4/loader_directory.hpp"

#include <bit>
#include <cstring>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw LoaderDirectoryError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
struct Running {
    bool& value;
    explicit Running(bool& value):value(value) {value=true;}
    ~Running() {value=false;}
};
std::int32_t advance(std::int32_t value,std::uint32_t amount) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+amount);
}
bool has_parent(const std::weak_ptr<SystemStageLoader>& value) {
    const std::weak_ptr<SystemStageLoader> empty;
    return value.owner_before(empty) || empty.owner_before(value);
}
std::shared_ptr<SystemStageDirectory>& physical(const std::shared_ptr<LoaderDirectoryList::Storage>& storage,
    std::int32_t index) {
    if(index<0 || index>=262144 || !storage || static_cast<std::size_t>(index)>=storage->size())
        fail("Loader directory reaches an unavailable physical list slot.");
    return (*storage)[static_cast<std::size_t>(index)];
}
void release_owner(std::shared_ptr<SystemStageDirectory>& owner) {
    if(owner && owner.use_count()==1)
        fail("Loader directory reaches a final object destructor whose effects are not represented.");
    owner.reset();
}
}

struct LoaderDirectoryLookupInvocation::Impl {
    enum class Phase {entry,field,root,done};
    std::shared_ptr<SystemStageLoader> loader;
    SystemStageString filename;
    NameComparison compare;
    std::shared_ptr<LoaderDirectoryList> list;
    std::shared_ptr<SystemStageDirectory> result;
    decltype(LoaderDirectoryServices::root_directory) factory;
    std::shared_ptr<LoaderDirectoryRootOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry;
    bool running{},fault{};
};
LoaderDirectoryLookupInvocation::LoaderDirectoryLookupInvocation(std::shared_ptr<SystemStageLoader> loader,
    SystemStageString filename):impl_(std::make_unique<Impl>()) {
    impl_->loader=std::move(loader);impl_->filename=std::move(filename);
}
LoaderDirectoryLookupInvocation::~LoaderDirectoryLookupInvocation()=default;
LoaderDirectoryLookupInvocation::LoaderDirectoryLookupInvocation(std::shared_ptr<SystemStageLoader> loader,
    NameComparison compare,LazyNameTag):impl_(std::make_unique<Impl>()) {
    impl_->loader=std::move(loader);impl_->compare=std::move(compare);
}
bool LoaderDirectoryLookupInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderDirectoryLookupInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<SystemStageDirectory> LoaderDirectoryLookupInvocation::result() const {
    if(!completed())fail("An incomplete directory lookup has no published result.");
    return impl_->result;
}
MessengerCallResult LoaderDirectoryLookupInvocation::resume(std::shared_ptr<LoaderDirectoryServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {s.fault=true;fail("Directory lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&] {if(s.fault)fail("The retained directory lookup was invalidated.");};
    try {
        if(s.phase==Impl::Phase::entry) {
            if(!s.filename && !s.compare)fail("Original directory lookup compares a null filename.");
            const bool root=s.compare?s.compare(std::make_shared<const std::string>("igImageDirectory"),true):
                std::strcmp(s.filename->c_str(),"igImageDirectory")==0;
            check();
            if(!s.loader)fail("Original directory lookup dereferences a null loader.");
            if(root)s.phase=Impl::Phase::root;
            else {s.list=s.loader->directories;s.phase=Impl::Phase::field;}
        }
        if(s.phase==Impl::Phase::field) {
            if(!services)return pending("Directory lookup requires its original filename-field global binding.");
            const auto field=services->filename_field;
            if(field) {
                if(!s.list)fail("Original named directory lookup dereferences a null list.");
                const auto count=s.list->count;
                for(std::int32_t index=0;index<count;++index) {
                    const auto storage=s.list->storage;
                    const auto offset=field->offset;
                    const auto directory=physical(storage,index);
                    if(!directory)fail("Original named directory lookup dereferences a null row.");
                    if(offset!=0x14)fail("Directory filename metadata selects an unrepresented field.");
                    const auto filename=directory->filename;
                    const bool matched=filename && (s.compare?s.compare(filename,false):
                        std::strcmp(filename->c_str(),s.filename->c_str())==0);
                    check();
                    if(matched) {
                        s.result=directory;break;
                    }
                }
            }
            s.phase=Impl::Phase::done;return {true,0,{}};
        }
        if(!s.factory && services)s.factory=services->root_directory;
        if(!s.factory)return pending("Directory lookup requires actual igImageDirectory accessor8006F5E0.");
        if(!s.operation) {s.operation=s.factory(s.loader);check();}
        if(!s.operation)return pending("Original igImageDirectory accessor is unavailable.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Original igImageDirectory accessor has no continuation.");
        const auto result=s.callback();check();
        if(!result.completed)return result;
        s.result=s.operation->result;
        s.callback={};s.operation.reset();s.factory={};check();
        s.phase=Impl::Phase::done;return {true,0,{}};
    }catch(...) {s.fault=true;throw;}
}

struct LoaderDirectoryClaimInvocation::Impl {
    enum class Phase {entry,lock,after_lock,unlock,remove,done};
    std::shared_ptr<SystemStageDirectory> directory;
    std::shared_ptr<LoaderDirectoryMutex> mutex;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::function<MessengerCallResult()> callback;
    LoaderDirectoryClaim kind{};
    Phase phase=Phase::entry;
    std::uint32_t lock_result{};
    std::int32_t value{};
    bool running{},fault{},owns{},remove{};
};
LoaderDirectoryClaimInvocation::LoaderDirectoryClaimInvocation(std::shared_ptr<SystemStageDirectory> directory,
    LoaderDirectoryClaim kind):impl_(std::make_unique<Impl>()) {
    impl_->directory=std::move(directory);impl_->kind=kind;
}
LoaderDirectoryClaimInvocation::~LoaderDirectoryClaimInvocation() {
    if(impl_->owns && !completed())impl_->directory->claim_fault_=true;
}
bool LoaderDirectoryClaimInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderDirectoryClaimInvocation::faulted() const noexcept {return impl_->fault;}
std::int32_t LoaderDirectoryClaimInvocation::value() const {
    if(!completed())fail("An incomplete directory claim has no return value.");
    return impl_->value;
}
MessengerCallResult LoaderDirectoryClaimInvocation::resume(std::shared_ptr<LoaderDirectoryServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {
        s.fault=true;if(s.owns)s.directory->claim_fault_=true;
        fail("Directory claims require an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,std::bit_cast<std::uint32_t>(s.value),{}};
    Running running(s.running);
    const auto check=[&] {
        if(s.fault || s.directory->claim_fault_ || s.directory->active_claim_.lock()!=s.token)
            fail("The retained directory claim owner was invalidated.");
    };
    const auto finish=[&] {
        check();s.callback={};s.mutex.reset();check();
        s.phase=Impl::Phase::done;s.directory->active_claim_.reset();s.owns=false;
        return MessengerCallResult{true,std::bit_cast<std::uint32_t>(s.value),{}};
    };
    const auto unlock=[&](bool required) {
        s.mutex=s.directory->mutex;
        if(required && !s.mutex)fail("Original claim acquisition dereferences its replaced null mutex.");
        s.phase=s.mutex ? Impl::Phase::unlock:Impl::Phase::remove;
    };
    try {
        if(!s.directory)fail("Original directory claim dereferences a null directory.");
        if(!s.owns) {
            if(s.directory->claim_fault_)fail("A faulted directory claim prefix cannot restart.");
            if(!s.directory->active_claim_.expired())fail("Another invocation owns this directory claim.");
            s.directory->active_claim_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            s.mutex=s.directory->mutex;
            if(s.mutex)s.phase=Impl::Phase::lock;
            else {
                s.value=advance(s.directory->claim_count,s.kind==LoaderDirectoryClaim::acquire ? 1u:0xffffffffu);
                s.directory->claim_count=s.value;
                if(s.kind==LoaderDirectoryClaim::acquire)return finish();
                s.remove=s.value<1;unlock(false);
            }
            break;
        case Impl::Phase::lock: {
            if(!s.callback)s.callback=s.mutex->acquire;
            if(!s.callback)return pending("Directory claim requires its mutex virtual+84(true).");
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.lock_result=result.value;s.callback={};s.mutex.reset();check();
            s.phase=Impl::Phase::after_lock;break;
        }
        case Impl::Phase::after_lock:
            if(s.kind==LoaderDirectoryClaim::acquire) {
                if(!services || !services->mutex_success)
                    return pending("Directory claim requires the live original mutex-success word.");
                const auto success=*services->mutex_success;
                s.value=s.directory->claim_count;
                if(s.lock_result!=success)return finish();
                s.value=advance(s.value,1);s.directory->claim_count=s.value;
                s.remove=false;unlock(true);
            } else {
                s.value=advance(s.directory->claim_count,0xffffffffu);s.directory->claim_count=s.value;
                s.remove=s.value<1;unlock(false);
            }
            break;
        case Impl::Phase::unlock: {
            if(!s.callback)s.callback=s.mutex->release;
            if(!s.callback)return pending("Directory claim requires its reloaded mutex virtual+7C.");
            const auto result=s.callback();check();
            if(!result.completed)return result;
            s.callback={};s.mutex.reset();check();s.phase=Impl::Phase::remove;break;
        }
        case Impl::Phase::remove: {
            if(!s.remove)return finish();
            if(!has_parent(s.directory->loader))return finish();
            const auto loader=s.directory->loader.lock();
            if(!loader)fail("Directory claim reaches an expired nonnull loader pointer.");
            const auto list=loader->directories;
            if(!list)fail("Directory claim dereferences a null parent list.");
            const auto count=list->count;
            std::int32_t found=-1;
            for(std::int32_t index=0;index<count;++index)
                if(physical(list->storage,index)==s.directory) {found=index;break;}
            // The original unconditionally reads backing[-1] when absent.
            // Report that reached invalid access, after decrement/unlock.
            auto storage=list->storage;
            release_owner(physical(storage,found));
            const auto current_count=list->count;
            if(current_count>0 && found>=0 && found<current_count) {
                if(found!=current_count-1) {
                    storage=list->storage;
                    for(auto index=found;index<current_count-1;++index)
                        physical(storage,index)=std::move(physical(storage,index+1));
                }
                list->count=advance(list->count,0xffffffffu);
            }
            storage=list->storage;
            physical(storage,list->count).reset();
            return finish();
        }
        case Impl::Phase::done:return {true,std::bit_cast<std::uint32_t>(s.value),{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.directory->claim_fault_=true;throw;}
}

struct LoaderDirectoryReleaseInvocation::Impl {
    enum class Phase {lookup,claim,clear_root,release_temporary,done};
    std::shared_ptr<SystemStageLoader> loader;
    SystemStageString filename;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<LoaderDirectoryLookupInvocation> lookup;
    std::unique_ptr<LoaderDirectoryClaimInvocation> claim;
    std::shared_ptr<SystemStageDirectory> directory;
    Phase phase=Phase::lookup;
    bool running{},fault{},owns{},was_root{};
    std::int32_t remaining{};
};
LoaderDirectoryReleaseInvocation::LoaderDirectoryReleaseInvocation(std::shared_ptr<SystemStageLoader> loader,
    SystemStageString filename):impl_(std::make_unique<Impl>()) {
    impl_->loader=std::move(loader);impl_->filename=std::move(filename);
}
LoaderDirectoryReleaseInvocation::~LoaderDirectoryReleaseInvocation() {
    if(impl_->owns && !completed()) {
        impl_->loader->release_fault_=true;
        if(impl_->directory)impl_->loader->unreleased_directories_.push_back(std::move(impl_->directory));
    }
}
bool LoaderDirectoryReleaseInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderDirectoryReleaseInvocation::faulted() const noexcept {return impl_->fault;}
MessengerCallResult LoaderDirectoryReleaseInvocation::resume(std::shared_ptr<LoaderDirectoryServices> services) {
    auto& s=*impl_;
    if(s.running || s.fault) {
        s.fault=true;if(s.owns)s.loader->release_fault_=true;
        fail("Directory release requires an unfaulted nonrecursive invocation.");
    }
    if(completed())return {true,0,{}};
    Running running(s.running);
    const auto check=[&] {
        if(s.fault || s.loader->release_fault_ || s.loader->active_release_.lock()!=s.token)
            fail("The retained directory release owner was invalidated.");
    };
    const auto finish=[&] {
        check();s.phase=Impl::Phase::done;s.loader->active_release_.reset();s.owns=false;
        return MessengerCallResult{true,0,{}};
    };
    try {
        if(!s.loader)fail("Directory claim release requires its original loader owner.");
        if(!s.owns) {
            if(s.loader->release_fault_)fail("A faulted directory release prefix cannot restart.");
            if(!s.loader->active_release_.expired())fail("Another invocation owns this loader claim release.");
            s.loader->active_release_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::lookup:
            if(!s.lookup)s.lookup=std::make_unique<LoaderDirectoryLookupInvocation>(s.loader,s.filename);
            if(const auto result=s.lookup->resume(services);!result.completed)return result;
            s.directory=s.lookup->result();s.lookup.reset();check();
            if(!s.directory)return finish();
            s.was_root=s.directory==s.loader->root_directory;
            if(!has_parent(s.directory->loader))s.directory->loader=s.loader;
            s.claim=std::make_unique<LoaderDirectoryClaimInvocation>(s.directory,LoaderDirectoryClaim::release);
            s.phase=Impl::Phase::claim;break;
        case Impl::Phase::claim:
            if(const auto result=s.claim->resume(services);!result.completed)return result;
            s.remaining=s.claim->value();s.claim.reset();check();s.phase=Impl::Phase::clear_root;break;
        case Impl::Phase::clear_root:
            if(s.was_root && s.remaining<1)release_owner(s.loader->root_directory);
            s.phase=Impl::Phase::release_temporary;break;
        case Impl::Phase::release_temporary:
            release_owner(s.directory);return finish();
        case Impl::Phase::done:return {true,0,{}};
        }}
    }catch(...) {
        s.fault=true;
        if(s.owns) {
            s.loader->release_fault_=true;
            // An original temporary still owns the reference at a destructor
            // boundary. Keep it beyond destruction of this failed continuation.
            if(s.directory)s.loader->unreleased_directories_.push_back(std::move(s.directory));
        }
        throw;
    }
}
} // namespace dw4
