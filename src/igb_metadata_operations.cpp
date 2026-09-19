#include "dw4/igb_metadata_operations.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataOperationsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
template<class Request,class Factory,class Operation> struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check> MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("Metadata operation requires its selected native engine service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Metadata operation reached an unbound engine service.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Metadata operation requires its retained continuation.");
        const auto status=callback();check();return status;
    }
};
using ParentCall=Call<IgbMetadataOperationCall,IgbMetadataOperationsFactory,IgbMetadataOperation>;
using NameCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
void retain_name(const std::shared_ptr<IgbRuntimeMetaType>& metadata){
    if(metadata->name_string && *metadata->name_string)metadata->overwritten_constructor_owners_.push_back(*metadata->name_string);
    if(metadata->name)metadata->overwritten_constructor_strings_.push_back(metadata->name);
}
} // namespace

void igb_metadata_publish_name(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbInternedString>& owner){
    if(!metadata)fail("Metadata name publication dereferences null metadata.");
    if(owner && !owner->value)fail("Metadata name publication requires the represented interned value identity.");
    retain_name(metadata);metadata->name_string=owner;metadata->name=owner?owner->value:SystemStageString{};
}

struct IgbMetadataParentInvocation::Impl {
    enum class P{entry,children,pool,after_pool,after_create,append_child,after_child,type_word,alignment,prefix,size,
        count,field,after_field,call,done};
    std::shared_ptr<IgbRuntimeMetaType> child,parent;
    std::shared_ptr<IgbRuntimeTypeList> created;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbMetadataOperationsServices> owner;
    ParentCall call;P phase=P::entry,after=P::done;
    std::int32_t count{},index{};
    bool running{},fault{};
};
IgbMetadataParentInvocation::IgbMetadataParentInvocation(std::shared_ptr<IgbRuntimeMetaType> child,
    std::shared_ptr<IgbRuntimeMetaType> parent):impl_(std::make_unique<Impl>()){
    impl_->child=std::move(child);impl_->parent=std::move(parent);
}
IgbMetadataParentInvocation::~IgbMetadataParentInvocation(){
    if((!completed() || faulted()) && impl_->owner){
        if(impl_->created)impl_->owner->unfinished.push_back(impl_->created);
        if(impl_->call.request.kind==IgbMetadataOperationKind::append_field && impl_->call.request.field)
            impl_->owner->unfinished.push_back(impl_->call.request.field);
    }
}
bool IgbMetadataParentInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataParentInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetadataParentInvocation::resume(std::shared_ptr<IgbMetadataOperationsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbMetadataOperationKind;
    if(s.running || s.fault){s.fault=true;fail("Parent attachment requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Parent attachment continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbMetadataOperationCall&{
        clear();s.call.request.kind=kind;s.call.request.metadata=s.child;s.call.request.parent=s.parent;
        if(services)s.call.factory=services->factory;s.phase=P::call;s.after=after;return s.call.request;
    };
    const auto live_parent=[&]()->std::shared_ptr<IgbRuntimeMetaType>{
        if(!s.child->parent || !*s.child->parent)fail("Parent attachment dereferences a null reloaded parent.");return *s.child->parent;
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:s.phase=s.parent?P::children:P::done;break;
    case P::children:
        if(!s.parent->children)return pending("Parent attachment requires represented parent+34 child-list state.");
        s.phase=*s.parent->children?P::append_child:P::pool;break;
    case P::pool:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Parent attachment requires metadata-pool handle805621F4.");
        {const auto handle=*services->types->metadata_pool_handle;select(K::pool_for_handle,P::after_pool).handle=handle;}break;
    case P::after_pool:
        s.pool=std::move(s.call.operation->pool_result);clear();select(K::create_children,P::after_create).pool=s.pool;break;
    case P::after_create:
        s.created=std::move(s.call.operation->children_result);clear();
        if(s.parent->children && *s.parent->children)s.parent->overwritten_constructor_owners_.push_back(*s.parent->children);
        s.parent->children=s.created;
        // No original-address mapping is inferred from a canonical host owner.
        if(s.created)s.parent->word_34.reset();else s.parent->word_34=0;
        s.phase=P::append_child;break;
    case P::append_child:
        if(!s.parent->children)return pending("Parent attachment requires the reloaded parent+34 child list.");
        {const auto children=*s.parent->children;select(K::append_child,P::after_child).children=children;}break;
    case P::after_child:
        clear();if(!s.child)fail("Parent attachment stores through null child metadata after appending it.");
        if(s.child->parent && *s.child->parent)s.child->overwritten_constructor_owners_.push_back(*s.child->parent);
        s.child->parent=s.parent;s.phase=P::type_word;break;
    case P::type_word:
        if(!s.parent->type_word)return pending("Parent attachment requires captured parent's type word+5C.");
        s.child->type_word=*s.parent->type_word;s.phase=P::alignment;break;
    case P::alignment:
        if(!s.child->parent)return pending("Parent attachment requires the live parent pointer for alignment.");
        s.child->native_alignment=live_parent()->native_alignment;s.phase=P::prefix;break;
    case P::prefix:
        if(!s.child->parent)return pending("Parent attachment requires the live parent pointer for allocation prefix.");
        if(const auto parent=live_parent();parent->allocation_prefix)s.child->allocation_prefix=*parent->allocation_prefix;
        else return pending("Parent attachment requires live parent allocation prefix+20.");
        s.phase=P::size;break;
    case P::size:
        if(!s.child->parent)return pending("Parent attachment requires the live parent pointer for native size.");
        s.child->native_size=live_parent()->native_size;s.phase=P::count;break;
    case P::count:s.count=igb_runtime_slot_count(s.parent);s.phase=P::field;break;
    case P::field:
        if(s.index>=s.count){s.phase=P::done;break;}
        {
            const auto field=igb_runtime_field_at(s.parent,s.index);
            //80065820 loads child+28 before53E6C retains the captured field.
            const auto fields=s.child->fields;
            if(field)++field->reference_word;
            auto& request=select(K::append_field,P::after_field);request.fields=fields;request.field=field;
        }break;
    case P::after_field:clear();++s.index;s.phase=P::field;break;
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbMetadataOperationsFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbMetadataNameInvocation::Impl {
    enum class P{global,after_allocate,after_initialize,publish_pool,intern,after_intern,old_name,resolve_old,release,publish,call,done};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    IgbDependencyName name;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbInternedString> incoming,old;
    SystemStageString old_value;
    std::shared_ptr<IgbMetadataOperationsServices> owner;
    NameCall call;P phase=P::global,after=P::done;bool running{},fault{};
};
IgbMetadataNameInvocation::IgbMetadataNameInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,IgbDependencyName name)
    :impl_(std::make_unique<Impl>()){impl_->metadata=std::move(metadata);impl_->name=std::move(name);}
IgbMetadataNameInvocation::~IgbMetadataNameInvocation(){
    auto& s=*impl_;if(!completed() && s.owner){
        if(s.incoming)s.owner->unfinished.push_back(s.incoming);if(s.old)s.owner->unfinished.push_back(s.old);
        if(s.pool)s.owner->unfinished.push_back(s.pool);
    }
}
bool IgbMetadataNameInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataNameInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetadataNameInvocation::resume(std::shared_ptr<IgbMetadataOperationsServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Metadata naming requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Metadata naming continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto dependencies=[&]{return services?services->dependencies:std::shared_ptr<IgbDependenciesServices>{};};
    const auto globals=[&]{const auto dep=dependencies();return dep?dep->globals:std::shared_ptr<SystemStageServices>{};};
    const auto select=[&](K kind,P after)->IgbDependenciesCall&{
        clear();s.call.request.kind=kind;if(const auto dep=dependencies())s.call.factory=dep->factory;
        s.phase=P::call;s.after=after;return s.call.request;
    };
    try{for(;;){check();switch(s.phase){
    case P::global:{
        const auto global=globals();if(!global)return pending("Metadata naming requires canonical string-pool global80562140.");
        if(global->string_pool)s.phase=P::intern;else select(K::create_string_pool,P::after_allocate).bytes=16;break;
    }
    case P::after_allocate:
        s.pool=std::move(s.call.operation->pool_result);clear();
        if(s.pool)select(K::initialize_string_pool,P::after_initialize).pool=s.pool;else s.phase=P::publish_pool;break;
    case P::after_initialize:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{
        const auto global=globals();if(!global)return pending("Metadata naming requires string-pool publication storage.");
        if(global->string_pool && global->string_pool!=s.pool && s.owner)s.owner->unfinished.push_back(global->string_pool);
        global->string_pool=s.pool;s.phase=P::intern;break;
    }
    case P::intern:{
        const auto global=globals();if(!global)return pending("Metadata naming requires the reloaded string-pool global.");
        const auto pool=global->string_pool;auto& request=select(K::intern_name,P::after_intern);request.pool=pool;request.name=s.name;break;
    }
    case P::after_intern:s.incoming=std::move(s.call.operation->string_result);clear();s.phase=P::old_name;break;
    case P::old_name:
        if(!s.metadata)fail("Metadata naming reads the old name through null metadata after interning.");
        s.old_value=s.metadata->name;s.phase=s.old_value?P::resolve_old:P::publish;break;
    case P::resolve_old:{
        if(s.metadata->name_string && *s.metadata->name_string && (*s.metadata->name_string)->value==s.old_value)s.old=*s.metadata->name_string;
        if(!s.old){const auto dep=dependencies();if(dep && dep->strings)for(const auto& candidate:dep->strings->owners)
            if(candidate && candidate->value==s.old_value){if(s.old && s.old!=candidate)fail("Metadata name has conflicting interned header owners.");s.old=candidate;}}
        if(!s.old)return pending("Metadata naming requires the captured old name's canonical string header.");
        if(s.old->released)fail("Metadata naming reads a released old string header.");
        --s.old->references;s.phase=s.old->references==0?P::release:P::publish;break;
    }
    case P::release:{const auto old=s.old;auto& request=select(K::release_string,P::publish);request.string=old;request.pool=old->container;break;}
    case P::publish:
        clear();if(s.incoming && !s.incoming->value)return pending("Metadata naming requires the returned interned value identity.");
        // Retain an old header resolved through the shared registry as well as
        // a different live pointer installed by the completed release callback.
        if(s.old)s.metadata->overwritten_constructor_owners_.push_back(s.old);
        igb_metadata_publish_name(s.metadata,s.incoming);s.phase=P::done;break;
    case P::call:{const auto dep=dependencies();const auto status=s.call.poll(dep?dep->factory:IgbDependenciesFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbMetadataOperationsFactory make_native_igb_metadata_pool_factory(std::shared_ptr<IgbFilePoolsServices> pools,IgbMetadataOperationsFactory remaining){
    std::weak_ptr<IgbFilePoolsServices> weak=pools;
    return [weak,remaining=std::move(remaining)](const IgbMetadataOperationCall& request){
        if(request.kind!=IgbMetadataOperationKind::pool_for_handle)return remaining?remaining(request):std::shared_ptr<IgbMetadataOperation>{};
        const auto services=weak.lock();if(!services)fail("Metadata parent pool services expired before selection.");
        auto invocation=std::make_shared<IgbPoolByHandleInvocation>(request.handle);auto operation=std::make_shared<IgbMetadataOperation>();
        operation->resume=[invocation,services,out=operation.get()]{const auto status=invocation->resume(services);if(status.completed)out->pool_result=invocation->result();return status;};return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_metadata_operations_factory(std::shared_ptr<IgbMetadataOperationsServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataOperationsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& request){
        using K=IgbTypeRegistrationServiceKind;
        if(request.kind!=K::attach_parent && request.kind!=K::set_name)return remaining?remaining(request):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata operation services expired before selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(request.kind==K::attach_parent){auto invocation=std::make_shared<IgbMetadataParentInvocation>(request.metadata,request.parent);
            operation->resume=[invocation,selected]{return invocation->resume(selected);};
        }else{IgbDependencyName name;if(request.name)name=request.name;auto invocation=std::make_shared<IgbMetadataNameInvocation>(request.metadata,std::move(name));
            operation->resume=[invocation,selected]{return invocation->resume(selected);};}
        return operation;
    };
}
} // namespace dw4
