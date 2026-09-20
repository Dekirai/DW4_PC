#include "dw4/igb_metadata_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <array>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataDefaultsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
constexpr std::array<std::uint32_t,25> factories={
    0x8002e1ac,0x8002e1ac,0x8002e1ac,0x8002e1ac,0x800385e0,0x800385e0,0x800385e0,0x800251ec,
    0x8002e1ac,0x8002e1ac,0x8002879c,0x8002e1ac,0x8002879c,0x8002879c,0x8002879c,0x800274ec,
    0x800274ec,0x8002879c,0x8002e1ac,0x800274ec,0x800274ec,0x800274ec,0x80021e10,0x800274ec,0x800251ec};
constexpr std::array<const char*,25> names={"_id","_index","_majorVersion","_minorVersion","_volatile","_persistent",
    "_abstractState","_name","_runTimeSize","_validity","_metaFields","_instanceCount","_instances","_children",
    "_parent","_abstractProxy","_writeProxy","_templateObject","_sizeofSize","_setupFunction","_cleanupFunction",
    "_getMetaObjectFunction","_requiredAlignment","_vTablePointer","_poolName"};
constexpr std::array<std::uint32_t,25> offsets={8,12,16,20,24,25,26,28,32,36,40,44,48,52,56,60,64,68,72,76,80,84,88,92,96};
enum class Action {integer,boolean,string,short_integer,flags,field_list,metadata_list,metadata,object};
struct Step {std::uint32_t index;Action action;std::uint32_t value;bool clear_flag;};
constexpr std::array steps={
    Step{0,Action::integer,0xffffffffu,true},Step{1,Action::integer,0xffffffffu,true},Step{2,Action::integer,1,false},
    Step{5,Action::boolean,1,false},Step{7,Action::string,0,false},Step{8,Action::integer,0xffffffffu,true},
    Step{9,Action::flags,0,true},Step{10,Action::field_list,0,false},Step{11,Action::flags,0,true},
    Step{12,Action::metadata_list,0,true},Step{13,Action::metadata_list,0,true},Step{14,Action::metadata,0,true},
    Step{15,Action::flags,0,true},Step{16,Action::flags,0,true},Step{17,Action::object,0,true},
    Step{18,Action::integer,0xffffffffu,true},Step{19,Action::flags,0,true},Step{20,Action::flags,0,true},
    Step{21,Action::flags,0,true},Step{22,Action::short_integer,4,true},Step{23,Action::flags,0,true}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        if(!factory)factory=fallback;
        if(!factory)return pending("Metadata defaults require their reached operation factory.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Metadata defaults reached an unbound original operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Metadata defaults require their selected operation continuation.");
        const auto status=callback();check();return status;
    }
};
}
IgbMetadataDefaultsServices::IgbMetadataDefaultsServices(){
    this->factories=std::make_shared<IgbFieldFactoryTable>();this->names=std::make_shared<IgbFieldNameTable>();
    globals=std::make_shared<IgbFieldGlobalTable>();this->offsets=std::make_shared<IgbFieldOffsetTable>();
    for(std::size_t i=0;i<25;++i){
        this->factories->push_back(dw4::factories[i]);this->names->push_back(std::make_shared<const std::string>(dw4::names[i]));
        globals->push_back(IgbFieldGlobalBinding{std::make_shared<IgbRuntimeFieldSlot>(),0x80561750u+static_cast<std::uint32_t>(i)*4});
        this->offsets->push_back(dw4::offsets[i]);
    }
}
struct IgbMetadataDefaultsInvocation::Impl {
    enum class P {capture,batch,select,scalar,scalar_call,string_call,getter,getter_handle,getter_pool,getter_create,
        getter_publish,getter_reload,write_type,finish_step,schema,done};
    std::shared_ptr<IgbMetadataDefaultsServices> owner;
    std::shared_ptr<IgbMetadataFactoryServices> metadata_services;
    std::shared_ptr<IgbRuntimeMetaType> metadata,type_result;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbRegistrationMetaSlot> getter_slot;
    std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbFieldBatchInvocation> batch;
    std::unique_ptr<IgbFieldSchemaInvocation> schema;
    std::unique_ptr<IgbPoolByHandleInvocation> pool_call;
    std::unique_ptr<IgbMetadataFactoryInvocation> metadata_factory;
    Call<IgbFieldOperationCall,IgbFieldOperationsFactory,IgbFieldOperation> scalar;
    Call<IgbMetadataDefaultsCall,IgbMetadataDefaultsFactory,IgbMetadataDefaultsOperation> string;
    P phase=P::capture;std::uint32_t first{};std::size_t step{};bool running{},fault{};
};
IgbMetadataDefaultsInvocation::IgbMetadataDefaultsInvocation():impl_(std::make_unique<Impl>()){}
IgbMetadataDefaultsInvocation::~IgbMetadataDefaultsInvocation(){auto& s=*impl_;
    if((!completed() || faulted()) && s.owner){
        if(s.metadata)s.owner->unfinished.push_back(s.metadata);if(s.field)s.owner->unfinished.push_back(s.field);
        if(s.type_result)s.owner->unfinished.push_back(s.type_result);if(s.pool)s.owner->unfinished.push_back(s.pool);
        if(s.scalar.request.source && s.scalar.request.source->buffer)s.owner->unfinished.push_back(s.scalar.request.source->buffer);
    }
}
bool IgbMetadataDefaultsInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataDefaultsInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMetadataDefaultsInvocation::resume(std::shared_ptr<IgbMetadataDefaultsServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata defaults require an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Metadata defaults continuation was invalidated.");};
    const auto field_services=[&]{return services && services->schema?services->schema->fields:std::shared_ptr<IgbFieldOperationsServices>{};};
    const auto require_field=[&]{if(!s.field)fail("Metadata defaults dereference a null selected field.");};
    try{for(;;){check();switch(s.phase){
    case P::capture:
        if(!services || !services->metadata || !services->metadata->metadata)return pending("Metadata defaults require canonical8056174C.");
        s.metadata_services=services->metadata;s.metadata=*s.metadata_services->metadata;
        s.first=std::bit_cast<std::uint32_t>(igb_runtime_slot_count(s.metadata));
        s.batch=std::make_unique<IgbFieldBatchInvocation>(s.metadata,services->factories,25);s.phase=P::batch;break;
    case P::batch:{
        const auto status=s.batch->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.batch.reset();check();s.phase=P::select;break;
    }
    case P::select:{
        if(s.step==steps.size()){
            if(!services)return pending("Metadata defaults require their original schema tables.");
            s.schema=std::make_unique<IgbFieldSchemaInvocation>(s.metadata,services->names,services->globals,services->offsets,s.first);
            s.phase=P::schema;break;
        }
        const auto& step=steps[s.step];const auto physical=((s.first+step.index)<<2u)>>2u;
        s.field=igb_runtime_field_at(s.metadata,std::bit_cast<std::int32_t>(physical));
        if(step.action==Action::flags){s.phase=P::finish_step;break;}
        if(step.action==Action::integer || step.action==Action::boolean || step.action==Action::short_integer){s.phase=P::scalar;break;}
        if(step.action==Action::string){s.string.clear();check();s.string.request.field=s.field;s.string.request.argument=step.value;
            if(services)s.string.factory=services->factory;s.phase=P::string_call;break;}
        s.phase=P::getter;break;
    }
    case P::scalar:{
        require_field();if(!s.field->vtable_word)return pending("Metadata scalar default requires its fresh field vtable.");
        const auto& step=steps[s.step];const std::size_t width=step.action==Action::integer?4:step.action==Action::boolean?1:2;
        auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.resize(width);bytes->initialized.assign(width,1);
        for(std::size_t i=0;i<width;++i)bytes->bytes[i]=static_cast<std::uint8_t>(step.value>>((width-1-i)*8));
        s.scalar.clear();check();s.scalar.request.kind=IgbFieldOperationKind::field_virtual;s.scalar.request.field=s.field;
        s.scalar.request.vtable_word=*s.field->vtable_word;s.scalar.request.virtual_slot=0x8c;
        s.scalar.request.source=IgbRuntimeFieldDefaultSource{bytes,0};
        if(const auto fields=field_services())s.scalar.factory=fields->factory;s.phase=P::scalar_call;break;
    }
    case P::scalar_call:{
        const auto fields=field_services();const auto status=s.scalar.poll(fields?fields->factory:IgbFieldOperationsFactory{},check);
        if(!status.completed)return status;s.scalar.clear();check();s.phase=P::finish_step;break;
    }
    case P::string_call:{
        const auto status=s.string.poll(services?services->factory:IgbMetadataDefaultsFactory{},check);
        if(!status.completed)return status;s.string.clear();check();s.phase=P::finish_step;break;
    }
    case P::getter:{
        if(!services)return pending("Metadata defaults require the reached canonical type global.");
        const auto action=steps[s.step].action;
        if(action==Action::metadata){
            if(!s.metadata_services->metadata)return pending("Metadata defaults require reloaded8056174C.");
            s.getter_slot=std::shared_ptr<IgbRegistrationMetaSlot>(s.metadata_services,&s.metadata_services->metadata);
        }else s.getter_slot=action==Action::field_list?services->field_list_metadata:action==Action::metadata_list?services->metadata_list_metadata:services->object_metadata;
        if(!s.getter_slot || !*s.getter_slot)return pending("Metadata defaults require represented getter-global storage.");
        if(action==Action::object || **s.getter_slot)s.phase=P::getter_reload;else s.phase=P::getter_handle;break;
    }
    case P::getter_handle:
        if(!services || !services->schema || !services->schema->types || !services->schema->types->metadata_pool_handle)
            return pending("Metadata defaults lazy type getter requires handle805621F4.");
        s.pool_call=std::make_unique<IgbPoolByHandleInvocation>(*services->schema->types->metadata_pool_handle);s.phase=P::getter_pool;break;
    case P::getter_pool:{
        const auto status=s.pool_call->resume(services && services->schema?services->schema->pools:nullptr);check();if(!status.completed)return status;
        s.pool=s.pool_call->result();s.pool_call.reset();check();s.metadata_factory=std::make_unique<IgbMetadataFactoryInvocation>(s.pool);s.phase=P::getter_create;break;
    }
    case P::getter_create:{
        const auto status=s.metadata_factory->resume(s.metadata_services);check();if(!status.completed)return status;
        s.type_result=s.metadata_factory->result();s.metadata_factory.reset();check();s.phase=P::getter_publish;break;
    }
    case P::getter_publish:
        if(*s.getter_slot && **s.getter_slot && **s.getter_slot!=s.type_result && s.owner)s.owner->unfinished.push_back(**s.getter_slot);
        *s.getter_slot=s.type_result;s.phase=P::getter_reload;break;
    case P::getter_reload:
        if(!*s.getter_slot)return pending("Metadata defaults require the getter's reloaded global.");
        s.type_result=**s.getter_slot;s.phase=P::write_type;break;
    case P::write_type:{
        require_field();s.field->object_type=std::weak_ptr<IgbRuntimeMetaType>(s.type_result);
        const auto action=steps[s.step].action;
        if(action==Action::field_list)s.field->flag_34=std::uint8_t{1};
        if(action!=Action::metadata)s.field->flag_35=std::uint8_t{1};
        s.field->flag_3c=std::uint8_t{0};s.phase=P::finish_step;break;
    }
    case P::finish_step:
        if(steps[s.step].clear_flag){require_field();s.field->flag_26=0;}
        s.field.reset();s.type_result.reset();s.getter_slot.reset();s.pool.reset();++s.step;s.phase=P::select;break;
    case P::schema:{
        const auto status=s.schema->resume(services?services->schema:nullptr);check();if(!status.completed)return status;
        s.schema.reset();check();s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_metadata_defaults_factory(std::shared_ptr<IgbMetadataDefaultsServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataDefaultsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& request){
        if(request.kind!=IgbTypeRegistrationServiceKind::defaults || request.function!=0x8002a164)
            return remaining?remaining(request):std::shared_ptr<IgbTypeRegistrationOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Metadata defaults services expired before selection.");
        auto invocation=std::make_shared<IgbMetadataDefaultsInvocation>();auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
