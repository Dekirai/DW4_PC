#include "dw4/igb_metadata_init.hpp"
#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_string_init.hpp"

#include <iostream>

namespace {
using namespace dw4;unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& f,const char* why){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbScratchBuffer> bytes(unsigned size,std::uint8_t value){auto out=std::make_shared<IgbScratchBuffer>();
    out->bytes.assign(size,value);out->initialized.assign(size,1);return out;}
std::shared_ptr<IgbRuntimeMetaType> schema(){auto out=std::make_shared<IgbRuntimeMetaType>();
    out->fields=std::make_shared<IgbDecodeFieldRegistry>();out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    out->allocation_prefix=8;out->type_word=0x804761d8;out->native_size=0x64;out->class_flags=7;
    out->factory_redirect=0;out->default_pool_name=SystemStageString{};out->instance_count=0;out->instances=std::shared_ptr<IgbObjectInstanceList>{};return out;}
struct Fixture {
    std::shared_ptr<IgbMetadataInitServices> services=std::make_shared<IgbMetadataInitServices>();
    std::shared_ptr<IgbRuntimeMetaType> type=schema(),base=schema(),target=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbObjectStorage> storage=std::make_shared<IgbObjectStorage>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbFieldInitServices> fields=std::make_shared<IgbFieldInitServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    unsigned allocations{},pool_queries{};bool ready=true;
    Fixture(){
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->metadata=type;
        services->reflected=std::make_shared<IgbFileFactoryServices>();services->reflected->initializer=std::make_shared<IgbObjectInitServices>();
        auto init=services->reflected->initializer;init->runtime=std::make_shared<IgbRuntimeTypesServices>();init->runtime->singleton=core;init->object_metadata=base;
        core->enabled=1;core->vtable_offset=0;core->field_initialization_enabled=1;
        storage->original_address=0x10000;storage->bytes=bytes(0x70,0xcc);storage->bindings={{8,{},target}};
        fields->values=std::make_shared<IgbFieldValuesServices>();fields->values->fields=std::make_shared<IgbFieldOperationsServices>();
        fields->values->fields->factory=make_native_igb_field_values_factory(fields->values);
        init->factory=make_native_igb_metadata_init_factory(services,make_native_igb_scalar_field_factory(make_native_igb_field_init_factory(fields,
            [this](const IgbObjectInitCall& c){
                if(c.kind!=IgbObjectInitServiceKind::allocation_pool && c.kind!=IgbObjectInitServiceKind::pool_index)return std::shared_ptr<IgbObjectInitOperation>{};
                auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[this,c,out=op.get()]{++pool_queries;
                    if(c.kind==IgbObjectInitServiceKind::allocation_pool){check(c.address==0x10000,"Metadata allocation base lost its prefix.");out->pool_result=pool;}
                    return MessengerCallResult{true,0xa5,{}};};return op;
            })));
        services->reflected->factory=[this](const IgbFileFactoryCall& c){
            check(c.kind==IgbFileFactoryServiceKind::allocate && c.metadata==type && c.bytes==0x6c && c.pool==pool,
                "Reflected metadata changed original allocation arguments.");
            auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[this,out=op.get()]{if(!ready)return MessengerCallResult{false,0,"prepared metadata allocation"};
                ++allocations;out->allocation_result=storage;return MessengerCallResult{true,0,{}};};return op;
        };
    }
    std::shared_ptr<IgbDecodeRuntimeField> add(std::uint32_t table,std::uint32_t offset,unsigned size,std::uint32_t value=0){
        auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=table;field->offset=offset;field->size=static_cast<std::uint16_t>(size);
        field->flag_34=0;field->flag_35=0;field->flag_3c=0;field->word_40=0;field->default_mode=0;
        auto data=bytes(size,0);for(unsigned i=0;i<size;++i)data->bytes[i]=static_cast<std::uint8_t>(value>>(8*(size-i-1)));
        field->default_bytes=IgbRuntimeFieldDefaultSource{data,0};type->fields->backing->push_back(field);++type->fields->count;return field;
    }
};
void reflected_members(){
    Fixture f;f.add(0x80475b60,8,4,0xffffffff);f.add(0x80473e30,0x19,1,1);f.add(0x8047693c,0x1c,4);
    f.add(0x804762fc,0x28,4);f.add(0x80477054,0x58,2,4);f.add(0x80476630,0x5c,4);f.add(0x8047693c,0x60,4);
    IgbReflectedFactoryInvocation call(f.type,f.pool);check(call.resume(f.services->reflected).completed,"Reflected metadata field passes did not finish.");
    const auto result=call.result_instance();check(result.metadata==f.target && !result.file && !result.field && result.offset==8 && result.storage==f.storage,
        "Reflected metadata lost its adjusted typed result.");
    check(f.target->reference_word==0xa5000001 && igb_object_reference_word(result)==0xa5000001 && f.target->word_08==0xffffffff && f.target->flag_19==1,
        "Metadata header/scalar stores were not published canonically.");
    check(f.target->name_string && !*f.target->name_string && f.target->name_string_address==0 && !f.target->name &&
        f.target->default_pool_string && !*f.target->default_pool_string && f.target->default_pool_name && !*f.target->default_pool_name,
        "Metadata name and pool-name slots lost represented null state.");
    check(!f.target->fields && f.target->fields_address==0 && f.target->native_alignment==4 && f.target->type_word==0 &&
        igb_buffer_word(f.storage->bytes,8)==0x804761d8 && f.type->instance_count==1,
        "Metadata instance vtable was conflated with its +5C member, or final class count was skipped.");
    check(f.storage->bytes->bytes[8+0x1b]==0xcc && f.storage->bytes->bytes[8+0x5a]==0xcc && f.storage->bytes->bytes[0]==0xcc,
        "Metadata field initialization overwrote padding or allocation prefix.");
    rejects([&]{call.result();},"Metadata instance became a null file through the strict adapter.");
    check(f.target->construction_active_.expired() && !f.target->construction_fault_,"Completed metadata retained its construction lease.");
}
void adapter_and_lifetime(){
    Fixture f;f.ready=false;const auto adapter=make_native_igb_reflected_metadata_factory(f.services);
    IgbMetadataFactoryCall request;request.kind=IgbMetadataFactoryServiceKind::reflected_factory;request.metadata=f.type;request.pool=f.pool;
    auto op=adapter(request);check(op && !op->resume().completed && f.allocations==0,"Reflected metadata adapter fabricated a ready allocation.");
    f.ready=true;check(op->resume().completed && f.allocations==1 && op->allocation_result.storage==f.storage &&
        op->allocation_result.offset==8 && f.services->completed_allocations.size()==1,"Adapter lost typed result or allocated twice.");
    check(op->resume().completed && f.allocations==1 && f.type->instance_count==1 && f.services->completed_allocations.size()==1,
        "Completed adapter repeated initialization or retained its allocation twice.");
    request.kind=IgbMetadataFactoryServiceKind::register_type;check(!adapter(request),"Reflected adapter consumed an unrelated registration.");
    Fixture abandoned;abandoned.services->metadata->metadata.reset();
    {IgbReflectedFactoryInvocation pending(abandoned.type,abandoned.pool);check(!pending.resume(abandoned.services->reflected).completed,
        "Missing canonical metadata getter state was skipped.");}
    check(abandoned.storage->faulted() && abandoned.target->construction_fault_ && abandoned.target->construction_active_.expired(),
        "Abandoned metadata initialization lost canonical fault/lease cleanup.");
    Fixture changed;changed.services->metadata->metadata.reset();IgbReflectedFactoryInvocation pending(changed.type,changed.pool);
    check(!pending.resume(changed.services->reflected).completed,"Changed-owner fixture did not suspend.");
    const auto before=changed.storage->bytes->bytes;changed.storage->bindings[0].metadata=std::make_shared<IgbRuntimeMetaType>();
    changed.services->metadata->metadata=changed.type;rejects([&]{pending.resume(changed.services->reflected);},"Changed metadata binding resumed.");
    check(changed.target->construction_fault_ && changed.storage->bytes->bytes==before,"Changed binding published additional bytes or lost captured fault state.");
}
void list_typed_headers(){
    for(bool types:{false,true}){
        Fixture f;auto fields=std::make_shared<IgbDecodeFieldRegistry>();auto list=std::make_shared<IgbRuntimeTypeList>();
        f.storage->bindings={{8,{},{},types?nullptr:fields,types?list:nullptr}};
        // Prepared class callbacks isolate generic typed list initialization;
        // this does not claim the original list field/member bodies.
        f.services->reflected->initializer->factory=[&](const IgbObjectInitCall& c){auto op=std::make_shared<IgbObjectInitOperation>();
            op->resume=[&,c,out=op.get()]{if(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x58)out->metadata_result=f.type;
                return MessengerCallResult{true,0,{}};};return op;};
        IgbReflectedFactoryInvocation call(f.type,f.pool);check(call.resume(f.services->reflected).completed,"Prepared typed list did not finish generic initialization.");
        const auto result=call.result_instance();check(types?result.types==list:result.fields==fields,"Generic initializer lost its distinct list type.");
        check((types?list->reference_word:fields->reference_word)==1 && igb_object_reference_word(result)==1,"List physical and canonical headers differ.");
        rejects([&]{call.result();},"Typed list became a null file result.");
        const auto backing=bytes(32,0);
        if(types){list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(2);list->backing_bytes=backing;list->backing_address=0x44005;list->backing_offset=5;}
        else{fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(2);fields->backing_bytes=backing;fields->backing_address=0x44005;fields->backing_offset=5;}
        igb_bootstrap_write_word({result.storage,result.offset},8,0x44005);
        igb_synchronize_field_storage(result,result.offset+8,4);
        check(types?(list->backing && list->backing_bytes==backing && list->backing_offset==5):
            (fields->backing && fields->backing_bytes==backing && fields->backing_offset==5),
            "Raw same-pointer publication added a host buffer offset to its already adjusted address.");
        igb_bootstrap_write_word({result.storage,result.offset},8,0x55007);
        igb_synchronize_field_storage(result,result.offset+8,4);
        check(types?(!list->backing && !list->backing_bytes && list->backing_address==0x55007 && list->backing_offset==0):
            (!fields->backing && !fields->backing_bytes && fields->backing_address==0x55007 && fields->backing_offset==0),
            "Raw changed pointer kept an unrelated typed backing or invented a new mapping.");
    }
}
void field_list_virtuals(){
    Fixture f;auto list=std::make_shared<IgbDecodeFieldRegistry>();
    f.storage->bindings={{8,{},{},list}};f.type->type_word=0x80475cb0;
    f.services->field_lists=std::make_shared<IgbFieldRegistryServices>();f.services->field_lists->metadata=f.type;
    // This synthetic descriptor tests the real null-clear pass without
    // requesting the separate memory default or positive allocation body.
    f.add(0x80476088,8,4)->default_mode=1;f.add(0x80475b60,12,4);
    list->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    std::weak_ptr<IgbDecodeFieldRegistry::Storage> displaced=list->backing;
    IgbReflectedFactoryInvocation call(f.type,f.pool);
    check(call.resume(f.services->reflected).completed,"Field-list native getter/leaves did not compose the actual field passes.");
    const auto result=call.result_instance();
    check(result.fields==list && list->reference_word==0xa5000001 && list->count==0 && !list->backing &&
        list->backing_address==0 && !displaced.expired() && igb_buffer_word(f.storage->bytes,16)==0 &&
        igb_buffer_word(f.storage->bytes,20)==0,"Reflected list lost canonical null backing/count or displaced owner retention.");
    const auto adapter=make_native_igb_metadata_init_factory(f.services);
    IgbObjectInitCall request;request.kind=IgbObjectInitServiceKind::object_virtual;
    request.object=result;request.vtable_word=0x80475cb0;request.virtual_slot=0x58;
    auto getter=adapter(request);f.services->field_lists->metadata.reset();
    check(!getter->resume().completed,"Unknown field-list metadata was converted to a null getter result.");
    auto replacement=schema();f.services->field_lists->metadata=replacement;
    check(getter->resume().completed && getter->metadata_result==replacement,"Field-list getter cached an earlier canonical cell value.");
    f.services->field_lists->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(getter->resume().completed && !getter->metadata_result,"Represented null field-list metadata was not returned as null.");
    request.virtual_slot=0x90;check(!adapter(request),"Metadata adapter consumed an unknown list virtual.");
    request.virtual_slot=0x58;request.vtable_word=0x804761d8;
    rejects([&]{adapter(request);},"Metadata virtual accepted a field-list identity.");
}
}
int main(){try{reflected_members();adapter_and_lifetime();list_typed_headers();field_list_virtuals();
    std::cout<<"igb_metadata_init_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Metadata initialization failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
