#include "dw4/igb_field_copy.hpp"
#include "dw4/igb_stream.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldCopyError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;if(!factory)return pending("Field copy requires its selected native service.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("Field copy reached an unbound native service.");
        if(!callback)callback=operation->resume;if(!callback)return pending("Field copy requires its retained continuation.");
        const auto status=callback();check();return status;
    }
};
using CopyCall=Call<IgbFieldCopyCall,IgbFieldCopyFactory,IgbFieldCopyOperation>;
using NameCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
std::optional<std::uint32_t> name_address(const std::shared_ptr<IgbFieldValuesServices>& services,
    const std::shared_ptr<IgbInternedString>& owner){
    if(!owner)return 0;std::optional<std::uint32_t> address;
    if(services)for(const auto& binding:services->strings)if(binding.owner==owner){
        if(!binding.address || (address && *address!=binding.address))fail("Copied name has conflicting arithmetic identities.");
        address=binding.address;
    }return address;
}
std::optional<std::uint32_t> type_address(const std::shared_ptr<IgbFieldValuesServices>& services,
    const std::weak_ptr<IgbRuntimeMetaType>& owner){
    if(igb_field_declaring_type_is_null(owner))return 0;std::optional<std::uint32_t> address;
    const auto lists=services && services->fields?services->fields->pointer_lists:nullptr;
    if(lists)for(const auto& binding:lists->values)if(binding.metadata &&
        !owner.owner_before(binding.metadata) && !binding.metadata.owner_before(owner)){
        if(binding.field || !binding.address || (address && *address!=binding.address))fail("Copied declaring type has conflicting arithmetic identities.");
        address=binding.address;
    }return address;
}
std::optional<std::uint32_t> buffer_address(const std::shared_ptr<IgbFieldValuesServices>& services,
    const IgbRuntimeFieldDefaultSource& source){
    if(!source.buffer)return 0;std::optional<std::uint32_t> address;
    if(services)for(const auto& binding:services->buffers)if(binding.buffer==source.buffer){
        const auto candidate=binding.base_address+source.offset;
        if(!candidate || (address && *address!=candidate))fail("Copied buffer has conflicting arithmetic identities.");address=candidate;
    }return address;
}
void write_byte(const IgbRuntimeFieldDefaultSource& destination,std::uint32_t relative,std::uint8_t value){
    const auto at=std::uint64_t(destination.offset)+relative;
    if(!destination.buffer || destination.buffer->released || at>=destination.buffer->bytes.size() || at>=destination.buffer->initialized.size())
        fail("Field copy writes unavailable default storage.");
    destination.buffer->bytes[static_cast<std::size_t>(at)]=value;destination.buffer->initialized[static_cast<std::size_t>(at)]=1;
}
}
struct IgbFieldCopyInvocation::Impl {
    enum class P {entry,reset,flag,core,metadata,known_metadata,after_metadata,prefix,allocation_pool,pool_index,
        table,offset,flag25,type_ordinal,size,ordinal,global,name_retain,name_old,name_release,name_capture,name_publish,
        declaring,declaring_publish,word30,bytes,allocate,allocate_pool,publish,copy_capture,copy_source,copy,call,name_call,done};
    IgbBootstrapPointer destination;std::shared_ptr<IgbDecodeRuntimeField> source,field;
    std::shared_ptr<IgbFieldCopyServices> owner;
    std::unique_ptr<IgbBootstrapBaseInvocation> reset;
    std::shared_ptr<IgbRuntimeMetaType> metadata;std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbInternedString> incoming,old,name;
    std::optional<std::uint32_t> name_pointer,source_address,destination_address;
    std::weak_ptr<IgbRuntimeMetaType> declaring;
    IgbRuntimeFieldDefaultSource source_bytes,destination_bytes,incoming_bytes;
    CopyCall call;NameCall name_call;
    P phase=P::entry,after=P::done;
    unsigned byte_index{};std::uint32_t count{},returned{};
    bool running{},fault{};
};
IgbFieldCopyInvocation::IgbFieldCopyInvocation(IgbBootstrapPointer destination,std::shared_ptr<IgbDecodeRuntimeField> source)
    :impl_(std::make_unique<Impl>()){impl_->destination=std::move(destination);impl_->source=std::move(source);}
IgbFieldCopyInvocation::~IgbFieldCopyInvocation(){auto& s=*impl_;if(!completed() && s.owner){
    if(s.destination.storage)s.owner->unfinished.push_back(s.destination.storage);
    if(s.source)s.owner->unfinished.push_back(s.source);
    if(s.incoming)s.owner->unfinished.push_back(s.incoming);
    if(s.old)s.owner->unfinished.push_back(s.old);
    if(s.incoming_bytes.buffer)s.owner->unfinished.push_back(s.incoming_bytes.buffer);
    if(s.call.operation && s.call.operation->buffer_result.buffer)s.owner->unfinished.push_back(s.call.operation->buffer_result.buffer);
}}
bool IgbFieldCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldCopyInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbDecodeRuntimeField> IgbFieldCopyInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete base field copy has no result.");return impl_->field;
}
MessengerCallResult IgbFieldCopyInvocation::resume(std::shared_ptr<IgbFieldCopyServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbFieldCopyServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Field copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.destination),{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault || (s.field && s.field->construction_fault_))fail("Field copy continuation was invalidated.");
        if(s.field && (igb_bootstrap_field(s.destination)!=s.field || !s.field->physical_storage ||
            s.field->physical_storage->storage.lock()!=s.destination.storage || s.field->physical_storage->offset!=s.destination.offset))
            fail("Field copy destination changed its canonical physical binding.");};
    const auto source=[&]()->IgbDecodeRuntimeField&{if(!s.source)fail("Field copy dereferences a null source descriptor.");return *s.source;};
    const auto bootstrap=[&]{return services && services->fields?services->fields->bootstrap:nullptr;};
    const auto values=[&]{return services?services->values:nullptr;};
    const auto select=[&](K kind,P after)->IgbFieldCopyCall&{
        s.call.clear();check();s.call.request.kind=kind;s.call.request.object=s.destination;s.call.request.field=s.field;
        s.call.factory=services?services->factory:IgbFieldCopyFactory{};s.after=after;s.phase=P::call;return s.call.request;
    };
    const auto word=[&](std::uint32_t at,std::uint32_t value){igb_bootstrap_write_word(s.destination,at,value);};
    const auto half=[&](std::uint32_t at,std::uint16_t value){igb_bootstrap_write_halfword(s.destination,at,value);};
    const auto byte=[&](std::uint32_t at,std::uint8_t value){igb_bootstrap_write_byte(s.destination,at,value);};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        word(0,0x804717f0);s.field=igb_bootstrap_field(s.destination);
        if(s.field->physical_storage && (s.field->physical_storage->storage.lock()!=s.destination.storage ||
            s.field->physical_storage->offset!=s.destination.offset))fail("Copied field has a different physical identity.");
        s.field->physical_storage=IgbFieldStorageBinding{s.destination.storage,s.destination.offset};
        s.reset=std::make_unique<IgbBootstrapBaseInvocation>(s.destination,false);s.phase=P::reset;break;
    case P::reset:{const auto status=s.reset->resume(bootstrap());check();if(!status.completed)return status;
        s.reset.reset();check();s.phase=P::flag;break;}
    case P::flag:{const auto flag=static_cast<std::uint8_t>((source().reference_word>>16u)&0x80u);
        const auto old=igb_bootstrap_read_byte(s.destination,5);byte(5,static_cast<std::uint8_t>((old&0x7fu)|flag));s.phase=P::core;break;}
    case P::core:{const auto b=bootstrap();
        if(!b || !b->runtime || !b->runtime->singleton)return pending("Base copy requires ArkCore80562100.");
        if(!*b->runtime->singleton)fail("Base copy dereferences a null ArkCore.");
        s.phase=(*b->runtime->singleton)->enabled?P::metadata:P::table;break;}
    case P::metadata:{const auto table=igb_bootstrap_read_word(s.destination,0);
        if(!table)fail("Base copy dereferences a null vtable.");
        if(table==0x804717f0)s.phase=P::known_metadata;else select(K::base_metadata,P::after_metadata).vtable_word=table;break;}
    case P::known_metadata:{const auto b=bootstrap();
        if(!b || !b->object_metadata || !b->object_metadata->has_value())return pending("Base copy requires metadata805622A4.");
        s.metadata=**b->object_metadata;s.phase=P::prefix;break;}
    case P::after_metadata:s.metadata=s.call.operation->metadata_result;s.call.clear();check();s.phase=P::prefix;break;
    case P::prefix:
        if(!s.metadata)fail("Base copy dereferences null allocation metadata.");
        if(!s.metadata->allocation_prefix)return pending("Base copy requires metadata allocation prefix+20.");
        select(K::allocation_pool,P::allocation_pool).address=igb_bootstrap_address(s.destination)-*s.metadata->allocation_prefix;break;
    case P::allocation_pool:s.pool=s.call.operation->pool_result;s.call.clear();check();select(K::pool_index,P::pool_index).pool=s.pool;break;
    case P::pool_index:s.call.clear();check();byte(4,static_cast<std::uint8_t>(s.returned));s.phase=P::table;break;
    case P::table:word(0,0x80471914);igb_meta_field_set_name(s.destination,{},0);s.phase=P::offset;break;
    case P::offset:word(8,source().offset);s.field->offset=source().offset;s.phase=P::flag25;break;
    case P::flag25:byte(0x25,source().flag_25);s.field->flag_25=source().flag_25;s.phase=P::type_ordinal;break;
    case P::type_ordinal:
        if(!source().type_ordinal)return pending("Base copy requires source type ordinal+10.");
        half(0x10,*source().type_ordinal);s.field->type_ordinal=*source().type_ordinal;s.phase=P::size;break;
    case P::size:igb_field_store_size(s.field,source().size);s.phase=P::ordinal;break;
    case P::ordinal:half(0x12,source().ordinal);s.field->ordinal=source().ordinal;s.phase=P::global;break;
    case P::global:{
        if(!source().global_slot)return pending("Base copy requires source global pointer+1C.");
        auto address=source().global_slot_address;
        const auto null=igb_field_global_slot_is_null(*source().global_slot);
        if(!address && null)address=0;
        if(!address)return pending("Base copy requires source global pointer arithmetic identity.");
        if((*address==0)!=null)fail("Copied global pointer and canonical identity disagree.");
        word(0x1c,*address);s.field->global_slot=source().global_slot;s.field->global_slot_address=*address;s.phase=P::name_retain;break;}
    case P::name_retain:
        if(!source().name_string)return pending("Base copy requires source name pointer+0C.");
        s.incoming=*source().name_string;
        if(s.incoming){if(s.incoming->released)fail("Base copy retains a released source string.");++s.incoming->references;}
        s.phase=P::name_old;break;
    case P::name_old:
        if(!s.field->name_string)return pending("Base copy requires the live old name pointer.");s.old=*s.field->name_string;
        if(s.old){if(s.old->released)fail("Base copy releases an unavailable string header.");--s.old->references;}
        s.phase=s.old && s.old->references==0?P::name_release:P::name_capture;break;
    case P::name_release:
        s.name_call.request.kind=IgbDependenciesServiceKind::release_string;s.name_call.request.string=s.old;
        s.name_call.request.pool=s.old->container;s.phase=P::name_call;break;
    case P::name_call:{const auto deps=services && services->fields?services->fields->dependencies:nullptr;
        const auto status=s.name_call.poll(deps?deps->factory:IgbDependenciesFactory{},check);if(!status.completed)return status;
        s.name_call.clear();check();s.phase=P::name_capture;break;}
    case P::name_capture:
        if(!source().name_string)return pending("Base copy requires its reloaded source name.");
        s.name=*source().name_string;s.name_pointer=source().name_string_address;s.phase=P::name_publish;break;
    case P::name_publish:
        if(!s.name_pointer)s.name_pointer=name_address(values(),s.name);
        if(!s.name_pointer)return pending("Base copy requires the captured name's arithmetic address.");
        igb_meta_field_set_name(s.destination,s.name,*s.name_pointer);
        if(s.incoming && s.incoming!=s.name)s.field->overwritten_constructor_owners_.push_back(s.incoming);
        if(s.old)s.field->overwritten_constructor_owners_.push_back(s.old);
        s.phase=P::declaring;break;
    case P::declaring:
        if(!source().declaring_type)return pending("Base copy requires source declaring type+18.");
        s.declaring=*source().declaring_type;s.phase=P::declaring_publish;break;
    case P::declaring_publish:{const auto address=type_address(values(),s.declaring);
        if(!address)return pending("Base copy requires the captured declaring type's arithmetic address.");
        word(0x18,*address);s.field->declaring_type=s.declaring;s.phase=P::word30;break;}
    case P::word30:
        if(!source().word_30)return pending("Base copy requires source word+30.");
        word(0x30,*source().word_30);s.field->word_30=*source().word_30;s.phase=P::bytes;break;
    case P::bytes:{
        constexpr std::array<std::uint32_t,7> offsets={0x24,0x27,0x28,0x29,0x2a,0x2b,0x2c};
        using Member=std::optional<std::uint8_t> IgbDecodeRuntimeField::*;
        constexpr std::array<Member,7> members={&IgbDecodeRuntimeField::default_mode,&IgbDecodeRuntimeField::flag_27,
            &IgbDecodeRuntimeField::flag_28,&IgbDecodeRuntimeField::flag_29,&IgbDecodeRuntimeField::flag_2a,
            &IgbDecodeRuntimeField::flag_2b,&IgbDecodeRuntimeField::flag_2c};
        if(s.byte_index<offsets.size()){
            const auto member=members[s.byte_index];const auto value=source().*member;
            if(!value)return pending("Base copy requires its next source flag byte.");
            byte(offsets[s.byte_index],*value);s.field.get()->*member=*value;++s.byte_index;
        }else{byte(0x26,source().flag_26);s.field->flag_26=source().flag_26;s.phase=P::allocate;}break;}
    case P::allocate:s.count=s.field->size;select(K::tagged_pool,P::allocate_pool).handle=s.field->reference_word>>24u;break;
    case P::allocate_pool:s.pool=s.call.operation->pool_result;s.call.clear();check();
        if(!s.pool)fail("Base copy default allocation dereferences a null tagged pool.");
        {auto& call=select(K::allocate_default,P::publish);call.pool=s.pool;call.bytes=s.count;}break;
    case P::publish:{s.incoming_bytes=s.call.operation->buffer_result;const auto address=s.call.operation->buffer_address;
        s.call.clear();check();igb_field_store_default(s.field,s.incoming_bytes,address);s.phase=P::copy_capture;break;}
    case P::copy_capture:
        if(!s.field->default_bytes)return pending("Base copy requires its reloaded destination default pointer.");
        s.destination_bytes=*s.field->default_bytes;s.destination_address=s.field->default_bytes_address;s.phase=P::copy_source;break;
    case P::copy_source:
        if(!source().default_bytes)return pending("Base copy requires its reloaded source default pointer.");
        s.source_bytes=*source().default_bytes;s.source_address=source().default_bytes_address;s.count=s.field->size;s.phase=P::copy;break;
    case P::copy:{
        if(!s.count){s.phase=P::done;break;}
        if(!s.source_address)s.source_address=buffer_address(values(),s.source_bytes);
        if(!s.destination_address)s.destination_address=buffer_address(values(),s.destination_bytes);
        if(s.source_bytes.buffer && s.source_bytes.buffer==s.destination_bytes.buffer){
            if(s.source_address && !s.destination_address)s.destination_address=*s.source_address-s.source_bytes.offset+s.destination_bytes.offset;
            if(s.destination_address && !s.source_address)s.source_address=*s.destination_address-s.destination_bytes.offset+s.source_bytes.offset;
            if(s.source_address && s.destination_address && *s.source_address-s.source_bytes.offset!=*s.destination_address-s.destination_bytes.offset)
                fail("Copied overlapping buffers have conflicting arithmetic identities.");
        }
        if(!s.source_address || !s.destination_address)return pending("Base copy requires both captured default-buffer addresses.");
        if((*s.source_address==0)!=(!s.source_bytes.buffer) || (*s.destination_address==0)!=(!s.destination_bytes.buffer))
            fail("Copied default pointers and physical storage disagree.");
        const bool backwards=*s.source_address<*s.destination_address;
        for(std::uint32_t i=0;i<s.count;++i){const auto relative=backwards?s.count-1u-i:i;
            const auto value=igb_buffer_bytes(s.source_bytes.buffer,static_cast<std::uint64_t>(s.source_bytes.offset)+relative,1).front();
            write_byte(s.destination_bytes,relative,value);
        }s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbFieldCopyFactory{},check);
        if(!status.completed)return status;s.returned=status.value;s.phase=s.after;break;}
    case P::done:return {true,igb_bootstrap_address(s.destination),{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
