#include "dw4/igb_data_list_registration.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbDataListRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using E=IgbDataListKind;using L=IgbDataListLookup;
constexpr std::array profiles{
    IgbDataListProfile{"igDataList",0x80561d3c,0x800339ec,0x80033a14,0x80066b08,0x800237d0,0x80033a84,0x800339ac,0x80033aa4,0x80033970,0x80033934,0x80024d1c},
    IgbDataListProfile{"igNonRefCountedObjectList",0x80561730,0x8002966c,0x80029694,0x80033a14,0x80024d1c,0x80029704,0x80029614,0x80029724,0x800295d8,0,0x80023fdc},
    IgbDataListProfile{"igNonRefCountedThreadList",0x80561530,0x80023f28,0x80023f50,0x80029694,0x80023fdc,0x80023fbc,0x80023eb8,0,0x80023e7c,0x80023e40,0},
    IgbDataListProfile{"igObjectList",0x80561708,0x80029054,0x8002907c,0x80033a14,0x80024d1c,0x800290ec,0x80028ffc,0x8002910c,0x80028fc0,0x80028f84,0x80024180},
    IgbDataListProfile{"igThreadList",0x80561534,0x800240cc,0x800240f4,0x8002907c,0x80024180,0x80024160,0x8002405c,0,0x80024020,0x80023fe4,0}
};
std::optional<E> registration_function(std::uint32_t function,bool wrapper){
    for(std::size_t i=0;i<profiles.size();++i)if(function==(wrapper?profiles[i].wrapper:profiles[i].body))return static_cast<E>(i);return {};
}
struct Lookup {E kind;L mode;};
std::optional<Lookup> lookup_function(std::uint32_t function){
    if(!function)return {};
    for(std::size_t i=0;i<profiles.size();++i){const auto& p=profiles[i];const auto kind=static_cast<E>(i);
        if(function==p.full_getter || function==p.field_callback)return Lookup{kind,L::fully_registered};
        if(function==p.plain_getter)return Lookup{kind,L::plain};
        if(function==p.lazy_getter)return Lookup{kind,L::allocate_if_null};
    }return {};
}
std::optional<E> probe_function(std::uint32_t function){
    for(std::size_t i=0;i<profiles.size();++i)if(function==profiles[i].probe)return static_cast<E>(i);return {};
}
constexpr std::array<std::uint32_t,5> nonref_probe_words{0x80472fa0,0x80476e0c,0x80476da8,0x80476d44,0x80476ce0};
constexpr std::array<std::uint32_t,5> ref_probe_words{0x80472fa0,0x80476e0c,0x80476c7c,0x80476c18,0x80476bb4};
const auto& probe_words(E kind){return kind==E::ref_object || kind==E::ref_thread?ref_probe_words:nonref_probe_words;}
unsigned probe_count(E kind){return kind==E::data_list?1u:(kind==E::nonref_object || kind==E::ref_object)?3u:5u;}
unsigned probe_capture(E kind){return kind==E::data_list?0u:(kind==E::nonref_object || kind==E::ref_object)?1u:3u;}
}
const IgbDataListProfile& igb_data_list_profile(E kind){
    const auto index=static_cast<std::size_t>(kind);if(index>=profiles.size())fail("Unknown data-list registration class.");return profiles[index];
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_data_list_metadata_slot(const std::shared_ptr<IgbDataListRegistrationServices>& services,E kind){
    igb_data_list_profile(kind);if(!services)return {};
    if(kind==E::data_list)return services->data_metadata;
    if(kind==E::nonref_object)return services->object_metadata;
    if(kind==E::ref_object)return services->ref_object_metadata;
    if(kind==E::ref_thread)return services->ref_thread_metadata;
    return services->dependencies?services->dependencies->thread_list:nullptr;
}
IgbTypeRegistrationArguments igb_data_list_type_registration_arguments(const std::shared_ptr<IgbDataListRegistrationServices>& services,E kind){
    const auto& p=igb_data_list_profile(kind);IgbTypeRegistrationArguments args;
    args.metadata_slot=igb_data_list_metadata_slot(services,kind);if(!args.metadata_slot)fail("Data-list registration requires its canonical metadata cell.");
    args.name=std::make_shared<const std::string>(p.name);args.flag=0;args.native_size=0x14;
    args.parent_registration=p.parent;args.parent_getter=p.parent_getter;args.field_callback=p.field_callback;
    args.layout_probe=p.probe;args.defaults_callback=p.defaults;
    if(kind==E::thread_list || kind==E::ref_thread)args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x80023c40,0});
    return args;
}
struct IgbDataListTypeRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;E kind{};
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbDataListTypeRegistrationInvocation::IgbDataListTypeRegistrationInvocation(E kind):impl_(std::make_unique<Impl>()){igb_data_list_profile(kind);impl_->kind=kind;}
IgbDataListTypeRegistrationInvocation::~IgbDataListTypeRegistrationInvocation()=default;
bool IgbDataListTypeRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbDataListTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete data-list registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbDataListTypeRegistrationInvocation::resume(std::shared_ptr<IgbDataListRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Data-list registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Data-list registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!igb_data_list_metadata_slot(services,s.kind))return pending("Data-list registration requires its canonical metadata cell after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_data_list_type_registration_arguments(services,s.kind));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbDataListLookupInvocation::Impl {
    enum class P{read,handle,pool,construct,publish,registration,reload,thread_lazy,done};P phase=P::read;
    E kind{};L mode{};std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbDataListRegistrationServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<ModelSceneObject> pool;std::shared_ptr<IgbRuntimeMetaType> result;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::unique_ptr<IgbThreadDependencyLookupInvocation> thread;
    bool running{},fault{};
};
IgbDataListLookupInvocation::IgbDataListLookupInvocation(E kind,L mode):impl_(std::make_unique<Impl>()){
    const auto& p=igb_data_list_profile(kind);
    if(mode!=L::plain && mode!=L::fully_registered && mode!=L::allocate_if_null)fail("Unknown data-list metadata lookup mode.");
    if(mode==L::allocate_if_null && !p.lazy_getter)fail("No recovered lazy getter exists for this data-list profile.");
    impl_->kind=kind;impl_->mode=mode;
    if(kind==E::thread_list && mode==L::allocate_if_null){impl_->thread=std::make_unique<IgbThreadDependencyLookupInvocation>(IgbThreadDependencyEntry::thread_list);impl_->phase=Impl::P::thread_lazy;}
}
IgbDataListLookupInvocation::~IgbDataListLookupInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.result)s.owner->retained.push_back(s.result);if(s.pool)s.owner->retained.push_back(s.pool);if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
}}
bool IgbDataListLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbDataListLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete data-list lookup has no metadata result.");return impl_->result;
}
MessengerCallResult IgbDataListLookupInvocation::resume(std::shared_ptr<IgbDataListRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Data-list lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Data-list lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_data_list_metadata_slot(services,s.kind);if(!s.slot)return pending("Data-list lookup requires its canonical metadata cell.");}
        if(!*s.slot)return pending("Data-list lookup requires represented canonical metadata.");
        if(s.mode==L::plain)s.phase=P::reload;
        else if(s.mode==L::allocate_if_null)s.phase=**s.slot?P::reload:P::handle;
        else if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(igb_data_list_profile(s.kind).body);s.phase=P::registration;}break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Data-list lazy getter requires metadata pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::construct;break;}
    case P::construct:
        if(!s.factory){if(!services || !services->metadata)return pending("Data-list lazy getter requires actual metadata factory29E64.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);}
        {const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status;}
        s.result=s.factory->result();s.allocation=s.factory->allocation();if(s.owner && s.allocation.storage)s.owner->allocations.push_back(s.allocation);
        s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Data-list lookup requires its reloaded canonical metadata.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::thread_lazy:{const auto status=s.thread->resume(services?services->dependencies:nullptr);check();if(!status.completed)return status;
        s.result=s.thread->result();s.thread.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbDataListLayoutProbeInvocation::Impl {
    enum class P{reset,before_core,core,after_core,offset,word,done};P phase=P::reset;
    E kind{};IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbDataListRegistrationServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    unsigned index{};std::uint32_t offset{},value{};bool running{},fault{};
};
IgbDataListLayoutProbeInvocation::IgbDataListLayoutProbeInvocation(IgbBootstrapPointer local,E kind):impl_(std::make_unique<Impl>()){
    igb_data_list_profile(kind);impl_->local=std::move(local);impl_->kind=kind;
}
IgbDataListLayoutProbeInvocation::~IgbDataListLayoutProbeInvocation()=default;
bool IgbDataListLayoutProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListLayoutProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbDataListLayoutProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete data-list probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbDataListLayoutProbeInvocation::resume(std::shared_ptr<IgbDataListRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Data-list probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Data-list probe was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::before_core;break;
    case P::before_core:
        while(s.index<probe_capture(s.kind)){igb_bootstrap_write_word(s.local,0,probe_words(s.kind)[s.index]);++s.index;}
        s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Data-list probe requires fresh ArkCore80562100 at its original capture point.");
        s.core=*services->types->runtime->singleton;s.phase=P::after_core;break;
    case P::after_core:
        while(s.index<probe_count(s.kind)){igb_bootstrap_write_word(s.local,0,probe_words(s.kind)[s.index]);++s.index;}
        s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Data-list probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Data-list probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>16 || (s.offset&3u)!=0)fail("Data-list probe exceeds its aligned20-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::done;return {true,s.value,{}};
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_data_list_registration_function_factory(std::shared_ptr<IgbDataListRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Data-list registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct){auto invocation=std::make_shared<IgbDataListTypeRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(igb_data_list_profile(*wrapper).body);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_data_list_registration_type_factory(std::shared_ptr<IgbDataListRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<E>{};
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        const auto probe=call.kind==IgbTypeRegistrationServiceKind::layout_probe?probe_function(call.function):std::optional<E>{};
        if(!registration && !lookup && !probe)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Data-list registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbDataListTypeRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(lookup){auto invocation=std::make_shared<IgbDataListLookupInvocation>(lookup->kind,lookup->mode);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else operation->resume=[selected,kind=*probe,invocation=std::shared_ptr<IgbDataListLayoutProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Data-list probe requires its represented20-byte stack local.");
                auto local=selected->local_storage(kind);if(!igb_bootstrap_address(local))fail("Data-list probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbDataListLayoutProbeInvocation>(std::move(local),kind);}
            return invocation->resume(selected);
        };return operation;
    };
}
} // namespace dw4
