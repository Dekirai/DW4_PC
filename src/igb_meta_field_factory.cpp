#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_string_pool.hpp"
#include "dw4/igb_field_operations.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetaFieldFactoryError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("Metadata field requires its reached native operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Metadata field reached an unbound native operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Metadata field requires its retained continuation.");
        const auto result=callback();check();return result;
    }
};
using FieldCall=Call<IgbMetaFieldCall,IgbMetaFieldFactory,IgbMetaFieldOperation>;
using ValueCall=Call<IgbFieldOperationCall,IgbFieldOperationsFactory,IgbFieldOperation>;
using StringCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
using K=IgbMetaFieldServiceKind;using S=IgbDependenciesServiceKind;
constexpr std::array<IgbMetaFieldProfile,10> profiles={{
    {0x80022a90,0x800759e0,0x80075a2c,0x80476e6c,0x34,0x80022cb0,0x80022cd8,0x805614bc},
    {0x8002e1ac,0x8005356c,0x800535b8,0x80475b60,0x34,0x8002e390,0x8002e3b8,0x805619e0},
    {0x800385e0,0x8003eb7c,0x8003ebc8,0x80473e30,0x34,0x80038800,0x80038828,0x80561e48},
    {0x800251ec,0x80071df8,0x80071e44,0x8047693c,0x3c,0x800253dc,0x80025404,0x805615b4},
    {0x8002879c,0x8006ac50,0x8006aca4,0x804762fc,0x44,0x800289f8,0x80028a20,0x805616e8},
    {0x800274ec,0x8006c46c,0x8006c4b8,0x80476630,0x38,0x800276dc,0x80027704,0x80561694},
    {0x80021e10,0x80075ec0,0x80075f0c,0x80477054,0x34,0x80021ff4,0x8002201c,0x80561494},
    {0x8002ac34,0x8006324c,0x800632a4,0x80476088,0x50,0x8002ae84,0x8002aeac,0x80561818},
    {0x800259a8,0x800710bc,0x80071108,0x80476848,0x34,0x80025b8c,0x80025bb4,0x805615d8},
    {0x80037b1c,0x8003ff44,0x8003ff90,0x80473f24,0x34,0x80037d00,0x80037d28,0x80561e1c}
}};
bool reference_kind(IgbMetaFieldKind kind){return kind==IgbMetaFieldKind::string || kind==IgbMetaFieldKind::object || kind==IgbMetaFieldKind::pointer || kind==IgbMetaFieldKind::memory_reference;}
bool known_base_vtable(std::uint32_t word){return word==0x80471914u || word==0x804717f0u || word==0x80471384u;}
bool known_profile_vtable(std::uint32_t word){for(const auto& profile:profiles)if(word==profile.vtable)return true;return false;}
void retain_name(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(field->name)field->overwritten_constructor_strings_.push_back(field->name);
    if(field->name_string && *field->name_string)field->overwritten_constructor_owners_.push_back(*field->name_string);
}
void name_zero(const IgbBootstrapPointer& object,const std::shared_ptr<IgbDecodeRuntimeField>& field){
    retain_name(field);igb_bootstrap_write_word(object,0x0c,0);
    field->name.reset();field->name_string=std::shared_ptr<IgbInternedString>{};field->name_string_address=0;
}
}
const IgbMetaFieldProfile& igb_meta_field_profile(IgbMetaFieldKind kind){
    const auto index=static_cast<std::size_t>(kind);if(index>=profiles.size())fail("Unknown metadata-field constructor profile.");return profiles[index];
}
std::optional<IgbMetaFieldKind> igb_meta_field_kind_for_factory(std::uint32_t function){
    for(std::size_t i=0;i<profiles.size();++i)if(profiles[i].factory==function)return static_cast<IgbMetaFieldKind>(i);return {};
}
void igb_meta_field_set_name(const IgbBootstrapPointer& object,
    const std::shared_ptr<IgbInternedString>& owner,std::uint32_t address){
    if((address==0)!=(!owner))fail("Field name address and canonical header disagree.");
    auto field=igb_bootstrap_field(object);
    if(!field->name_string || *field->name_string!=owner)retain_name(field);
    igb_bootstrap_write_word(object,0x0c,address);
    field->name_string=owner;field->name_string_address=address;field->name=owner?owner->value:SystemStageString{};
}

struct IgbMetaFieldConstructorInvocation::Impl {
    enum class P {bind,first_base,initial,reset_base,stores,string_global,after_allocate,after_initialize,
        publish_pool,intern_null,old_name,release_name,publish_name,tail_stores,finish,string_call,done};
    IgbBootstrapPointer object;IgbMetaFieldConstructor entry{};IgbMetaFieldKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbMetaFieldFactoryServices> owner;
    std::shared_ptr<ModelSceneObject> string_pool;
    std::shared_ptr<IgbInternedString> old_name;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    std::unique_ptr<IgbBootstrapBaseInvocation> base;
    StringCall call;P phase=P::bind,after=P::done;
    unsigned resets{};bool running{},fault{},owns{};
};
IgbMetaFieldConstructorInvocation::IgbMetaFieldConstructorInvocation(IgbBootstrapPointer object,IgbMetaFieldConstructor entry,IgbMetaFieldKind kind)
    :impl_(std::make_unique<Impl>()){igb_meta_field_profile(kind);impl_->object=std::move(object);impl_->entry=entry;impl_->kind=kind;}
IgbMetaFieldConstructorInvocation::~IgbMetaFieldConstructorInvocation(){auto& s=*impl_;
    if(s.owns && !completed()){
        s.field->construction_fault_=true;
        if(s.owner){if(s.object.storage)s.owner->unfinished_.push_back(s.object.storage);
            if(s.old_name)s.owner->unfinished_.push_back(s.old_name);if(s.string_pool)s.owner->unfinished_.push_back(s.string_pool);}
        else if(s.object.storage && s.object.storage->bytes)s.field->overwritten_constructor_owners_.push_back(s.object.storage->bytes);
    }
}
bool IgbMetaFieldConstructorInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaFieldConstructorInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbMetaFieldConstructorInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete metadata-field construction has no result.");return impl_->field;
}
MessengerCallResult IgbMetaFieldConstructorInvocation::resume(std::shared_ptr<IgbMetaFieldFactoryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.field->construction_fault_=true;fail("Metadata-field constructor requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.owns && s.field->construction_fault_))fail("Metadata-field construction was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](S kind,P after)->IgbDependenciesCall&{
        clear();s.call.request.kind=kind;if(services && services->dependencies)s.call.factory=services->dependencies->factory;
        s.phase=P::string_call;s.after=after;return s.call.request;
    };
    const auto globals=[&]()->std::shared_ptr<SystemStageServices>{return services && services->dependencies?services->dependencies->globals:nullptr;};
    const auto byte=[&](std::uint32_t off,std::uint8_t value){igb_bootstrap_write_byte(s.object,off,value);};
    const auto word=[&](std::uint32_t off,std::uint32_t value){igb_bootstrap_write_word(s.object,off,value);};
    const auto derived=igb_meta_field_profile(s.kind).vtable;
    const bool reference_entry=s.entry==IgbMetaFieldConstructor::reference_base || s.entry==IgbMetaFieldConstructor::reference_reset;
    const bool reference=reference_entry || ((s.entry==IgbMetaFieldConstructor::specialized || s.entry==IgbMetaFieldConstructor::specialized_reset) && reference_kind(s.kind));
    try{for(;;){check();switch(s.phase){
    case P::bind:
        if(igb_bootstrap_address(s.object)==0)igb_bootstrap_write_word(s.object,0,0x804717f0);
        s.field=igb_bootstrap_field(s.object);
        if(s.field->construction_fault_ || !s.field->construction_active_.expired())fail("Metadata field is faulted or already constructing.");
        s.field->construction_active_=s.token;s.owns=true;
        s.field->physical_storage=IgbFieldStorageBinding{s.object.storage,s.object.offset};
        s.phase=s.entry==IgbMetaFieldConstructor::specialized || s.entry==IgbMetaFieldConstructor::base ||
            s.entry==IgbMetaFieldConstructor::reference_base?P::first_base:P::reset_base;break;
    case P::first_base:
        if(!s.base)s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,true);
        {const auto status=s.base->resume(services?services->bootstrap:nullptr);check();if(!status.completed)return status;}
        s.base.reset();check();s.phase=P::initial;break;
    case P::initial:
        word(0,0x80471914);name_zero(s.object,s.field);s.phase=P::reset_base;break;
    case P::reset_base:
        if(!s.base)s.base=std::make_unique<IgbBootstrapBaseInvocation>(s.object,false);
        {const auto status=s.base->resume(services?services->bootstrap:nullptr);check();if(!status.completed)return status;}
        s.base.reset();check();s.phase=P::stores;break;
    case P::stores:
        word(0,0x80471914);name_zero(s.object,s.field);
        word(8,0);s.field->offset=0;byte(0x25,0);s.field->flag_25=0;
        igb_bootstrap_write_halfword(s.object,0x10,0xffff);s.field->type_ordinal=std::uint16_t{0xffff};
        igb_bootstrap_write_halfword(s.object,0x12,0);s.field->ordinal=0;
        igb_bootstrap_write_halfword(s.object,0x14,0);s.field->size=0;
        word(0x20,0);if(s.field->default_bytes && s.field->default_bytes->buffer)s.field->overwritten_constructor_owners_.push_back(s.field->default_bytes->buffer);
        s.field->default_bytes=IgbRuntimeFieldDefaultSource{};s.field->default_bytes_address=0;
        word(0x1c,0);s.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
        s.field->global_slot_address=0;s.phase=P::string_global;break;
    case P::string_global:{
        const auto owner=globals();if(!owner)return pending("Metadata-field reset requires canonical string-pool global80562140.");
        if(owner->string_pool){s.phase=P::intern_null;break;}
        select(S::create_string_pool,P::after_allocate).bytes=16;break;
    }
    case P::after_allocate:
        s.string_pool=std::move(s.call.operation->pool_result);clear();
        if(s.string_pool)select(S::initialize_string_pool,P::after_initialize).pool=s.string_pool;
        else s.phase=P::publish_pool;break;
    case P::after_initialize:s.string_pool=std::move(s.call.operation->pool_result);clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{
        const auto owner=globals();if(!owner)return pending("Metadata-field reset requires string-pool publication storage.");
        if(owner->string_pool && owner->string_pool!=s.string_pool)s.field->overwritten_constructor_owners_.push_back(owner->string_pool);
        owner->string_pool=s.string_pool;s.phase=P::intern_null;break;
    }
    case P::intern_null:{
        const auto owner=globals();if(!owner)return pending("Metadata-field reset requires its freshly reloaded string pool.");
        IgbStringInternInvocation intern(owner->string_pool,std::monostate{});const auto result=intern.resume();
        if(!result.completed || intern.result())fail("Null-name interner did not execute its original null leaf.");
        s.phase=P::old_name;break;
    }
    case P::old_name:{
        const auto address=igb_bootstrap_read_word(s.object,0x0c);
        if(!address){s.phase=P::publish_name;break;}
        if(!s.field->name_string || !s.field->name_string_address || *s.field->name_string_address!=address)
            return pending("Metadata-field reset requires a canonical header for its live name pointer.");
        s.old_name=*s.field->name_string;
        if(!s.old_name || s.old_name->released)fail("Metadata-field reset reads an unavailable string header.");
        --s.old_name->references;s.phase=s.old_name->references==0?P::release_name:P::publish_name;break;
    }
    case P::release_name:{auto& call=select(S::release_string,P::publish_name);call.string=s.old_name;call.pool=s.old_name->container;break;}
    case P::publish_name:
        clear();igb_meta_field_set_name(s.object,nullptr,0);s.phase=P::tail_stores;break;
    case P::tail_stores:
        word(0x18,0);s.field->declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};word(0x30,0);s.field->word_30=0;
        byte(0x24,0);s.field->default_mode=std::uint8_t{0};
        byte(0x27,0);s.field->flag_27=std::uint8_t{0};byte(0x28,0);s.field->flag_28=std::uint8_t{0};
        byte(0x29,0);s.field->flag_29=std::uint8_t{0};byte(0x2a,0);s.field->flag_2a=std::uint8_t{0};
        byte(0x2b,1);s.field->flag_2b=std::uint8_t{1};byte(0x2c,2);s.field->flag_2c=std::uint8_t{2};
        byte(0x26,1);s.field->flag_26=1;
        ++s.resets;
        if(reference){
            const bool full=s.entry==IgbMetaFieldConstructor::specialized || s.entry==IgbMetaFieldConstructor::reference_base;
            if(full && s.resets==1){word(0,0x80471384);s.phase=P::reset_base;break;}
            word(0,0x80471384);byte(0x34,0);s.field->flag_34=std::uint8_t{0};byte(0x35,0);s.field->flag_35=std::uint8_t{0};
            if(s.entry==IgbMetaFieldConstructor::specialized && s.resets==2){
                word(0,derived);if(s.kind==IgbMetaFieldKind::object){word(0x40,0);s.field->word_40=0;}
                else if(s.kind==IgbMetaFieldKind::memory_reference){word(0x48,0);s.field->word_48=0;word(0x4c,0);s.field->word_4c=0;}
                s.phase=P::reset_base;break;
            }
        }else if(s.entry==IgbMetaFieldConstructor::specialized && s.resets==1){word(0,derived);s.phase=P::reset_base;break;}
        s.phase=P::finish;break;
    case P::finish:
        clear();if(s.entry==IgbMetaFieldConstructor::specialized || s.entry==IgbMetaFieldConstructor::specialized_reset){
            word(0,derived);
            if(s.kind==IgbMetaFieldKind::string){byte(0x38,1);s.field->flag_38=std::uint8_t{1};byte(0x27,2);s.field->flag_27=std::uint8_t{2};
                byte(0x35,1);s.field->flag_35=std::uint8_t{1};}
            else if(s.kind==IgbMetaFieldKind::object){word(0x40,0);s.field->word_40=0;byte(0x3c,1);s.field->flag_3c=std::uint8_t{1};
                word(0x38,0);s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>{};}
            else if(s.kind==IgbMetaFieldKind::memory_reference){
                word(0x48,0);s.field->word_48=0;word(0x4c,0);s.field->word_4c=0;
                byte(0x42,1);s.field->flag_42=std::uint8_t{1};word(0x38,0);s.field->word_38=0;
                word(0x3c,0);s.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};
                byte(0x24,3);s.field->default_mode=std::uint8_t{3};byte(0x41,1);s.field->flag_41=std::uint8_t{1};
                byte(0x40,1);s.field->flag_40=std::uint8_t{1};word(0x44,0xffffffffu);s.field->word_44=0xffffffffu;
                byte(0x27,2);s.field->flag_27=std::uint8_t{2};
            }
        }
        s.field->construction_active_.reset();s.owns=false;s.phase=P::done;return {true,0,{}};
    case P::string_call:{const auto status=s.call.poll(services && services->dependencies?services->dependencies->factory:IgbDependenciesFactory{},check);
        if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;if(s.owns)s.field->construction_fault_=true;throw;}
}

struct IgbMetaFieldFactoryInvocation::Impl {
    enum class P {gate,register_type,metadata,reflected,after_reflected,allocate,construct,tail_2c,tail_defaults,tail_24,result,value_call,call,done};
    std::shared_ptr<ModelSceneObject> pool;std::shared_ptr<IgbMetaFieldFactoryServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;std::shared_ptr<IgbDecodeRuntimeField> result;
    IgbBootstrapPointer allocation;
    std::unique_ptr<IgbRawAllocationInvocation> allocator;
    std::unique_ptr<IgbMetaFieldConstructorInvocation> constructor;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    IgbMetaFieldKind kind{};FieldCall call;ValueCall value;P phase=P::gate,after=P::done;bool running{},fault{};
};
IgbMetaFieldFactoryInvocation::IgbMetaFieldFactoryInvocation(std::shared_ptr<ModelSceneObject> pool,IgbMetaFieldKind kind):impl_(std::make_unique<Impl>()){
    igb_meta_field_profile(kind);impl_->pool=std::move(pool);impl_->kind=kind;
}
IgbMetaFieldFactoryInvocation::~IgbMetaFieldFactoryInvocation(){auto& s=*impl_;if(!completed() && s.owner && s.allocation.storage)s.owner->unfinished_.push_back(s.allocation.storage);}
bool IgbMetaFieldFactoryInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetaFieldFactoryInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbMetaFieldFactoryInvocation::result()const{if(!completed() || faulted())fail("Incomplete metadata-field factory has no result.");return impl_->result;}
IgbBootstrapPointer IgbMetaFieldFactoryInvocation::allocation()const{if(!completed() || faulted())fail("Incomplete metadata-field factory has no allocation.");return impl_->allocation;}
MessengerCallResult IgbMetaFieldFactoryInvocation::resume(std::shared_ptr<IgbMetaFieldFactoryServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata-field factory requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Metadata-field factory continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto& profile=igb_meta_field_profile(s.kind);
    const auto select=[&](K kind,P after)->IgbMetaFieldCall&{
        clear();s.call.request.kind=kind;s.call.request.object=s.allocation;s.call.factory=services?services->factory:IgbMetaFieldFactory{};
        s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto tail=[&](std::uint32_t slot,P after){
        clear();const auto word=igb_bootstrap_read_word(s.allocation,0);
        if(!word)fail("Metadata-field factory dereferences a null tail vtable.");
        if(slot==0x2c && (word==0x8047693cu || word==0x804762fcu)){s.phase=P::tail_defaults;return;}
        if(known_base_vtable(word) || known_profile_vtable(word)){s.phase=after;return;}
        auto& call=select(K::object_virtual,after);call.vtable_word=word;call.virtual_slot=slot;call.argument=0;
    };
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!services || !services->bootstrap || !services->bootstrap->runtime || !services->bootstrap->runtime->singleton)
            return pending("Metadata-field factory requires represented ArkCore80562100.");
        if(!*services->bootstrap->runtime->singleton)fail("Metadata-field factory dereferences a null ArkCore.");
        if((*services->bootstrap->runtime->singleton)->enabled)s.phase=P::register_type;
        else{s.allocator=std::make_unique<IgbRawAllocationInvocation>(profile.bytes,0,s.pool);s.phase=P::allocate;}break;
    case P::register_type:
        if(s.registration || (services && services->callbacks)){
            if(!s.registration)s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(profile.callback);
            const auto progress=s.registration->resume(services?services->callbacks:nullptr);check();if(!progress.completed)return progress;
            s.registration.reset();check();s.phase=P::metadata;
        }else select(K::register_type,P::metadata).function=profile.registration;break;
    case P::metadata:{
        clear();const auto slot=services?(s.kind==IgbMetaFieldKind::unsigned_int?services->metadata:services->metadata_slots[static_cast<std::size_t>(s.kind)]):nullptr;
        if(!slot || !slot->has_value())return pending("Metadata-field factory requires its profile's canonical metadata cell after registration.");
        s.metadata=**slot;s.phase=P::reflected;break;}
    case P::reflected:{auto& call=select(K::reflected_factory,P::after_reflected);call.metadata=s.metadata;call.pool=s.pool;break;}
    case P::after_reflected:s.allocation=std::move(s.call.operation->allocation_result);clear();s.phase=P::result;break;
    case P::allocate:{
        const auto progress=s.allocator->resume(services?services->bootstrap:nullptr);check();if(!progress.completed)return progress;
        s.allocation=s.allocator->result();s.allocator.reset();check();
        if(!igb_bootstrap_address(s.allocation))s.phase=P::tail_2c;
        else{s.constructor=std::make_unique<IgbMetaFieldConstructorInvocation>(s.allocation,IgbMetaFieldConstructor::specialized,s.kind);s.phase=P::construct;}break;
    }
    case P::construct:{const auto progress=s.constructor->resume(services);check();if(!progress.completed)return progress;
        s.result=s.constructor->result();s.constructor.reset();check();s.phase=P::tail_2c;break;}
    case P::tail_2c:tail(0x2c,P::tail_24);break;
    case P::tail_defaults:{
        // Both concrete +2C bodies call the exact BLR667D0 and then reload
        // the field's physical vtable for +8C(NULL).
        const auto table=igb_bootstrap_read_word(s.allocation,0);if(!table)fail("Field post-constructor defaults dereference a null vtable.");
        s.value.clear();check();s.value.request.kind=IgbFieldOperationKind::field_virtual;s.value.request.field=igb_bootstrap_field(s.allocation);
        s.value.request.vtable_word=table;s.value.request.virtual_slot=0x8c;s.value.request.argument=0;
        if(services && services->fields)s.value.factory=services->fields->factory;s.phase=P::value_call;break;}
    case P::value_call:{const auto status=s.value.poll(services && services->fields?services->fields->factory:IgbFieldOperationsFactory{},check);
        if(!status.completed)return status;s.value.clear();check();s.phase=P::tail_24;break;}
    case P::tail_24:tail(0x24,P::result);break;
    case P::result:
        clear();if(igb_bootstrap_address(s.allocation)){s.result=igb_bootstrap_field(s.allocation);if(s.owner)s.owner->completed_allocations.push_back(s.allocation);}
        else s.result.reset();s.phase=P::done;return {true,0,{}};
    case P::call:{const auto progress=s.call.poll(services?services->factory:IgbMetaFieldFactory{},check);if(!progress.completed)return progress;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbMetaFieldFactory make_native_igb_reflected_meta_field_factory(std::shared_ptr<IgbFileFactoryServices> reflected,IgbMetaFieldFactory remaining){
    return [weak=std::weak_ptr(reflected),remaining=std::move(remaining)](const IgbMetaFieldCall& call)->std::shared_ptr<IgbMetaFieldOperation>{
        if(call.kind!=IgbMetaFieldServiceKind::reflected_factory)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Reflected descriptor services expired before selection.");
        auto invocation=std::make_shared<IgbReflectedFactoryInvocation>(call.metadata,call.pool);
        auto operation=std::make_shared<IgbMetaFieldOperation>();
        operation->resume=[selected,invocation,out=operation.get()]{const auto status=invocation->resume(selected);
            if(!status.completed)return status;const auto instance=invocation->result_instance();
            if(instance.storage){if(!instance.field)fail("Reflected descriptor factory returned another native object kind.");igb_object_validate_instance(instance);}
            out->allocation_result={instance.storage,instance.offset};return status;
        };return operation;
    };
}
} // namespace dw4
