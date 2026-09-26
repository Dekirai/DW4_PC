#include "dw4/igb_context_info_registration.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbContextInfoError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
void check_fault(bool fault){if(fault)fail("Context-info continuation was invalidated by a callback.");}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t index){
    if(!metadata || !metadata->fields)fail("Context-info defaults read a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(index<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("Context-info defaults read an unavailable physical field slot.");return (*backing)[slot];
}
struct FieldAccess {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> physical;
    std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t address{};
    explicit FieldAccess(std::shared_ptr<IgbDecodeRuntimeField> value):field(std::move(value)){
        if(field && field->physical_storage){physical=IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
            if(physical->storage){bytes=physical->storage->bytes;address=physical->storage->original_address;}}
    }
    std::optional<IgbBootstrapPointer> pointer()const{
        if(!field)fail("Context-info defaults store through a null descriptor.");
        if(field->construction_fault_)fail("Context-info defaults reach a faulted descriptor.");
        if(!physical){if(field->physical_storage)fail("Context-info descriptor acquired another physical binding while captured.");return {};}
        if(!physical->storage || !field->physical_storage || physical->storage->faulted() ||
            field->physical_storage->storage.lock()!=physical->storage || field->physical_storage->offset!=physical->offset ||
            physical->storage->original_address!=address || physical->storage->bytes!=bytes || !bytes || bytes->released ||
            igb_bootstrap_field(*physical)!=field)fail("Context-info descriptor lost its captured physical identity.");
        return physical;
    }
};
std::shared_ptr<IgbPointerListServices> pointers(const std::shared_ptr<IgbContextInfoServices>& services){
    return services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
}
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbContextInfoServices>& services){
    if(!metadata)return 0;std::optional<std::uint32_t> result;
    const auto registry=pointers(services);if(!registry)return {};
    for(const auto& binding:registry->values)if(binding.metadata==metadata){
        if(binding.field || !binding.address || (result && *result!=binding.address))fail("Context-info metadata has conflicting arithmetic identities.");
        result=binding.address;
    }
    if(result)for(const auto& binding:registry->values)if(binding.address==*result &&
        (binding.field || (binding.metadata && binding.metadata!=metadata)))fail("Context-info metadata pointer conflicts with another canonical identity.");
    return result;
}
std::optional<std::uint32_t> field_address(const std::shared_ptr<IgbDecodeRuntimeField>& field,
    const std::shared_ptr<IgbContextInfoServices>& services){
    if(!field)return 0;std::optional<std::uint32_t> result;
    if(field->physical_storage){const IgbBootstrapPointer physical{field->physical_storage->storage.lock(),field->physical_storage->offset};
        if(!physical.storage || igb_bootstrap_field(physical)!=field)fail("Context-info prototype lost its physical arithmetic identity.");
        // This is a pointer publication, not a read of prototype allocation bytes.
        result=igb_bootstrap_address(physical);if(!*result)fail("Context-info prototype has a null physical arithmetic identity.");}
    const auto registry=pointers(services);if(registry){
        for(const auto& binding:registry->values)if(binding.field==field){
            if(binding.metadata || !binding.address || (result && *result!=binding.address))fail("Context-info prototype has conflicting arithmetic identities.");
            result=binding.address;
        }
        if(result)for(const auto& binding:registry->values)if(binding.address==*result &&
            (binding.metadata || (binding.field && binding.field!=field)))fail("Context-info prototype pointer conflicts with another canonical identity.");
    }return result;
}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.file==b.file && a.field==b.field && a.metadata==b.metadata && a.fields==b.fields && a.types==b.types && a.runtime_object==b.runtime_object;}
std::optional<IgbObjectInstance> cleanup_object(const std::shared_ptr<IgbContextInfoServices>& services,std::uint32_t address){
    std::optional<IgbObjectInstance> result;
    if(services && services->object_cleanup)for(const auto& object:services->object_cleanup->objects){
        if(igb_bootstrap_address({object.storage,object.offset})!=address)continue;
        if(!object.storage || object.storage->faulted())fail("Context-info probe cleanup requires an unfaulted object allocation.");
        igb_object_validate_instance(object);
        if(result && !same(*result,object))fail("Context-info probe cleanup pointer has conflicting native identities.");result=object;
    }return result;
}
}
IgbContextInfoServices::IgbContextInfoServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002879cu,0x800251ecu,0x8002ac34u});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_pMeta"),
        std::make_shared<const std::string>("_pName"),std::make_shared<const std::string>("_pVendorName")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561dec},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561df0},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561df4}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u,12u,16u});
}
IgbTypeRegistrationArguments igb_context_info_type_registration_arguments(const std::shared_ptr<IgbContextInfoServices>& services){
    if(!services || !services->metadata)fail("Context-info registration requires canonical metadata storage80561DE8.");
    IgbTypeRegistrationArguments args;args.metadata_slot=services->metadata;
    args.name=std::make_shared<const std::string>("igContextExtInfo");args.flag=0;args.native_size=0x14;
    args.parent_registration=0x80066b08;args.parent_getter=0x800237d0;args.field_callback=0x800373cc;
    args.layout_probe=0x80037268;args.defaults_callback=0x800373ec;
    args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x8002a0a8,0});return args;
}
struct IgbContextInfoRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;
    IgbRegistrationPrerequisitesInvocation prerequisites;std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbContextInfoRegistrationInvocation::IgbContextInfoRegistrationInvocation():impl_(std::make_unique<Impl>()){}
IgbContextInfoRegistrationInvocation::~IgbContextInfoRegistrationInvocation()=default;
bool IgbContextInfoRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextInfoRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbContextInfoRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete context-info registration has no callback result.");return impl_->result;
}
MessengerCallResult IgbContextInfoRegistrationInvocation::resume(std::shared_ptr<IgbContextInfoServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context-info registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!services || !services->metadata)return pending("Context-info registration requires canonical metadata after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_context_info_type_registration_arguments(services));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbContextInfoLookupInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;IgbContextInfoLookup mode{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::shared_ptr<IgbRuntimeMetaType> result;bool running{},fault{};
};
IgbContextInfoLookupInvocation::IgbContextInfoLookupInvocation(IgbContextInfoLookup mode):impl_(std::make_unique<Impl>()){
    if(mode!=IgbContextInfoLookup::fully_registered && mode!=IgbContextInfoLookup::plain)fail("Unknown context-info metadata lookup mode.");impl_->mode=mode;
}
IgbContextInfoLookupInvocation::~IgbContextInfoLookupInvocation()=default;
bool IgbContextInfoLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextInfoLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbContextInfoLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete context-info lookup has no metadata result.");return impl_->result;
}
MessengerCallResult IgbContextInfoLookupInvocation::resume(std::shared_ptr<IgbContextInfoServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context-info lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot && services)s.slot=services->metadata;
        if(!s.slot || !*s.slot)return pending("Context-info lookup requires represented metadata80561DE8.");
        if(s.mode==IgbContextInfoLookup::plain){s.result=**s.slot;s.phase=P::done;break;}
        if(const auto metadata=**s.slot;metadata && (metadata->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x80037358);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Context-info lookup requires its fresh metadata reload.");s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbSignedBytePrototypeInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;
    std::shared_ptr<IgbRuntimeFieldSlot> slot;std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    std::shared_ptr<IgbDecodeRuntimeField> result;bool running{},fault{};
};
IgbSignedBytePrototypeInvocation::IgbSignedBytePrototypeInvocation():impl_(std::make_unique<Impl>()){}
IgbSignedBytePrototypeInvocation::~IgbSignedBytePrototypeInvocation()=default;
bool IgbSignedBytePrototypeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbSignedBytePrototypeInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbSignedBytePrototypeInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete signed-byte prototype getter has no result.");return impl_->result;
}
MessengerCallResult IgbSignedBytePrototypeInvocation::resume(std::shared_ptr<IgbContextInfoServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Signed-byte prototype getter requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot && services && services->values)s.slot=std::shared_ptr<IgbRuntimeFieldSlot>(services->values,&services->values->signed_byte_prototype);
        if(!s.slot || !*s.slot)return pending("Signed-byte prototype getter requires represented80561E20.");
        if(**s.slot)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(0x80037d28);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Signed-byte prototype getter requires its fresh80561E20 reload.");s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbContextInfoProbeInvocation::Impl {
    enum class P{reset,core,vtable,object_null,string_null,offset,word,restore,member,resolve,decrement,test,destroy,done};P phase=P::reset;
    IgbBootstrapPointer local;std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t allocation_address{},offset{},value{},address{};
    std::shared_ptr<IgbContextInfoServices> owner;std::shared_ptr<IgbRuntimeTypeRegistry> core;IgbObjectInstance object;
    std::shared_ptr<IgbScratchBuffer> object_bytes;std::uint32_t object_allocation_address{};
    IgbObjectFieldConstructCall request;IgbObjectFieldConstructFactory factory;
    std::shared_ptr<IgbObjectFieldConstructOperation> operation;std::function<MessengerCallResult()> callback;
    bool running{},fault{};
};
IgbContextInfoProbeInvocation::IgbContextInfoProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){
    auto& s=*impl_;s.local=std::move(local);if(s.local.storage){s.bytes=s.local.storage->bytes;s.allocation_address=s.local.storage->original_address;}
}
IgbContextInfoProbeInvocation::~IgbContextInfoProbeInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.local.storage)s.owner->retained.push_back(s.local.storage);if(s.bytes)s.owner->retained.push_back(s.bytes);
    if(s.object.storage)s.owner->retained.push_back(s.object.storage);if(s.operation)s.owner->retained.push_back(s.operation);
    if(s.object_bytes)s.owner->retained.push_back(s.object_bytes);
}}
bool IgbContextInfoProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextInfoProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbContextInfoProbeInvocation::value()const{if(!completed() || faulted())fail("Incomplete context-info probe has no captured result.");return impl_->value;}
MessengerCallResult IgbContextInfoProbeInvocation::resume(std::shared_ptr<IgbContextInfoServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context-info probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);
        if(!s.local.storage || s.local.storage->faulted() || !s.bytes || s.bytes->released || s.local.storage->bytes!=s.bytes ||
            s.local.storage->original_address!=s.allocation_address)fail("Context-info probe lost its captured local allocation identity.");};
    const auto fallback=[&]{return services && services->object_cleanup?services->object_cleanup->factory:IgbObjectFieldConstructFactory{};};
    const auto object_check=[&]{
        if(!s.object.storage || s.object.storage->bytes!=s.object_bytes || s.object.storage->original_address!=s.object_allocation_address)
            fail("Context-info probe cleanup lost its captured object allocation identity.");
        igb_object_validate_instance(s.object);
    };
    try{for(;;){check();switch(s.phase){
    case P::reset:
        if(!s.reset)s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.local,false);
        {const auto status=s.reset->resume();check();if(!status.completed)return status;}
        s.reset.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->types || !services->types->runtime || !services->types->runtime->singleton)
            return pending("Context-info probe requires ArkCore80562100 before its derived vtable store.");
        s.core=*services->types->runtime->singleton;s.phase=P::vtable;break;
    case P::vtable:igb_bootstrap_write_word(s.local,0,0x804741d4);s.phase=P::object_null;break;
    case P::object_null:igb_bootstrap_write_word(s.local,8,0);s.phase=P::string_null;break;
    case P::string_null:igb_bootstrap_write_word(s.local,12,0);s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Context-info probe dereferences null captured ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Context-info probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>0x10 || (s.offset&3u)!=0)fail("Context-info probe exceeds its aligned0x14-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);s.phase=P::restore;break;
    case P::restore:
        igb_bootstrap_write_word(s.local,0,0x804741d4);
        // The string branch tests retained literal r5=0, not a fresh+0C word.
        s.phase=P::member;break;
    case P::member:
        if(igb_bootstrap_address(s.local)+8u==0){s.phase=P::done;break;}
        s.address=igb_bootstrap_read_word(s.local,8);s.phase=s.address?P::resolve:P::done;break;
    case P::resolve:{const auto object=cleanup_object(services,s.address);
        if(!object)return pending("Context-info probe requires its captured object+08 physical binding.");
        s.object=*object;s.object_bytes=s.object.storage->bytes;s.object_allocation_address=s.object.storage->original_address;s.phase=P::decrement;break;}
    case P::decrement:
        object_check();igb_object_set_reference_word(s.object,igb_object_reference_word(s.object)-1u);s.phase=P::test;break;
    case P::test:
        object_check();if((igb_object_reference_word(s.object)&0x7fffffu)!=0){s.phase=P::done;break;}
        s.request={};s.request.kind=IgbObjectFieldConstructServiceKind::destroy_object;s.request.object=s.object;
        s.factory=fallback();s.phase=P::destroy;break;
    case P::destroy:
        if(!s.factory)s.factory=fallback();if(!s.factory)return pending("Context-info probe requires reached object destruction80066E1C.");
        if(!s.operation){s.operation=s.factory(s.request);check();}if(!s.operation)return pending("Context-info probe reached unbound object destruction80066E1C.");
        if(!s.callback)s.callback=s.operation->resume;if(!s.callback)return pending("Context-info probe requires its retained object-destruction continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        s.callback={};s.operation.reset();s.factory={};s.request={};check();s.phase=P::done;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbContextInfoDefaultsInvocation::Impl {
    enum class P{receiver,count,batch,meta_field,metadata,type_store,string_field,string_default,string_flag,
        vendor_field,prototype,element_store,schema_setup,schema,done};P phase=P::receiver;
    std::shared_ptr<IgbContextInfoServices> owner;std::shared_ptr<IgbRuntimeMetaType> metadata,related;
    std::shared_ptr<IgbDecodeRuntimeField> prototype;std::unique_ptr<FieldAccess> access;
    std::unique_ptr<IgbFieldBatchInvocation> batch;std::unique_ptr<IgbMetadataLookupInvocation> lookup;
    std::unique_ptr<IgbFieldStringDefaultInvocation> string_default;std::unique_ptr<IgbSignedBytePrototypeInvocation> getter;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;std::uint32_t first{};bool running{},fault{};
};
IgbContextInfoDefaultsInvocation::IgbContextInfoDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbContextInfoDefaultsInvocation::~IgbContextInfoDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.related)s.owner->retained.push_back(s.related);
    if(s.prototype)s.owner->retained.push_back(s.prototype);
    if(s.access){if(s.access->field)s.owner->retained.push_back(s.access->field);if(s.access->physical && s.access->physical->storage)s.owner->retained.push_back(s.access->physical->storage);
        if(s.access->bytes)s.owner->retained.push_back(s.access->bytes);}
}}
bool IgbContextInfoDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextInfoDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbContextInfoDefaultsInvocation::resume(std::shared_ptr<IgbContextInfoServices> services,const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context-info defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{check_fault(s.fault);if(enclosing_check)enclosing_check();check_fault(s.fault);};
    const auto clear_field=[&]{s.access.reset();s.related.reset();s.prototype.reset();check();};
    try{for(;;){check();switch(s.phase){
    case P::receiver:
        if(!services || !services->metadata || !*services->metadata)return pending("Context-info defaults require canonical metadata80561DE8.");
        s.metadata=**services->metadata;s.phase=P::count;break;
    case P::count:
        s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services?services->factories:nullptr,3);s.phase=P::batch;break;
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::meta_field;break;}
    case P::meta_field:
        s.access=std::make_unique<FieldAccess>(field_at(s.metadata,s.first));
        s.lookup=std::make_unique<IgbMetadataLookupInvocation>(IgbMetadataLookup::allocate_if_null);s.phase=P::metadata;break;
    case P::metadata:{const auto status=s.lookup->resume(services?services->meta_objects:nullptr);check();if(!status.completed)return status;
        s.related=s.lookup->result();s.lookup.reset();check();s.phase=P::type_store;break;}
    case P::type_store:
        if(const auto pointer=s.access->pointer()){
            const auto address=metadata_address(s.related,services);if(!address)return pending("Context-info descriptor type store requires captured metadata arithmetic identity.");
            igb_bootstrap_write_word(*pointer,0x38,*address);
        }
        s.access->field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.related);clear_field();s.phase=P::string_field;break;
    case P::string_field:
        s.access=std::make_unique<FieldAccess>(field_at(s.metadata,s.first+1u));
        s.string_default=std::make_unique<IgbFieldStringDefaultInvocation>(s.access->field,IgbDependencyName{});s.phase=P::string_default;break;
    case P::string_default:{const auto status=s.string_default->resume(services?services->values:nullptr,check);check();if(!status.completed)return status;
        s.string_default.reset();check();s.phase=P::string_flag;break;}
    case P::string_flag:
        if(const auto pointer=s.access->pointer())igb_bootstrap_write_byte(*pointer,0x35,1);
        s.access->field->flag_35=std::uint8_t{1};clear_field();s.phase=P::vendor_field;break;
    case P::vendor_field:
        s.access=std::make_unique<FieldAccess>(field_at(s.metadata,s.first+2u));
        s.getter=std::make_unique<IgbSignedBytePrototypeInvocation>();s.phase=P::prototype;break;
    case P::prototype:{const auto status=s.getter->resume(services,check);check();if(!status.completed)return status;
        s.prototype=s.getter->result();s.getter.reset();check();s.phase=P::element_store;break;}
    case P::element_store:
        if(const auto pointer=s.access->pointer()){
            const auto address=field_address(s.prototype,services);if(!address)return pending("Context-info descriptor element store requires captured prototype arithmetic identity.");
            igb_bootstrap_write_word(*pointer,0x3c,*address);
        }
        s.access->field->element_field=std::weak_ptr<IgbDecodeRuntimeField>(s.prototype);clear_field();s.phase=P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Context-info defaults require original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_context_info_registration_function_factory(std::shared_ptr<IgbContextInfoServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (call.function!=0x80037330 && call.function!=0x80037358))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Context-info services expired before callback selection.");auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.function==0x80037358){auto invocation=std::make_shared<IgbContextInfoRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(0x80037358);operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_context_info_registration_type_factory(std::shared_ptr<IgbContextInfoServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        using K=IgbTypeRegistrationServiceKind;
        const bool registration=call.kind==K::register_parent && call.function==0x80037358;
        const bool lookup=call.kind==K::get_parent && (call.function==0x8003722c || call.function==0x800373cc || call.function==0x80093ee0);
        const bool probe=call.kind==K::layout_probe && call.function==0x80037268;
        const bool defaults=call.kind==K::defaults && call.function==0x800373ec;
        if(!registration && !lookup && !probe && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Context-info services expired before type selection.");auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbContextInfoRegistrationInvocation>();
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(lookup){auto invocation=std::make_shared<IgbContextInfoLookupInvocation>(call.function==0x80093ee0?IgbContextInfoLookup::plain:IgbContextInfoLookup::fully_registered);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else if(probe)operation->resume=[selected,invocation=std::shared_ptr<IgbContextInfoProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Context-info probe requires represented stack-local storage.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Context-info probe has null stack-local storage.");
                invocation=std::make_shared<IgbContextInfoProbeInvocation>(std::move(local));}return invocation->resume(selected);
        };
        else{auto invocation=std::make_shared<IgbContextInfoDefaultsInvocation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};}
        return operation;
    };
}
} // namespace dw4
