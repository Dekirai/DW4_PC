#include "dw4/igb_dependencies.hpp"

#include <algorithm>
#include <bit>
#include <new>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbDependenciesError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::int32_t signed_word(std::uint32_t value) {return std::bit_cast<std::int32_t>(value);}
std::int32_t increment(std::int32_t value) {return signed_word(std::bit_cast<std::uint32_t>(value)+1u);}
std::shared_ptr<SystemStageLoader> loader_owner(const std::weak_ptr<SystemStageLoader>& value) {
    const std::weak_ptr<SystemStageLoader> empty;
    const auto owner=value.lock();
    if(!owner && (value.owner_before(empty) || empty.owner_before(value)))
        fail("IGB dependency caller reaches an expired nonnull loader owner.");
    return owner;
}
const char* boundary(IgbDependenciesServiceKind kind) {
    using K=IgbDependenciesServiceKind;
    switch(kind) {
    case K::allocate_body:return "IGB dependency table requires captured pool virtual+DC(size,16).";
    case K::free_body:return "IGB dependency table requires captured pool virtual+108(buffer).";
    case K::loader:return "IGB dependency table requires loader singleton getter8007042C.";
    case K::create_string_pool:return "IGB dependency table requires string-pool allocation80054140(16).";
    case K::initialize_string_pool:return "IGB dependency table requires string-pool initialization80053F28.";
    case K::intern_name:return "IGB dependency table requires captured string interning80054094.";
    case K::release_string:return "IGB dependency table requires final string-container release8007381C.";
    case K::load_dependency:return "IGB dependency table requires synchronous loader request8006FA6C.";
    case K::reserve_directories:return "IGB dependency append requires list allocation8004155C(width4).";
    }
    return "IGB dependency table requires its reached operation.";
}
IgbDependencyName name_pointer(const SystemStageString& value) {
    if(value)return value;
    return std::monostate{};
}
}

void igb_bind_interned_string(const std::shared_ptr<IgbInternedStringRegistry>& registry,
    const std::shared_ptr<IgbInternedString>& owner) {
    if(!registry || !owner || !owner->value || owner->released)
        fail("Interned-string binding requires a live owner, nonnull value and registry.");
    for(const auto& candidate:registry->owners)if(candidate && candidate->value==owner->value && !candidate->released) {
        if(candidate!=owner)fail("One interned string allocation has conflicting reference-count owners.");
        return;
    }
    registry->owners.push_back(owner);
}
SystemStageString igb_dependency_name(const IgbDependencyName& name) {
    return std::visit([](const auto& source)->SystemStageString {
        using T=std::decay_t<decltype(source)>;
        if constexpr(std::is_same_v<T,std::monostate>)return {};
        else if constexpr(std::is_same_v<T,SystemStageString>) {
            if(!source)fail("IGB dependency name has an unrepresented nonnull string pointer.");
            return source;
        }else return igb_runtime_read_name(source);
    },name);
}
IgbDependencyName igb_interned_string_name(const std::shared_ptr<IgbInternedString>& owner) {
    if(!owner)return {};
    if(owner->native_name)return *owner->native_name;
    return name_pointer(owner->value);
}
IgbDependenciesFactory make_native_igb_dependency_pool_factory(IgbDependenciesFactory remaining) {
    return [remaining=std::move(remaining)](const IgbDependenciesCall& request)->std::shared_ptr<IgbDependenciesOperation> {
        using K=IgbDependenciesServiceKind;
        if(request.kind==K::allocate_body || request.kind==K::free_body) {
            bool bound{};
            if(request.stream && request.pool)for(const auto& binding:request.stream->native_scratch_pools)
                if(binding.owner.lock()==request.pool) {bound=true;break;}
            if(bound) {
                auto result=std::make_shared<IgbDependenciesOperation>();const auto weak=std::weak_ptr(result);
                result->resume=[request,weak] {
                    if(request.kind==K::allocate_body) {
                        if(request.bytes>64u*1024u*1024u)fail("IGB dependency allocation exceeds the 64MiB native profile.");
                        if(request.alignment!=16)fail("IGB dependency allocation lost its original alignment16 argument.");
                        std::shared_ptr<IgbScratchBuffer> buffer;
                        try {
                            buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.resize(request.bytes);
                            buffer->initialized.resize(request.bytes);
                        }catch(const std::bad_alloc&) {buffer.reset();}
                        weak.lock()->buffer_result=std::move(buffer);
                    }else if(request.buffer)request.buffer->released=true;
                    return MessengerCallResult{true,0,{}};
                };
                return result;
            }
        }
        return remaining?remaining(request):nullptr;
    };
}

struct IgbDependenciesInvocation::Impl {
    enum class Phase {
        entry,header,allocate,after_allocate,body,swap,loader,after_loader,save_prefix,save_capture,
        install_prefix,visit,after_dependency,append,after_resize,store,advance,free_body,after_free,
        restore_prefix,restore_capture,clear_prefix,pool,after_pool_factory,after_pool_init,publish_pool,intern,after_intern,
        bind_incoming,capture_old,resolve_old,release_old,after_release,publish_string,status,call,done
    };
    enum class Transaction {save,install,restore,clear};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbStreamState> stream;
    std::shared_ptr<IgbLoadingPathsState> paths;
    std::shared_ptr<IgbDependenciesState> state;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::shared_ptr<IgbScratchBuffer> header=std::make_shared<IgbScratchBuffer>(),data;
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<SystemStageDirectory> directory;
    std::shared_ptr<LoaderDirectoryList> list;
    std::shared_ptr<ModelSceneObject> new_pool;
    std::shared_ptr<IgbInternedString> incoming,old_string;
    SystemStageString old_value;
    IgbDependencyName name;
    IgbDependenciesCall request;
    IgbDependenciesFactory factory;
    std::shared_ptr<IgbDependenciesOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry,after=Phase::done;
    Transaction transaction=Transaction::save;
    std::int32_t count{},index{},append_index{},append_count{};
    std::uint32_t name_offset{},result{};
    bool success{},running{},fault{},owns{},append_retained{},pool_missing{};
};
IgbDependenciesInvocation::IgbDependenciesInvocation(std::shared_ptr<LoaderRequestFile> file,
    std::shared_ptr<IgbStreamState> stream,std::shared_ptr<IgbLoadingPathsState> paths,
    std::shared_ptr<IgbDependenciesState> state):impl_(std::make_unique<Impl>()) {
    impl_->file=std::move(file);impl_->stream=std::move(stream);impl_->paths=std::move(paths);impl_->state=std::move(state);
    impl_->header->bytes.resize(12);impl_->header->initialized.resize(12);
}
IgbDependenciesInvocation::~IgbDependenciesInvocation() {
    if(impl_->owns && !completed()) {
        impl_->state->fault_=true;
        if(impl_->incoming)impl_->state->unreleased_.push_back(impl_->incoming);
        if(impl_->new_pool)impl_->state->unreleased_.push_back(impl_->new_pool);
        if(impl_->append_retained && impl_->directory)impl_->state->unreleased_.push_back(impl_->directory);
    }
}
bool IgbDependenciesInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool IgbDependenciesInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbDependenciesInvocation::value() const {
    if(!completed() || faulted())fail("An incomplete dependency caller has no returned status.");
    return impl_->result;
}
MessengerCallResult IgbDependenciesInvocation::resume(std::shared_ptr<IgbDependenciesServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using T=Impl::Transaction;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault) {s.fault=true;if(s.owns)s.state->fault_=true;fail("IGB dependency invocation must be unfaulted and nonrecursive.");}
    if(completed())return {true,s.result,{}};
    struct Running {bool& value;Running(bool& v):value(v){value=true;}~Running(){value=false;}} running(s.running);
    const auto check=[&] {if(s.fault || (s.owns && s.state->fault_))fail("IGB dependency continuation was invalidated.");};
    const auto clear_call=[&] {s.callback={};s.factory={};s.operation.reset();s.request={};check();};
    const auto select=[&](K kind,P after)->IgbDependenciesCall& {
        clear_call();s.request.kind=kind;s.request.file=s.file;s.request.stream=s.stream;s.request.state=s.state;
        s.factory=services?services->factory:IgbDependenciesFactory{};s.after=after;s.phase=P::call;return s.request;
    };
    const auto finish=[&](bool success) {s.success=success;s.phase=P::status;};
    const auto start_string=[&](T transaction,IgbDependencyName name,bool missing_pool) {
        s.transaction=transaction;s.name=std::move(name);s.pool_missing=missing_pool;
        s.incoming.reset();s.old_string.reset();s.old_value.reset();s.phase=P::pool;
    };
    try {
        if(!s.file || !s.stream || !s.state)fail("IGB dependency caller requires canonical file/stream state.");
        if(s.stream->file.lock()!=s.file)fail("IGB dependency stream belongs to a different file.");
        if(!s.owns) {
            if(s.state->fault_)fail("A faulted dependency prefix cannot restart.");
            if(!s.state->active_.expired())fail("Another invocation owns the dependency prefix.");
            s.state->active_=s.token;s.owns=true;
        }
        for(;;) {check();switch(s.phase) {
        case P::entry:
            if(s.stream->flag_d5==0)finish(true);else s.phase=P::header;break;
        case P::header:
            if(igb_read_elements(s.stream->stream,s.header,0,12,1)!=1) {finish(false);break;}
            if(s.stream->swapped!=0) {
                if(!s.stream->int_field)fail("IGB dependency header dereferences a null integer converter.");
                igb_swap_words(s.header,0,s.stream->int_field->width,3);
            }
            s.state->body_bytes=igb_buffer_word(s.header,0)-12u;
            s.count=signed_word(igb_buffer_word(s.header,8));s.phase=P::allocate;break;
        case P::allocate: {
            const auto pool=s.file->pool_118;
            if(!pool)fail("IGB dependency allocation dereferences a null pool.");
            auto& request=select(K::allocate_body,P::after_allocate);request.pool=pool;
            request.bytes=s.state->body_bytes;request.alignment=16;break;
        }
        case P::after_allocate:
            if(s.state->body)s.state->unreleased_.push_back(s.state->body);
            s.state->body=std::move(s.operation->buffer_result);clear_call();s.phase=P::body;break;
        case P::body:
            if(igb_read_elements(s.stream->stream,s.state->body,0,s.state->body_bytes,1)!=1) {finish(false);break;}
            s.phase=P::swap;break;
        case P::swap: {
            const auto swapped=s.stream->swapped;s.data=s.state->body;
            if(swapped!=0) {
                if(!s.stream->int_field)fail("IGB dependency lengths dereference a null integer converter.");
                igb_swap_words(s.data,0,s.stream->int_field->width,s.count);
            }
            s.name_offset=std::bit_cast<std::uint32_t>(s.count)*4u;s.phase=P::loader;break;
        }
        case P::loader:
            s.loader=loader_owner(s.file->loader);
            if(!s.loader)select(K::loader,P::after_loader);else s.phase=P::save_prefix;
            break;
        case P::after_loader:
            s.loader=std::move(s.operation->loader_result);s.file->loader=s.loader;clear_call();s.phase=P::save_prefix;break;
        case P::save_prefix:
            s.loader=loader_owner(s.file->loader);
            if(s.loader)s.phase=P::save_capture;else {s.index=0;s.phase=P::visit;}
            break;
        case P::save_capture:
            if(!services || !services->globals)return pending("IGB dependency save requires string-pool global storage before prefix capture.");
            {const bool missing=!services->globals->string_pool;
                start_string(T::save,name_pointer(s.loader->primary_prefix),missing);}
            break;
        case P::install_prefix: {
            if(!services || !services->globals)return pending("IGB dependency install requires string-pool global storage before path/loader capture.");
            const bool missing=!services->globals->string_pool;
            if(!s.paths)fail("IGB dependency caller reaches unavailable directory-path state.");
            const auto path=s.paths->directory_path;s.loader=loader_owner(s.file->loader);
            start_string(T::install,path?IgbDependencyName(IgbRuntimeNameSource{path,0}):IgbDependencyName{},missing);break;
        }
        case P::visit:
            if(s.index>=s.count) {s.phase=P::free_body;break;}
            if(s.index>=262144)fail("IGB dependency visits exceed the bounded native profile.");
            s.loader=loader_owner(s.file->loader);
            select(K::load_dependency,P::after_dependency).loader=s.loader;
            s.request.name=IgbRuntimeNameSource{s.data,s.name_offset};break;
        case P::after_dependency:
            s.directory=std::move(s.operation->directory_result);clear_call();
            s.list=s.state->directories;
            if(s.directory)++s.directory->reference_word;
            s.append_retained=true;s.phase=P::append;break;
        case P::append: {
            if(!s.list)fail("IGB dependency append dereferences a null list after retaining its incoming owner.");
            s.append_index=s.list->count;
            if(s.append_index<s.list->capacity) {
                s.list->count=increment(s.append_index);s.phase=P::store;
            }else {
                s.append_index=s.list->count;s.append_count=increment(s.append_index);
                auto capacity=std::max<std::int32_t>(s.list->capacity,4);
                while(capacity<s.append_count) {
                    if(capacity>=262144)fail("IGB dependency list growth exceeds its native profile.");
                    capacity=capacity<1024?capacity*2:capacity+1024;
                }
                if(capacity>262144)fail("IGB dependency list growth exceeds its native profile.");
                auto& request=select(K::reserve_directories,P::after_resize);request.directories=s.list;request.capacity=capacity;
            }
            break;
        }
        case P::after_resize:clear_call();s.list->count=s.append_count;s.phase=P::store;break;
        case P::store: {
            const auto storage=s.list->storage;
            if(s.append_index<0 || s.append_index>=262144 || !storage || static_cast<std::size_t>(s.append_index)>=storage->size())
                fail("IGB dependency append reaches an unavailable physical slot after count publication.");
            auto& slot=(*storage)[static_cast<std::size_t>(s.append_index)];
            if(slot)s.state->unreleased_.push_back(slot);
            slot=s.directory;s.append_retained=false;s.phase=P::advance;break;
        }
        case P::advance:
            s.name_offset+=igb_buffer_word(s.data,std::bit_cast<std::uint32_t>(s.index)*4u);
            ++s.index;s.phase=P::visit;break;
        case P::free_body: {
            const auto pool=s.file->pool_118;const auto buffer=s.state->body;
            if(!pool)fail("IGB dependency free dereferences a null live pool.");
            auto& request=select(K::free_body,P::after_free);request.pool=pool;request.buffer=buffer;break;
        }
        case P::after_free: {
            const auto freed=s.request.buffer;clear_call();
            if(s.state->body && s.state->body!=freed)s.state->unreleased_.push_back(s.state->body);
            s.state->body.reset();s.phase=P::restore_prefix;break;
        }
        case P::restore_prefix:
            s.loader=loader_owner(s.file->loader);
            if(!s.loader) {finish(true);break;}
            s.phase=P::restore_capture;break;
        case P::restore_capture:
            if(!services || !services->globals)return pending("IGB dependency restore requires string-pool global storage before saved-prefix capture.");
            {const bool missing=!services->globals->string_pool;
                start_string(T::restore,igb_interned_string_name(s.state->saved_prefix),missing);}
            break;
        case P::clear_prefix:
            if(!services || !services->globals)return pending("IGB dependency clear requires string-pool global storage.");
            start_string(T::clear,{},!services->globals->string_pool);break;
        case P::pool:
            if(s.pool_missing)select(K::create_string_pool,P::after_pool_factory).capacity=16;
            else s.phase=P::intern;
            break;
        case P::after_pool_factory:
            s.new_pool=std::move(s.operation->pool_result);clear_call();
            if(s.new_pool)select(K::initialize_string_pool,P::after_pool_init).pool=s.new_pool;
            else s.phase=P::publish_pool;
            break;
        case P::after_pool_init:
            s.new_pool=std::move(s.operation->pool_result);clear_call();s.phase=P::publish_pool;break;
        case P::publish_pool:
            if(!services || !services->globals)return pending("IGB dependency pool creation awaits its live singleton publication.");
            if(services->globals->string_pool)s.state->unreleased_.push_back(services->globals->string_pool);
            services->globals->string_pool=s.new_pool;s.phase=P::intern;break;
        case P::intern: {
            if(!services || !services->globals)return pending("IGB dependency interning requires its live string-pool singleton.");
            const auto pool=services->globals->string_pool;
            auto& request=select(K::intern_name,P::after_intern);request.pool=pool;request.name=s.name;break;
        }
        case P::after_intern:
            s.incoming=std::move(s.operation->string_result);clear_call();s.phase=P::bind_incoming;break;
        case P::bind_incoming:
            if(s.incoming) {
                if(!services || !services->strings)return pending("IGB dependency interning requires its canonical string ownership registry.");
                igb_bind_interned_string(services->strings,s.incoming);
            }
            s.phase=P::capture_old;break;
        case P::capture_old:
            if(s.transaction==T::save || s.transaction==T::clear) {
                s.old_string=s.state->saved_prefix;s.phase=P::release_old;
            }else {
                if(!s.loader)fail("IGB dependency path transaction dereferences a captured null loader.");
                s.old_value=s.loader->primary_prefix;s.phase=P::resolve_old;
            }
            break;
        case P::resolve_old:
            if(!s.old_value) {s.old_string.reset();s.phase=P::release_old;break;}
            if(services && services->strings)for(const auto& candidate:services->strings->owners)
                if(candidate && candidate->value==s.old_value && !candidate->released) {s.old_string=candidate;break;}
            if(!s.old_string)return pending("IGB dependency string decrement requires the captured string's explicit reference owner.");
            s.phase=P::release_old;break;
        case P::release_old:
            if(s.old_string) {
                if(s.old_string->released)fail("IGB dependency string decrement reaches a released interned owner.");
                --s.old_string->references;
                if(s.old_string->references==0) {
                    auto& request=select(K::release_string,P::after_release);request.string=s.old_string;request.pool=s.old_string->container;break;
                }
            }
            s.phase=P::publish_string;break;
        case P::after_release:s.old_string->released=true;clear_call();s.phase=P::publish_string;break;
        case P::publish_string:
            if(s.transaction==T::save || s.transaction==T::clear) {
                if(s.state->saved_prefix && s.state->saved_prefix!=s.old_string)s.state->unreleased_.push_back(s.state->saved_prefix);
                s.state->saved_prefix=s.incoming;
            }else {
                if(s.loader->primary_prefix && s.loader->primary_prefix!=s.old_value)s.state->unreleased_values_.push_back(s.loader->primary_prefix);
                s.loader->primary_prefix=s.incoming?s.incoming->value:SystemStageString{};
            }
            s.incoming.reset();s.old_string.reset();s.old_value.reset();
            if(s.transaction==T::save)s.phase=P::install_prefix;
            else if(s.transaction==T::install) {s.index=0;s.phase=P::visit;}
            else if(s.transaction==T::restore)s.phase=P::clear_prefix;
            else finish(true);
            break;
        case P::status: {
            if(!s.stream->statuses)return pending("IGB dependency caller requires its reached status-word owner.");
            const auto status=s.success?s.stream->statuses->success:s.stream->statuses->failure;
            if(!status)return pending("IGB dependency caller requires its reached success/failure word.");
            s.result=*status;s.state->active_.reset();s.owns=false;s.phase=P::done;return {true,s.result,{}};
        }
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending(boundary(s.request.kind));
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending(boundary(s.request.kind));
            {const auto result=s.callback();check();if(!result.completed)return result;}
            s.phase=s.after;break;
        case P::done:return {true,s.result,{}};
        }}
    }catch(...) {s.fault=true;if(s.owns)s.state->fault_=true;throw;}
}
} // namespace dw4
