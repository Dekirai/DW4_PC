#include "dw4/igb_list_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbListRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
using Entry=IgbTypeListFactoryEntry;
bool registry(Entry entry){
    if(entry!=Entry::registry && entry!=Entry::children)fail("Unknown internal-list registration class.");return entry==Entry::registry;
}
std::uint32_t body(Entry entry){return registry(entry)?0x8002dd40u:0x8002e07cu;}
std::shared_ptr<IgbRegistrationMetaSlot> cell(const std::shared_ptr<IgbListRegistrationServices>& services,Entry entry){
    const bool internal=registry(entry);if(!services || !services->lists)return {};return internal?services->lists->registry_metadata:services->lists->children_metadata;
}
std::optional<Entry> registration_function(std::uint32_t function,bool wrapper){
    if(function==(wrapper?0x8002dd18u:0x8002dd40u))return Entry::registry;
    if(function==(wrapper?0x8002e054u:0x8002e07cu))return Entry::children;return {};
}
struct Lookup {Entry entry;IgbListLookup mode;};
std::optional<Lookup> lookup_function(std::uint32_t function){
    switch(function){
    case 0x8002dc6c:return Lookup{Entry::registry,IgbListLookup::allocate_if_null};
    case 0x8002dca8:case 0x8002ddac:return Lookup{Entry::registry,IgbListLookup::fully_registered};
    case 0x8002ddcc:return Lookup{Entry::children,IgbListLookup::plain};
    case 0x8002dfa8:return Lookup{Entry::children,IgbListLookup::allocate_if_null};
    case 0x8002dfe4:case 0x8002e0e8:return Lookup{Entry::children,IgbListLookup::fully_registered};
    default:return {};
    }
}
std::optional<Entry> probe_function(std::uint32_t function){
    if(function==0x8002dce4)return Entry::registry;if(function==0x8002e020)return Entry::children;return {};
}
}
IgbTypeRegistrationArguments igb_list_type_registration_arguments(const std::shared_ptr<IgbListRegistrationServices>& services,Entry entry){
    IgbTypeRegistrationArguments args;args.metadata_slot=cell(services,entry);
    if(!args.metadata_slot)fail("List registration requires its canonical metadata cell.");
    const bool internal=registry(entry);args.flag=0;args.native_size=16;
    args.name=std::make_shared<const std::string>(internal?"__internalObjectList":"__internalNonRefCountedObjectList");
    args.parent_registration=internal?0x8002e07cu:0x80066b08u;args.parent_getter=internal?0x8002ddccu:0x800237d0u;
    args.field_callback=internal?0x8002ddacu:0x8002e0e8u;args.layout_probe=internal?0x8002dce4u:0x8002e020u;
    args.defaults_callback=internal?0x8002ddd4u:0x8002e108u;return args;
}
struct IgbListTypeRegistrationInvocation::Impl {
    Entry entry{};std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{},done{};
};
IgbListTypeRegistrationInvocation::IgbListTypeRegistrationInvocation(Entry entry):impl_(std::make_unique<Impl>()){registry(entry);impl_->entry=entry;}
IgbListTypeRegistrationInvocation::~IgbListTypeRegistrationInvocation()=default;
bool IgbListTypeRegistrationInvocation::completed()const noexcept{return impl_->done;}
bool IgbListTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbListTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete list registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbListTypeRegistrationInvocation::resume(std::shared_ptr<IgbListRegistrationServices> services){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("List registration requires an unfaulted nonrecursive invocation.");}
    if(s.done)return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("List registration continuation was invalidated.");};
    try{
        if(!s.registration){if(!cell(services,s.entry))return pending("List registration requires its canonical metadata cell.");
            s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_list_type_registration_arguments(services,s.entry));}
        const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.done=true;return {true,0,{}};
    }catch(...){s.fault=true;throw;}
}
struct IgbListLookupInvocation::Impl {
    enum class P{read,handle,pool,construct,publish,registration,reload,done};
    Entry entry{};IgbListLookup mode{};P phase=P::read;
    std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbListRegistrationServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRuntimeMetaType> result;
    std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    bool running{},fault{};
};
IgbListLookupInvocation::IgbListLookupInvocation(Entry entry,IgbListLookup mode):impl_(std::make_unique<Impl>()){
    registry(entry);impl_->entry=entry;impl_->mode=mode;
}
IgbListLookupInvocation::~IgbListLookupInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.result)s.owner->retained.push_back(s.result);if(s.pool)s.owner->retained.push_back(s.pool);
}}
bool IgbListLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbListLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbListLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete list metadata getter has no result.");return impl_->result;
}
MessengerCallResult IgbListLookupInvocation::resume(std::shared_ptr<IgbListRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("List metadata getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("List metadata getter continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=cell(services,s.entry);if(!s.slot)return pending("List metadata getter requires its canonical global cell.");}
        if(!*s.slot)return pending("List metadata getter requires represented canonical global state.");
        if(s.mode==IgbListLookup::plain)s.phase=P::reload;
        else if(s.mode==IgbListLookup::allocate_if_null)s.phase=**s.slot?P::reload:P::handle;
        else if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(body(s.entry));s.phase=P::registration;}break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Lazy list metadata requires pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::construct;break;}
    case P::construct:
        if(!s.factory){if(!services || !services->metadata)return pending("Lazy list metadata requires original metadata factory services.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);}
        {const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status;}
        s.result=s.factory->result();s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("List metadata getter requires its reloaded canonical global.");
        s.result=**s.slot;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbListLayoutProbeInvocation::Impl {
    enum class P{construct,core,offset,word,done};
    Entry entry{};IgbBootstrapPointer local;P phase=P::construct;
    std::shared_ptr<IgbListRegistrationServices> owner;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::unique_ptr<IgbFieldRegistryConstructorInvocation> fields;
    std::unique_ptr<IgbTypeListConstructorInvocation> types;
    std::uint32_t offset{},value{};bool running{},fault{};
};
IgbListLayoutProbeInvocation::IgbListLayoutProbeInvocation(IgbBootstrapPointer local,Entry entry):impl_(std::make_unique<Impl>()){
    registry(entry);impl_->local=std::move(local);impl_->entry=entry;
}
IgbListLayoutProbeInvocation::~IgbListLayoutProbeInvocation()=default;
bool IgbListLayoutProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbListLayoutProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbListLayoutProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete list layout probe has no value.");return impl_->value;
}
MessengerCallResult IgbListLayoutProbeInvocation::resume(std::shared_ptr<IgbListRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("List layout probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("List layout probe continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::construct:
        if(registry(s.entry)){
            if(!s.fields)s.fields=std::make_unique<IgbFieldRegistryConstructorInvocation>(s.local,IgbFieldRegistryConstructor::derived_reset);
            const auto status=s.fields->resume(services?services->field_registries:nullptr);check();if(!status.completed)return status;s.fields.reset();check();
        }else{
            if(!s.types)s.types=std::make_unique<IgbTypeListConstructorInvocation>(s.local,IgbTypeListConstructor::reset);
            const auto status=s.types->resume(services?services->lists:nullptr);check();if(!status.completed)return status;s.types.reset();check();
        }s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("List layout probe requires live ArkCore80562100 after its reset.");
        s.core=*services->types->runtime->singleton;s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("List layout probe dereferences null ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("List layout probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>12 || (s.offset&3u)!=0)fail("List layout probe exceeds its aligned16-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::done;return {true,s.value,{}};
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_list_registration_function_factory(
    std::shared_ptr<IgbListRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbListRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("List registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct){auto invocation=std::make_shared<IgbListTypeRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(body(*wrapper));
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_list_registration_type_factory(
    std::shared_ptr<IgbListRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbListRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<Entry>{};
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        const auto probe=call.kind==IgbTypeRegistrationServiceKind::layout_probe?probe_function(call.function):std::optional<Entry>{};
        if(!registration && !lookup && !probe)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("List registration services expired before type-operation selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbListTypeRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(lookup){auto invocation=std::make_shared<IgbListLookupInvocation>(lookup->entry,lookup->mode);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else operation->resume=[selected,entry=*probe,invocation=std::shared_ptr<IgbListLayoutProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("List layout probe requires its represented stack local.");
                auto local=selected->local_storage(entry);if(!igb_bootstrap_address(local))fail("List layout probe has null stack-local storage.");
                invocation=std::make_shared<IgbListLayoutProbeInvocation>(std::move(local),entry);}
            return invocation->resume(selected);
        };
        return operation;
    };
}
IgbTypeListFactory make_native_igb_list_registration_factory(std::shared_ptr<IgbListRegistrationServices> services,IgbTypeListFactory remaining){
    std::weak_ptr<IgbListRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeListCall& call){
        const auto entry=registration_function(call.function,true);
        if(call.kind!=IgbTypeListServiceKind::register_type || !entry)return remaining?remaining(call):std::shared_ptr<IgbTypeListOperation>{};
        const auto selected=weak.lock();if(!selected)fail("List registration services expired before list-factory selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(body(*entry));auto operation=std::make_shared<IgbTypeListOperation>();
        operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};return operation;
    };
}
IgbFieldRegistryFactory make_native_igb_field_list_registration_factory(std::shared_ptr<IgbListRegistrationServices> services,IgbFieldRegistryFactory remaining){
    std::weak_ptr<IgbListRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbFieldRegistryCall& call){
        if(call.kind!=IgbFieldRegistryServiceKind::register_type)return remaining?remaining(call):std::shared_ptr<IgbFieldRegistryOperation>{};
        const auto selected=weak.lock();if(!selected)fail("List registration services expired before field-list factory selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(body(Entry::registry));auto operation=std::make_shared<IgbFieldRegistryOperation>();
        operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};return operation;
    };
}
} // namespace dw4
