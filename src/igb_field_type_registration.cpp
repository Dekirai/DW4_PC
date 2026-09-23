#include "dw4/igb_field_type_registration.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldTypeRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
using Entry=IgbFieldTypeKind;
constexpr std::array profiles{
    IgbFieldTypeProfile{"igMetaField",0x805617bc,0x8002a6b0,0x8002a6d8,0x34,1,0x80066b08,0x800237d0,
        0x8002a744,0,0x8002a764,0x8002a638,0x8002a674,0x80021d70,0x8055d290,{0x8002a0a8,0,0}},
    IgbFieldTypeProfile{"igRefMetaField",0x80561684,0x80027338,0x80027360,0x38,1,0x8002a6d8,0x80021d70,
        0x800273cc,0,0x800273ec,0,0x800272fc,0x80025494,0x8055d158,{0x80027454,0,0}},
    IgbFieldTypeProfile{"igObjectRefMetaField",0x805616e8,0x800289f8,0x80028a20,0x44,0,0x80027360,0x80025494,
        0x80028a94,0x80028934,0x80028ab4,0x800288bc,0x800288f8,0,0x804640c4,{0x8002a0a8,0x80028b64,0}},
    IgbFieldTypeProfile{"igIntMetaField",0x805619e0,0x8002e390,0x8002e3b8,0x34,0,0x8002a6d8,0x80021d70,
        0x8002e420,0x8002e308,0,0,0x8002e2cc,0,0x8055d3c4,{0x8002e440,0,0}},
    IgbFieldTypeProfile{"igMemoryRefMetaField",0x80561818,0x8002ae84,0x8002aeac,0x50,0,0x80027360,0x80025494,
        0x8002af20,0x8002ad90,0x8002af40,0,0x8002ad54,0,0x804649f0,{0x8002a6b0,0x8002b178,0}},
    IgbFieldTypeProfile{"igBoolMetaField",0x80561e48,0x80038800,0x80038828,0x34,0,0x8002a6d8,0x80021d70,
        0x80038890,0x80038778,0,0x80038700,0x8003873c,0,0x8055d690,{0x800388b0,0,0}}
};
std::optional<Entry> registration_function(std::uint32_t function,bool wrapper){
    for(std::size_t i=0;i<profiles.size();++i)if(function==(wrapper?profiles[i].wrapper:profiles[i].body))return static_cast<Entry>(i);return {};
}
struct Lookup {Entry entry;IgbFieldTypeLookup mode;};
std::optional<Lookup> lookup_function(std::uint32_t function){
    if(!function)return {};
    for(std::size_t i=0;i<profiles.size();++i){const auto& p=profiles[i];const auto entry=static_cast<Entry>(i);
        if(function==p.full_getter || function==p.field_callback)return Lookup{entry,IgbFieldTypeLookup::fully_registered};
        if(function==p.lazy_getter)return Lookup{entry,IgbFieldTypeLookup::allocate_if_null};
        if(function==p.plain_getter)return Lookup{entry,IgbFieldTypeLookup::plain};
    }return {};
}
}
const IgbFieldTypeProfile& igb_field_type_profile(Entry entry){
    const auto index=static_cast<std::size_t>(entry);if(index>=profiles.size())fail("Unknown field-type registration class.");return profiles[index];
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_field_type_metadata_slot(const std::shared_ptr<IgbFieldTypeRegistrationServices>& services,Entry entry){
    igb_field_type_profile(entry);if(!services)return {};
    if(entry==Entry::base)return services->base_metadata;
    if(entry==Entry::reference)return services->reference_metadata;
    const auto kind=entry==Entry::signed_int?IgbMetaFieldKind::signed_int:
        entry==Entry::memory_reference?IgbMetaFieldKind::memory_reference:
        entry==Entry::boolean?IgbMetaFieldKind::boolean:IgbMetaFieldKind::object;
    return services->meta_fields?services->meta_fields->metadata_slots[static_cast<std::size_t>(kind)]:nullptr;
}
IgbTypeRegistrationArguments igb_field_type_registration_arguments(const std::shared_ptr<IgbFieldTypeRegistrationServices>& services,Entry entry){
    IgbTypeRegistrationArguments args;args.metadata_slot=igb_field_type_metadata_slot(services,entry);
    if(!args.metadata_slot)fail("Field-type registration requires its canonical metadata cell.");
    const auto& p=igb_field_type_profile(entry);args.name=std::make_shared<const std::string>(p.name);args.flag=p.flag;args.native_size=p.size;
    args.parent_registration=p.parent_body;args.parent_getter=p.parent_getter;args.field_callback=p.field_callback;
    args.layout_probe=p.probe;args.defaults_callback=p.defaults;args.trailing_callback=0;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>();
    for(const auto address:p.extra_callbacks){args.extra_callbacks->push_back(address);if(!address)break;}
    return args;
}
struct IgbFieldTypeRegistrationInvocation::Impl {
    Entry entry{};std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{},done{};
};
IgbFieldTypeRegistrationInvocation::IgbFieldTypeRegistrationInvocation(Entry entry):impl_(std::make_unique<Impl>()){igb_field_type_profile(entry);impl_->entry=entry;}
IgbFieldTypeRegistrationInvocation::~IgbFieldTypeRegistrationInvocation()=default;
bool IgbFieldTypeRegistrationInvocation::completed()const noexcept{return impl_->done;}
bool IgbFieldTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbFieldTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete field-type registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbFieldTypeRegistrationInvocation::resume(std::shared_ptr<IgbFieldTypeRegistrationServices> services){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("Field-type registration requires an unfaulted nonrecursive invocation.");}
    if(s.done)return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Field-type registration continuation was invalidated.");};
    try{
        if(!s.registration){if(!igb_field_type_metadata_slot(services,s.entry))return pending("Field-type registration requires its canonical metadata cell.");
            s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_field_type_registration_arguments(services,s.entry));}
        const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.done=true;return {true,0,{}};
    }catch(...){s.fault=true;throw;}
}
struct IgbFieldTypeLookupInvocation::Impl {
    enum class P{read,handle,pool,construct,publish,registration,reload,done};
    Entry entry{};IgbFieldTypeLookup mode{};P phase=P::read;
    std::shared_ptr<IgbRegistrationMetaSlot> slot;
    std::shared_ptr<IgbFieldTypeRegistrationServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRuntimeMetaType> result;
    std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> factory;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    bool running{},fault{};
};
IgbFieldTypeLookupInvocation::IgbFieldTypeLookupInvocation(Entry entry,IgbFieldTypeLookup mode):impl_(std::make_unique<Impl>()){
    const auto& profile=igb_field_type_profile(entry);
    if((mode==IgbFieldTypeLookup::allocate_if_null && !profile.lazy_getter) ||
       (mode==IgbFieldTypeLookup::plain && !profile.plain_getter) ||
       (mode!=IgbFieldTypeLookup::allocate_if_null && mode!=IgbFieldTypeLookup::plain && mode!=IgbFieldTypeLookup::fully_registered))
        fail("This field-type profile has no represented getter for the requested mode.");
    impl_->entry=entry;impl_->mode=mode;
}
IgbFieldTypeLookupInvocation::~IgbFieldTypeLookupInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.result)s.owner->retained.push_back(s.result);if(s.pool)s.owner->retained.push_back(s.pool);
}}
bool IgbFieldTypeLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldTypeLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbFieldTypeLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete field-type metadata getter has no result.");return impl_->result;
}
MessengerCallResult IgbFieldTypeLookupInvocation::resume(std::shared_ptr<IgbFieldTypeRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field-type metadata getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field-type metadata getter continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_field_type_metadata_slot(services,s.entry);if(!s.slot)return pending("Field-type metadata getter requires its canonical global cell.");}
        if(!*s.slot)return pending("Field-type metadata getter requires represented canonical global state.");
        if(s.mode==IgbFieldTypeLookup::plain)s.phase=P::reload;
        else if(s.mode==IgbFieldTypeLookup::allocate_if_null)s.phase=**s.slot?P::reload:P::handle;
        else if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(igb_field_type_profile(s.entry).body);s.phase=P::registration;}break;
    case P::handle:
        if(!services || !services->types || !services->types->metadata_pool_handle)return pending("Lazy field-type metadata requires pool handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.pool_call->resume(services?services->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.phase=P::construct;break;}
    case P::construct:
        if(!s.factory){if(!services || !services->metadata)return pending("Lazy field-type metadata requires original metadata factory services.");
            s.metadata=services->metadata;s.factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);}
        {const auto status=s.factory->resume(s.metadata);check();if(!status.completed)return status;}
        s.result=s.factory->result();s.factory.reset();check();s.phase=P::publish;break;
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result && s.owner)s.owner->retained.push_back(**s.slot);
        *s.slot=s.result;s.phase=P::reload;break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Field-type metadata getter requires its reloaded canonical global.");
        s.result=**s.slot;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_field_type_registration_function_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbFieldTypeRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field-type registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct){auto invocation=std::make_shared<IgbFieldTypeRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(igb_field_type_profile(*wrapper).body);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_field_type_registration_type_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbFieldTypeRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<Entry>{};
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        if(!registration && !lookup)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field-type registration services expired before type-operation selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbFieldTypeRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbFieldTypeLookupInvocation>(lookup->entry,lookup->mode);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        return operation;
    };
}
IgbMetaFieldFactory make_native_igb_field_type_registration_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices> services,IgbMetaFieldFactory remaining){
    std::weak_ptr<IgbFieldTypeRegistrationServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbMetaFieldCall& call){
        const auto wrapper=registration_function(call.function,true);
        if(call.kind!=IgbMetaFieldServiceKind::register_type || !wrapper)return remaining?remaining(call):std::shared_ptr<IgbMetaFieldOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field-type registration services expired before field-factory selection.");
        auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(igb_field_type_profile(*wrapper).body);
        auto operation=std::make_shared<IgbMetaFieldOperation>();
        operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};return operation;
    };
}
} // namespace dw4
