#include "dw4/igb_field_type_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <bit>
#include <span>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldTypeDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
enum class Action {clear_persistent,short_value,byte_value,parent_type,enum_storage,memory_reference};
struct Step {std::uint32_t index;Action action;std::uint32_t value{};bool clear_after{};};
constexpr std::array base_steps={Step{0,Action::clear_persistent},Step{2,Action::short_value,0xffff,true},Step{3,Action::short_value,0xffff,true},
    Step{4,Action::clear_persistent},Step{5,Action::parent_type,0,true},Step{6,Action::enum_storage},Step{7,Action::memory_reference},
    Step{8,Action::byte_value,0},Step{10,Action::byte_value,1},Step{11,Action::byte_value,1},Step{12,Action::byte_value,0},
    Step{13,Action::byte_value,0},Step{14,Action::byte_value,0},Step{15,Action::byte_value,1},Step{16,Action::byte_value,2},Step{17,Action::clear_persistent}};
constexpr std::array object_steps={Step{0,Action::parent_type},Step{1,Action::byte_value,1}};
std::span<const Step> steps(IgbFieldTypeKind kind){if(kind==IgbFieldTypeKind::base)return base_steps;if(kind==IgbFieldTypeKind::object)return object_steps;return {};}
std::uint32_t count(IgbFieldTypeKind kind){return kind==IgbFieldTypeKind::base?18u:kind==IgbFieldTypeKind::reference?2u:3u;}
std::size_t index(IgbFieldTypeKind kind){const auto n=static_cast<std::size_t>(kind);if(n>=3)fail("Unknown field-type defaults profile.");return n;}
IgbFieldTypeDefaultsTables table(std::span<const std::uint32_t> factories,std::span<const char* const> names,
    std::span<const std::uint32_t> offsets,std::uint32_t first_global){
    IgbFieldTypeDefaultsTables result;result.factories=std::make_shared<IgbFieldFactoryTable>();result.names=std::make_shared<IgbFieldNameTable>();
    result.globals=std::make_shared<IgbFieldGlobalTable>();result.offsets=std::make_shared<IgbFieldOffsetTable>();
    for(std::size_t i=0;i<factories.size();++i){result.factories->push_back(factories[i]);result.names->push_back(std::make_shared<const std::string>(names[i]));
        result.globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),first_global+static_cast<std::uint32_t>(i)*4});result.offsets->push_back(offsets[i]);}
    return result;
}
struct FieldCall {
    IgbFieldOperationCall request;IgbFieldOperationsFactory factory;std::shared_ptr<IgbFieldOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const IgbFieldOperationsFactory& fallback,Check check){
        if(!factory)factory=fallback;if(!factory)return pending("Field-type scalar default requires its native virtual factory.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("Field-type scalar default reached an unbound virtual.");
        if(!callback)callback=operation->resume;if(!callback)return pending("Field-type scalar default requires its retained continuation.");
        const auto status=callback();check();return status;
    }
};
}
IgbFieldTypeDefaultsServices::IgbFieldTypeDefaultsServices(){
    constexpr std::array<std::uint32_t,18> factories={0x8002e1ac,0x800251ec,0x800259a8,0x800259a8,0x80021e10,0x8002879c,0x800274ec,0x8002ac34,
        0x80037b1c,0x800385e0,0x800385e0,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x800274ec};
    constexpr std::array<const char*,18> names={"_offset","_fieldName","_typeIndex","_internalIndex","_size","_parentMetaObject","_externalEnumStorage",
        "_default","_resetMethod","_isRunTime","_persistent","_shallowCopyMethod","_deepCopyMethod","_constructCopyMethod","_isAlikeExactlyMethod",
        "_isAlikeDeepMethod","_isAlikeShallowMethod","_invarianceFunction"};
    constexpr std::array<std::uint32_t,18> offsets={8,12,16,18,20,24,28,32,36,37,38,39,40,41,42,43,44,48};
    tables[0]=table(factories,names,offsets,0x805617c0);
    constexpr std::array<std::uint32_t,2> reference_factories={0x800385e0,0x800385e0},reference_offsets={0x34,0x35};
    constexpr std::array<const char*,2> reference_names={"_construct","_destruct"};
    tables[1]=table(reference_factories,reference_names,reference_offsets,0x8056168c);
    constexpr std::array<std::uint32_t,3> object_factories={0x8002879c,0x800385e0,0x800251ec},object_offsets={0x38,0x3c,0x40};
    constexpr std::array<const char*,3> object_names={"_metaObject","_refCounted","_poolName"};
    tables[2]=table(object_factories,object_names,object_offsets,0x805616f0);
}
struct IgbFieldTypeDefaultsInvocation::Impl {
    enum class P {capture,batch,select,scalar,scalar_call,parent,parent_result,finish_step,schema,done};
    IgbFieldTypeKind kind;
    std::shared_ptr<IgbFieldTypeDefaultsServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbMetadataLookupInvocation> lookup;
    FieldCall call;P phase=P::capture;std::uint32_t first{};std::size_t step{};bool running{},fault{};
};
IgbFieldTypeDefaultsInvocation::IgbFieldTypeDefaultsInvocation(IgbFieldTypeKind kind):impl_(std::make_unique<Impl>()){index(kind);impl_->kind=kind;}
IgbFieldTypeDefaultsInvocation::~IgbFieldTypeDefaultsInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.metadata)s.owner->unfinished.push_back(s.metadata);if(s.field)s.owner->unfinished.push_back(s.field);
    if(s.call.request.source && s.call.request.source->buffer)s.owner->unfinished.push_back(s.call.request.source->buffer);
}}
bool IgbFieldTypeDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldTypeDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldTypeDefaultsInvocation::resume(std::shared_ptr<IgbFieldTypeDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field-type defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field-type defaults continuation was invalidated.");};
    const auto fields=[&]{return services && services->schema?services->schema->fields:std::shared_ptr<IgbFieldOperationsServices>{};};
    const auto require_field=[&]{if(!s.field)fail("Field-type defaults dereference a null selected descriptor.");};
    try{for(;;){check();switch(s.phase){
    case P::capture:{
        if(!services || !services->registration)return pending("Field-type defaults require their canonical registration globals.");
        const auto slot=igb_field_type_metadata_slot(services->registration,s.kind);if(!slot || !*slot)return pending("Field-type defaults require their represented metadata global.");
        s.metadata=**slot;s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->tables[index(s.kind)].factories,count(s.kind));s.phase=P::batch;break;
    }
    case P::batch:{const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;s.batch.reset();check();s.phase=P::select;break;}
    case P::select:{
        const auto actions=steps(s.kind);if(s.step==actions.size()){
            if(!services)return pending("Field-type defaults require their original schema tables.");const auto& t=services->tables[index(s.kind)];
            s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,t.names,t.globals,t.offsets,s.first);s.phase=P::schema;break;
        }
        const auto& step=actions[s.step];const auto physical=((s.first+step.index)<<2u)>>2u;
        s.field=igb_runtime_field_at(s.metadata,std::bit_cast<std::int32_t>(physical));
        if(step.action==Action::short_value || step.action==Action::byte_value)s.phase=P::scalar;
        else if(step.action==Action::parent_type){s.lookup=std::make_unique<IgbMetadataLookupInvocation>(IgbMetadataLookup::allocate_if_null);s.phase=P::parent;}
        else{
            require_field();
            if(step.action==Action::clear_persistent)s.field->flag_26=0;
            else if(step.action==Action::enum_storage){s.field->flag_26=0;s.field->flag_28=std::uint8_t{1};}
            else{s.field->flag_35=std::uint8_t{1};s.field->flag_40=std::uint8_t{1};s.field->flag_41=std::uint8_t{1};s.field->flag_26=0;}
            s.phase=P::finish_step;
        }break;
    }
    case P::scalar:{
        require_field();if(!s.field->vtable_word)return pending("Field-type scalar default requires its selected field's fresh vtable.");
        const auto& step=steps(s.kind)[s.step];const unsigned width=step.action==Action::short_value?2u:1u;
        auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.resize(width);bytes->initialized.assign(width,1);
        for(unsigned i=0;i<width;++i)bytes->bytes[i]=static_cast<std::uint8_t>(step.value>>((width-1-i)*8));
        s.call.clear();check();s.call.request.kind=IgbFieldOperationKind::field_virtual;s.call.request.field=s.field;
        s.call.request.vtable_word=*s.field->vtable_word;s.call.request.virtual_slot=0x8c;s.call.request.source=IgbRuntimeFieldDefaultSource{bytes,0};
        if(const auto f=fields())s.call.factory=f->factory;s.phase=P::scalar_call;break;
    }
    case P::scalar_call:{const auto f=fields();const auto status=s.call.poll(f?f->factory:IgbFieldOperationsFactory{},check);if(!status.completed)return status;
        s.call.clear();check();s.phase=P::finish_step;break;}
    case P::parent:{const auto status=s.lookup->resume(services?services->metadata:nullptr);check();if(!status.completed)return status;s.phase=P::parent_result;break;}
    case P::parent_result:{const auto parent=s.lookup->result();s.lookup.reset();check();require_field();
        s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(parent);s.field->flag_3c=std::uint8_t{0};
        if(steps(s.kind)[s.step].clear_after)s.field->flag_26=0;
        s.field->flag_28=std::uint8_t{1};s.field->flag_27=std::uint8_t{1};s.phase=P::finish_step;break;}
    case P::finish_step:
        if(steps(s.kind)[s.step].clear_after && steps(s.kind)[s.step].action!=Action::parent_type){require_field();s.field->flag_26=0;}
        s.field.reset();++s.step;s.phase=P::select;break;
    case P::schema:{const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_field_type_defaults_factory(std::shared_ptr<IgbFieldTypeDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbFieldTypeDefaultsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::defaults || (call.function!=0x8002a764 && call.function!=0x800273ec && call.function!=0x80028ab4))
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Field-type defaults services expired before selection.");
        const auto kind=call.function==0x8002a764?IgbFieldTypeKind::base:call.function==0x800273ec?IgbFieldTypeKind::reference:IgbFieldTypeKind::object;
        auto invocation=std::make_shared<IgbFieldTypeDefaultsInvocation>(kind);auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
