#include "dw4/igb_gamecube_thread_registration.hpp"

#include <array>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbGamecubeThreadRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
using Lookup=IgbGamecubeThreadLookup;
void validate(Lookup mode){if(mode!=Lookup::fully_registered && mode!=Lookup::plain_parent)fail("Unknown GameCube-thread metadata lookup mode.");}
IgbRuntimeFieldDefaultSource one_byte(){
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes={1};buffer->initialized={1};return {buffer,0};
}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t position){
    if(!metadata || !metadata->fields)fail("GameCube-thread defaults select through a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(position<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("GameCube-thread defaults reach an unavailable physical field slot.");return (*backing)[slot];
}
std::optional<IgbBootstrapPointer> physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("GameCube-thread defaults dereference a null descriptor.");
    if(field->construction_fault_)fail("GameCube-thread defaults reach a faulted descriptor.");
    if(!field->physical_storage)return {};
    IgbBootstrapPointer pointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
    if(!pointer.storage || pointer.storage->faulted() ||
        !pointer.storage->bytes || pointer.storage->bytes->released || igb_bootstrap_field(pointer)!=field)
        fail("GameCube-thread defaults lost their descriptor's physical identity.");return pointer;
}
std::optional<IgbBootstrapPointer> physical_metadata(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbGamecubeThreadRegistrationServices>& services){
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata){
        if(igb_bootstrap_metadata(pointer)!=metadata)continue;
        if(pointer.storage->faulted())fail("GameCube-thread defaults reached faulted metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("GameCube-thread metadata has conflicting physical identities.");
        result=pointer;
    }return result;
}
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const IgbBootstrapPointer& allocation,const std::shared_ptr<IgbGamecubeThreadRegistrationServices>& services){
    if(!metadata){if(allocation.storage || allocation.offset)fail("Null thread dependency metadata has a nonnull allocation.");return 0;}
    std::optional<std::uint32_t> result;
    const auto accept=[&](std::uint32_t address){
        if(!address || (result && *result!=address))fail("Thread dependency metadata has conflicting arithmetic identities.");result=address;
    };
    if(allocation.storage || allocation.offset){
        if(igb_bootstrap_metadata(allocation)!=metadata)fail("Thread dependency allocation disagrees with its returned owner.");
        accept(igb_bootstrap_address(allocation));
    }
    if(const auto pointer=physical_metadata(metadata,services))accept(igb_bootstrap_address(*pointer));
    const auto pointers=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    if(pointers)for(const auto& binding:pointers->values)if(binding.metadata==metadata){
        if(binding.field)fail("Thread dependency metadata address aliases a descriptor binding.");accept(binding.address);
    }
    return result;
}
}
IgbGamecubeThreadRegistrationServices::IgbGamecubeThreadRegistrationServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{
        0x8002450cu,0x800274ecu,0x8002450cu,0x800274ecu,0x8002879cu,
        0x800385e0u,0x800385e0u,0x800385e0u,0x800385e0u});
    names=std::make_shared<IgbFieldNameTable>();globals=std::make_shared<IgbFieldGlobalTable>();
    constexpr std::array labels={"_thread","_master","_alarm","_result","_threadDoneSema",
        "_isDeferred","_stopRequested","_atStoppingPoint","_isActive"};
    for(std::size_t i=0;i<labels.size();++i){names->push_back(std::make_shared<const std::string>(labels[i]));
        globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80562078u+static_cast<std::uint32_t>(4*i)});}
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x38u,0x350u,0x358u,0x30u,0x380u,0x34u,0x35u,0x36u,0x37u});
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_gamecube_thread_metadata_slot(
    const std::shared_ptr<IgbGamecubeThreadRegistrationServices>& services,Lookup mode){
    validate(mode);if(!services)return {};
    if(mode==Lookup::plain_parent)return services->parent?services->parent->thread_metadata:nullptr;
    return services->state?std::shared_ptr<IgbRegistrationMetaSlot>(services->state,&services->state->metadata):nullptr;
}
IgbTypeRegistrationArguments igb_gamecube_thread_registration_arguments(const std::shared_ptr<IgbGamecubeThreadRegistrationState>& state){
    if(!state)fail("GameCube-thread registration requires canonical80562074 storage.");
    IgbTypeRegistrationArguments args;args.metadata_slot=std::shared_ptr<IgbRegistrationMetaSlot>(state,&state->metadata);
    args.name=std::make_shared<const std::string>("igGamecubeThread");args.flag=0;args.native_size=0x388;
    args.parent_registration=0x80023c68;args.parent_getter=0x8003b154;args.field_callback=0x80023e20;
    args.layout_probe=0x8003afc0;args.defaults_callback=0x8003b15c;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x80026098,0});
    return args;
}
struct IgbGamecubeThreadRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;
    std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbGamecubeThreadRegistrationInvocation::IgbGamecubeThreadRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbGamecubeThreadRegistrationInvocation::~IgbGamecubeThreadRegistrationInvocation()=default;
bool IgbGamecubeThreadRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbGamecubeThreadRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete GameCube-thread registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbGamecubeThreadRegistrationInvocation::resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("GameCube-thread registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("GameCube-thread registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->state)return pending("GameCube-thread registration requires canonical80562074 after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_gamecube_thread_registration_arguments(services->state));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbGamecubeThreadMetadataInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;Lookup mode{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;bool running{},fault{};
};
IgbGamecubeThreadMetadataInvocation::IgbGamecubeThreadMetadataInvocation(Lookup mode):impl_(std::make_unique<Impl>()){validate(mode);impl_->mode=mode;}
IgbGamecubeThreadMetadataInvocation::~IgbGamecubeThreadMetadataInvocation()=default;
bool IgbGamecubeThreadMetadataInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadMetadataInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbGamecubeThreadMetadataInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete GameCube-thread metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbGamecubeThreadMetadataInvocation::resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("GameCube-thread metadata lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);const auto check=[&]{if(s.fault)fail("GameCube-thread metadata lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_gamecube_thread_metadata_slot(services,s.mode);if(!s.slot)return pending("GameCube-thread metadata lookup requires its canonical global cell.");}
        if(!*s.slot)return pending("GameCube-thread metadata lookup requires represented canonical state.");
        if(s.mode==Lookup::plain_parent || (**s.slot && ((**s.slot)->class_flags&4u)!=0))s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x8003b0e0);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("GameCube-thread metadata lookup requires its reloaded canonical pointer.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbGamecubeThreadProbeInvocation::Impl {
    enum class P{reset,core,named,base,name,derived,semaphore,offset,word,restore_derived,restore_base,restore_named,
        cleanup_name,resolve_name,release_name,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<IgbInternedString> string;
    IgbDependenciesCall request;IgbDependenciesFactory factory;
    std::shared_ptr<IgbDependenciesOperation> operation;std::function<MessengerCallResult()> callback;
    std::uint32_t allocation_address{},offset{},value{},name_address{};bool running{},fault{};
};
IgbGamecubeThreadProbeInvocation::IgbGamecubeThreadProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){
    auto& s=*impl_;s.local=std::move(local);if(s.local.storage){s.bytes=s.local.storage->bytes;s.allocation_address=s.local.storage->original_address;}
}
IgbGamecubeThreadProbeInvocation::~IgbGamecubeThreadProbeInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.local.storage)s.owner->retained.push_back(s.local.storage);if(s.bytes)s.owner->retained.push_back(s.bytes);
    if(s.string)s.owner->retained.push_back(s.string);if(s.operation)s.owner->retained.push_back(s.operation);
}}
bool IgbGamecubeThreadProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbGamecubeThreadProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete GameCube-thread probe has no physical word result.");return impl_->value;
}
MessengerCallResult IgbGamecubeThreadProbeInvocation::resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("GameCube-thread probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("GameCube-thread probe was invalidated by a callback.");
        if(!s.local.storage || s.local.storage->faulted() || !s.local.storage->bytes || s.local.storage->bytes->released ||
            s.local.storage->bytes!=s.bytes || s.local.storage->original_address!=s.allocation_address)
            fail("GameCube-thread probe lost its captured local allocation identity.");
    };
    const auto fallback=[&]{return services && services->schema && services->schema->dependencies?
        services->schema->dependencies->factory:IgbDependenciesFactory{};};
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("GameCube-thread probe requires ArkCore80562100 before its named-object vtable store.");
        s.core=*services->types->runtime->singleton;s.phase=P::named;break;
    case P::named:igb_bootstrap_write_word(s.local,0,0x8047650c);s.phase=P::base;break;
    case P::base:igb_bootstrap_write_word(s.local,0,0x80470c94);s.phase=P::name;break;
    case P::name:igb_bootstrap_write_word(s.local,8,0);s.phase=P::derived;break;
    case P::derived:igb_bootstrap_write_word(s.local,0,0x80470110);s.phase=P::semaphore;break;
    case P::semaphore:igb_bootstrap_write_word(s.local,0x380,0);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("GameCube-thread probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("GameCube-thread probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>0x384 || (s.offset&3u)!=0)fail("GameCube-thread probe exceeds its aligned0x388-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::restore_derived;break;
    case P::restore_derived:
        igb_bootstrap_write_word(s.local,0,0x80470110);
        // Actual cleanup tests retained r6=0. It never reads or releases a
        // replacement physical semaphore pointer written while suspended.
        s.phase=P::restore_base;break;
    case P::restore_base:igb_bootstrap_write_word(s.local,0,0x80470c94);s.phase=P::restore_named;break;
    case P::restore_named:igb_bootstrap_write_word(s.local,0,0x8047650c);s.phase=P::cleanup_name;break;
    case P::cleanup_name:
        if(igb_bootstrap_address(s.local)+8u==0){s.phase=P::done;break;}
        s.name_address=igb_bootstrap_read_word(s.local,8);s.phase=s.name_address?P::resolve_name:P::done;break;
    case P::resolve_name:{
        if(!services)return pending("GameCube-thread probe requires its captured name's canonical header.");
        const auto found=services->strings.find(s.name_address);
        if(found==services->strings.end())return pending("GameCube-thread probe requires its captured name's canonical header.");
        s.string=found->second;
        if(!s.string || s.string->released)fail("GameCube-thread probe reads a null or released captured name header.");
        --s.string->references;
        if(s.string->references!=0){s.phase=P::done;break;}
        s.request={};s.request.kind=IgbDependenciesServiceKind::release_string;s.request.string=s.string;s.request.pool=s.string->container;
        s.factory=fallback();s.phase=P::release_name;break;
    }
    case P::release_name:
        if(!s.factory)s.factory=fallback();
        if(!s.factory)return pending("GameCube-thread probe requires reached string release8007381C.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("GameCube-thread probe reached an unbound string release8007381C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("GameCube-thread probe requires its retained name-release continuation.");
        {const auto result=s.callback();check();if(!result.completed)return result;}
        s.callback={};s.operation.reset();s.factory={};s.request={};check();s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbGamecubeThreadDefaultsInvocation::Impl {
    enum class P{capture,batch,thread_field,thread_size,alarm_field,alarm_size,semaphore_field,lookup,type_store,construct_store,
        boolean_field,source,table,scalar_call,schema_setup,schema,callback_owner,callback_store,done};P phase=P::capture;
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,related,callback_owner;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> binding;std::shared_ptr<IgbScratchBuffer> field_bytes;
    std::uint32_t field_address{};IgbBootstrapPointer allocation;
    std::unique_ptr<IgbFieldBatchInvocation> batch;std::unique_ptr<IgbThreadDependencyLookupInvocation> lookup;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    IgbFieldOperationCall request;IgbFieldOperationsFactory factory;
    std::shared_ptr<IgbFieldOperation> operation;std::function<MessengerCallResult()> callback;
    std::uint32_t first{};bool running{},fault{};
};
IgbGamecubeThreadDefaultsInvocation::IgbGamecubeThreadDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbGamecubeThreadDefaultsInvocation::~IgbGamecubeThreadDefaultsInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.owner){
        if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.callback_owner)s.owner->retained.push_back(s.callback_owner);
        if(s.related)s.owner->retained.push_back(s.related);if(s.allocation.storage)s.owner->retained.push_back(s.allocation.storage);
        if(s.field)s.owner->retained.push_back(s.field);if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
        if(s.field_bytes)s.owner->retained.push_back(s.field_bytes);
        if(s.request.source && s.request.source->buffer)s.owner->retained.push_back(s.request.source->buffer);
    }
}
bool IgbGamecubeThreadDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbGamecubeThreadDefaultsInvocation::resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("GameCube-thread defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("GameCube-thread defaults were invalidated by a callback.");};
    const auto select=[&](std::uint32_t local){
        s.field=field_at(s.metadata,s.first+local);s.binding.reset();s.field_bytes.reset();
        // Capture represented identity without dereferencing the descriptor:
        // original3B15C selects field4 before calling lazy26020.
        if(s.field && s.field->physical_storage){const auto& physical=*s.field->physical_storage;
            s.binding=IgbBootstrapPointer{physical.storage.lock(),physical.offset};
            if(s.binding->storage){s.field_bytes=s.binding->storage->bytes;s.field_address=s.binding->storage->original_address;}
        }
    };
    const auto pointer=[&]{
        const auto value=physical_field(s.field);
        if(s.binding){
            if(!value || value->storage!=s.binding->storage || value->offset!=s.binding->offset ||
                value->storage->bytes!=s.field_bytes || value->storage->original_address!=s.field_address)
                fail("GameCube-thread defaults changed their captured descriptor allocation.");
        }else if(value){s.binding=value;s.field_bytes=value->storage->bytes;s.field_address=value->storage->original_address;}
        check();return value;
    };
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    const auto release_field=[&]{s.binding.reset();s.field_bytes.reset();s.field.reset();};
    const auto field_factory=[&]{return services && services->schema && services->schema->fields?
        services->schema->fields->factory:IgbFieldOperationsFactory{};};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        const auto cell=igb_gamecube_thread_metadata_slot(services);
        if(!cell || !*cell)return pending("GameCube-thread defaults require represented canonical80562074 metadata.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,9u);s.phase=P::batch;break;
    }
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::thread_field;break;}
    case P::thread_field:select(0);s.phase=P::thread_size;break;
    case P::thread_size:
        if(const auto physical=pointer())igb_bootstrap_write_halfword(*physical,0x14,0x318);
        s.field->size=0x318;release_field();s.phase=P::alarm_field;break;
    case P::alarm_field:select(2);s.phase=P::alarm_size;break;
    case P::alarm_size:
        if(const auto physical=pointer())igb_bootstrap_write_halfword(*physical,0x14,0x28);
        s.field->size=0x28;release_field();s.phase=P::semaphore_field;break;
    case P::semaphore_field:
        select(4);s.lookup=std::make_unique<IgbThreadDependencyLookupInvocation>(IgbThreadDependencyEntry::semaphore);s.phase=P::lookup;break;
    case P::lookup:{const auto status=s.lookup->resume(services?services->dependencies:nullptr);check();if(!status.completed)return status;
        s.related=s.lookup->result();s.allocation=s.lookup->allocation();s.lookup.reset();check();s.phase=P::type_store;break;}
    case P::type_store:
        if(const auto physical=pointer()){
            const auto address=metadata_address(s.related,s.allocation,services);
            if(!address)return pending("GameCube-thread type store requires its captured semaphore metadata arithmetic address.");
            igb_bootstrap_write_word(*physical,0x38,*address);
        }
        s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.related);s.phase=P::construct_store;break;
    case P::construct_store:
        if(const auto physical=pointer())igb_bootstrap_write_byte(*physical,0x34,1);
        s.field->flag_34=std::uint8_t{1};s.related.reset();s.allocation={};release_field();s.phase=P::boolean_field;break;
    case P::boolean_field:select(5);s.phase=P::source;break;
    case P::source:
        //3EC68 writes one stack byte before its fresh descriptor-table load.
        s.request={};s.request.source=one_byte();s.phase=P::table;break;
    case P::table:{
        const auto physical=pointer();const auto table=physical?std::optional<std::uint32_t>{igb_bootstrap_read_word(*physical,0)}:s.field->vtable_word;
        if(!table)return pending("GameCube-thread boolean default requires its fresh descriptor vtable.");
        if(!*table)fail("GameCube-thread boolean default dereferences a null vtable.");
        s.request.kind=IgbFieldOperationKind::field_virtual;s.request.field=s.field;s.request.vtable_word=*table;s.request.virtual_slot=0x8c;
        s.factory=field_factory();s.phase=P::scalar_call;break;
    }
    case P::scalar_call:
        pointer();
        if(!s.factory)s.factory=field_factory();
        if(!s.factory)return pending("GameCube-thread defaults require the reached boolean-value factory.");
        if(!s.operation){s.operation=s.factory(s.request);check();pointer();}
        if(!s.operation)return pending("GameCube-thread defaults reached an unbound boolean virtual+8C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("GameCube-thread defaults require their retained boolean continuation.");
        {const auto status=s.callback();check();pointer();if(!status.completed)return status;}
        clear();release_field();s.phase=P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("GameCube-thread defaults require their schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);
        s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::callback_owner;break;}
    case P::callback_owner:{
        const auto cell=igb_gamecube_thread_metadata_slot(services);
        if(!cell || !*cell)return pending("GameCube-thread defaults require freshly reloaded canonical80562074 after schema.");
        s.callback_owner=**cell;s.phase=P::callback_store;break;
    }
    case P::callback_store:
        if(!s.callback_owner)fail("GameCube-thread defaults store through null reloaded metadata at+40.");
        if(const auto physical=physical_metadata(s.callback_owner,services))igb_bootstrap_write_word(*physical,0x40,0x80023cd4);
        s.callback_owner->word_40=0x80023cd4;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_gamecube_thread_registration_function_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x8003b0e0 && call.function!=0x8003b0b8))
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("GameCube-thread registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x8003b0e0){auto invocation=std::make_shared<IgbGamecubeThreadRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x8003b0e0);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_gamecube_thread_registration_type_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x8003b0e0;
        const bool getter=call.kind==K::get_parent && (call.function==0x8003af84 || call.function==0x8003b154);
        const bool probe=call.kind==K::layout_probe && call.function==0x8003afc0;
        const bool defaults=call.kind==K::defaults && call.function==0x8003b15c;
        if(!registration && !getter && !probe && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("GameCube-thread registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbGamecubeThreadRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(getter){auto invocation=std::make_shared<IgbGamecubeThreadMetadataInvocation>(call.function==0x8003b154?Lookup::plain_parent:Lookup::fully_registered);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else if(defaults){auto invocation=std::make_shared<IgbGamecubeThreadDefaultsInvocation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};}
        else operation->resume=[selected,invocation=std::shared_ptr<IgbGamecubeThreadProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("GameCube-thread probe requires its represented0x388-byte stack local.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("GameCube-thread probe has null arithmetic stack storage.");
                invocation=std::make_shared<IgbGamecubeThreadProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
IgbFileFactoryFactory make_native_igb_gamecube_thread_redirect_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> services,IgbFileFactoryFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFileFactoryCall& call)->std::shared_ptr<IgbFileFactoryOperation>{
        if(call.kind!=IgbFileFactoryServiceKind::redirect_type || call.redirect!=0x80023e20)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("GameCube-thread redirect services expired before selection.");
        auto invocation=std::make_shared<IgbGamecubeThreadMetadataInvocation>();auto operation=std::make_shared<IgbFileFactoryOperation>();
        operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
            if(status.completed)out->metadata_result=invocation->result();return status;};return operation;
    };
}
} // namespace dw4
