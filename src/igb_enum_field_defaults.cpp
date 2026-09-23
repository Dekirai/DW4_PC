#include "dw4/igb_enum_field_defaults.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbEnumFieldDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::shared_ptr<IgbDecodeRuntimeField> select_field(
    const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t index){
    if(!metadata)fail("Enum-field defaults select through null metadata.");
    const auto fields=metadata->fields;
    if(!fields)fail("Enum-field defaults select through a null live field registry.");
    const auto backing=fields->backing;
    // Actual658E4 uses slwi before lwzx. Bound the reached physical slot,
    // preserving high count bits that wrap out of the original byte offset.
    const auto slot=(index<<2u)>>2u;
    if(slot>=262144 || !backing || slot>=backing->size())
        fail("Enum-field defaults reach an unavailable physical field slot.");
    return (*backing)[slot];
}
}
IgbEnumFieldDefaultsServices::IgbEnumFieldDefaultsServices(){
    factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x800274ecu});
    names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_getMetaEnumFunction")});
    globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561c38u}});
    offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x34u});
}
struct IgbEnumFieldDefaultsInvocation::Impl {
    enum class P {capture,batch,select,clear_flag,schema_setup,schema,done};
    P phase=P::capture;
    std::shared_ptr<IgbEnumFieldDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> binding;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::uint32_t first{};
    bool running{},fault{};
};
IgbEnumFieldDefaultsInvocation::IgbEnumFieldDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbEnumFieldDefaultsInvocation::~IgbEnumFieldDefaultsInvocation(){auto& s=*impl_;
    if((!completed() || faulted()) && s.owner){
        if(s.metadata)s.owner->retained.push_back(s.metadata);
        if(s.field)s.owner->retained.push_back(s.field);
        if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
    }
}
bool IgbEnumFieldDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbEnumFieldDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbEnumFieldDefaultsInvocation::resume(std::shared_ptr<IgbEnumFieldDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Enum-field defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Enum-field defaults were invalidated by a callback.");};
    const auto schema_services=[&]{return services?services->schema:std::shared_ptr<IgbFieldSchemaServices>{};};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        const auto cell=igb_field_type_metadata_slot(services?services->registration:nullptr,IgbFieldTypeKind::enumeration);
        if(!cell || !*cell)return pending("Enum-field defaults require represented canonical metadata80561C30.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,1);
        s.phase=P::batch;break;
    }
    case P::batch:{
        const auto status=s.batch->resume(schema_services());check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::select;break;
    }
    case P::select:s.field=select_field(s.metadata,s.first);s.phase=P::clear_flag;break;
    case P::clear_flag:
        if(!s.field)fail("Enum-field defaults clear a byte through a null descriptor.");
        if(s.field->physical_storage){
            const auto& physical=*s.field->physical_storage;
            s.binding=IgbBootstrapPointer{physical.storage.lock(),physical.offset};
            if(!s.binding->storage || s.binding->storage->faulted() || igb_bootstrap_field(*s.binding)!=s.field)
                fail("Enum-field defaults reached an unavailable or changed physical descriptor identity.");
            if(igb_bootstrap_address(*s.binding)+0x26u==0)
                fail("Enum-field defaults clear through a null effective byte address.");
            igb_bootstrap_write_byte(*s.binding,0x26,std::uint8_t{0});
        }
        s.field->flag_26=0;s.phase=P::schema_setup;break;
    case P::schema_setup:
        if(!services)return pending("Enum-field defaults require their final schema tables.");
        s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);
        s.phase=P::schema;break;
    case P::schema:{
        const auto status=s.schema->resume(schema_services());check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_enum_field_defaults_factory(
    std::shared_ptr<IgbEnumFieldDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || call.function!=0x80031614u)
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Enum-field defaults services expired before selection.");
        auto invocation=std::make_shared<IgbEnumFieldDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
