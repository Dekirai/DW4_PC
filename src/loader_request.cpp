#include "dw4/loader_request.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw LoaderRequestError(reason);}
MessengerCallResult pending(const char* reason) {return {false,0,reason};}
std::int32_t add_word(std::int32_t value,std::uint32_t amount) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+amount);
}
template<class T> void release_field(std::shared_ptr<T>& field) {
    if(field && field.use_count()==1)
        fail("Loader request reaches a final object destructor whose effects are not represented.");
    field.reset();
}
const char* boundary(LoaderRequestServiceKind kind) {
    switch(kind) {
    case LoaderRequestServiceKind::create_igb_file:return "Loader request requires concrete igIGBFile factory8002EBEC and its post-defaults.";
    case LoaderRequestServiceKind::create_mutex:return "Loader request requires mutex factory80025FE8(null).";
    case LoaderRequestServiceKind::initialize_mutex:return "Loader request requires the selected mutex initialization virtual+5C.";
    case LoaderRequestServiceKind::create_string_pool:return "Loader request requires string-pool allocation80054140 and conditional initialization80053F28.";
    case LoaderRequestServiceKind::intern_filename:return "Loader request requires filename interning80054094.";
    case LoaderRequestServiceKind::resize_directory_list:return "Loader request requires list metadata/allocation helper8004155C.";
    case LoaderRequestServiceKind::enqueue_directory:return "Loader request requires captured queue virtual+68(file,true).";
    case LoaderRequestServiceKind::decode_igb:return "Loader requires concrete igIGBFile decoder800528DC, including native deserialization/post-load operations.";
    }
    return "Loader request requires its reached original operation.";
}
struct Call {
    LoaderRequestCall request;
    LoaderRequestFactory factory;
    std::shared_ptr<LoaderRequestOperation> operation;
    std::function<MessengerCallResult()> callback;
    void begin(LoaderRequestCall value,const std::shared_ptr<LoaderRequestServices>& services) {
        request=std::move(value);factory=services ? services->factory:LoaderRequestFactory{};
        operation.reset();callback={};
    }
    template<class Check> MessengerCallResult poll(const std::shared_ptr<LoaderRequestServices>& services,Check&& check) {
        if(!factory && services)factory=services->factory;
        if(!factory)return pending(boundary(request.kind));
        if(!operation) {
            operation=factory(request);check();
            if(!operation || !operation->resume)fail("A loader operation factory returned no valid continuation.");
            callback=operation->resume;
        }
        const auto result=callback();check();
        if(result.completed) {callback={};factory={};check();}
        return result;
    }
    void clear() {callback={};factory={};request={};operation.reset();}
};
SystemStageString path(const SystemStageString& prefix,const SystemStageString& name) {
    if(!name)fail("Loader path formatting reaches a null filename for %s.");
    const auto prefix_size=std::strlen(prefix->c_str()),name_size=std::strlen(name->c_str());
    if(prefix_size>2047 || name_size>2047 || prefix_size+1+name_size>2047)
        fail("Loader path exceeds the represented original 2048-byte formatting buffer.");
    return std::make_shared<const std::string>(std::string(prefix->c_str(),prefix_size)+"/"+std::string(name->c_str(),name_size));
}
}

struct LoaderFileLoadInvocation::Impl {
    enum class Phase {entry,primary,secondary,bare,call,done};
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<SystemStageDirectory> directory;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    bool owns{},running{},fault{};
};
LoaderFileLoadInvocation::LoaderFileLoadInvocation(std::shared_ptr<SystemStageLoader> loader,
    std::shared_ptr<SystemStageDirectory> directory):impl_(std::make_unique<Impl>()) {
    impl_->loader=std::move(loader);impl_->directory=std::move(directory);
}
LoaderFileLoadInvocation::~LoaderFileLoadInvocation() {
    if(impl_->owns && !completed())impl_->file->load_fault_=true;
}
bool LoaderFileLoadInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderFileLoadInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<SystemStageDirectory> LoaderFileLoadInvocation::result() const {
    if(!completed() || faulted())fail("Loader file result requires a completed, unfaulted invocation.");
    return impl_->directory;
}
MessengerCallResult LoaderFileLoadInvocation::resume(std::shared_ptr<LoaderRequestServices> services) {
    auto& s=*impl_;
    if(s.fault || s.running) {s.fault=true;if(s.owns)s.file->load_fault_=true;fail("Loader file call requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Guard {bool& flag;Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.file->load_fault_))fail("Loader file invocation was invalidated by a recursive operation.");};
    const auto finish=[&] {
        s.call.clear();check();s.phase=Impl::Phase::done;
        if(s.owns) {s.file->active_load_.reset();s.owns=false;s.token.reset();}
        return MessengerCallResult{true,0,{}};
    };
    const auto select=[&](SystemStageString filename,Impl::Phase after) {
        LoaderRequestCall request;request.kind=LoaderRequestServiceKind::decode_igb;
        request.loader=s.loader;request.file=s.file;request.filename=std::move(filename);
        s.call.begin(std::move(request),services);s.after=after;s.phase=Impl::Phase::call;
    };
    try {
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            if(!s.directory)return finish(); // Original does not read loader on this branch.
            if(!s.loader)fail("Loader file call dereferences a null loader for its first prefix.");
            s.file=std::dynamic_pointer_cast<LoaderRequestFile>(s.directory);
            if(!s.file)return pending("Loader file call requires the represented concrete directory virtual+74 binding.");
            if(s.file->load_fault_)fail("A faulted loader file cannot replay its decoder prefix.");
            if(!s.file->active_load_.expired())fail("Another pending invocation owns this loader file call.");
            s.file->active_load_=s.token;s.owns=true;s.phase=Impl::Phase::primary;break;
        case Impl::Phase::primary:
            if(s.loader->primary_prefix && !s.loader->primary_prefix->empty() && s.loader->primary_prefix->front()!=0)
                select(path(s.loader->primary_prefix,s.directory->filename),Impl::Phase::secondary);
            else s.phase=Impl::Phase::secondary;
            break;
        case Impl::Phase::secondary:
            if(s.loader->secondary_prefix && !s.loader->secondary_prefix->empty() && s.loader->secondary_prefix->front()!=0)
                select(path(s.loader->secondary_prefix,s.directory->filename),Impl::Phase::bare);
            else s.phase=Impl::Phase::bare;
            break;
        case Impl::Phase::bare:select(s.directory->filename,Impl::Phase::done);break;
        case Impl::Phase::call: {
            const auto result=s.call.poll(services,check);if(!result.completed)return result;
            const auto value=s.call.operation->load_result;
            if(s.after==Impl::Phase::done || value>0)return finish();
            s.call.clear();check();s.phase=s.after;break;
        }
        case Impl::Phase::done:return {true,0,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.file->load_fault_=true;throw;}
}

struct LoaderDirectoryRequestInvocation::Impl {
    enum class Phase {entry,lookup,found_claim,factory,after_factory,pools,async_mutex,after_mutex,
        after_mutex_init,publish_loading,string_pool,after_string_pool,intern,after_intern,
        first_claim,append,after_resize,store_append,branch,second_claim,queue,after_queue,load,call,done};
    std::shared_ptr<SystemStageLoader> loader;
    SystemStageString filename;
    std::shared_ptr<SystemStageDirectory> directory;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<LoaderDirectoryList> list;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<LoaderDirectoryLookupInvocation> lookup;
    std::unique_ptr<LoaderDirectoryClaimInvocation> claim;
    std::unique_ptr<LoaderFileLoadInvocation> load;
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    std::int32_t append_index{},append_count{};
    unsigned pool_index{};
    bool owns{},running{},fault{},retained_append{};
};
LoaderDirectoryRequestInvocation::LoaderDirectoryRequestInvocation(std::shared_ptr<SystemStageLoader> loader,
    SystemStageString filename):impl_(std::make_unique<Impl>()) {
    impl_->loader=std::move(loader);impl_->filename=std::move(filename);
}
LoaderDirectoryRequestInvocation::~LoaderDirectoryRequestInvocation() {
    if(impl_->owns && !completed()) {
        impl_->loader->request_fault_=true;
        if(impl_->directory)impl_->loader->unreleased_directories_.push_back(std::move(impl_->directory));
    }
}
bool LoaderDirectoryRequestInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderDirectoryRequestInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<SystemStageDirectory> LoaderDirectoryRequestInvocation::result() const {
    if(!completed() || faulted())fail("Directory request result requires a completed, unfaulted invocation.");
    return impl_->directory;
}
MessengerCallResult LoaderDirectoryRequestInvocation::resume(std::shared_ptr<LoaderRequestServices> services) {
    auto& s=*impl_;
    if(s.fault || s.running) {s.fault=true;if(s.owns)s.loader->request_fault_=true;fail("Directory request requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Guard {bool& flag;Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.loader->request_fault_))fail("Directory request was invalidated by a recursive operation.");};
    const auto finish=[&] {
        s.call.clear();s.lookup.reset();s.claim.reset();s.load.reset();check();
        s.phase=Impl::Phase::done;s.loader->active_request_.reset();s.owns=false;s.token.reset();
        return MessengerCallResult{true,0,{}};
    };
    const auto select=[&](LoaderRequestServiceKind kind,Impl::Phase after) -> LoaderRequestCall& {
        LoaderRequestCall request;request.kind=kind;request.loader=s.loader;request.file=s.file;
        s.call.begin(std::move(request),services);s.after=after;s.phase=Impl::Phase::call;return s.call.request;
    };
    const auto directory_services=[&] {return services ? services->directory:std::shared_ptr<LoaderDirectoryServices>{};};
    const auto claim=[&]() {
        if(!s.claim)s.claim=std::make_unique<LoaderDirectoryClaimInvocation>(s.directory,LoaderDirectoryClaim::acquire);
        const auto result=s.claim->resume(directory_services());check();
        if(result.completed)s.claim.reset();return result;
    };
    try {
        if(!s.loader)fail("Directory request requires its represented loader owner.");
        if(!s.owns) {
            if(s.loader->request_fault_)fail("A faulted directory request cannot replay its loader prefix.");
            if(!s.loader->active_request_.expired())fail("Another pending directory request owns this loader.");
            s.loader->active_request_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case Impl::Phase::entry:
            s.lookup=std::make_unique<LoaderDirectoryLookupInvocation>(s.loader,s.filename);
            s.phase=Impl::Phase::lookup;break;
        case Impl::Phase::lookup: {
            const auto result=s.lookup->resume(directory_services());check();if(!result.completed)return result;
            s.directory=s.lookup->result();s.lookup.reset();check();
            s.phase=s.directory ? Impl::Phase::found_claim:Impl::Phase::factory;break;
        }
        case Impl::Phase::found_claim: {const auto result=claim();if(!result.completed)return result;return finish();}
        case Impl::Phase::factory:
            select(LoaderRequestServiceKind::create_igb_file,Impl::Phase::after_factory).object=s.loader->pool_38;break;
        case Impl::Phase::after_factory:
            s.file=std::move(s.call.operation->file_result);s.directory=s.file;s.call.clear();check();
            if(!s.file)fail("Directory request dereferences a null igIGBFile factory result.");
            s.file->format_24=s.loader->format_44;
            s.file->option_a4=s.loader->option_48;
            s.file->option_41=s.loader->option_08;
            s.phase=Impl::Phase::pools;break;
        case Impl::Phase::pools: {
            auto pool=s.pool_index==0 ? s.loader->pool_38:s.pool_index==1 ? s.loader->pool_3c:s.loader->pool_40;
            if(pool) {
                auto& field=s.pool_index==0 ? s.file->pool_118:s.pool_index==1 ? s.file->pool_11c:s.file->pool_120;
                try {release_field(field);field=std::move(pool);check();}
                catch(...) {
                    // Original retains incoming before releasing the old field.
                    // Preserve that acquired reference even when destruction is
                    // the reached unsupported boundary.
                    if(pool)s.loader->unpublished_request_objects_.push_back(std::move(pool));
                    throw;
                }
            }
            if(++s.pool_index==3)s.phase=Impl::Phase::async_mutex;break;
        }
        case Impl::Phase::async_mutex:
            if(s.loader->async_1c==1) {
                s.file->initialize_after_load_115=s.loader->initialize_after_load_34;
                if(!s.file->mutex)select(LoaderRequestServiceKind::create_mutex,Impl::Phase::after_mutex);
                else s.phase=Impl::Phase::publish_loading;
            }else s.phase=Impl::Phase::publish_loading;
            break;
        case Impl::Phase::after_mutex:
            // Factory callbacks may have published a replacement after the
            // original null test. Its raw store does not release that owner.
            if(s.file->mutex)s.loader->unpublished_request_mutexes_.push_back(std::move(s.file->mutex));
            s.file->mutex=std::move(s.call.operation->mutex_result);s.call.clear();check();
            if(!s.file->mutex)fail("Directory request dereferences a null mutex after publishing its factory result.");
            select(LoaderRequestServiceKind::initialize_mutex,Impl::Phase::after_mutex_init).mutex=s.file->mutex;break;
        case Impl::Phase::after_mutex_init:s.call.clear();check();s.phase=Impl::Phase::publish_loading;break;
        case Impl::Phase::publish_loading:
            s.file->succeeded_31=0;s.file->loading=1;s.phase=Impl::Phase::string_pool;break;
        case Impl::Phase::string_pool:
            if(!services || !services->globals)return pending("Directory request requires its represented string-pool singleton owner.");
            if(!services->globals->string_pool)select(LoaderRequestServiceKind::create_string_pool,Impl::Phase::after_string_pool);
            else s.phase=Impl::Phase::intern;
            break;
        case Impl::Phase::after_string_pool:
            if(!services || !services->globals)return pending("Directory request requires its live string-pool singleton for publication.");
            if(services->globals->string_pool)s.loader->unpublished_request_objects_.push_back(std::move(services->globals->string_pool));
            services->globals->string_pool=std::move(s.call.operation->object_result);s.call.clear();check();s.phase=Impl::Phase::intern;break;
        case Impl::Phase::intern:
            if(!services || !services->globals)return pending("Directory request requires its live string-pool singleton for interning.");
            if(!s.filename) {s.file->filename.reset();s.file->loader=s.loader;s.phase=Impl::Phase::first_claim;}
            else {
                auto& request=select(LoaderRequestServiceKind::intern_filename,Impl::Phase::after_intern);
                request.object=services->globals->string_pool;request.filename=s.filename;
            }
            break;
        case Impl::Phase::after_intern:
            s.file->filename=std::move(s.call.operation->string_result);s.file->loader=s.loader;
            s.call.clear();check();s.phase=Impl::Phase::first_claim;break;
        case Impl::Phase::first_claim: {
            const auto result=claim();if(!result.completed)return result;s.phase=Impl::Phase::append;break;
        }
        case Impl::Phase::append: {
            s.retained_append=true;s.list=s.loader->directories;
            if(!s.list)fail("Directory append dereferences a null loader list after retaining the file.");
            s.append_index=s.list->count;s.append_count=add_word(s.append_index,1);
            if(s.append_index<s.list->capacity) {
                s.list->count=s.append_count;s.phase=Impl::Phase::store_append;
            }else {
                auto capacity=std::max<std::int32_t>(s.list->capacity,4);
                while(capacity<s.append_count) {
                    if(capacity>=262144)fail("Directory append exceeds the bounded native allocation profile.");
                    capacity=capacity<0x400 ? capacity*2:capacity+0x400;
                }
                if(capacity>262144)fail("Directory append exceeds the bounded native allocation profile.");
                auto& request=select(LoaderRequestServiceKind::resize_directory_list,Impl::Phase::after_resize);
                request.list=s.list;request.capacity=capacity;
            }
            break;
        }
        case Impl::Phase::after_resize:
            s.call.clear();check();s.list->count=s.append_count;s.phase=Impl::Phase::store_append;break;
        case Impl::Phase::store_append: {
            const auto backing=s.list->storage;
            if(s.append_index<0 || s.append_index>=262144 || !backing || static_cast<std::size_t>(s.append_index)>=backing->size())
                fail("Directory append reaches an unavailable physical slot after publishing its count.");
            auto& field=(*backing)[static_cast<std::size_t>(s.append_index)];
            // The original raw store does not release an overwritten spare slot.
            if(field)s.loader->unreleased_directories_.push_back(std::move(field));
            field=s.directory;s.retained_append=false;s.list.reset();s.phase=Impl::Phase::branch;break;
        }
        case Impl::Phase::branch:
            s.phase=s.loader->async_1c!=0 ? Impl::Phase::second_claim:Impl::Phase::load;break;
        case Impl::Phase::second_claim: {
            const auto result=claim();if(!result.completed)return result;s.phase=Impl::Phase::queue;break;
        }
        case Impl::Phase::queue: {
            const auto queue=s.loader->queue_20;
            if(!queue)fail("Directory request dereferences a null asynchronous queue after its second loading claim.");
            auto& request=select(LoaderRequestServiceKind::enqueue_directory,Impl::Phase::after_queue);
            request.object=queue;request.queue_wait=true;break;
        }
        case Impl::Phase::after_queue:s.call.clear();check();return finish();
        case Impl::Phase::load: {
            if(!s.load)s.load=std::make_unique<LoaderFileLoadInvocation>(s.loader,s.directory);
            const auto result=s.load->resume(services);check();if(!result.completed)return result;
            s.directory=s.load->result();s.load.reset();return finish();
        }
        case Impl::Phase::call: {
            const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;break;
        }
        case Impl::Phase::done:return {true,0,{}};
        }}
    }catch(...) {
        s.fault=true;
        if(s.owns) {
            s.loader->request_fault_=true;
            if(s.retained_append && s.directory)s.loader->unreleased_directories_.push_back(s.directory);
        }
        throw;
    }
}
} // namespace dw4
