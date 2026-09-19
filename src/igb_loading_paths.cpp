#include "dw4/igb_loading_paths.hpp"
#include "dw4/igb_memory_pools.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbLoadingPathsError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}
std::int32_t next(std::int32_t value) {return signed_word(std::bit_cast<std::uint32_t>(value)+1u);}
std::shared_ptr<IgbScratchBuffer> allocate(std::uint32_t size) {
    if(size>64u*1024u*1024u)fail("IGB path allocation exceeds the64MiB native profile.");
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.resize(size);result->initialized.resize(size);return result;
}
std::uint32_t length(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t offset) {
    for(std::uint32_t i=0;i<1024u*1024u;++i)if(igb_buffer_bytes(buffer,offset+i,1).front()==0)return i;
    fail("IGB path C string exceeds the1MiB native profile.");
}
void zero(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t offset) {
    if(!buffer || buffer->released || offset>=buffer->bytes.size() || buffer->initialized.size()!=buffer->bytes.size())
        fail("IGB path terminator store reaches unavailable allocation memory.");
    buffer->bytes[offset]=0;buffer->initialized[offset]=1;
}
std::int32_t growth(std::int32_t capacity,std::int32_t requested) {
    capacity=std::max<std::int32_t>(capacity,4);
    while(capacity<requested) {
        if(capacity>=262144)fail("IGB pool list exceeds its bounded allocation profile.");
        capacity=capacity<1024?capacity*2:capacity+1024;
    }
    if(capacity>262144)fail("IGB pool list exceeds its bounded allocation profile.");return capacity;
}
const char* boundary(IgbLoadingPathsServiceKind kind) {
    using K=IgbLoadingPathsServiceKind;
    switch(kind) {
    case K::duplicate_path:return "IGB directory path800505E0 requires captured pool virtual+F0 filename duplication.";
    case K::allocate_pool_names:return "IGB pool-name table80052554 requires captured pool virtual+D8 allocation.";
    case K::free_pool_names:return "IGB pool-name table requires live pool virtual+108 release.";
    case K::create_pool_handle_list:return "IGB pool-name table requires list constructor8003657C.";
    case K::reserve_pool_handle_list:return "IGB pool-name append requires list allocation8004155C.";
    }
    return "IGB loading paths require their reached native operation.";
}
struct Work {
    enum class Phase {entry,path_result,path_scan,header,allocation,body,list,after_list,names,lookup,
        append,after_resize,store,after_name,padding,free_success,free_failure,status,call,done};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbLoadingPathsState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<IgbScratchBuffer> header,buffer;
    std::shared_ptr<IgbPoolHandleList> list;
    std::unique_ptr<IgbMemoryPoolLookupInvocation> lookup;
    IgbLoadingPathsCall request;
    IgbLoadingPathsFactory factory;
    std::shared_ptr<IgbLoadingPathsOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry,after=Phase::done;
    std::uint32_t bytes{},remaining{},cursor{},handle{},result{};
    std::int32_t named{},index{},requested{};
    bool pool_table{},padding{},success{},owns{},running{},fault{};
};
MessengerCallResult run(Work& s,const std::shared_ptr<IgbLoadingPathsServices>& services,
    std::weak_ptr<void>& active,bool& state_fault,std::vector<std::shared_ptr<IgbScratchBuffer>>& unreleased) {
    using P=Work::Phase;using K=IgbLoadingPathsServiceKind;
    if(s.running || s.fault) {s.fault=true;if(s.owns)state_fault=true;fail("IGB loading paths require an unfaulted nonrecursive invocation.");}
    if(s.phase==P::done)return {true,s.result,{}};
    struct Guard {bool& value;Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(s.running);
    const auto check=[&] {if(s.fault || (s.owns && state_fault))fail("IGB loading paths were invalidated by recursive work.");};
    const auto clear=[&] {s.callback={};s.factory={};s.request={};s.operation.reset();check();};
    const auto select=[&](K kind,P after)->IgbLoadingPathsCall& {
        clear();s.request.kind=kind;s.request.file=s.file;s.request.stream=s.stream;s.request.state=s.state;
        s.factory=services?services->factory:IgbLoadingPathsFactory{};s.after=after;s.phase=P::call;return s.request;
    };
    const auto status=[&](bool value) {s.success=value;s.phase=P::status;};
    const auto free=[&](P after) {
        const auto pool=s.file->pool_118;
        if(!pool)fail("IGB pool-name free dereferences a null live pool.");
        auto& call=select(K::free_pool_names,after);call.pool=pool;call.buffer=s.buffer;
    };
    try {
        if(!s.file || !s.stream || !s.state || s.stream->file.lock()!=s.file)
            fail("IGB loading paths require their canonical file, stream and field owners.");
        if(!s.owns) {
            if(state_fault)fail("Faulted IGB loading paths cannot restart their prefix.");
            if(!active.expired())fail("Another invocation owns this pending loading-path operation.");
            active=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case P::entry:
            if(s.pool_table) {s.header=allocate(8);s.phase=P::header;break;}
            {
                const auto pool=s.file->pool_118;const auto stream=s.stream->stream;
                if(!pool)fail("IGB directory duplication selects a null pool virtual.");
                if(!stream)fail("IGB directory duplication reads a null stream filename.");
                auto& call=select(K::duplicate_path,P::path_result);call.pool=pool;call.filename=stream->filename;break;
            }
        case P::path_result:
            if(s.state->directory_path)unreleased.push_back(s.state->directory_path);
            s.state->directory_path=std::move(s.operation->buffer_result);clear();s.phase=P::path_scan;break;
        case P::path_scan: {
            auto index=signed_word(length(s.state->directory_path,0));
            while(--index>=0) {
                const auto path=s.state->directory_path;const auto byte=igb_buffer_bytes(path,static_cast<std::uint32_t>(index),1).front();
                if(byte=='/' || byte=='\\') {zero(path,static_cast<std::uint32_t>(index));break;}
            }
            if(index<0)zero(s.state->directory_path,0);status(true);break;
        }
        case P::header:
            if(igb_read_elements(s.stream->stream,s.header,0,8,1)!=1) {status(false);break;}
            if(s.stream->swapped!=0) {
                if(!s.stream->int_field)fail("IGB pool-name header selects a null integer converter.");
                igb_swap_words(s.header,0,s.stream->int_field->width,2);
            }
            s.bytes=igb_buffer_word(s.header,0);s.remaining=igb_buffer_word(s.header,4);
            {
                const auto pool=s.file->pool_118;if(!pool)fail("IGB pool-name allocation selects a null pool.");
                auto& call=select(K::allocate_pool_names,P::allocation);call.pool=pool;call.bytes=s.bytes;break;
            }
        case P::allocation:
            s.buffer=std::move(s.operation->buffer_result);clear();
            if(!s.buffer)status(false);else s.phase=P::body;break;
        case P::body:
            if(igb_read_elements(s.stream->stream,s.buffer,0,s.bytes-8u,1)!=1) {free(P::free_failure);break;}
            s.phase=P::list;break;
        case P::list:
            if(s.state->pool_handles) {s.state->pool_handles->count=0;s.phase=P::names;}
            else select(K::create_pool_handle_list,P::after_list).pool=s.file->pool_118;
            break;
        case P::after_list:
            s.state->pool_handles=std::move(s.operation->list_result);clear();s.phase=P::names;break;
        case P::names: {
            const auto count=signed_word(s.remaining);--s.remaining;
            if(count<=0) {s.phase=P::padding;break;}
            if(s.named>=262144)fail("IGB named-pool iteration exceeds the native profile.");
            s.lookup=std::make_unique<IgbMemoryPoolLookupInvocation>(IgbRuntimeNameSource{s.buffer,s.cursor});
            s.padding=false;s.phase=P::lookup;break;
        }
        case P::lookup: {
            const auto result=s.lookup->resume(services?services->memory_pools:nullptr);check();if(!result.completed)return result;
            s.handle=s.lookup->value();s.lookup.reset();s.phase=P::append;break;
        }
        case P::append:
            s.list=s.state->pool_handles;if(!s.list)fail("IGB pool-name append reads a null handle list.");
            s.index=s.list->count;
            if(s.index<s.list->capacity) {s.list->count=next(s.index);s.phase=P::store;}
            else {
                s.requested=next(s.index);const auto capacity=growth(s.list->capacity,s.requested);
                auto& call=select(K::reserve_pool_handle_list,P::after_resize);call.list=s.list;call.capacity=capacity;
            }
            break;
        case P::after_resize:clear();s.list->count=s.requested;s.phase=P::store;break;
        case P::store:
            if(s.padding) {
                if(!services || !services->default_pool_handle)return pending("IGB pool-name padding requires live default handle8055D89C.");
                s.handle=*services->default_pool_handle;
            }
            if(s.index<0 || s.index>=262144 || !s.list->backing || static_cast<std::size_t>(s.index)>=s.list->backing->size())
                fail("IGB pool-name append reaches an unavailable physical handle slot.");
            s.list->backing->at(static_cast<std::size_t>(s.index))=s.handle;s.list.reset();
            if(s.padding) {++s.named;s.phase=P::padding;}else s.phase=P::after_name;
            break;
        case P::after_name:s.cursor+=length(s.buffer,s.cursor)+1u;++s.named;s.phase=P::names;break;
        case P::padding:
            if(s.named<46) {s.padding=true;s.phase=P::append;}else free(P::free_success);
            break;
        case P::free_success:clear();status(true);break;
        case P::free_failure:clear();status(false);break;
        case P::status: {
            const auto statuses=s.stream->statuses;
            const auto value=statuses?(s.success?statuses->success:statuses->failure):std::optional<std::uint32_t>{};
            if(!value)return pending("IGB loading paths require the reached success/failure word.");
            s.result=*value;active.reset();s.owns=false;s.phase=P::done;return {true,s.result,{}};
        }
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending(boundary(s.request.kind));
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending(boundary(s.request.kind));
            {const auto result=s.callback();check();if(!result.completed)return result;s.phase=s.after;break;}
        case P::done:return {true,s.result,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)state_fault=true;throw;}
}
}

IgbLoadingPathsFactory make_native_igb_path_pool_factory(IgbLoadingPathsFactory remaining) {
    return [remaining=std::move(remaining)](const IgbLoadingPathsCall& request) {
        using K=IgbLoadingPathsServiceKind;
        if(request.kind==K::create_pool_handle_list || request.kind==K::reserve_pool_handle_list) {
            if(remaining)return remaining(request);
            auto result=std::make_shared<IgbLoadingPathsOperation>();result->resume=[kind=request.kind] {return pending(boundary(kind));};return result;
        }
        auto operation=std::make_shared<IgbLoadingPathsOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[request,weak] {
            if(!request.pool || !request.stream)fail("Native path storage requires its selected pool and stream-state identity.");
            bool bound{};for(const auto& binding:request.stream->native_scratch_pools)if(binding.owner.lock()==request.pool) {bound=true;break;}
            if(!bound)return pending("IGB path storage requires an explicit native binding for the captured pool.");
            const auto output=weak.lock();if(!output)fail("IGB path storage result owner was destroyed.");
            if(request.kind==K::free_pool_names) {
                if(request.buffer) {
                    if(request.buffer->released)fail("Native path storage reaches an already freed allocation.");
                    request.buffer->released=true;
                }
            }else if(request.kind==K::allocate_pool_names) {
                try {output->buffer_result=allocate(request.bytes);}catch(const std::bad_alloc&) {output->buffer_result.reset();}
            }else {
                if(!request.filename)fail("Native path duplication consumes a null filename.");
                const auto end=request.filename->find('\0');const auto size=end==std::string::npos?request.filename->size():end;
                if(size>=1024u*1024u)fail("Native path duplication exceeds the1MiB C-string profile.");
                try {
                    auto value=allocate(static_cast<std::uint32_t>(size+1));
                    std::copy_n(request.filename->begin(),size,value->bytes.begin());value->bytes[size]=0;
                    std::fill(value->initialized.begin(),value->initialized.end(),std::uint8_t{1});output->buffer_result=std::move(value);
                }catch(const std::bad_alloc&) {output->buffer_result.reset();}
            }
            return MessengerCallResult{true,0,{}};
        };return operation;
    };
}
struct IgbDirectoryPathInvocation::Impl:Work {};
IgbDirectoryPathInvocation::IgbDirectoryPathInvocation(std::shared_ptr<LoaderRequestFile> file,
    std::shared_ptr<IgbStreamState> stream,std::shared_ptr<IgbLoadingPathsState> state):impl_(std::make_unique<Impl>()) {
    impl_->file=std::move(file);impl_->stream=std::move(stream);impl_->state=std::move(state);
}
IgbDirectoryPathInvocation::~IgbDirectoryPathInvocation() {if(impl_->owns && !completed())impl_->state->path_fault_=true;}
bool IgbDirectoryPathInvocation::completed() const noexcept {return impl_->phase==Work::Phase::done;}
bool IgbDirectoryPathInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbDirectoryPathInvocation::value() const {if(!completed() || faulted())fail("Directory-path result requires completed unfaulted work.");return impl_->result;}
MessengerCallResult IgbDirectoryPathInvocation::resume(std::shared_ptr<IgbLoadingPathsServices> services) {
    if(!impl_->state)fail("IGB directory path requires its canonical state.");
    return run(*impl_,services,impl_->state->active_path_,impl_->state->path_fault_,impl_->state->unreleased_);
}
struct IgbPoolNamesInvocation::Impl:Work {};
IgbPoolNamesInvocation::IgbPoolNamesInvocation(std::shared_ptr<LoaderRequestFile> file,
    std::shared_ptr<IgbStreamState> stream,std::shared_ptr<IgbLoadingPathsState> state):impl_(std::make_unique<Impl>()) {
    impl_->file=std::move(file);impl_->stream=std::move(stream);impl_->state=std::move(state);impl_->pool_table=true;
}
IgbPoolNamesInvocation::~IgbPoolNamesInvocation() {
    if(impl_->owns && !completed()) {
        impl_->state->pools_fault_=true;if(impl_->buffer)impl_->state->unreleased_.push_back(impl_->buffer);
    }
}
bool IgbPoolNamesInvocation::completed() const noexcept {return impl_->phase==Work::Phase::done;}
bool IgbPoolNamesInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbPoolNamesInvocation::value() const {if(!completed() || faulted())fail("Pool-name result requires completed unfaulted work.");return impl_->result;}
MessengerCallResult IgbPoolNamesInvocation::resume(std::shared_ptr<IgbLoadingPathsServices> services) {
    if(!impl_->state)fail("IGB pool names require their canonical state.");
    return run(*impl_,services,impl_->state->active_pools_,impl_->state->pools_fault_,impl_->state->unreleased_);
}
} // namespace dw4
