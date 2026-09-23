#include "dw4/igb_field_type_defaults.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned size,std::uint8_t fill=0){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,fill);b->initialized.assign(size,1);return b;}
std::shared_ptr<IgbRuntimeMetaType> metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(64);m->fields->backing_bytes=bytes(256);m->fields->backing_address=0x900000;return m;}
struct Fixture {
    std::shared_ptr<IgbFieldTypeDefaultsServices> services=std::make_shared<IgbFieldTypeDefaultsServices>();
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<IgbRuntimeMetaType> base=metadata(),reference=metadata(),object=metadata(),parent=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<std::uint32_t> functions;
    std::vector<IgbFieldOperationCall> scalar_calls;
    std::function<void(const IgbFieldOperationCall&)> scalar_action;
    std::function<void(const std::shared_ptr<IgbDecodeRuntimeField>&)> factory_action;
    unsigned factory_polls{},scalar_polls{},interns{};
    bool factories_ready=true,scalars_ready=true;
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
        services->registration->base_metadata=std::make_shared<IgbRegistrationMetaSlot>(base);
        services->registration->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(reference);
        services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>(object);
        services->metadata=std::make_shared<IgbMetadataRegistrationServices>();services->metadata->metadata=std::make_shared<IgbMetadataFactoryServices>();
        services->metadata->metadata->metadata=parent;
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++interns;auto header=std::make_shared<IgbInternedString>();header->value=igb_dependency_name(c.name);
                header->container=pool;header->references=1;out->string_result=header;}
            else if(c.kind!=IgbDependenciesServiceKind::release_string)throw std::runtime_error("Unexpected prepared string operation");
            return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
            else if(c.kind==IgbPointerListServiceKind::allocation_size)out->size_result=256;
            else throw std::runtime_error("Prepared descriptor registry unexpectedly grew");return MessengerCallResult{true,0,{}};};return op;};
        // These are explicitly prepared cold-zero prototype cells and allocation
        // capacities. The real value adapter still performs buffer preparation,
        // size dispatch, scalar copying, and schema finalization.
        values->fields=schema.fields;schema.fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->unsigned_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->signed_short_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        auto native=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=pool;
            else if(c.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,4,{}};
            else throw std::runtime_error("Unexpected prepared scalar allocation endpoint");return MessengerCallResult{true,0,{}};};return op;});
        schema.fields->factory=[this,native](const IgbFieldOperationCall& c){auto selected=native(c);if(!c.source)return selected;scalar_calls.push_back(c);
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,selected]{++scalar_polls;if(!scalars_ready)return MessengerCallResult{false,0,"prepared scalar suspension"};
                if(!selected || !selected->resume)throw std::runtime_error("Original scalar operation was not selected");
                const auto result=selected->resume();if(result.completed && scalar_action)scalar_action(c);return result;};return op;};
        // Specialized allocation/construction is prepared in this caller suite;
        // exact profile constructors have their separate original-code proof.
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field,"Unexpected prepared descriptor destruction");
            functions.push_back(c.function);auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,c,out=op.get()]{
                ++factory_polls;if(!factories_ready)return MessengerCallResult{false,0,"prepared specialized factory suspension"};
                check(c.pool==pool,"Descriptor creation lost the original selected pool");const auto kind=igb_meta_field_kind_for_factory(c.function);
                check(kind.has_value(),"Default table has no verified field profile");auto f=std::make_shared<IgbDecodeRuntimeField>();
                f->vtable_word=igb_meta_field_profile(*kind).vtable;f->flag_26=1;f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
                f->name_string=std::shared_ptr<IgbInternedString>{};f->default_mode=std::uint8_t{0};f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0xa5),0};
                created.push_back(f);out->field_result=f;out->field_address=0x100000u+static_cast<std::uint32_t>(created.size())*0x100;
                f->default_bytes_address=*out->field_address+0x80;if(factory_action)factory_action(f);return MessengerCallResult{true,0,{}};};return op;};
        for(const auto& m:{base,reference,object}){m->fields->count=2;for(unsigned i=0;i<2;++i){auto f=std::make_shared<IgbDecodeRuntimeField>();
            f->name=std::make_shared<const std::string>("inherited");m->fields->backing->at(i)=f;}}
    }
    std::shared_ptr<IgbRuntimeMetaType> target(IgbFieldTypeKind kind){return kind==IgbFieldTypeKind::base?base:kind==IgbFieldTypeKind::reference?reference:object;}
};
void check_schema(Fixture& f,IgbFieldTypeKind kind,std::uint32_t count,std::uint32_t first_global){const auto m=f.target(kind);const auto& t=f.services->tables[static_cast<std::size_t>(kind)];
    check(f.created.size()==count && m->fields->count==count+2 && f.interns==count,"Local batch/schema count differs");
    check(*m->fields->backing->at(0)->name=="inherited" && *m->fields->backing->at(1)->name=="inherited","Caller overwrote inherited fields");
    for(unsigned i=0;i<count;++i){const auto field=f.created[i];check(m->fields->backing->at(i+2)==field,"Native batch lost descriptor identity");
        check(field->reference_word==1 && field->ordinal==0,"Caller changed guest ownership or invented field ordinals");
        check(field->declaring_type && field->declaring_type->lock()==m,"Schema lost its captured metadata receiver");
        check(field->name==*t.names->at(i) && field->offset==*t.offsets->at(i),"Field names/offsets differ from literal table order");
        check(t.globals->at(i)->address==first_global+4*i && *t.globals->at(i)->slot==field && field->global_slot_address==first_global+4*i,"Canonical descriptor publication differs");
    }
}
void complete_base(){Fixture f;IgbFieldTypeDefaultsInvocation call(IgbFieldTypeKind::base);check(call.resume(f.services).completed,"Base defaults did not complete");
    check_schema(f,IgbFieldTypeKind::base,18,0x805617c0);
    check(f.functions==std::vector<std::uint32_t>{0x8002e1ac,0x800251ec,0x800259a8,0x800259a8,0x80021e10,0x8002879c,0x800274ec,0x8002ac34,
        0x80037b1c,0x800385e0,0x800385e0,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x80037b1c,0x800274ec},"Original base factory table differs");
    const std::array names={"_offset","_fieldName","_typeIndex","_internalIndex","_size","_parentMetaObject","_externalEnumStorage","_default","_resetMethod",
        "_isRunTime","_persistent","_shallowCopyMethod","_deepCopyMethod","_constructCopyMethod","_isAlikeExactlyMethod","_isAlikeDeepMethod","_isAlikeShallowMethod","_invarianceFunction"};
    const std::array<unsigned,18> offsets={8,12,16,18,20,24,28,32,36,37,38,39,40,41,42,43,44,48};
    for(unsigned i=0;i<18;++i)check(*f.created[i]->name==names[i] && f.created[i]->offset==offsets[i],"Base literal name/offset row differs");
    for(unsigned i=0;i<18;++i){const bool cleared=i==0 || (i>=2 && i<=7) || i==17;check(f.created[i]->flag_26==(cleared?0:1),"Base persistent flag changed for the wrong descriptor");}
    check(f.created[5]->object_type->lock()==f.parent && f.created[5]->flag_3c==0 && f.created[5]->flag_28==1 && f.created[5]->flag_27==1,"Base parent metadata configuration differs");
    check(f.created[6]->flag_28==1 && !f.created[6]->flag_27,"External enum storage copied an unrelated copy flag");
    check(f.created[7]->flag_35==1 && f.created[7]->flag_40==1 && f.created[7]->flag_41==1 && !f.created[7]->flag_34,"Memory-reference default flags differ");
    check(f.scalar_calls.size()==10,"Base scalar setter count differs");
    const std::array<unsigned,10> indices={2,3,8,10,11,12,13,14,15,16};const std::array<unsigned,10> defaults={0xffff,0xffff,0,1,1,0,0,0,1,2};
    for(unsigned i=0;i<10;++i){const auto& c=f.scalar_calls[i];const auto width=i<2?2u:1u;
        check(c.field==f.created[indices[i]] && c.virtual_slot==0x8c && c.source->buffer->bytes.size()==width,"Scalar selected the wrong descriptor or input width");
        const auto& data=c.field->default_bytes->buffer->bytes;check(data[0]==static_cast<std::uint8_t>(defaults[i]>>(width==2?8:0)) && (width==1 || data[1]==0xff),"Actual scalar copy changed original default bits");
        check(data[width]==0xa5 && c.field->size==width,"Scalar copy widened its field or clobbered unused allocation bytes");
    }
    check(call.resume().completed && f.functions.size()==18,"Completed base defaults replayed");
}
void complete_reference_and_object(){Fixture ref;IgbFieldTypeDefaultsInvocation r(IgbFieldTypeKind::reference);check(r.resume(ref.services).completed,"Reference defaults did not complete");
    check_schema(ref,IgbFieldTypeKind::reference,2,0x8056168c);check(ref.functions==std::vector<std::uint32_t>{0x800385e0,0x800385e0},"Reference factory table differs");
    check(*ref.created[0]->name=="_construct" && *ref.created[1]->name=="_destruct" && ref.created[0]->offset==0x34 && ref.created[1]->offset==0x35,"Reference literal schema differs");
    check(ref.scalar_calls.empty() && ref.created[0]->flag_26==1 && ref.created[1]->flag_26==1 && !ref.created[0]->flag_35,"Reference defaults invented customizations");
    Fixture object;IgbFieldTypeDefaultsInvocation o(IgbFieldTypeKind::object);check(o.resume(object.services).completed,"Object defaults did not complete");
    check_schema(object,IgbFieldTypeKind::object,3,0x805616f0);check(object.functions==std::vector<std::uint32_t>{0x8002879c,0x800385e0,0x800251ec},"Object factory table differs");
    check(*object.created[0]->name=="_metaObject" && *object.created[1]->name=="_refCounted" && *object.created[2]->name=="_poolName","Object literal names differ");
    check(object.created[0]->offset==0x38 && object.created[1]->offset==0x3c && object.created[2]->offset==0x40,"Object literal offsets differ");
    check(object.created[0]->object_type->lock()==object.parent && object.created[0]->flag_3c==0 && object.created[0]->flag_27==1 && object.created[0]->flag_28==1 && object.created[0]->flag_26==1,"Object parent metadata flags differ");
    check(object.scalar_calls.size()==1 && object.scalar_calls[0].field==object.created[1] && object.created[1]->default_bytes->buffer->bytes[0]==1,"Object reference-counted default differs");
}
void pending_and_captured_state(){Fixture f;f.factories_ready=false;IgbFieldTypeDefaultsInvocation call(IgbFieldTypeKind::base);
    check(!call.resume(f.services).completed && f.functions==std::vector<std::uint32_t>{0x8002e1ac} && f.created.empty(),"Cold factory boundary skipped original work");
    const auto replacement=metadata();*f.services->registration->base_metadata=replacement;f.factories_ready=true;f.scalars_ready=false;
    check(!call.resume(f.services).completed && f.created.size()==18 && f.factory_polls==19 && f.scalar_calls.size()==1,"Resumed batch replayed its selected factory");
    check(f.base->fields->count==20 && replacement->fields->count==0 && f.created[0]->flag_26==0 && f.created[2]->flag_26==1 && f.interns==0,"Scalar pause changed the reached prefix or receiver");
    const auto captured=f.scalar_calls[0].vtable_word;f.created[2]->vtable_word=0x80477054; // Another represented two-byte profile.
    // The original setter has already loaded its virtual target. Restore the
    // descriptor after the retained operation copies, before later finalization.
    f.scalar_action=[&](const auto& c){if(c.field==f.created[2]){check(c.vtable_word==captured,"Pending scalar reselected a fresh virtual target");f.created[2]->vtable_word=captured;}};
    const auto fresh_parent=metadata();f.services->metadata->metadata->metadata=fresh_parent;f.scalars_ready=true;
    check(call.resume(f.services).completed && f.scalar_calls.size()==10 && f.scalar_polls==11,"Scalar continuation was recreated or replayed");
    check(f.created[5]->object_type->lock()==fresh_parent && f.created[5]->declaring_type->lock()==f.base,"Fresh29F84 global and captured defaults receiver were conflated");
}
void live_field_and_count(){Fixture f;std::shared_ptr<IgbDecodeRuntimeField> displaced;
    f.scalar_action=[&](const auto& c){if(c.field==f.created[2]){displaced=f.created[3];f.base->fields->backing->at(5)=f.created[2];f.base->fields->count=19;}};
    IgbFieldTypeDefaultsInvocation call(IgbFieldTypeKind::base);check(call.resume(f.services).completed,"Live field-list mutation did not complete");
    check(f.scalar_calls[1].field==f.created[2] && displaced->flag_26==1 && !displaced->name,"Next field selection reused captured backing content");
    check(f.created[17]->flag_26==0 && !f.created[17]->name && f.interns==17,"Defaults/schema failed to distinguish indexed access from fresh schema count");
}
void unknown_getter_and_faults(){Fixture f;f.services->metadata->metadata->metadata.reset();IgbFieldTypeDefaultsInvocation call(IgbFieldTypeKind::base);
    check(!call.resume(f.services).completed && f.created.size()==18 && f.scalar_calls.size()==2 && f.created[4]->flag_26==0 && !f.created[5]->object_type && f.interns==0,"Unknown29F84 fabricated a metadata result or lost preceding defaults");
    f.services->metadata->metadata->metadata=f.parent;check(call.resume(f.services).completed && f.created[5]->object_type->lock()==f.parent,"Late known getter global did not resume");
    Fixture cold;cold.services->metadata->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbFieldTypeDefaultsInvocation entered(IgbFieldTypeKind::object);
    check(!entered.resume(cold.services).completed && cold.created.size()==3 && cold.scalar_calls.empty() && !cold.created[0]->object_type,"Null lazy getter skipped original allocation dependencies");
    cold.services->metadata->metadata->metadata=cold.parent;check(!entered.resume(cold.services).completed,"Entered lazy allocation was replaced by a late cache hit");
    Fixture missing;missing.services->registration->base_metadata->reset();IgbFieldTypeDefaultsInvocation waits(IgbFieldTypeKind::base);
    check(!waits.resume(missing.services).completed && missing.functions.empty(),"Unknown defaults global fabricated a receiver");
    *missing.services->registration->base_metadata=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{waits.resume(missing.services);},"Explicit null receiver did not fault at count access");
    check(waits.faulted() && missing.functions.empty(),"Null receiver executed a later factory");
    Fixture null_field;null_field.factory_action=[&](const auto&){if(null_field.created.size()==18)null_field.base->fields->backing->at(2).reset();};
    IgbFieldTypeDefaultsInvocation dereference(IgbFieldTypeKind::base);rejects([&]{dereference.resume(null_field.services);},"Null selected descriptor did not fault at its reached flag store");
    check(null_field.created.size()==18 && null_field.scalar_calls.empty() && null_field.interns==0,"Null descriptor fault skipped the batch or committed later defaults");
    Fixture recursion;IgbFieldTypeDefaultsInvocation recursive(IgbFieldTypeKind::base);recursion.scalar_action=[&](const auto&){try{recursive.resume(recursion.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursive.resume(recursion.services);},"Swallowed recursive failure did not poison the original caller");
    check(recursive.faulted() && recursion.created[2]->flag_26==1 && recursion.interns==0,"Recursive failure committed unreached flag/name stores");
}
void adapter_and_lifetime(){for(const auto kind:{IgbFieldTypeKind::base,IgbFieldTypeKind::reference,IgbFieldTypeKind::object}){
        Fixture f;auto adapter=make_native_igb_field_type_defaults_factory(f.services);IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::defaults;
        request.function=kind==IgbFieldTypeKind::base?0x8002a764u:kind==IgbFieldTypeKind::reference?0x800273ecu:0x80028ab4u;request.metadata=metadata();
        auto operation=adapter(request);check(operation && operation->resume().completed && request.metadata->fields->count==0,"Adapter used generic request metadata instead of the canonical global");
        request.function=0x8002a164;check(!adapter(request),"Field-type defaults adapter swallowed unrelated metadata defaults");
    }
    auto services=std::make_shared<IgbFieldTypeDefaultsServices>();std::weak_ptr<IgbFieldTypeDefaultsServices> weak=services;auto adapter=make_native_igb_field_type_defaults_factory(services);services.reset();
    check(weak.expired(),"Defaults adapter formed a service ownership cycle");IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80028ab4;
    rejects([&]{adapter(request);},"Expired defaults service was silently recreated");
    Fixture abandoned;abandoned.scalars_ready=false;std::weak_ptr<IgbScratchBuffer> source;
    {IgbFieldTypeDefaultsInvocation call(IgbFieldTypeKind::base);check(!call.resume(abandoned.services).completed,"Prepared scalar failed to suspend");source=abandoned.scalar_calls[0].source->buffer;}
    abandoned.scalar_calls.clear();check(!source.expired(),"Abandoned pending original stack bytes were released prematurely");abandoned.services.reset();check(source.expired(),"Abandoned scalar storage formed a cycle");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_type_defaults_tests");complete_base();complete_reference_and_object();pending_and_captured_state();
    live_field_and_count();unknown_getter_and_faults();adapter_and_lifetime();std::cout<<"igb_field_type_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Field-type defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
