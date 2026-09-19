#include "dw4/loader_dependency.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw LoaderDependencyError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
std::int32_t increment(std::int32_t value) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(value)+1u);
}
const char* boundary(LoaderDependencyServiceKind kind) {
    using K=LoaderDependencyServiceKind;
    switch(kind) {
    case K::object_pool:return "Dependency request requires captured pool-table lookup8005929C after80068430.";
    case K::create_igb_file:return "Dependency request requires concrete igIGBFile factory8002EBEC.";
    case K::destroy_object:return "Dependency request requires final object destruction80066E1C.";
    case K::create_string_pool:return "Dependency request requires string-pool allocation80054140(16).";
    case K::initialize_string_pool:return "Dependency request requires string-pool initialization80053F28.";
    case K::intern_name:return "Dependency request requires actual string interning80054094.";
    case K::release_string:return "Dependency request requires final string-container release8007381C.";
    case K::reserve_directories:return "Dependency request requires directory-list reserve8004155C(width4).";
    }
    return "Dependency request requires its reached operation.";
}
struct NameReader {
    const IgbDependencyName& name;
    std::uint32_t alignment() const {
        if(const auto* source=std::get_if<IgbRuntimeNameSource>(&name))return source->offset&3u;
        return 0; // Native interned strings represent aligned character allocations.
    }
    std::uint8_t byte(std::uint32_t index) const {
        if(index>=4096)fail("Dependency filename comparison exceeds its bounded native range.");
        if(const auto* source=std::get_if<IgbRuntimeNameSource>(&name))
            return igb_buffer_bytes(source->buffer,source->offset+index,1).front();
        const auto* value=std::get_if<SystemStageString>(&name);
        if(!value || !*value)fail("Dependency lookup compares a null or unrepresented filename.");
        if(index<(*value)->size())return static_cast<std::uint8_t>((**value)[index]);
        if(index<(*value)->size()+4)return 0; // Represented terminator and aligned padding.
        fail("Dependency filename comparison reaches an unavailable character.");
    }
    std::uint32_t word(std::uint32_t index) const {
        if(const auto* source=std::get_if<IgbRuntimeNameSource>(&name))
            return igb_buffer_word(source->buffer,source->offset+index);
        return (std::uint32_t(byte(index))<<24)|(std::uint32_t(byte(index+1))<<16)|
            (std::uint32_t(byte(index+2))<<8)|byte(index+3);
    }
};
// Equality path of8009D5D8, including reached left/right aligned word loads.
bool compare_name(const IgbDependencyName& request,const SystemStageString& candidate,bool request_first) {
    const IgbDependencyName stored=candidate;
    const NameReader left{request_first?request:stored},right{request_first?stored:request};
    std::uint32_t index{};
    auto a=left.byte(index),b=right.byte(index);
    if(a!=b)return false;
    if(left.alignment()==right.alignment()) {
        if(left.alignment()!=0) {
            if(a==0)return true;
            while(((left.alignment()+index)&3u)!=3u) {
                a=left.byte(++index);b=right.byte(index);
                if(a!=b)return false;
                if(a==0)return true;
            }
            ++index;
        }
        for(;;) {
            if(index>=4096)fail("Dependency filename comparison exceeds its bounded native range.");
            const auto first=left.word(index),second=right.word(index);
            if(((first-0x01010101u)&0x80808080u)!=0) {
                a=left.byte(index);b=right.byte(index);if(a!=b)return false;break;
            }
            if(first!=second)return false;
            index+=4;
        }
    }
    for(;;) {
        if(a==0)return true;
        a=left.byte(++index);b=right.byte(index);
        if(a!=b)return false;
    }
}
}

LoaderDependencyFactory make_loader_dependency_string_factory(IgbDependenciesFactory strings,LoaderDependencyFactory remaining) {
    return [strings=std::move(strings),remaining=std::move(remaining)](const LoaderDependencyCall& call)
        ->std::shared_ptr<LoaderDependencyOperation> {
        using K=LoaderDependencyServiceKind;using S=IgbDependenciesServiceKind;
        IgbDependenciesCall request;
        switch(call.kind) {
        case K::create_string_pool:request.kind=S::create_string_pool;break;
        case K::initialize_string_pool:request.kind=S::initialize_string_pool;break;
        case K::intern_name:request.kind=S::intern_name;break;
        case K::release_string:request.kind=S::release_string;break;
        default:return remaining?remaining(call):nullptr;
        }
        request.file=call.file;request.loader=call.loader;request.pool=call.object;
        request.string=call.string;request.name=call.name;request.capacity=call.capacity;
        auto selected=strings?strings(request):nullptr;
        if(!selected)return remaining?remaining(call):nullptr;
        auto operation=std::make_shared<LoaderDependencyOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[selected,weak,callback=std::function<MessengerCallResult()>{}]() mutable {
            if(!callback)callback=selected->resume;
            if(!callback)return pending("Dependency string adapter requires its selected continuation.");
            const auto result=callback();
            if(result.completed) {const auto out=weak.lock();out->object_result=selected->pool_result;out->string_result=selected->string_result;}
            return result;
        };
        return operation;
    };
}

IgbDependenciesFactory make_native_loader_dependency_factory(std::shared_ptr<LoaderDependencyServices> services,
    IgbDependenciesFactory remaining) {
    return [weak_services=std::weak_ptr(services),remaining=std::move(remaining)](const IgbDependenciesCall& call)
        ->std::shared_ptr<IgbDependenciesOperation> {
        if(call.kind!=IgbDependenciesServiceKind::load_dependency)return remaining?remaining(call):nullptr;
        const auto selected_services=weak_services.lock();
        if(!selected_services)fail("Native dependency factory requires its live canonical services owner.");
        auto invocation=std::make_shared<LoaderDependencyInvocation>(call.loader,call.name);
        auto operation=std::make_shared<IgbDependenciesOperation>();const auto weak=std::weak_ptr(operation);
        operation->resume=[invocation,selected_services,weak] {
            const auto result=invocation->resume(selected_services);
            if(result.completed)weak.lock()->directory_result=invocation->result();
            return result;
        };
        return operation;
    };
}

struct LoaderDependencyInvocation::Impl {
    enum class Phase {entry,lookup,drop_lookup,found_claim,factory_pool,after_fallback,create_file,after_file,
        pools,pool_retain,pool_release,pool_publish,options,globals,after_pool_create,after_pool_init,
        publish_pool,intern,after_intern,bind_string,capture_old,resolve_old,release_old,after_string_release,
        publish_name,claim,append,after_reserve,store,load,drop_file,call,done};
    std::shared_ptr<SystemStageLoader> loader;
    IgbDependencyName name;
    std::unique_ptr<LoaderDirectoryLookupInvocation> lookup;
    std::unique_ptr<LoaderDirectoryClaimInvocation> claim;
    std::unique_ptr<LoaderFileLoadInvocation> load;
    std::shared_ptr<SystemStageDirectory> result;
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<ModelSceneObject> incoming_pool,old_pool,new_string_pool;
    std::shared_ptr<IgbInternedString> incoming_string,old_string;
    SystemStageString old_name;
    std::shared_ptr<LoaderDirectoryList> list;
    LoaderDependencyCall request;
    LoaderDependencyFactory factory;
    std::shared_ptr<LoaderDependencyOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::entry,after=Phase::done,fallback_after=Phase::done;
    unsigned pool_index{};
    std::int32_t append_index{},append_count{};
    bool running{},fault{},file_temporary{},pool_retained{},append_retained{};
};
LoaderDependencyInvocation::LoaderDependencyInvocation(std::shared_ptr<SystemStageLoader> loader,IgbDependencyName name)
    :impl_(std::make_unique<Impl>()) {impl_->loader=std::move(loader);impl_->name=std::move(name);}
LoaderDependencyInvocation::~LoaderDependencyInvocation() {
    auto& s=*impl_;
    if(!completed() && s.loader) {
        if(s.file_temporary && s.file)s.loader->unreleased_dependency_.push_back(s.file);
        if(s.pool_retained && s.incoming_pool)s.loader->unreleased_dependency_.push_back(s.incoming_pool);
        if(s.append_retained && s.file)s.loader->unreleased_dependency_.push_back(s.file);
        if(s.incoming_string)s.loader->unreleased_dependency_.push_back(s.incoming_string);
        // The original reference decrement already happened, but a selected
        // final destructor has not returned. Callback mutations may remove all
        // ordinary host holders; abandoning must not finish it implicitly.
        if(s.request.kind==LoaderDependencyServiceKind::destroy_object) {
            if(s.request.directory)s.loader->unreleased_dependency_.push_back(s.request.directory);
            if(s.request.object)s.loader->unreleased_dependency_.push_back(s.request.object);
        }
        if(s.request.kind==LoaderDependencyServiceKind::release_string && s.request.string)
            s.loader->unreleased_dependency_.push_back(s.request.string);
    }
}
bool LoaderDependencyInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::done;}
bool LoaderDependencyInvocation::faulted() const noexcept {return impl_->fault;}
std::shared_ptr<SystemStageDirectory> LoaderDependencyInvocation::result() const {
    if(!completed() || faulted())fail("An incomplete dependency request has no published return value.");return impl_->result;
}
MessengerCallResult LoaderDependencyInvocation::resume(std::shared_ptr<LoaderDependencyServices> services) {
    auto& s=*impl_;using P=Impl::Phase;using K=LoaderDependencyServiceKind;
    if(s.running || s.fault) {s.fault=true;fail("Dependency request requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running {bool& value;Running(bool& v):value(v){value=true;}~Running(){value=false;}} running(s.running);
    const auto check=[&] {if(s.fault)fail("Dependency request continuation was invalidated.");};
    const auto clear=[&] {s.callback={};s.factory={};s.operation.reset();s.request={};check();};
    const auto select=[&](K kind,P after)->LoaderDependencyCall& {
        clear();s.request.kind=kind;s.request.loader=s.loader;s.request.file=s.file;
        s.factory=services?services->factory:LoaderDependencyFactory{};s.after=after;s.phase=P::call;return s.request;
    };
    const auto finish=[&] {clear();s.lookup.reset();s.claim.reset();s.load.reset();check();s.phase=P::done;return MessengerCallResult{true,0,{}};};
    const auto pool_field=[&]() ->std::shared_ptr<ModelSceneObject>& {
        return s.pool_index==0?s.file->pool_118:s.pool_index==1?s.file->pool_11c:s.file->pool_120;
    };
    const auto claim=[&](const std::shared_ptr<SystemStageDirectory>& owner) {
        if(!s.claim)s.claim=std::make_unique<LoaderDirectoryClaimInvocation>(owner,LoaderDirectoryClaim::acquire);
        const auto result=s.claim->resume(services?services->directory:nullptr);check();
        if(result.completed) {s.claim.reset();check();}return result;
    };
    const auto pool=[&](std::shared_ptr<ModelSceneObject> incoming,P after) {
        if(incoming) {s.incoming_pool=std::move(incoming);s.phase=after;}
        else {s.fallback_after=after;select(K::object_pool,P::after_fallback).pool_handle=s.loader->reference_word>>24;}
    };
    try {
        for(;;) {check();switch(s.phase) {
        case P::entry:
            s.lookup=std::make_unique<LoaderDirectoryLookupInvocation>(s.loader,
                [name=s.name](const SystemStageString& candidate,bool request_first) {return compare_name(name,candidate,request_first);},
                LoaderDirectoryLookupInvocation::LazyNameTag{});
            s.phase=P::lookup;break;
        case P::lookup: {
            const auto result=s.lookup->resume(services?services->directory:nullptr);check();if(!result.completed)return result;
            s.result=s.lookup->result();
            // Existing lookup returns host ownership. Its original8006F35C
            // temporary acquire is represented here before the caller drop.
            if(s.result)++s.result->reference_word;
            s.lookup.reset();check();s.phase=P::drop_lookup;break;
        }
        case P::drop_lookup:
            if(s.result) {
                --s.result->reference_word;
                if((s.result->reference_word&0x7fffffu)==0) {select(K::destroy_object,P::found_claim).directory=s.result;break;}
                s.phase=P::found_claim;
            }else s.phase=P::factory_pool;
            break;
        case P::found_claim: {
            clear();const auto result=claim(s.result);if(!result.completed)return result;return finish();
        }
        case P::factory_pool:pool(s.loader->pool_38,P::create_file);break;
        case P::after_fallback:
            s.incoming_pool=std::move(s.operation->object_result);clear();s.phase=s.fallback_after;break;
        case P::create_file:select(K::create_igb_file,P::after_file).object=s.incoming_pool;break;
        case P::after_file:
            s.file=std::move(s.operation->file_result);s.file_temporary=bool(s.file);clear();
            if(!s.file)fail("Dependency request dereferences a null igIGBFile factory result.");
            s.file->format_24=s.loader->format_44;s.file->option_a4=s.loader->option_48;s.file->option_41=s.loader->option_08;
            s.phase=P::pools;break;
        case P::pools:pool(s.pool_index==0?s.loader->pool_38:s.pool_index==1?s.loader->pool_3c:s.loader->pool_40,P::pool_retain);break;
        case P::pool_retain:
            if(s.incoming_pool)++s.incoming_pool->reference_word;
            s.pool_retained=true;s.old_pool=pool_field();s.phase=P::pool_release;break;
        case P::pool_release:
            if(s.old_pool) {
                --s.old_pool->reference_word;
                if((s.old_pool->reference_word&0x7fffffu)==0) {select(K::destroy_object,P::pool_publish).object=s.old_pool;break;}
            }
            s.phase=P::pool_publish;break;
        case P::pool_publish: {
            clear();auto& field=pool_field();
            if(field && field!=s.old_pool)s.loader->unreleased_dependency_.push_back(field);
            field=s.incoming_pool;s.pool_retained=false;s.incoming_pool.reset();s.old_pool.reset();check();
            s.phase=++s.pool_index==3?P::options:P::pools;break;
        }
        case P::options:
            if(s.loader->async_1c==1)s.file->initialize_after_load_115=s.loader->initialize_after_load_34;
            s.file->succeeded_31=0;s.file->loading=1;s.phase=P::globals;break;
        case P::globals:
            if(!services || !services->globals)return pending("Dependency request requires original string-pool global storage.");
            if(!services->globals->string_pool)select(K::create_string_pool,P::after_pool_create).capacity=16;
            else s.phase=P::intern;
            break;
        case P::after_pool_create:
            s.new_string_pool=std::move(s.operation->object_result);clear();
            if(s.new_string_pool)select(K::initialize_string_pool,P::after_pool_init).object=s.new_string_pool;
            else s.phase=P::publish_pool;
            break;
        case P::after_pool_init:s.new_string_pool=std::move(s.operation->object_result);clear();s.phase=P::publish_pool;break;
        case P::publish_pool:
            if(!services || !services->globals)return pending("Dependency request awaits its live string-pool global publication.");
            if(services->globals->string_pool)s.loader->unreleased_dependency_.push_back(services->globals->string_pool);
            services->globals->string_pool=s.new_string_pool;s.phase=P::intern;break;
        case P::intern:
            if(!services || !services->globals)return pending("Dependency request requires live string-pool global reload.");
            {const auto object=services->globals->string_pool;auto& request=select(K::intern_name,P::after_intern);
                request.object=object;request.name=s.name;}break;
        case P::after_intern:s.incoming_string=std::move(s.operation->string_result);clear();s.phase=P::bind_string;break;
        case P::bind_string:
            if(s.incoming_string) {
                if(!services || !services->strings)return pending("Dependency request requires the canonical interned-string registry.");
                igb_bind_interned_string(services->strings,s.incoming_string);
            }
            s.phase=P::capture_old;break;
        case P::capture_old:s.old_name=s.file->filename;s.phase=P::resolve_old;break;
        case P::resolve_old:
            if(s.old_name) {
                if(services && services->strings)for(const auto& owner:services->strings->owners)
                    if(owner && !owner->released && owner->value==s.old_name) {s.old_string=owner;break;}
                if(!s.old_string)return pending("Dependency request requires the captured old filename's explicit reference owner.");
            }
            s.phase=P::release_old;break;
        case P::release_old:
            if(s.old_string) {
                if(s.old_string->released)fail("Dependency request releases an already released filename.");
                if(--s.old_string->references==0) {
                    auto& request=select(K::release_string,P::after_string_release);request.string=s.old_string;request.object=s.old_string->container;break;
                }
            }
            s.phase=P::publish_name;break;
        case P::after_string_release:s.old_string->released=true;clear();s.phase=P::publish_name;break;
        case P::publish_name:
            if(s.file->filename && s.file->filename!=s.old_name)s.loader->unreleased_dependency_strings_.push_back(s.file->filename);
            s.file->filename=s.incoming_string?s.incoming_string->value:SystemStageString{};
            s.incoming_string.reset();s.old_string.reset();s.old_name.reset();s.file->loader=s.loader;s.phase=P::claim;break;
        case P::claim: {
            const auto result=claim(s.file);if(!result.completed)return result;
            s.list=s.loader->directories;++s.file->reference_word;s.append_retained=true;s.phase=P::append;break;
        }
        case P::append:
            if(!s.list)fail("Dependency directory append dereferences a null list after retaining the incoming owner.");
            s.append_index=s.list->count;
            if(s.append_index<s.list->capacity) {s.list->count=increment(s.append_index);s.phase=P::store;}
            else {
                s.append_index=s.list->count;s.append_count=increment(s.append_index);
                auto capacity=std::max<std::int32_t>(s.list->capacity,4);
                while(capacity<s.append_count) {
                    if(capacity>=262144)fail("Dependency directory growth exceeds the bounded native profile.");
                    capacity=capacity<1024?capacity*2:capacity+1024;
                }
                if(capacity>262144)fail("Dependency directory growth exceeds the bounded native profile.");
                auto& request=select(K::reserve_directories,P::after_reserve);request.list=s.list;request.capacity=capacity;
            }
            break;
        case P::after_reserve:clear();s.list->count=s.append_count;s.phase=P::store;break;
        case P::store: {
            const auto storage=s.list->storage;
            if(s.append_index<0 || s.append_index>=262144 || !storage || static_cast<std::size_t>(s.append_index)>=storage->size())
                fail("Dependency directory append reaches an unavailable physical slot after count publication.");
            auto& field=(*storage)[static_cast<std::size_t>(s.append_index)];
            if(field)s.loader->unreleased_dependency_.push_back(field);
            field=s.file;s.append_retained=false;s.phase=P::load;break;
        }
        case P::load: {
            if(!s.load)s.load=std::make_unique<LoaderFileLoadInvocation>(s.loader,s.file);
            const auto result=s.load->resume(services?services->request:nullptr);check();if(!result.completed)return result;
            s.result=s.load->result();s.load.reset();check();s.phase=P::drop_file;break;
        }
        case P::drop_file:
            --s.file->reference_word;s.file_temporary=false;
            if((s.file->reference_word&0x7fffffu)==0) {select(K::destroy_object,P::done).directory=s.file;break;}
            return finish();
        case P::call:
            if(!s.factory && services)s.factory=services->factory;
            if(!s.factory)return pending(boundary(s.request.kind));
            if(!s.operation) {s.operation=s.factory(s.request);check();}
            if(!s.operation)return pending(boundary(s.request.kind));
            if(!s.callback)s.callback=s.operation->resume;
            if(!s.callback)return pending(boundary(s.request.kind));
            {const auto result=s.callback();check();if(!result.completed)return result;}
            s.phase=s.after;if(s.phase==P::done)return finish();break;
        case P::done:return {true,0,{}};
        }}
    }catch(...) {s.fault=true;throw;}
}
} // namespace dw4
