#include "dw4/igb_field_prototype.hpp"
#include "dw4/igb_field_reference.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldPrototypeError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
void profile(IgbFieldTypeKind kind){if(kind!=IgbFieldTypeKind::reference && kind!=IgbFieldTypeKind::object &&
    kind!=IgbFieldTypeKind::signed_int && kind!=IgbFieldTypeKind::memory_reference &&
    kind!=IgbFieldTypeKind::boolean && kind!=IgbFieldTypeKind::string && kind!=IgbFieldTypeKind::signed_byte &&
    kind!=IgbFieldTypeKind::enumeration && kind!=IgbFieldTypeKind::pointer && kind!=IgbFieldTypeKind::unsigned_byte &&
    kind!=IgbFieldTypeKind::unsigned_int && kind!=IgbFieldTypeKind::unsigned_short &&
    kind!=IgbFieldTypeKind::structure && kind!=IgbFieldTypeKind::signed_long)fail("Unsupported field prototype callback.");}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field->physical_storage)return {};
    const auto& binding=*field->physical_storage;IgbBootstrapPointer pointer{binding.storage.lock(),binding.offset};
    if(!pointer.storage || igb_bootstrap_field(pointer)!=field)fail("Prototype store requires its live canonical physical field binding.");
    return pointer;
}
void store_ordinal(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint16_t value){
    if(const auto pointer=physical_field(field))igb_bootstrap_write_halfword(*pointer,0x12,value);
    field->ordinal=value;
}
}
std::shared_ptr<IgbRuntimeFieldSlot> igb_field_prototype_slot(const std::shared_ptr<IgbFieldPrototypeServices>& services,IgbFieldTypeKind kind){
    profile(kind);if(!services)return {};if(kind==IgbFieldTypeKind::reference)return services->reference_prototype;
    if(kind==IgbFieldTypeKind::unsigned_int){const auto fields=services->values?services->values->fields:nullptr;
        return fields?std::shared_ptr<IgbRuntimeFieldSlot>(fields,&fields->unsigned_int_prototype):nullptr;}
    if(kind==IgbFieldTypeKind::signed_int)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->signed_int_prototype):nullptr;
    if(kind==IgbFieldTypeKind::memory_reference)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->memory_reference_prototype):nullptr;
    if(kind==IgbFieldTypeKind::boolean)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->boolean_prototype):nullptr;
    if(kind==IgbFieldTypeKind::string)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->string_prototype):nullptr;
    if(kind==IgbFieldTypeKind::signed_byte)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->signed_byte_prototype):nullptr;
    if(kind==IgbFieldTypeKind::enumeration)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->enumeration_prototype):nullptr;
    if(kind==IgbFieldTypeKind::pointer)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->pointer_prototype):nullptr;
    if(kind==IgbFieldTypeKind::unsigned_byte)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->unsigned_byte_prototype):nullptr;
    if(kind==IgbFieldTypeKind::unsigned_short)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->unsigned_short_prototype):nullptr;
    if(kind==IgbFieldTypeKind::signed_long)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->signed_long_prototype):nullptr;
    if(kind==IgbFieldTypeKind::structure)return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->struct_prototype):nullptr;
    return services->values?std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->object_prototype):nullptr;
}
struct IgbFieldPrototypeLookupInvocation::Impl {
    enum class P {read,registration,reload,done};P phase=P::read;IgbFieldTypeKind kind{};
    std::shared_ptr<IgbRuntimeFieldSlot> slot;std::shared_ptr<IgbDecodeRuntimeField> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;bool running{},fault{};
};
IgbFieldPrototypeLookupInvocation::IgbFieldPrototypeLookupInvocation(IgbFieldTypeKind kind):impl_(std::make_unique<Impl>()){
    if(kind!=IgbFieldTypeKind::enumeration && kind!=IgbFieldTypeKind::pointer && kind!=IgbFieldTypeKind::unsigned_int &&
        kind!=IgbFieldTypeKind::unsigned_short)
        fail("Unsupported prototype getter profile.");
    impl_->kind=kind;
}
IgbFieldPrototypeLookupInvocation::~IgbFieldPrototypeLookupInvocation()=default;
bool IgbFieldPrototypeLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldPrototypeLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbFieldPrototypeLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete prototype getter has no result.");return impl_->result;
}
MessengerCallResult IgbFieldPrototypeLookupInvocation::resume(std::shared_ptr<IgbFieldPrototypeServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Prototype getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Prototype getter was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_field_prototype_slot(services,s.kind);if(!s.slot)return pending("Prototype getter requires its canonical field cell.");}
        if(!*s.slot)return pending("Prototype getter requires represented canonical state.");
        if(**s.slot)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(igb_field_type_profile(s.kind).body);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services && services->registration?services->registration->callbacks:nullptr);
        check();if(!status.completed)return status;s.registration.reset();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Prototype getter requires its reloaded canonical state.");s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbFieldPrototypeRegistryInvocation::Impl {
    enum class P {read,handle,pool,construct,publish,reload,done};P phase=P::read;
    std::shared_ptr<IgbFieldPrototypeServices> owner;
    std::shared_ptr<std::optional<std::shared_ptr<IgbDecodeFieldRegistry>>> slot;
    std::shared_ptr<IgbDecodeFieldRegistry> result;
    std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbPoolByHandleInvocation> lookup;
    std::unique_ptr<IgbFieldRegistryFactoryInvocation> factory;
    bool running{},fault{};
};
IgbFieldPrototypeRegistryInvocation::IgbFieldPrototypeRegistryInvocation():impl_(std::make_unique<Impl>()){}
IgbFieldPrototypeRegistryInvocation::~IgbFieldPrototypeRegistryInvocation()=default;
bool IgbFieldPrototypeRegistryInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldPrototypeRegistryInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeFieldRegistry> IgbFieldPrototypeRegistryInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete prototype registry getter has no result.");return impl_->result;
}
MessengerCallResult IgbFieldPrototypeRegistryInvocation::resume(std::shared_ptr<IgbFieldPrototypeServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Prototype registry requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Prototype registry continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){if(!services || !services->registry)return pending("Prototype registry requires canonical8056229C storage.");s.owner=services;s.slot=services->registry;}
        if(!*s.slot)return pending("Prototype registry requires represented8056229C.");s.phase=**s.slot?P::reload:P::handle;break;
    case P::handle:
        if(!services || !services->registration || !services->registration->types || !services->registration->types->metadata_pool_handle)
            return pending("Prototype registry requires metadata pool handle805621F4.");
        s.lookup=std::make_unique<IgbPoolByHandleInvocation>(*services->registration->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.lookup->resume(services && services->registration?services->registration->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.lookup->result();s.lookup.reset();check();s.factory=std::make_unique<IgbFieldRegistryFactoryInvocation>(s.pool);s.phase=P::construct;break;}
    case P::construct:{const auto status=s.factory->resume(services?services->registries:nullptr);check();if(!status.completed)return status;
        s.result=s.factory->result();s.factory.reset();check();s.phase=P::publish;break;}
    case P::publish:
        if(*s.slot && **s.slot && **s.slot!=s.result)s.owner->retained.push_back(**s.slot);*s.slot=s.result;s.phase=P::reload;break;
    case P::reload:
        if(!*s.slot)return pending("Prototype registry requires reloaded8056229C.");s.result=**s.slot;s.phase=P::done;return {true,0,{}};
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbFieldPrototypeInvocation::Impl {
    enum class P {read,handle,pool,metadata,reflected,after_reflected,publish,registry,retain,append,release,release_test,after_destroy,
        registry_again,ordinal,finalize_capture,finalize,call,done};P phase=P::read,after=P::done;
    IgbFieldTypeKind kind{};
    std::shared_ptr<IgbFieldPrototypeServices> owner;
    std::shared_ptr<IgbRuntimeFieldSlot> slot;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbDecodeRuntimeField> field,live;
    std::shared_ptr<IgbFieldReferenceAccess> retained_reference,released_reference;
    std::optional<std::uint32_t> address;
    std::shared_ptr<IgbDecodeFieldRegistry> registry;
    std::unique_ptr<IgbPoolByHandleInvocation> lookup;
    std::unique_ptr<IgbReflectedFactoryInvocation> reflected;
    std::shared_ptr<IgbFileFactoryServices> reflected_services;
    IgbObjectInstance reflected_instance;
    std::unique_ptr<IgbFieldPrototypeRegistryInvocation> registry_getter;
    std::shared_ptr<IgbMetadataOperation> append;
    std::function<MessengerCallResult()> append_callback;
    std::unique_ptr<IgbFieldFinalizeInvocation> finalize;
    IgbFieldPrototypeCall request;
    IgbFieldPrototypeFactory factory;
    std::shared_ptr<IgbFieldPrototypeOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::uint16_t ordinal{};bool running{},fault{};
};
IgbFieldPrototypeInvocation::IgbFieldPrototypeInvocation(IgbFieldTypeKind kind):impl_(std::make_unique<Impl>()){profile(kind);impl_->kind=kind;}
IgbFieldPrototypeInvocation::~IgbFieldPrototypeInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->retained.push_back(s.field);if(s.live)s.owner->retained.push_back(s.live);
    if(s.retained_reference)s.owner->retained.push_back(s.retained_reference);
    if(s.released_reference)s.owner->retained.push_back(s.released_reference);
}}
bool IgbFieldPrototypeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldPrototypeInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldPrototypeInvocation::resume(std::shared_ptr<IgbFieldPrototypeServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbFieldPrototypeServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field prototype requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Field prototype continuation was invalidated.");};
    const auto clear=[&]{s.callback={};s.factory={};s.operation.reset();s.request={};check();};
    const auto select=[&](K kind,P after)->IgbFieldPrototypeCall&{clear();s.request.kind=kind;s.factory=services?services->factory:IgbFieldPrototypeFactory{};
        s.after=after;s.phase=P::call;return s.request;};
    const auto pointers=[&]{return services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;};
    const auto bind=[&]{const auto lists=pointers();if(!lists || !s.field || !s.address)return;
        for(const auto& value:lists->values)if(value.address==*s.address && value.field==s.field && !value.metadata)return;lists->values.push_back({*s.address,{},s.field});};
    const auto live=[&]()->bool{if(!*s.slot)return false;s.live=**s.slot;if(!s.live)fail("Field prototype dereferences a null reloaded descriptor.");return true;};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_field_prototype_slot(services,s.kind);if(!s.slot)return pending("Field prototype requires its canonical descriptor cell.");s.owner=services;}
        if(!*s.slot)return pending("Field prototype requires represented canonical descriptor state.");s.phase=**s.slot?P::done:P::handle;break;
    case P::handle:
        if(!services || !services->registration || !services->registration->types || !services->registration->types->metadata_pool_handle)
            return pending("Field prototype requires metadata pool handle805621F4.");
        s.lookup=std::make_unique<IgbPoolByHandleInvocation>(*services->registration->types->metadata_pool_handle);s.phase=P::pool;break;
    case P::pool:{const auto status=s.lookup->resume(services && services->registration?services->registration->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.lookup->result();s.lookup.reset();check();s.phase=P::metadata;break;}
    case P::metadata:{const auto cell=igb_field_type_metadata_slot(services?services->registration:nullptr,s.kind);
        if(!cell || !*cell)return pending("Field prototype requires its live class metadata after pool lookup.");s.metadata=**cell;s.phase=P::reflected;break;}
    case P::reflected:
        if(s.reflected || (services && services->reflected)){
            if(!s.reflected){s.reflected_services=services->reflected;s.reflected=std::make_unique<IgbReflectedFactoryInvocation>(s.metadata,s.pool);}
            const auto status=s.reflected->resume(s.reflected_services);check();if(!status.completed)return status;
            s.reflected_instance=s.reflected->result_instance();
            if(s.reflected_instance.file)fail("Reflected prototype returned a file binding instead of a typed field descriptor.");
            s.field=s.reflected_instance.field;
            s.retained_reference=std::make_shared<IgbFieldReferenceAccess>(s.field);
            s.address=(s.reflected_instance.storage?s.reflected_instance.storage->original_address:0u)+s.reflected_instance.offset;
            if(s.field && s.reflected_instance.storage)s.owner->retained.push_back(s.reflected_instance.storage);
            s.reflected.reset();s.reflected_services.reset();check();s.phase=P::publish;
        }else{auto& request=select(K::reflected_field,P::after_reflected);request.function=0x8006546c;request.metadata=s.metadata;request.pool=s.pool;}break;
    case P::after_reflected:s.field=s.operation->field_result;s.address=s.operation->field_address;
        s.retained_reference=std::make_shared<IgbFieldReferenceAccess>(s.field);clear();s.phase=P::publish;break;
    case P::publish:
        if(s.address && ((*s.address==0)!=(!s.field)))fail("Reflected prototype pointer and typed descriptor disagree.");
        if(*s.slot && **s.slot && **s.slot!=s.field)s.owner->retained.push_back(**s.slot);*s.slot=s.field;bind();
        if(!s.field)s.phase=P::done;else{s.registry_getter=std::make_unique<IgbFieldPrototypeRegistryInvocation>();s.phase=P::registry;}break;
    case P::registry:{const auto status=s.registry_getter->resume(services);check();if(!status.completed)return status;
        s.registry=s.registry_getter->result();s.registry_getter.reset();check();s.phase=P::retain;break;}
    case P::retain:
        s.retained_reference->write(s.retained_reference->read()+1u);s.phase=P::append;break;
    case P::append:
        bind();if(!s.append){const auto lists=pointers();if(!lists)return pending("Prototype append requires actual pointer-list services.");
            IgbMetadataOperationCall request;request.kind=IgbMetadataOperationKind::append_field;request.fields=s.registry;request.field=s.field;
            s.append=make_native_igb_pointer_list_metadata_factory(lists)(request);check();}
        if(!s.append_callback)s.append_callback=s.append->resume;
        if(!s.append_callback)return pending("Prototype append requires its retained continuation.");
        {const auto status=s.append_callback();check();if(!status.completed)return status;}
        s.append_callback={};s.append.reset();check();s.phase=P::release;break;
    case P::release:
        if(!live())return pending("Prototype release requires its live canonical descriptor.");
        s.released_reference=std::make_shared<IgbFieldReferenceAccess>(s.live);
        s.released_reference->write(s.released_reference->read()-1u);s.phase=P::release_test;break;
    case P::release_test:
        if((s.released_reference->read()&0x7fffffu)==0){const auto field=s.live;auto& request=select(K::destroy_field,P::after_destroy);request.function=0x80066e1c;request.field=field;}
        else s.phase=P::after_destroy;break;
    case P::after_destroy:clear();s.live.reset();s.registry_getter=std::make_unique<IgbFieldPrototypeRegistryInvocation>();s.phase=P::registry_again;break;
    case P::registry_again:{const auto status=s.registry_getter->resume(services);check();if(!status.completed)return status;
        s.registry=s.registry_getter->result();s.registry_getter.reset();check();
        if(!s.registry)fail("Prototype ordinal reads count from null registry.");s.ordinal=static_cast<std::uint16_t>(std::bit_cast<std::uint32_t>(s.registry->count)-1u);s.phase=P::ordinal;break;}
    case P::ordinal:
        if(!live())return pending("Prototype ordinal requires its reloaded canonical descriptor.");store_ordinal(s.live,s.ordinal);s.live.reset();s.phase=P::finalize_capture;break;
    case P::finalize_capture:
        if(!live())return pending("Prototype finalization requires its reloaded descriptor.");s.finalize=std::make_unique<IgbFieldFinalizeInvocation>(s.live);s.phase=P::finalize;break;
    case P::finalize:{const auto status=s.finalize->resume(services && services->schema?services->schema->fields:nullptr);check();if(!status.completed)return status;
        s.finalize.reset();check();s.phase=P::done;break;}
    case P::call:
        if(!s.factory && services)s.factory=services->factory;if(!s.factory)return pending("Field prototype requires its reached typed reflection or destruction operation.");
        if(!s.operation){s.operation=s.factory(s.request);check();}if(!s.operation)return pending("Field prototype reached unbound typed reflection or destruction.");
        if(!s.callback)s.callback=s.operation->resume;if(!s.callback)return pending("Field prototype requires its retained operation continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;s.phase=s.after;}break;
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_field_prototype_function_factory(std::shared_ptr<IgbFieldPrototypeServices> services,IgbRegistrationCallbacksFactory remaining){
    std::weak_ptr<IgbFieldPrototypeServices> weak=services;return [weak,remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x80027454 && call.function!=0x80028b64 &&
            call.function!=0x8002e440 && call.function!=0x8002b178 && call.function!=0x800388b0 && call.function!=0x80025628 && call.function!=0x80037db0 &&
            call.function!=0x80031690 && call.function!=0x8002778c && call.function!=0x800232c0 && call.function!=0x80022d60 &&
            call.function!=0x800220a4 && call.function!=0x800247e4 && call.function!=0x8002d05c))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field prototype services expired before callback selection.");
        const auto kind=call.function==0x80027454?IgbFieldTypeKind::reference:call.function==0x8002e440?IgbFieldTypeKind::signed_int:
            call.function==0x8002b178?IgbFieldTypeKind::memory_reference:call.function==0x800388b0?IgbFieldTypeKind::boolean:
            call.function==0x80025628?IgbFieldTypeKind::string:call.function==0x80037db0?IgbFieldTypeKind::signed_byte:
            call.function==0x80031690?IgbFieldTypeKind::enumeration:call.function==0x8002778c?IgbFieldTypeKind::pointer:
            call.function==0x80022d60?IgbFieldTypeKind::unsigned_int:
            call.function==0x800220a4?IgbFieldTypeKind::unsigned_short:
            call.function==0x800247e4?IgbFieldTypeKind::structure:
            call.function==0x8002d05c?IgbFieldTypeKind::signed_long:
            call.function==0x800232c0?IgbFieldTypeKind::unsigned_byte:IgbFieldTypeKind::object;
        auto invocation=std::make_shared<IgbFieldPrototypeInvocation>(kind);
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
