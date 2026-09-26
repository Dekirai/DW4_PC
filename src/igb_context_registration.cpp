#include "dw4/igb_context_registration.hpp"

#include <array>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbContextRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using E=IgbContextKind;using L=IgbContextLookup;
constexpr std::array profiles{
    IgbContextProfile{"igContext",0x80561e00,0x80037770,0x80037798,0x80066b08,0x800237d0,0x8003780c,0x8003782c,0x80037734,0x80037594},
    IgbContextProfile{"igContextExt",0x80561df8,0x800374e8,0x80037510,0x80037798,0x80037594,0x80037574,0,0x800374ac,0}
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
    }return {};
}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t index){
    if(!metadata || !metadata->fields)fail("Context defaults read a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(index<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("Context defaults read an unavailable physical field slot.");return (*backing)[slot];
}
// The indexed descriptor stays captured across a lazy getter. An existing
// physical allocation may not silently become another allocation while that
// getter is pending. Canonical-only fixtures remain explicitly canonical-only.
struct FieldAccess {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> physical;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t address{};
    explicit FieldAccess(std::shared_ptr<IgbDecodeRuntimeField> value):field(std::move(value)){
        if(field && field->physical_storage){physical=IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
            if(physical->storage){bytes=physical->storage->bytes;address=physical->storage->original_address;}}
    }
    std::optional<IgbBootstrapPointer> pointer()const{
        if(!field)fail("Context defaults store through a null descriptor.");
        if(!physical){if(field->physical_storage)fail("Context descriptor acquired a different physical binding while captured.");return {};}
        if(!physical->storage || !field->physical_storage || physical->storage->faulted() ||
            field->physical_storage->storage.lock()!=physical->storage || field->physical_storage->offset!=physical->offset ||
            physical->storage->original_address!=address || physical->storage->bytes!=bytes || !bytes || bytes->released ||
            igb_bootstrap_field(*physical)!=field)fail("Context descriptor lost its captured physical identity.");
        return physical;
    }
};
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbContextRegistrationServices>& services){
    if(!metadata)return 0;
    std::optional<std::uint32_t> result;
    const auto pointers=services && services->schema && services->schema->fields?services->schema->fields->pointer_lists:nullptr;
    const auto accept=[&](std::uint32_t address){
        if(!address || (result && *result!=address))fail("Context dependency metadata has conflicting arithmetic identities.");
        if(pointers)for(const auto& binding:pointers->values)if(binding.address==address &&
            (binding.field || (binding.metadata && binding.metadata!=metadata)))
            fail("Context dependency metadata address conflicts with another canonical pointer identity.");
        result=address;
    };
    if(services && services->lists)for(const auto& pointer:services->lists->allocations){
        if(igb_bootstrap_metadata(pointer)!=metadata)continue;
        if(pointer.storage->faulted())fail("Context dependency metadata allocation is faulted.");
        accept(igb_bootstrap_address(pointer));
    }
    if(pointers)for(const auto& binding:pointers->values)if(binding.metadata==metadata){
        if(binding.field)fail("Context dependency metadata address aliases a descriptor binding.");accept(binding.address);
    }return result;
}
}
IgbContextRegistrationServices::IgbContextRegistrationServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x8002879cu,0x8002e1acu,0x8002879cu});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_pExtList"),
        std::make_shared<const std::string>("_iNumExts"),std::make_shared<const std::string>("_pExtInstances")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561e04},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561e08},
        IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561e0c}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u,12u,16u});
}
const IgbContextProfile& igb_context_profile(E kind){
    const auto index=static_cast<std::size_t>(kind);if(index>=profiles.size())fail("Unknown context registration class.");return profiles[index];
}
std::shared_ptr<IgbRegistrationMetaSlot> igb_context_metadata_slot(const std::shared_ptr<IgbContextRegistrationServices>& services,E kind){
    igb_context_profile(kind);if(!services)return {};return kind==E::context?services->context_metadata:services->extension_metadata;
}
IgbTypeRegistrationArguments igb_context_type_registration_arguments(const std::shared_ptr<IgbContextRegistrationServices>& services,E kind){
    const auto& p=igb_context_profile(kind);IgbTypeRegistrationArguments args;
    args.metadata_slot=igb_context_metadata_slot(services,kind);if(!args.metadata_slot)fail("Context registration requires its canonical metadata cell.");
    args.name=std::make_shared<const std::string>(p.name);args.flag=1;args.native_size=0x14;
    args.parent_registration=p.parent;args.parent_getter=p.parent_getter;args.field_callback=p.field_callback;args.defaults_callback=p.defaults;
    if(kind==E::context)args.extra_callbacks=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0x80037140,0x80036f6c,0});
    return args;
}
struct IgbContextRegistrationInvocation::Impl {
    enum class P{prerequisites,arguments,registration,done};P phase=P::prerequisites;E kind{};
    IgbRegistrationPrerequisitesInvocation prerequisites;std::unique_ptr<IgbTypeRegistrationInvocation> registration;
    std::shared_ptr<IgbRegistrationCallbackList> result;bool running{},fault{};
};
IgbContextRegistrationInvocation::IgbContextRegistrationInvocation(E kind):impl_(std::make_unique<Impl>()){igb_context_profile(kind);impl_->kind=kind;}
IgbContextRegistrationInvocation::~IgbContextRegistrationInvocation()=default;
bool IgbContextRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbContextRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete context registration has no callback-list result.");return impl_->result;
}
MessengerCallResult IgbContextRegistrationInvocation::resume(std::shared_ptr<IgbContextRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Context registration was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::prerequisites:{const auto status=s.prerequisites.resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.phase=P::arguments;break;}
    case P::arguments:
        if(!igb_context_metadata_slot(services,s.kind))return pending("Context registration requires its canonical metadata cell after prerequisites.");
        s.registration=std::make_unique<IgbTypeRegistrationInvocation>(igb_context_type_registration_arguments(services,s.kind));s.phase=P::registration;break;
    case P::registration:{const auto status=s.registration->resume(services?services->types:nullptr);check();if(!status.completed)return status;
        s.result=s.registration->result();s.registration.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbContextLookupInvocation::Impl {
    enum class P{read,registration,reload,done};P phase=P::read;E kind{};L mode{};
    std::shared_ptr<IgbRegistrationMetaSlot> slot;std::shared_ptr<IgbRuntimeMetaType> result;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;bool running{},fault{};
};
IgbContextLookupInvocation::IgbContextLookupInvocation(E kind,L mode):impl_(std::make_unique<Impl>()){
    const auto& p=igb_context_profile(kind);
    if(mode!=L::fully_registered && mode!=L::plain)fail("Unknown context metadata lookup mode.");
    if(mode==L::plain && !p.plain_getter)fail("No recovered plain getter exists for this context class.");impl_->kind=kind;impl_->mode=mode;
}
IgbContextLookupInvocation::~IgbContextLookupInvocation()=default;
bool IgbContextLookupInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextLookupInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRuntimeMetaType> IgbContextLookupInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete context metadata lookup has no result.");return impl_->result;
}
MessengerCallResult IgbContextLookupInvocation::resume(std::shared_ptr<IgbContextRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context lookup requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    const auto check=[&]{if(s.fault)fail("Context lookup was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::read:
        if(!s.slot){s.slot=igb_context_metadata_slot(services,s.kind);if(!s.slot)return pending("Context lookup requires its canonical metadata cell.");}
        if(!*s.slot)return pending("Context lookup requires represented canonical metadata.");
        if(s.mode==L::plain){s.result=**s.slot;s.phase=P::done;break;}
        if(**s.slot && ((**s.slot)->class_flags&4u)!=0)s.phase=P::reload;
        else{s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(igb_context_profile(s.kind).body);s.phase=P::registration;}break;
    case P::registration:{const auto status=s.registration->resume(services?services->callbacks:nullptr);check();if(!status.completed)return status;
        s.registration.reset();check();s.phase=P::reload;break;}
    case P::reload:
        if(!*s.slot)return pending("Context lookup requires its fresh canonical metadata reload.");
        s.result=**s.slot;s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbContextDefaultsInvocation::Impl {
    enum class P{receiver,count,batch,field,lookup,type_store,construct_store,schema_setup,schema,done};P phase=P::receiver;
    std::shared_ptr<IgbContextRegistrationServices> owner;std::shared_ptr<IgbRuntimeMetaType> metadata,related;
    std::unique_ptr<FieldAccess> access;std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbDataListLookupInvocation> lookup;std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::uint32_t first{},index{};bool running{},fault{};
};
IgbContextDefaultsInvocation::IgbContextDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbContextDefaultsInvocation::~IgbContextDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.related)s.owner->retained.push_back(s.related);
    if(s.access){if(s.access->field)s.owner->retained.push_back(s.access->field);if(s.access->physical && s.access->physical->storage)s.owner->retained.push_back(s.access->physical->storage);}
}}
bool IgbContextDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbContextDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbContextDefaultsInvocation::resume(std::shared_ptr<IgbContextRegistrationServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Context defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Context defaults were invalidated by a callback.");};
    const auto release_field=[&]{s.related.reset();s.access.reset();check();};
    try{for(;;){check();switch(s.phase){
    case P::receiver:
        if(!services || !services->context_metadata || !*services->context_metadata)return pending("Context defaults require canonical metadata80561E00.");
        s.metadata=**services->context_metadata;s.phase=P::count;break;
    case P::count:
        s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services?services->factories:nullptr,3);s.phase=P::batch;break;
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;}
    case P::field:
        s.access=std::make_unique<FieldAccess>(field_at(s.metadata,s.first+s.index));
        s.lookup=std::make_unique<IgbDataListLookupInvocation>(s.index==0?IgbDataListKind::context_info:IgbDataListKind::context_extension,IgbDataListLookup::allocate_if_null);
        s.phase=P::lookup;break;
    case P::lookup:{const auto status=s.lookup->resume(services?services->lists:nullptr);check();if(!status.completed)return status;
        s.related=s.lookup->result();s.lookup.reset();check();s.phase=P::type_store;break;}
    case P::type_store:
        if(const auto pointer=s.access->pointer()){
            const auto address=metadata_address(s.related,services);
            if(!address)return pending("Context descriptor type store requires its captured dependency metadata arithmetic address.");
            igb_bootstrap_write_word(*pointer,0x38,*address);
        }
        s.access->field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.related);
        if(s.index==0){release_field();s.index=2;s.phase=P::field;}else s.phase=P::construct_store;break;
    case P::construct_store:
        if(const auto pointer=s.access->pointer())igb_bootstrap_write_byte(*pointer,0x34,1);
        s.access->field->flag_34=std::uint8_t{1};release_field();s.phase=P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Context defaults require their original schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr,check);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbRegistrationCallbacksFactory make_native_igb_context_registration_function_factory(std::shared_ptr<IgbContextRegistrationServices> services,IgbRegistrationCallbacksFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbRegistrationCallbacksCall& call)->std::shared_ptr<IgbRegistrationCallbacksOperation>{
        const auto direct=registration_function(call.function,false),wrapper=registration_function(call.function,true);
        if(call.kind!=IgbRegistrationCallbacksServiceKind::invoke_function || (!direct && !wrapper))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Context registration services expired before callback selection.");
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(direct){auto invocation=std::make_shared<IgbContextRegistrationInvocation>(*direct);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbRegistrationCallbacksInvocation>(igb_context_profile(*wrapper).body);
            operation->resume=[selected,invocation]{return invocation->resume(selected->callbacks);};}
        return operation;
    };
}
IgbTypeRegistrationFactory make_native_igb_context_registration_type_factory(std::shared_ptr<IgbContextRegistrationServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        const auto registration=call.kind==IgbTypeRegistrationServiceKind::register_parent?registration_function(call.function,false):std::optional<E>{};
        const auto lookup=call.kind==IgbTypeRegistrationServiceKind::get_parent?lookup_function(call.function):std::optional<Lookup>{};
        const bool defaults=call.kind==IgbTypeRegistrationServiceKind::defaults && call.function==0x8003782c;
        if(!registration && !lookup && !defaults)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Context registration services expired before type selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        if(registration){auto invocation=std::make_shared<IgbContextRegistrationInvocation>(*registration);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->callbacks_result=invocation->result();return status;};}
        else if(lookup){auto invocation=std::make_shared<IgbContextLookupInvocation>(lookup->kind,lookup->mode);
            operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);if(status.completed)out->metadata_result=invocation->result();return status;};}
        else{auto invocation=std::make_shared<IgbContextDefaultsInvocation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};}
        return operation;
    };
}
} // namespace dw4
