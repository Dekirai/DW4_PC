#include "dw4/igb_named_type_defaults.hpp"

#include <bit>

namespace dw4 {
namespace {
using Kind=IgbNamedTypeKind;
[[noreturn]] void fail(const char* message){throw IgbNamedTypeDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::size_t index(Kind kind){const auto value=static_cast<std::size_t>(kind);if(value>=3)fail("Unknown named-type defaults profile.");return value;}
IgbRuntimeFieldDefaultSource word_source(std::uint32_t value){
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.resize(4);buffer->initialized.assign(4,1);
    for(unsigned i=0;i<4;++i)buffer->bytes[i]=static_cast<std::uint8_t>(value>>(24u-i*8u));return {buffer,0};
}
std::shared_ptr<IgbDecodeRuntimeField> field_at(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t position){
    if(!metadata || !metadata->fields)fail("Named-type defaults select through a null metadata field registry.");
    const auto backing=metadata->fields->backing;const auto slot=(position<<2u)>>2u;
    if(!backing || slot>=backing->size())fail("Named-type defaults reach an unavailable physical field slot.");return (*backing)[slot];
}
std::optional<IgbBootstrapPointer> field_storage(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Named-type defaults dereference a null field descriptor.");
    if(!field->physical_storage)return {};
    IgbBootstrapPointer pointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
    if(!pointer.storage || pointer.storage->faulted() || igb_bootstrap_field(pointer)!=field)
        fail("Named-type defaults lost their descriptor's physical identity.");return pointer;
}
std::optional<IgbBootstrapPointer> metadata_storage(const std::shared_ptr<IgbRuntimeMetaType>& metadata,
    const std::shared_ptr<IgbNamedTypeDefaultsServices>& services){
    std::optional<IgbBootstrapPointer> result;
    if(services)for(const auto& pointer:services->physical_metadata){
        if(igb_bootstrap_metadata(pointer)!=metadata)continue;
        if(pointer.storage->faulted())fail("Named-type defaults reached faulted metadata storage.");
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("Named-type metadata has conflicting physical identities.");result=pointer;
    }return result;
}
}
IgbNamedTypeDefaultsServices::IgbNamedTypeDefaultsServices(){
    auto& named=tables[index(Kind::named_object)];
    named.factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x800251ecu});
    named.names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{std::make_shared<const std::string>("_name")});
    named.globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561748}});
    named.offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8u});
    auto& thread=tables[index(Kind::thread)];
    thread.factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{
        0x80031378u,0x80031378u,0x80031378u,0x80031378u,0x800274ecu,0x800274ecu,0x80022a90u,0x800274ecu,0x800385e0u,0x800385e0u});
    thread.names=std::make_shared<IgbFieldNameTable>();thread.globals=std::make_shared<IgbFieldGlobalTable>();
    constexpr std::array labels={"_priority","_type","_scope","_state","_arg","_stack","_stackSize","_function","_isMaster","_userStack"};
    for(std::size_t i=0;i<labels.size();++i){thread.names->push_back(std::make_shared<const std::string>(labels[i]));
        thread.globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561508u+static_cast<std::uint32_t>(4*i)});}
    thread.offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{0x18u,0x0cu,0x10u,0x14u,0x1cu,0x20u,0x24u,0x28u,0x2cu,0x2du});
    for(unsigned i=0;i<4;++i)enum_default_sources[i]=word_source(i==0?0x80u:0u);
}
struct IgbNamedTypeDefaultsInvocation::Impl {
    enum class P {capture,batch,field,string_default,scalar_select,scalar_source,scalar_call,method,schema_setup,schema,redirect_owner,redirect_store,done};
    Kind kind{};P phase=P::capture;
    std::shared_ptr<IgbNamedTypeDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata,redirect_owner;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> binding;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldStringDefaultInvocation> string_default;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    IgbFieldOperationCall request;
    IgbFieldOperationsFactory factory;
    std::shared_ptr<IgbFieldOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::uint32_t first{},local{};bool running{},fault{};
};
IgbNamedTypeDefaultsInvocation::IgbNamedTypeDefaultsInvocation(Kind kind):impl_(std::make_unique<Impl>()){
    index(kind);impl_->kind=kind;if(kind==Kind::semaphore)impl_->phase=Impl::P::redirect_owner;
}
IgbNamedTypeDefaultsInvocation::~IgbNamedTypeDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->retained.push_back(s.metadata);if(s.redirect_owner)s.owner->retained.push_back(s.redirect_owner);
    if(s.field)s.owner->retained.push_back(s.field);if(s.binding && s.binding->storage)s.owner->retained.push_back(s.binding->storage);
    if(s.request.source && s.request.source->buffer)s.owner->retained.push_back(s.request.source->buffer);
}}
bool IgbNamedTypeDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbNamedTypeDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbNamedTypeDefaultsInvocation::resume(std::shared_ptr<IgbNamedTypeDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Named-type defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Named-type defaults were invalidated by a callback.");
        if(s.binding){const auto current=field_storage(s.field);
            if(!current || current->storage!=s.binding->storage || current->offset!=s.binding->offset)
                fail("Named-type defaults changed their captured descriptor allocation.");}};
    const auto pointer=[&]{const auto value=field_storage(s.field);if(value && !s.binding)s.binding=value;check();return value;};
    const auto field_services=[&]{return services && services->schema?services->schema->fields:nullptr;};
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        const auto cell=igb_named_type_metadata_slot(services?services->registration:nullptr,s.kind);
        if(!cell || !*cell)return pending("Named-type defaults require their represented canonical metadata.");
        s.metadata=**cell;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->tables[index(s.kind)].factories,s.kind==Kind::named_object?1u:10u);
        s.phase=P::batch;break;
    }
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::field;break;}
    case P::field:
        s.field=field_at(s.metadata,s.first+(s.local==4?6u:s.local));s.binding.reset();
        if(s.kind==Kind::named_object){s.string_default=std::make_unique<IgbFieldStringDefaultInvocation>(s.field,IgbDependencyName{});s.phase=P::string_default;}
        else{s.request={};if(s.local==4)s.request.source=word_source(0x1000);s.phase=P::scalar_select;}break;
    case P::string_default:{const auto status=s.string_default->resume(services?services->values:nullptr);check();if(!status.completed)return status;
        s.string_default.reset();check();s.field.reset();s.binding.reset();s.phase=P::schema_setup;break;}
    case P::scalar_select:{
        const auto physical=pointer();const auto table=physical?std::optional<std::uint32_t>{igb_bootstrap_read_word(*physical,0)}:s.field->vtable_word;
        if(!table)return pending("Named-type scalar default requires its descriptor's fresh vtable.");
        if(!*table)fail("Named-type scalar default dereferences a null vtable.");
        s.request.kind=IgbFieldOperationKind::field_virtual;s.request.field=s.field;s.request.vtable_word=*table;s.request.virtual_slot=0x8c;
        s.phase=P::scalar_source;break;
    }
    case P::scalar_source:
        if(s.local<4){if(!services || !services->enum_default_sources[s.local])return pending("Thread enum default requires its original static source binding.");
            s.request.source=services->enum_default_sources[s.local];s.request.source_address=0x8055d070u+4u*s.local;}
        if(const auto fields=field_services())s.factory=fields->factory;s.phase=P::scalar_call;break;
    case P::scalar_call:
        if(!s.factory){if(const auto fields=field_services())s.factory=fields->factory;}
        if(!s.factory)return pending("Named-type default requires its reached field-value factory.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Named-type default reached an unbound virtual+8C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Named-type default requires its retained scalar continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        clear();s.phase=s.local<4?P::method:P::schema_setup;break;
    case P::method:{
        constexpr std::array<std::uint32_t,4> getters={0x80023a70,0x80023b4c,0x80023b00,0x80023abc};
        if(const auto physical=pointer())igb_bootstrap_write_word(*physical,0x34,getters[s.local]);
        s.field->method_function=getters[s.local];s.binding.reset();s.field.reset();++s.local;s.phase=P::field;break;
    }
    case P::schema_setup:
        s.binding.reset();s.field.reset();
        if(!services)return pending("Named-type defaults require their final schema tables.");
        {const auto& tables=services->tables[index(s.kind)];s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,tables.names,tables.globals,tables.offsets,s.first);}
        s.phase=P::schema;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=s.kind==Kind::named_object?P::done:P::redirect_owner;break;}
    case P::redirect_owner:{
        const auto cell=igb_named_type_metadata_slot(services?services->registration:nullptr,s.kind);
        if(!cell || !*cell)return pending("Named-type defaults require their freshly reloaded redirect metadata.");
        s.redirect_owner=**cell;s.phase=P::redirect_store;break;
    }
    case P::redirect_store:{
        if(!s.redirect_owner)fail("Named-type defaults store a redirect through null reloaded metadata.");
        const auto redirect=s.kind==Kind::thread?0x80023e20u:0x80026160u;
        if(const auto physical=metadata_storage(s.redirect_owner,services))igb_bootstrap_write_word(*physical,0x3c,redirect);
        s.redirect_owner->factory_redirect=redirect;s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_named_type_defaults_factory(
    std::shared_ptr<IgbNamedTypeDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbTypeRegistrationCall& call)->std::shared_ptr<IgbTypeRegistrationOperation>{
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults ||
            (call.function!=0x80029de8 && call.function!=0x80023cfc && call.function!=0x8002614c))return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();if(!selected)fail("Named-type defaults services expired before selection.");
        const auto kind=call.function==0x80029de8?Kind::named_object:call.function==0x80023cfc?Kind::thread:Kind::semaphore;
        auto invocation=std::make_shared<IgbNamedTypeDefaultsInvocation>(kind);auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
