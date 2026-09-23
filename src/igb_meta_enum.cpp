#include "dw4/igb_meta_enum.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetaEnumError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
bool getter(std::uint32_t address){return address==0x8002a998 || address==0x8002ab74;}
std::optional<std::uint32_t> type_address(const std::shared_ptr<IgbMetaEnumServices>& services,
    const std::shared_ptr<IgbRuntimeMetaType>& type,std::optional<std::uint32_t> address){
    if(!type){if(address && *address!=0)fail("Null enum field type has a nonnull arithmetic address.");return 0;}
    const auto lists=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    if(lists)for(const auto& binding:lists->values)if(binding.metadata==type){
        if(binding.field || binding.address==0 || (address && *address!=binding.address))fail("Enum field type has conflicting arithmetic identities.");
        address=binding.address;
    }
    if(address && *address==0)fail("Nonnull enum field type has a null arithmetic address.");return address;
}
}
IgbMetaEnumServices::IgbMetaEnumServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x800251ec,0x8002879c,0x8002879c});
    names=std::make_shared<IgbFieldNameTable>();globals=std::make_shared<IgbFieldGlobalTable>();
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8,12,16});
    for(const auto name:{"_name","_names","_values"})names->push_back(std::make_shared<const std::string>(name));
    for(unsigned i=0;i<3;++i)globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x8056180cu+i*4});
}
IgbTypeRegistrationArguments igb_meta_enum_registration_arguments(const std::shared_ptr<IgbRegistrationMetaSlot>& slot){
    if(!slot)fail("Enum registration requires canonical80561808 storage.");
    IgbTypeRegistrationArguments args;args.metadata_slot=slot;args.name=std::make_shared<const std::string>("igMetaEnum");
    args.flag=0;args.native_size=0x14;args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;
    args.field_callback=0x8002ab74;args.layout_probe=0x8002a9d4;args.defaults_callback=0x8002ab94;args.trailing_callback=0;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x80024c64,0x80036684,0});
    return args;
}
struct IgbMetaEnumRegistrationInvocation::Impl {
    enum class P {prerequisites,arguments,registration,done};
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;
    P phase=P::prerequisites;bool running{},fault{};
};
IgbMetaEnumRegistrationInvocation::IgbMetaEnumRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbMetaEnumRegistrationInvocation::~IgbMetaEnumRegistrationInvocation()=default;
bool IgbMetaEnumRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaEnumRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbMetaEnumRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete enum registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbMetaEnumRegistrationInvocation::resume(std::shared_ptr<IgbMetaEnumServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Enum registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Enum registration continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->metadata)return pending("Enum registration requires canonical80561808 storage.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_meta_enum_registration_arguments(services->metadata));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbMetaEnumLookupInvocation::Impl {
    enum class P {read,registration,reload,done};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    P phase=P::read;bool running{},fault{};
};
IgbMetaEnumLookupInvocation::IgbMetaEnumLookupInvocation():impl_(std::make_unique<Impl>()){}
IgbMetaEnumLookupInvocation::~IgbMetaEnumLookupInvocation()=default;
bool IgbMetaEnumLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaEnumLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbMetaEnumLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete enum lookup has no result.");return impl_->result;
}
MessengerCallResult IgbMetaEnumLookupInvocation::resume(std::shared_ptr<IgbMetaEnumServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Enum lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Enum lookup continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){if(!services || !services->metadata)return pending("Enum lookup requires canonical80561808 storage.");s.slot=services->metadata;}
        if(!*s.slot)return pending("Enum lookup requires represented80561808.");
        if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8002aafc);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Enum lookup requires its reloaded80561808 value.");s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbMetaEnumDefaultsInvocation::Impl {
    enum class P {capture,batch,select,getter,store,schema,done};
    std::shared_ptr<IgbMetaEnumServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,type;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    IgbMetaEnumCall request;IgbMetaEnumFactory factory;std::shared_ptr<IgbMetaEnumOperation> operation;
    std::function<MessengerCallResult()> callback;std::optional<std::uint32_t> address;
    std::uint32_t first{},index{1};P phase=P::capture;bool running{},fault{};
};
IgbMetaEnumDefaultsInvocation::IgbMetaEnumDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbMetaEnumDefaultsInvocation::~IgbMetaEnumDefaultsInvocation(){auto& s=*impl_;if(!completed() && s.owner){
    if(s.metadata)s.owner->unfinished.push_back(s.metadata);if(s.field)s.owner->unfinished.push_back(s.field);
    if(s.type)s.owner->unfinished.push_back(s.type);
}}
bool IgbMetaEnumDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaEnumDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetaEnumDefaultsInvocation::resume(std::shared_ptr<IgbMetaEnumServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Enum defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Enum defaults continuation was invalidated.");};
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    try{for(;;){check();switch(s.phase){
    case P::capture:
        if(!services || !services->metadata || !*services->metadata)return pending("Enum defaults require represented80561808.");
        s.metadata=**services->metadata;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,3);s.phase=P::batch;break;
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::select;break;}
    case P::select:
        if(s.index==3){
            if(!services)return pending("Enum defaults require their original schema tables.");
            s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
        }
        s.field=igb_runtime_field_at(s.metadata,std::bit_cast<std::int32_t>(((s.first+s.index)<<2u)>>2u));
        clear();s.request.function=s.index==1?0x80024b94:0x800365b4;s.request.field=s.field;
        if(services)s.factory=services->factory;s.phase=P::getter;break;
    case P::getter:
        if(!s.factory && services)s.factory=services->factory;
        if(!s.factory)return pending("Enum defaults require the reached object-type getter.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Enum defaults reached an unbound object-type getter.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Enum defaults require their selected getter continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        s.type=s.operation->metadata_result;s.address=s.operation->metadata_address;clear();s.phase=P::store;break;
    case P::store:
        if(!s.field)fail("Enum defaults store through a null selected field.");
        if(s.field->physical_storage){
            const auto address=type_address(services,s.type,s.address);
            if(!address)return pending("Enum defaults require the captured type's arithmetic pointer binding.");
            const auto binding=*s.field->physical_storage;IgbBootstrapPointer pointer{binding.storage.lock(),binding.offset};
            if(igb_bootstrap_field(pointer)!=s.field)fail("Enum defaults lost the selected field's physical binding.");
            igb_bootstrap_write_word(pointer,0x38,*address);
        }
        s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.type);s.field.reset();s.type.reset();s.address.reset();++s.index;s.phase=P::select;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbMetaEnumLayoutProbeInvocation::Impl {
    enum class P {reset,core,initialize,offset,word,cleanup,done};
    IgbBootstrapPointer local;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<IgbMetaEnumServices> owner;
    std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::uint32_t offset{},value{};P phase=P::reset;bool running{},fault{};
};
IgbMetaEnumLayoutProbeInvocation::IgbMetaEnumLayoutProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbMetaEnumLayoutProbeInvocation::~IgbMetaEnumLayoutProbeInvocation(){auto& s=*impl_;
    if(!completed() && s.owner && s.local.storage)s.owner->unfinished.push_back(s.local.storage);
}
bool IgbMetaEnumLayoutProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaEnumLayoutProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbMetaEnumLayoutProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete enum layout probe has no value.");return impl_->value;
}
MessengerCallResult IgbMetaEnumLayoutProbeInvocation::resume(std::shared_ptr<IgbMetaEnumServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Enum layout probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Enum layout probe continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Enum probe requires ArkCore80562100 after base reset.");
        s.core=*services->types->runtime->singleton;s.phase=P::initialize;break;
    case P::initialize:
        igb_bootstrap_write_word(s.local,0,0x8047617c);igb_bootstrap_write_word(s.local,8,0);
        igb_bootstrap_write_word(s.local,12,0);igb_bootstrap_write_word(s.local,16,0);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Enum probe dereferences null ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Enum probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>16 || (s.offset&3u)!=0)fail("Enum probe exceeds its aligned0x14-byte stack object.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);igb_bootstrap_write_word(s.local,0,0x8047617c);s.phase=P::cleanup;break;
    case P::cleanup:
        // The original first cleanup uses register-known null, not a fresh+10
        // read. The other two members were zeroed with no intervening callback.
        if(igb_bootstrap_read_word(s.local,12)!=0 || igb_bootstrap_read_word(s.local,8)!=0)
            fail("Enum probe reaches nonnull teardown outside its inline-construction profile.");
        s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_meta_enum_function_factory(std::shared_ptr<IgbMetaEnumServices> services,
    IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call){
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x8002aafc && call.function!=0x8002aad4))
            return remaining?remaining(call):std::shared_ptr<IgbRegistrationCallbacksOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Enum services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8002aafc){auto invocation=std::make_shared<IgbMetaEnumRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8002aafc);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_meta_enum_type_factory(std::shared_ptr<IgbMetaEnumServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x8002aafc;
        const bool lookup=call.kind==K::get_parent && getter(call.function);
        const bool defaults=call.kind==K::defaults && call.function==0x8002ab94;
        const bool probe=call.kind==K::layout_probe && call.function==0x8002a9d4;
        if(!registration && !lookup && !defaults && !probe)return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Enum services expired before type operation selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbMetaEnumRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->callbacks_result=invocation->result();return status;};
        }else if(lookup){auto invocation=std::make_shared<IgbMetaEnumLookupInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
                if(status.completed)out->metadata_result=invocation->result();return status;};
        }else if(defaults){auto invocation=std::make_shared<IgbMetaEnumDefaultsInvocation>();
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
        }else{operation->resume=[selected,invocation=std::shared_ptr<IgbMetaEnumLayoutProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Enum probe requires its represented stack-local storage.");
                invocation=std::make_shared<IgbMetaEnumLayoutProbeInvocation>(selected->local_storage());}
            return invocation->resume(selected);};}
        return operation;
    };
}
} // namespace dw4
