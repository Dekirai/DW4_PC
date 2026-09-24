#include "dw4/igb_platform_semaphore_registration.hpp"

#include <array>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbPlatformSemaphoreRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
using Lookup=IgbPlatformSemaphoreLookup;
void validate(Lookup mode){if(mode!=Lookup::fully_registered && mode!=Lookup::plain_parent)fail("Unknown platform-semaphore metadata lookup mode.");}
IgbRuntimeFieldDefaultSource one_word(){
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes={0,0,0,1};buffer->initialized.assign(4,1);return {buffer,0};
}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t position){
    if(!metadata || !metadata->fields)fail("Platform-semaphore defaults select through a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(position<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("Platform-semaphore defaults reach an unavailable physical field slot.");return (*backing)[slot];
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Platform-semaphore defaults dereference a null descriptor.");
    if(!field->physical_storage)return {};
    IgbBootstrapPointer pointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
    if(!pointer.storage || pointer.storage->faulted() || igb_bootstrap_field(pointer)!=field)
        fail("Platform-semaphore defaults lost their descriptor's physical identity.");return pointer;
}
std::optional<IgbBootstrapPointer> physical_metadata(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>& services){
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata){
        if(igb_bootstrap_metadata(pointer)!=metadata)continue;
        if(pointer.storage->faulted())fail("Platform-semaphore defaults reached faulted metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("Platform-semaphore metadata has conflicting physical identities.");
        result=pointer;
    }return result;
}
}
IgbPlatformSemaphoreRegistrationServices::IgbPlatformSemaphoreRegistrationServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x80022a90u,0x80022a90u,0x800385e0u,0x8002450cu});
    names=std::make_shared<IgbFieldNameTable>();globals=std::make_shared<IgbFieldGlobalTable>();
    constexpr std::array labels={"_initCount","_maxCount","_isActive","_sema"};
    for(std::size_t i=0;i<labels.size();++i){names->push_back(std::make_shared<const std::string>(labels[i]));
        globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x805620a4u+static_cast<std::uint32_t>(4*i)});}
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x0cu,0x10u,0x14u,0x18u});
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_platform_semaphore_metadata_slot(
    const std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>& services,Lookup mode){
    validate(mode);if(!services)return {};
    if(mode==Lookup::plain_parent)return services->dependencies?services->dependencies->semaphore:nullptr;
    return services->state?std::shared_ptr<IgbRegistrationMetaSlot>(services->state,&services->state->metadata):nullptr;
}
IgbTypeRegistrationArguments igb_platform_semaphore_registration_arguments(const std::shared_ptr<IgbPlatformSemaphoreRegistrationState>& state){
    if(!state)fail("Platform-semaphore registration requires canonical805620A0 storage.");
    IgbTypeRegistrationArguments args;args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(state,&state->metadata);
    args.name=std::make_shared<const std::string>("igGamecubeSemaphore");args.flag=0;args.native_size=0x24;
    args.parent_registration=0x800260c0;args.parent_getter=0x8003b3b4;args.field_callback=0x80026160;
    args.layout_probe=0x8003b274;args.defaults_callback=0x8003b3bc;return args;
}
struct IgbPlatformSemaphoreRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbPlatformSemaphoreRegistrationInvocation::IgbPlatformSemaphoreRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbPlatformSemaphoreRegistrationInvocation::~IgbPlatformSemaphoreRegistrationInvocation()=default;
bool IgbPlatformSemaphoreRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformSemaphoreRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbPlatformSemaphoreRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete platform-semaphore registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbPlatformSemaphoreRegistrationInvocation::resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-semaphore registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Platform-semaphore registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->state)return pending("Platform-semaphore registration requires canonical805620A0 after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_platform_semaphore_registration_arguments(services->state));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformSemaphoreMetadataInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;Lookup mode{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;bool running{},fault{};
};
IgbPlatformSemaphoreMetadataInvocation::IgbPlatformSemaphoreMetadataInvocation(Lookup mode):impl_(std::make_unique<Impl>()){validate(mode);impl_->mode=mode;}
IgbPlatformSemaphoreMetadataInvocation::~IgbPlatformSemaphoreMetadataInvocation()=default;
bool IgbPlatformSemaphoreMetadataInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformSemaphoreMetadataInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbPlatformSemaphoreMetadataInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete platform-semaphore metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbPlatformSemaphoreMetadataInvocation::resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-semaphore metadata lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("Platform-semaphore metadata lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_platform_semaphore_metadata_slot(services,s.mode);if(!s.slot)return pending("Platform-semaphore metadata lookup requires its canonical global cell.");}
        if(!*s.slot)return pending("Platform-semaphore metadata lookup requires represented canonical state.");
        if(s.mode==Lookup::plain_parent || (**s.slot && ((**s.slot)->class_flags&4u)!=0))s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8003b344);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Platform-semaphore metadata lookup requires its reloaded canonical pointer.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformSemaphoreProbeInvocation::Impl {
    enum class P{reset,named,core,base,name,derived,offset,word,restore_derived,restore_base,restore_named,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::uint32_t offset{},value{};bool running{},fault{};
};
IgbPlatformSemaphoreProbeInvocation::IgbPlatformSemaphoreProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbPlatformSemaphoreProbeInvocation::~IgbPlatformSemaphoreProbeInvocation()=default;
bool IgbPlatformSemaphoreProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformSemaphoreProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbPlatformSemaphoreProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete platform-semaphore probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbPlatformSemaphoreProbeInvocation::resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Platform-semaphore probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Platform-semaphore probe was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::named;break;
    case P::named:igb_bootstrap_write_word(s.local,0,0x8047650c);s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Platform-semaphore probe requires ArkCore80562100 after its named-object vtable store.");
        s.core=*services->types->runtime->singleton;s.phase=P::base;break;
    case P::base:igb_bootstrap_write_word(s.local,0,0x80473480);s.phase=P::name;break;
    case P::name:igb_bootstrap_write_word(s.local,8,0);s.phase=P::derived;break;
    case P::derived:igb_bootstrap_write_word(s.local,0,0x80470314);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Platform-semaphore probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Platform-semaphore probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>32 || (s.offset&3u)!=0)fail("Platform-semaphore probe exceeds its aligned36-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::restore_derived;break;
    case P::restore_derived:igb_bootstrap_write_word(s.local,0,0x80470314);s.phase=P::restore_base;break;
    case P::restore_base:igb_bootstrap_write_word(s.local,0,0x80473480);s.phase=P::restore_named;break;
    case P::restore_named:
        igb_bootstrap_write_word(s.local,0,0x8047650c);
        // The original cleanup tests retained literal r7=0, not a fresh name
        // load. Its string release branch is therefore unreachable here.
        s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbPlatformSemaphoreDefaultsInvocation::Impl {
    enum class P{capture,batch,field,source,table,scalar_call,size,schema_setup,schema,callback_owner,callback_store,done};P phase=P::capture;
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,callback_owner;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> binding;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    IgbFieldOperationCall request;IgbFieldOperationsFactory factory;
    std::shared_ptr<IgbFieldOperation> operation;std::function<MessengerCallResult()> callback;
    std::uint32_t first{},local{};bool running{},fault{};
};
IgbPlatformSemaphoreDefaultsInvocation::IgbPlatformSemaphoreDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbPlatformSemaphoreDefaultsInvocation::~IgbPlatformSemaphoreDefaultsInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.owner){
        if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.callback_owner)s.owner->retained.push_back(s.callback_owner);
        if(s.field)s.owner->retained.push_back(s.field);if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
        if(s.request.source && s.request.source->buffer)s.owner->retained.push_back(s.request.source->buffer);
    }
}
bool IgbPlatformSemaphoreDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPlatformSemaphoreDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbPlatformSemaphoreDefaultsInvocation::resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Platform-semaphore defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Platform-semaphore defaults were invalidated by a callback.");
        if(s.binding){const auto current=physical_field(s.field);
            if(!current || current->storage!=s.binding->storage || current->offset!=s.binding->offset)
                fail("Platform-semaphore defaults changed their captured descriptor allocation.");}};
    const auto pointer=[&]{const auto value=physical_field(s.field);if(value && !s.binding)s.binding=value;check();return value;};
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    const auto field_factory=[&]{return services && services->schema && services->schema->fields?
        services->schema->fields->factory:IgbFieldOperationsFactory{};};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        const auto cell=igb_platform_semaphore_metadata_slot(services);
        if(!cell || !*cell)return pending("Platform-semaphore defaults require represented canonical805620A0 metadata.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,4u);s.phase=P::batch;break;
    }
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;}
    case P::field:
        s.field=field_at(s.metadata,s.first+(s.local<2?s.local:3u));s.binding.reset();
        s.phase=s.local<2?P::source:P::size;break;
    case P::source:
        // Actual75AC4 stores its value on the stack before loading the fresh
        // descriptor vtable. Keep the same four bytes across a pending call.
        s.request={};s.request.source=one_word();s.phase=P::table;break;
    case P::table:{
        const auto physical=pointer();const auto table=physical?std::optional<std::uint32_t>{igb_bootstrap_read_word(*physical,0)}:s.field->vtable_word;
        if(!table)return pending("Platform-semaphore scalar default requires its fresh descriptor vtable.");
        if(!*table)fail("Platform-semaphore scalar default dereferences a null vtable.");
        s.request.kind=IgbFieldOperationKind::field_virtual;s.request.field=s.field;s.request.vtable_word=*table;s.request.virtual_slot=0x8c;
        s.factory=field_factory();s.phase=P::scalar_call;break;
    }
    case P::scalar_call:
        if(!s.factory)s.factory=field_factory();
        if(!s.factory)return pending("Platform-semaphore defaults require the reached field-value factory.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Platform-semaphore defaults reached an unbound field virtual+8C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Platform-semaphore defaults require their retained scalar continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        clear();s.binding.reset();s.field.reset();++s.local;s.phase=P::field;break;
    case P::size:
        if(const auto physical=pointer())igb_bootstrap_write_halfword(*physical,0x14,0x0c);
        s.field->size=0x0c;s.binding.reset();s.field.reset();s.phase=P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Platform-semaphore defaults require their schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);
        s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::callback_owner;break;}
    case P::callback_owner:{
        const auto cell=igb_platform_semaphore_metadata_slot(services);
        if(!cell || !*cell)return pending("Platform-semaphore defaults require freshly reloaded canonical805620A0 after schema.");
        s.callback_owner=**cell;s.phase=P::callback_store;break;
    }
    case P::callback_store:
        if(!s.callback_owner)fail("Platform-semaphore defaults store through null reloaded metadata at+40.");
        if(const auto physical=physical_metadata(s.callback_owner,services))igb_bootstrap_write_word(*physical,0x40,0x8002612c);
        s.callback_owner->word_40=0x8002612c;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_platform_semaphore_registration_function_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x8003b344 && call.function!=0x8003b31c))
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-semaphore registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8003b344){auto invocation=std::make_shared<IgbPlatformSemaphoreRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8003b344);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_platform_semaphore_registration_type_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x8003b344;
        const bool getter=call.kind==K::get_parent && (call.function==0x8003b238 || call.function==0x8003b3b4);
        const bool probe=call.kind==K::layout_probe && call.function==0x8003b274;
        const bool defaults=call.kind==K::defaults && call.function==0x8003b3bc;
        if(!registration && !getter && !probe && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-semaphore registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbPlatformSemaphoreRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(getter){auto invocation=std::make_shared<IgbPlatformSemaphoreMetadataInvocation>(call.function==0x8003b3b4?Lookup::plain_parent:Lookup::fully_registered);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else if(defaults){auto invocation=std::make_shared<IgbPlatformSemaphoreDefaultsInvocation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};}
        else operation->resume=[selected,invocation=std::shared_ptr<IgbPlatformSemaphoreProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Platform-semaphore probe requires its represented36-byte stack local.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Platform-semaphore probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbPlatformSemaphoreProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
IgbFileFactoryFactory make_native_igb_platform_semaphore_redirect_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services,IgbFileFactoryFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFileFactoryCall& call)->std::shared_ptr<IgbFileFactoryOperation>{
        if(call.kind!=IgbFileFactoryServiceKind::redirect_type || call.redirect!=0x80026160)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Platform-semaphore redirect services expired before selection.");
        auto invocation=std::make_shared<IgbPlatformSemaphoreMetadataInvocation>();auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
            if(status.completed)out->metadata_result=invocation->result();return status;};return operation;
    };
}
} // namespace dw4
