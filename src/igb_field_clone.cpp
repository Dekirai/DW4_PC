#include "dw4/igb_field_clone.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldCloneError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
struct CloneProfile {std::uint32_t wrapper,body;};
CloneProfile profile(IgbMetaFieldKind kind){
    switch(kind){
    case IgbMetaFieldKind::memory_reference:return {0x8002ae84,0x8002aeac};
    case IgbMetaFieldKind::signed_byte:return {0x80037d00,0x80037d28};
    case IgbMetaFieldKind::boolean:return {0x80038800,0x80038828};
    default:fail("Unsupported field clone profile.");
    }
}
bool null_element(const std::weak_ptr<IgbDecodeRuntimeField>& field){const std::weak_ptr<IgbDecodeRuntimeField> empty;
    return !field.owner_before(empty) && !empty.owner_before(field);}
std::optional<std::uint32_t> element_address(const std::weak_ptr<IgbDecodeRuntimeField>& element,
    const std::shared_ptr<IgbFieldCloneServices>& services){
    if(null_element(element))return 0;
    const auto owner=element.lock();if(!owner)return {};
    std::optional<std::uint32_t> address;
    const auto lists=services && services->values && services->values->fields?services->values->fields->pointer_lists:nullptr;
    if(lists)for(const auto& value:lists->values)if(value.field==owner){
        if(value.metadata || value.address==0 || (address && *address!=value.address))fail("Cloned element has conflicting arithmetic pointer identities.");
        address=value.address;
    }
    return address;
}
struct Call {
    IgbFieldCloneCall request;IgbFieldCloneFactory factory;std::shared_ptr<IgbFieldCloneOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const IgbFieldCloneFactory& fallback,Check check){
        if(!factory)factory=fallback;if(!factory)return pending("Field clone requires its selected registration or reflected-copy operation.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("Field clone reached an unbound original operation.");
        if(!callback)callback=operation->resume;if(!callback)return pending("Field clone requires its retained operation continuation.");
        const auto status=callback();check();return status;
    }
};
}
struct IgbFieldCloneConstructorInvocation::Impl {
    enum class P {bind,base,scalar_table,reference_table,construct,destruct,memory_table,clear_48,clear_4c,flag_42,
        element,element_store,word_38,mode,flag_41,flag_40,word_44,done};
    IgbBootstrapPointer destination;std::shared_ptr<IgbDecodeRuntimeField> source,field;
    std::shared_ptr<IgbFieldCloneServices> owner;std::unique_ptr<IgbFieldCopyInvocation> base;
    std::shared_ptr<void> token=std::make_shared<int>(0);std::weak_ptr<IgbDecodeRuntimeField> element;
    IgbMetaFieldKind kind=IgbMetaFieldKind::memory_reference;
    P phase=P::bind;bool owns{},running{},fault{};
};
IgbFieldCloneConstructorInvocation::IgbFieldCloneConstructorInvocation(IgbBootstrapPointer destination,
    std::shared_ptr<IgbDecodeRuntimeField> source,IgbMetaFieldKind kind):impl_(std::make_unique<Impl>()){
    profile(kind);impl_->destination=std::move(destination);impl_->source=std::move(source);impl_->kind=kind;
}
IgbFieldCloneConstructorInvocation::~IgbFieldCloneConstructorInvocation(){auto& s=*impl_;if(s.owns && !completed()){
    s.field->construction_fault_=true;if(s.owner)s.owner->unfinished.push_back(s.destination.storage);
}}
bool IgbFieldCloneConstructorInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldCloneConstructorInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbFieldCloneConstructorInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete field copy constructor has no result.");return impl_->field;
}
MessengerCallResult IgbFieldCloneConstructorInvocation::resume(std::shared_ptr<IgbFieldCloneServices> services){
    auto& s=*impl_;using P=Impl::P;
    const auto invalidate=[&]{s.fault=true;if(s.owns)s.field->construction_fault_=true;};
    if(s.running || s.fault){invalidate();fail("Field copy constructor requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.destination),{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{
        if(s.fault || (s.owns && s.field->construction_fault_))fail("Field copy constructor was invalidated.");
        if(s.owns && igb_bootstrap_field(s.destination)!=s.field)fail("Field copy constructor lost its destination binding.");
        if(s.owns && (!s.field->physical_storage || s.field->physical_storage->storage.lock()!=s.destination.storage ||
            s.field->physical_storage->offset!=s.destination.offset))fail("Field copy constructor lost its physical storage identity.");
    };
    const auto source=[&]()->IgbDecodeRuntimeField&{if(!s.source)fail("Field copy constructor dereferences a null source.");return *s.source;};
    const auto byte=[&](std::uint32_t at,std::uint8_t value){igb_bootstrap_write_byte(s.destination,at,value);};
    const auto word=[&](std::uint32_t at,std::uint32_t value){igb_bootstrap_write_word(s.destination,at,value);};
    try{for(;;){check();switch(s.phase){
    case P::bind:
        s.field=igb_bootstrap_field(s.destination);
        if(s.field->construction_fault_ || !s.field->construction_active_.expired())fail("Destination field is faulted or already constructing.");
        if(s.field->physical_storage && (s.field->physical_storage->storage.lock()!=s.destination.storage ||
            s.field->physical_storage->offset!=s.destination.offset))fail("Destination field has another physical allocation identity.");
        s.field->physical_storage=IgbFieldStorageBinding{s.destination.storage,s.destination.offset};
        s.field->construction_active_=s.token;s.owns=true;
        s.base=std::make_unique<IgbFieldCopyInvocation>(s.destination,s.source);s.phase=P::base;break;
    case P::base:{const auto status=s.base->resume(services?services->copy:nullptr);check();if(!status.completed)return status;
        s.base.reset();check();s.phase=s.kind==IgbMetaFieldKind::memory_reference?P::reference_table:P::scalar_table;break;}
    case P::scalar_table:
        word(0,igb_meta_field_profile(s.kind).vtable);
        s.field->construction_active_.reset();s.owns=false;s.phase=P::done;return {true,igb_bootstrap_address(s.destination),{}};
    case P::reference_table:word(0,0x80471384);s.phase=P::construct;break;
    case P::construct:
        if(!source().flag_34)return pending("Field copy requires source construct flag+34.");
        byte(0x34,*source().flag_34);s.field->flag_34=*source().flag_34;s.phase=P::destruct;break;
    case P::destruct:
        if(!source().flag_35)return pending("Field copy requires source destruct flag+35.");
        byte(0x35,*source().flag_35);s.field->flag_35=*source().flag_35;s.phase=P::memory_table;break;
    case P::memory_table:word(0,0x80476088);s.phase=P::clear_48;break;
    case P::clear_48:word(0x48,0);s.field->word_48=0;s.phase=P::clear_4c;break;
    case P::clear_4c:word(0x4c,0);s.field->word_4c=0;s.phase=P::flag_42;break;
    case P::flag_42:
        if(!source().flag_42)return pending("Field copy requires source element-reference flag+42.");
        byte(0x42,*source().flag_42);s.field->flag_42=*source().flag_42;s.phase=P::element;break;
    case P::element:
        if(!source().element_field)return pending("Field copy requires source element pointer+3C.");
        s.element=*source().element_field;s.phase=P::element_store;break;
    case P::element_store:{
        const auto address=element_address(s.element,services);
        if(!address)return pending("Field copy requires its captured element's arithmetic pointer binding.");
        word(0x3c,*address);s.field->element_field=s.element;s.phase=P::word_38;break;
    }
    case P::word_38:
        if(!source().word_38)return pending("Field copy requires source memory extent+38.");
        word(0x38,*source().word_38);s.field->word_38=*source().word_38;s.phase=P::mode;break;
    case P::mode:
        if(!source().default_mode)return pending("Field copy requires source default mode+24.");
        byte(0x24,*source().default_mode);s.field->default_mode=*source().default_mode;s.phase=P::flag_41;break;
    case P::flag_41:
        if(!source().flag_41)return pending("Field copy requires source memory flag+41.");
        byte(0x41,*source().flag_41);s.field->flag_41=*source().flag_41;s.phase=P::flag_40;break;
    case P::flag_40:
        if(!source().flag_40)return pending("Field copy requires source memory flag+40.");
        byte(0x40,*source().flag_40);s.field->flag_40=*source().flag_40;s.phase=P::word_44;break;
    case P::word_44:
        if(!source().word_44)return pending("Field copy requires source memory word+44.");
        word(0x44,*source().word_44);s.field->word_44=*source().word_44;
        s.field->construction_active_.reset();s.owns=false;s.phase=P::done;return {true,igb_bootstrap_address(s.destination),{}};
    case P::done:return {true,igb_bootstrap_address(s.destination),{}};
    }}}catch(...){invalidate();throw;}
}
struct IgbFieldCloneInvocation::Impl {
    enum class P {gate,registration,reflected,after_reflected,allocate,construct,result,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> source,result;std::uint32_t argument{};
    IgbMetaFieldKind kind=IgbMetaFieldKind::memory_reference;
    std::shared_ptr<IgbFieldCloneServices> owner;IgbBootstrapPointer allocation;
    std::unique_ptr<IgbRawAllocationInvocation> allocator;
    std::unique_ptr<IgbFieldCloneConstructorInvocation> constructor;
    std::unique_ptr<IgbRegistrationCallbacksInvocation> registration;
    Call call;P phase=P::gate,after=P::done;bool running{},fault{};
};
IgbFieldCloneInvocation::IgbFieldCloneInvocation(std::shared_ptr<IgbDecodeRuntimeField> source,std::uint32_t argument,
    IgbMetaFieldKind kind):impl_(std::make_unique<Impl>()){
    profile(kind);impl_->source=std::move(source);impl_->argument=argument;impl_->kind=kind;
}
IgbFieldCloneInvocation::~IgbFieldCloneInvocation(){auto& s=*impl_;if(!completed() && s.owner && s.allocation.storage)s.owner->unfinished.push_back(s.allocation.storage);}
bool IgbFieldCloneInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldCloneInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbFieldCloneInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete field clone has no result.");return impl_->result;
}
IgbBootstrapPointer IgbFieldCloneInvocation::allocation()const{
    if(!completed() || faulted())fail("Incomplete field clone has no allocation result.");return impl_->allocation;
}
MessengerCallResult IgbFieldCloneInvocation::resume(std::shared_ptr<IgbFieldCloneServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbFieldCloneServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field clone requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.allocation),{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field clone continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after,std::uint32_t function){clear();s.call.request.kind=kind;s.call.request.source=s.source;
        s.call.request.function=function;s.call.request.argument=s.argument;s.call.factory=services?services->factory:IgbFieldCloneFactory{};
        s.after=after;s.phase=P::call;};
    const auto fields=[&]{return services?services->fields:nullptr;};
    try{for(;;){check();switch(s.phase){
    case P::gate:{const auto f=fields();
        if(!f || !f->bootstrap || !f->bootstrap->runtime || !f->bootstrap->runtime->singleton)return pending("Field clone requires represented ArkCore80562100.");
        if(!*f->bootstrap->runtime->singleton)fail("Field clone dereferences a null ArkCore.");
        if((*f->bootstrap->runtime->singleton)->enabled)s.phase=P::registration;
        else{s.allocator=std::make_unique<IgbRawAllocationInvocation>(igb_meta_field_profile(s.kind).bytes,0,nullptr);s.phase=P::allocate;}break;
    }
    case P::registration:{const auto f=fields();
        if(s.registration || (f && f->callbacks)){
            if(!s.registration)s.registration=std::make_unique<IgbRegistrationCallbacksInvocation>(profile(s.kind).body);
            const auto status=s.registration->resume(f?f->callbacks:nullptr);check();if(!status.completed)return status;
            s.registration.reset();check();s.phase=P::reflected;
        }else select(K::register_type,P::reflected,profile(s.kind).wrapper);break;
    }
    case P::reflected:select(K::reflected_clone,P::after_reflected,0x800680b4);break;
    case P::after_reflected:s.allocation=std::move(s.call.operation->allocation_result);clear();s.phase=P::result;break;
    case P::allocate:{const auto f=fields();const auto status=s.allocator->resume(f?f->bootstrap:nullptr);check();if(!status.completed)return status;
        s.allocation=s.allocator->result();s.allocator.reset();check();
        if(igb_bootstrap_address(s.allocation)){s.constructor=std::make_unique<IgbFieldCloneConstructorInvocation>(s.allocation,s.source,s.kind);s.phase=P::construct;}
        else s.phase=P::result;break;
    }
    case P::construct:{const auto status=s.constructor->resume(services);check();if(!status.completed)return status;
        s.result=s.constructor->result();s.constructor.reset();check();s.phase=P::result;break;}
    case P::result:
        clear();if(igb_bootstrap_address(s.allocation)){s.result=igb_bootstrap_field(s.allocation);if(s.owner)s.owner->completed_allocations.push_back(s.allocation);}
        else s.result.reset();s.phase=P::done;return {true,igb_bootstrap_address(s.allocation),{}};
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbFieldCloneFactory{},check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,igb_bootstrap_address(s.allocation),{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbListDefaultsFactory make_native_igb_list_field_clone_factory(std::shared_ptr<IgbFieldCloneServices> services,IgbListDefaultsFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbListDefaultsCall& call)->std::shared_ptr<IgbListDefaultsOperation>{
        if(call.kind!=IgbListDefaultsServiceKind::clone_field ||
            (call.vtable_word!=0x80476088 && call.vtable_word!=0x80473f24 && call.vtable_word!=0x80473e30) || call.virtual_slot!=0x54)
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Field clone services expired before operation selection.");
        const auto kind=call.vtable_word==0x80473f24?IgbMetaFieldKind::signed_byte:
            call.vtable_word==0x80473e30?IgbMetaFieldKind::boolean:IgbMetaFieldKind::memory_reference;
        auto invocation=std::make_shared<IgbFieldCloneInvocation>(call.field,call.argument,kind);auto operation=std::make_shared<IgbListDefaultsOperation>();
        operation->resume=[invocation,selected,out=operation.get()]{const auto status=invocation->resume(selected);
            if(status.completed){out->field_result=invocation->result();out->field_address=igb_bootstrap_address(invocation->allocation());}return status;};return operation;
    };
}
} // namespace dw4
