#include "dw4/igb_field_schema.hpp"

#include <iostream>

namespace {
using namespace dw4;using FK=IgbFieldOperationKind;using SK=IgbFieldSchemaServiceKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void fails(F run,const char* message){bool caught{};try{run();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto text(const char* value){return std::make_shared<const std::string>(value);}
auto bytes(unsigned size){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(size,0);out->initialized.assign(size,1);return out;}
auto field(){auto out=std::make_shared<IgbDecodeRuntimeField>();out->vtable_word=0x80476e6c;out->name_string=std::shared_ptr<IgbInternedString>{};
    out->default_bytes=IgbRuntimeFieldDefaultSource{};out->default_bytes_address=0;out->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};return out;}
auto metadata(){auto out=std::make_shared<IgbRuntimeMetaType>();out->fields=std::make_shared<IgbDecodeFieldRegistry>();
    out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);out->fields->backing_bytes=bytes(64);out->fields->backing_address=0x90000;return out;}
IgbBootstrapPointer physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t address=0x300008){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(0x48);
    for(auto& byte:storage->bytes->bytes)byte=0xcc;
    storage->bindings.push_back({8,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{storage,8};
    return {storage,8};
}
struct Fixture {
    std::shared_ptr<IgbFieldSchemaServices> services=std::make_shared<IgbFieldSchemaServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<std::uint32_t> functions,handles;
    std::function<void(const IgbFieldSchemaCall&,IgbFieldSchemaOperation&)> schema_action;
    std::function<void(const IgbFieldOperationCall&)> field_action;
    std::function<void(const IgbPointerListCall&)> pointer_action;
    bool create_ready=true,destroy_ready=true,pointer_ready=true;
    unsigned creates{},destroys{},pointer_polls{},buffer_allocations{},interns{},string_releases{};
    Fixture(){
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;
        services->pools->factory=[this](const IgbFilePoolsCall& call){handles.push_back(call.handle);auto op=std::make_shared<IgbFilePoolsOperation>();
            op->object_result=pool;op->resume=[]{return MessengerCallResult{true,0,{}};};return op;};
        services->dependencies=std::make_shared<IgbDependenciesServices>();services->dependencies->globals=std::make_shared<SystemStageServices>();
        services->dependencies->globals->string_pool=pool;
        services->dependencies->factory=[this](const IgbDependenciesCall& call){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,call,out=op.get()]{if(call.kind==IgbDependenciesServiceKind::intern_name){++interns;out->string_result=std::make_shared<IgbInternedString>();
                    out->string_result->value=igb_dependency_name(call.name);out->string_result->container=pool;}
                else if(call.kind==IgbDependenciesServiceKind::release_string)++string_releases;
                else throw std::runtime_error("Unexpected prepared string operation");return MessengerCallResult{true,0,{}};};return op;};
        services->fields=std::make_shared<IgbFieldOperationsServices>();services->fields->dependencies=services->dependencies;
        services->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};services->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->fields->pointer_lists->factory=[this](const IgbPointerListCall& call){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,call,out=op.get()]{++pointer_polls;if(!pointer_ready)return MessengerCallResult{false,0,"prepared backing query"};
                if(call.kind==PK::allocation_pool)out->pool_result=pool;else if(call.kind==PK::allocation_size)out->size_result=64;
                else throw std::runtime_error("Unexpected prepared pointer-list growth");if(pointer_action)pointer_action(call);return MessengerCallResult{true,0,{}};};return op;};
        services->fields->factory=make_native_igb_unsigned_int_field_factory(services->fields,[this](const IgbFieldOperationCall& call){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,call,out=op.get()]{std::uint32_t result{};
                if(call.kind==FK::object_pool || call.kind==FK::allocation_pool)out->pool_result=pool;
                else if(call.kind==FK::allocation_size)result=4;
                else if(call.kind==FK::allocate_default){++buffer_allocations;out->buffer_result={bytes(call.bytes),0};out->buffer_address=0xa0000+buffer_allocations*0x100;}
                else throw std::runtime_error("Unexpected field operation");if(field_action)field_action(call);return MessengerCallResult{true,result,{}};};return op;});
        services->factory=[this](const IgbFieldSchemaCall& call){auto op=std::make_shared<IgbFieldSchemaOperation>();
            if(call.kind==SK::create_field)functions.push_back(call.function);
            op->resume=[this,call,out=op.get()]{if(call.kind==SK::create_field){++creates;if(!create_ready)return MessengerCallResult{false,0,"prepared constructor"};
                    out->field_result=field();created.push_back(out->field_result);out->field_address=0x100000+static_cast<std::uint32_t>(created.size())*0x100;}
                else{++destroys;if(!destroy_ready)return MessengerCallResult{false,0,"prepared destructor"};}
                if(schema_action)schema_action(call,*out);return MessengerCallResult{true,0,{}};};return op;};
    }
};
void batch_order(){
    Fixture f;auto table=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x1111,0x2222});
    f.pointer_action=[&](const IgbPointerListCall& call){if(call.kind==PK::allocation_size && f.created.size()==1){
        f.services->types->metadata_pool_handle=9;(*table)[1]=0x3333;}};
    IgbFieldBatchInvocation run(f.meta,table,2);check(run.resume(f.services).completed,"Batch did not complete.");
    check(f.functions==std::vector<std::uint32_t>{0x1111,0x3333} && f.handles==std::vector<std::uint32_t>{5,9},"Batch did not reload each pool handle and factory entry.");
    check(f.meta->fields->count==2 && f.created.size()==2 && f.buffer_allocations==0 && f.interns==0,"Batch finalized fields or skipped raw append.");
    for(unsigned i=0;i<2;++i)check(f.created[i]->reference_word==1 && !f.created[i]->type_ordinal && f.meta->fields->backing->at(i)==f.created[i] &&
        igb_buffer_word(f.meta->fields->backing_bytes,i*4)==0x100100+i*0x100,"Raw append identity or creator release changed.");
    check(run.resume().completed && f.creates==2,"Completed batch replayed.");
    for(const auto count:{0u,0xffffffffu,0x80000000u}){IgbFieldBatchInvocation empty(nullptr,nullptr,count);check(empty.resume().completed,"Signed nonpositive batch count read its inputs.");}
}
void batch_pending_and_failures(){
    Fixture f;auto table=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x1111});f.create_ready=false;
    IgbFieldBatchInvocation run(f.meta,table,1);check(!run.resume(f.services).completed && f.functions.size()==1 && f.handles.size()==1,"Constructor pause did not retain selection.");
    (*table)[0]=0x2222;f.create_ready=true;f.pointer_ready=false;
    check(!run.resume(f.services).completed && f.created[0]->reference_word==2 && f.meta->fields->count==0,"Append pause lost retained field prefix.");
    auto captured=f.meta->fields;f.meta->fields=metadata()->fields;f.pointer_ready=true;
    check(run.resume(f.services).completed && captured->count==1 && f.meta->fields->count==0 && f.created[0]->reference_word==1 && f.functions==std::vector<std::uint32_t>{0x1111},"Pending append reread metadata or retained twice.");
    Fixture no_field;no_field.schema_action=[](const auto&,auto& op){op.field_result.reset();op.field_address=0;};
    IgbFieldBatchInvocation null_result(no_field.meta,table,1);fails([&]{null_result.resume(no_field.services);},"Null factory result skipped its release fault.");
    check(no_field.meta->fields->count==1 && !no_field.meta->fields->backing->at(0),"Null factory result did not append before release fault.");
    Fixture no_meta;IgbFieldBatchInvocation null_meta(nullptr,table,1);fails([&]{null_meta.resume(no_meta.services);},"Null metadata succeeded.");
    check(no_meta.created.size()==1 && no_meta.created[0]->reference_word==1,"Null metadata failure retained its field too early.");
    Fixture no_list;no_list.meta->fields.reset();IgbFieldBatchInvocation null_list(no_list.meta,table,1);
    fails([&]{null_list.resume(no_list.services);},"Null list succeeded.");check(no_list.created[0]->reference_word==2,"Null list failure omitted field retain.");
    Fixture missing;IgbFieldBatchInvocation absent(missing.meta,std::make_shared<IgbFieldFactoryTable>(1),1);
    check(!absent.resume(missing.services).completed && missing.handles.size()==1 && missing.functions.empty(),"Unrepresented function was selected or skipped pool lookup.");
    IgbFieldBatchInvocation zero(missing.meta,std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0}),1);
    fails([&]{zero.resume(missing.services);},"Null constructor function was accepted.");
}
void packed_release(){
    Fixture f;auto table=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x1111});
    f.schema_action=[](const auto& call,auto& op){if(call.kind==SK::create_field)op.field_result->reference_word=0x80000000;};
    f.destroy_ready=false;IgbFieldBatchInvocation run(f.meta,table,1);
    check(!run.resume(f.services).completed && f.created[0]->reference_word==0x80000000 && f.destroys==1 && f.meta->fields->count==1,"Packed creator release did not suspend after its decrement.");
    f.destroy_ready=true;check(run.resume(f.services).completed && f.destroys==2 && f.created[0]->reference_word==0x80000000,"Destructor resume replayed creator release.");
    Fixture abandoned;abandoned.pointer_ready=false;std::weak_ptr<IgbDecodeRuntimeField> retained;
    {IgbFieldBatchInvocation suspended(abandoned.meta,table,1);check(!suspended.resume(abandoned.services).completed,"Prepared append did not suspend.");retained=abandoned.created[0];
        abandoned.created.clear();abandoned.services->fields->pointer_lists->values.clear();}
    check(!retained.expired() && retained.lock()->reference_word==2,"Abandoned append lost its retained descriptor.");
    abandoned.services.reset();check(retained.expired(),"Abandoned descriptor created a services cycle.");
}
void configure_prefix(){
    Fixture f;auto descriptor=field();auto cell=std::make_shared<IgbRuntimeFieldSlot>();descriptor->vtable_word.reset();
    IgbFieldConfigureInvocation run(descriptor,nullptr,{cell,0x80560000},0xfffffffcu,f.meta);
    check(!run.resume(f.services).completed && f.interns==0 && descriptor->global_slot->lock()==cell && descriptor->global_slot_address==0x80560000 &&
        descriptor->offset==0xfffffffcu && descriptor->declaring_type->lock()==f.meta,"Null-name configuration skipped stores or attempted string work.");
    descriptor->vtable_word=0x80476e6c;check(run.resume(f.services).completed && descriptor->size==4 && f.buffer_allocations==1 && !cell->has_value(),"Configuration finalized its global or skipped buffer preparation.");
    auto named=field();IgbFieldConfigureInvocation set(named,text("_value"),{cell,0x80560000},12,f.meta);
    check(set.resume(f.services).completed && f.interns==1 && named->name && *named->name=="_value" && (*named->name_string)->references==1,"Configured name did not retain then release its temporary.");
    auto old=std::make_shared<IgbInternedString>();old->value=text("old");old->references=1;named->name_string=old;
    bool ready{};unsigned selects{},polls{};f.services->dependencies->factory=[&](const IgbDependenciesCall& call){++selects;check(call.kind==IgbDependenciesServiceKind::release_string,"Null name unexpectedly interned.");
        auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared old-name release"};};return op;};
    IgbFieldConfigureInvocation release(named,nullptr,{},20,nullptr);
    check(!release.resume(f.services).completed && old->references==0 && named->offset==12 && named->global_slot->lock()==cell,"Old-name release pause applied later stores.");
    ready=true;check(release.resume(f.services).completed && selects==1 && polls==2 && named->offset==20 && igb_field_declaring_type_is_null(*named->declaring_type),"Resumed null-name configuration replayed release or kept declaring type.");
    for(const auto binding:{IgbFieldGlobalBinding{nullptr,0x80560000},IgbFieldGlobalBinding{cell,0}}){auto malformed=field();
        IgbFieldConfigureInvocation invalid(malformed,nullptr,binding,99,f.meta);fails([&]{invalid.resume(f.services);},"Contradictory global-cell binding was accepted.");
        check(malformed->offset==0 && !malformed->declaring_type,"Malformed global-cell store applied later descriptor fields.");}
}
void configure_physical_stores(){
    Fixture f;auto descriptor=field();auto physical=physical_field(descriptor);auto cell=std::make_shared<IgbRuntimeFieldSlot>();
    f.services->fields->pointer_lists->values={{0x400008,f.meta,{}},{0x400008,f.meta,{}}};bool buffer_saw_stores{};
    f.field_action=[&](const auto&){buffer_saw_stores=true;
        check(igb_bootstrap_read_word(physical,0x1c)==0x80560000 && igb_bootstrap_read_word(physical,8)==0xfffffffc &&
            igb_bootstrap_read_word(physical,0x18)==0x400008,"Buffer preparation preceded a physical schema store.");};
    IgbFieldConfigureInvocation run(descriptor,nullptr,{cell,0x80560000},0xfffffffcu,f.meta);
    check(run.resume(f.services).completed && buffer_saw_stores && f.buffer_allocations==1,"Physically bound configuration did not reach buffer preparation.");
    check(descriptor->global_slot->lock()==cell && descriptor->global_slot_address==0x80560000 && descriptor->offset==0xfffffffc &&
        descriptor->declaring_type->lock()==f.meta && !cell->has_value(),"Physical schema configuration changed its canonical fields or finalized the global early.");
    for(unsigned n=0;n<physical.offset;++n)check(physical.storage->bytes->bytes[n]==0xcc,"Schema store overwrote allocation prefix padding.");
    for(unsigned n=physical.offset+0x28;n<physical.storage->bytes->bytes.size();++n)check(physical.storage->bytes->bytes[n]==0xcc,"Schema store overwrote trailing allocation padding.");
    check(igb_bootstrap_read_word(physical,0x0c)==0xcccccccc,"Bounded raw-schema synchronization unexpectedly rewrote physical name storage.");
    igb_bootstrap_write_word(physical,8,7);check(run.resume().completed && igb_bootstrap_read_word(physical,8)==7,"Completed configuration replayed raw stores.");

    Fixture null;auto empty=field();auto empty_physical=physical_field(empty);null.services->fields->pointer_lists.reset();
    IgbFieldConfigureInvocation clear(empty,nullptr,{},12,nullptr);
    check(clear.resume(null.services).completed && igb_bootstrap_read_word(empty_physical,0x1c)==0 &&
        igb_bootstrap_read_word(empty_physical,0x18)==0 && igb_field_declaring_type_is_null(*empty->declaring_type),
        "Null raw pointers required an address registry or failed to store physical zero.");
}
void configure_physical_pending(){
    Fixture f;auto descriptor=field();auto physical=physical_field(descriptor);auto cell=std::make_shared<IgbRuntimeFieldSlot>();
    const auto captured=f.meta;IgbFieldConfigureInvocation run(descriptor,nullptr,{cell,0x80560000},12,captured);
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(physical,0x1c)==0x80560000 &&
        igb_bootstrap_read_word(physical,8)==12 && igb_bootstrap_read_word(physical,0x18)==0xcccccccc &&
        descriptor->global_slot_address==0x80560000 && descriptor->offset==12 && !descriptor->declaring_type && f.buffer_allocations==0,
        "Missing declaring address did not preserve exactly the completed +1C/+08 prefix.");
    // The pending +18 operation must not replay either preceding raw store.
    igb_bootstrap_write_word(physical,8,28);descriptor->offset=28;f.meta=metadata();
    f.services->fields->pointer_lists->values.push_back({0x400008,captured,{}});
    check(run.resume(f.services).completed && descriptor->offset==28 && igb_bootstrap_read_word(physical,8)==28 &&
        igb_bootstrap_read_word(physical,0x18)==0x400008 && descriptor->declaring_type->lock()==captured,
        "Pending +18 resumed with a different metadata receiver or replayed completed prefix stores.");

    for(unsigned variant=0;variant<4;++variant){Fixture conflict;auto item=field();auto raw=physical_field(item);
        auto& values=conflict.services->fields->pointer_lists->values;values.push_back({0x400008,conflict.meta,{}});
        if(variant==0)values.push_back({0x500008,conflict.meta,{}});
        else if(variant==1)values.push_back({0x400008,metadata(),{}});
        else if(variant==2)values.push_back({0x400008,{},field()});
        else values[0].address=0;
        IgbFieldConfigureInvocation invalid(item,nullptr,{cell,0x80560000},16,conflict.meta);
        fails([&]{invalid.resume(conflict.services);},"Contradictory metadata arithmetic binding was accepted.");
        check(invalid.faulted() && item->global_slot_address==0x80560000 && item->offset==16 && !item->declaring_type &&
            igb_bootstrap_read_word(raw,0x1c)==0x80560000 && igb_bootstrap_read_word(raw,8)==16 &&
            igb_bootstrap_read_word(raw,0x18)==0xcccccccc && conflict.buffer_allocations==0,
            "Address conflict lost an earlier store or committed the declaring-type word.");
    }
}
void configure_physical_fault_prefix(){
    Fixture first;auto descriptor=field();auto physical=physical_field(descriptor);auto cell=std::make_shared<IgbRuntimeFieldSlot>();
    physical.storage->bytes->bytes.resize(physical.offset+0x1e);physical.storage->bytes->initialized.resize(physical.offset+0x1e);
    IgbFieldConfigureInvocation short_global(descriptor,nullptr,{cell,0x80560000},12,first.meta);
    fails([&]{short_global.resume(first.services);},"Short physical +1C store was accepted.");
    check(!descriptor->global_slot_address && descriptor->offset==0 && !descriptor->declaring_type && first.buffer_allocations==0 &&
        physical.storage->bytes->bytes[physical.offset+0x1c]==0xcc,"Failed first physical store committed canonical or partial bytes.");

    Fixture last;auto later=field();auto later_physical=physical_field(later);IgbFieldConfigureInvocation short_type(later,nullptr,{cell,0x80560000},24,last.meta);
    check(!short_type.resume(last.services).completed,"Unrepresented declaring pointer unexpectedly completed.");
    later_physical.storage->bytes->bytes.resize(later_physical.offset+0x1a);later_physical.storage->bytes->initialized.resize(later_physical.offset+0x1a);
    last.services->fields->pointer_lists->values.push_back({0x400008,last.meta,{}});
    fails([&]{short_type.resume(last.services);},"Short physical +18 store was accepted.");
    check(later->global_slot_address==0x80560000 && later->offset==24 && !later->declaring_type &&
        igb_bootstrap_read_word(later_physical,8)==24 && later_physical.storage->bytes->bytes[later_physical.offset+0x18]==0xcc && last.buffer_allocations==0,
        "Failed physical +18 lost earlier canonical stores or committed declaring type.");
    fails([&]{short_type.resume(last.services);},"Faulted physical schema configuration restarted.");
}
void configure_physical_identity(){
    for(unsigned variant=0;variant<5;++variant){Fixture f;auto descriptor=field();auto physical=physical_field(descriptor);IgbBootstrapPointer replacement;
        IgbFieldConfigureInvocation run(descriptor,nullptr,{},12,f.meta);check(!run.resume(f.services).completed,"Missing address did not retain the descriptor operation.");
        if(variant==0)descriptor->physical_storage.reset();
        else if(variant==1)replacement=physical_field(descriptor,0x500008);
        else if(variant==2)descriptor->physical_storage->offset+=4;
        else if(variant==3)physical.storage->original_address+=4;
        else physical.storage->bindings[0].field=field();
        f.services->fields->pointer_lists->values.push_back({0x400008,f.meta,{}});
        fails([&]{run.resume(f.services);},"Pending configuration redirected its captured allocation identity.");
        check(run.faulted() && !descriptor->declaring_type && f.buffer_allocations==0,
            "Changed allocation binding reached the declaring-type or buffer publication.");
        if(replacement.storage)check(igb_bootstrap_read_word(replacement,0x18)==0xcccccccc,"Conflicting replacement allocation received a schema store.");
    }
    Fixture expired;auto descriptor=field();auto raw=physical_field(descriptor);raw.storage.reset();
    IgbFieldConfigureInvocation missing(descriptor,nullptr,{},12,expired.meta);
    fails([&]{missing.resume(expired.services);},"Expired explicit physical identity became a canonical-only fixture.");

    Fixture retained;std::weak_ptr<IgbObjectStorage> weak_storage;std::weak_ptr<IgbDecodeRuntimeField> weak_field;std::weak_ptr<IgbRuntimeMetaType> weak_type;
    {auto target=field();auto type=metadata();auto allocation=physical_field(target);weak_storage=allocation.storage;weak_field=target;weak_type=type;
        IgbFieldConfigureInvocation abandoned(target,nullptr,{},12,type);
        check(!abandoned.resume(retained.services).completed,"Prepared declaring-address boundary did not suspend.");
        allocation.storage.reset();target.reset();type.reset();
        check(!weak_storage.expired() && !weak_field.expired() && !weak_type.expired(),"Pending physical operation lost its captured owners.");}
    check(!weak_storage.expired() && !weak_field.expired() && !weak_type.expired(),"Abandoned physical configuration discarded its completed prefix owners.");
    retained.services->unfinished.clear();check(weak_storage.expired() && weak_field.expired() && weak_type.expired(),"Retained physical configuration created an ownership cycle.");

    Fixture naming;auto named=field();auto original=physical_field(named);auto old=std::make_shared<IgbInternedString>();old->references=1;named->name_string=old;
    naming.services->dependencies->factory=[&](const IgbDependenciesCall&){auto op=std::make_shared<IgbDependenciesOperation>();
        op->resume=[] {return MessengerCallResult{false,0,"prepared name release"};};return op;};
    IgbFieldConfigureInvocation releasing(named,nullptr,{},20,naming.meta);
    check(!releasing.resume(naming.services).completed && old->references==0,"Name release did not suspend before raw stores.");
    auto other=physical_field(named,0x500008);fails([&]{releasing.resume(naming.services);},"Name callback pause permitted descriptor allocation replacement.");
    check(named->offset==0 && igb_bootstrap_read_word(original,0x1c)==0xcccccccc && igb_bootstrap_read_word(other,0x1c)==0xcccccccc,
        "Identity failure during name release reached later raw schema stores.");
}
void schema_live_loop(){
    Fixture f;auto inherited=field(),first=field(),unused=field(),second=field();f.meta->fields->count=3;
    *f.meta->fields->backing={inherited,first,unused};auto replacement=metadata()->fields;replacement->count=0;*replacement->backing={nullptr,nullptr,second};
    auto names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{text("first"),text("second")});
    auto a=std::make_shared<IgbRuntimeFieldSlot>(),b=std::make_shared<IgbRuntimeFieldSlot>();
    auto globals=std::make_shared<IgbFieldGlobalTable>(IgbFieldGlobalTable{IgbFieldGlobalBinding{a,0x80560000},IgbFieldGlobalBinding{b,0x80560004}});
    auto offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{8,12});bool changed{};
    f.field_action=[&](const IgbFieldOperationCall& call){if(call.kind==FK::allocate_default && !changed){changed=true;f.meta->fields=replacement;(*offsets)[1]=28;}};
    IgbFieldSchemaInvocation run(f.meta,names,globals,offsets,1);check(run.resume(f.services).completed,"Schema loop did not complete.");
    check(*a==first && *b==second && first->offset==8 && second->offset==28 && first->type_ordinal==0xffff && second->type_ordinal==0xffff,"Schema lost original count, fresh field list, live tables or finalization.");
    check(!inherited->name && !unused->name && f.interns==2 && f.buffer_allocations==2 && first->declaring_type->lock()==f.meta,"Schema configured inherited/replaced fields or skipped buffer allocation.");
    check(run.resume().completed && f.interns==2,"Completed schema replayed.");
    auto empty=metadata();empty->fields->count=0;IgbFieldSchemaInvocation zero(empty,nullptr,nullptr,nullptr,0);
    check(zero.resume().completed,"Empty schema read absent tables.");
    IgbFieldSchemaInvocation null(nullptr,nullptr,nullptr,nullptr,0xffffffffu);fails([&]{null.resume();},"Schema skipped mandatory count dereference.");
}
void schema_physical_order(){
    Fixture f;auto original=field(),changed=field();f.meta->fields->count=1;(*f.meta->fields->backing)[0]=original;
    auto names=std::make_shared<IgbFieldNameTable>(IgbFieldNameTable{SystemStageString{}});
    auto globals=std::make_shared<IgbFieldGlobalTable>(1);auto offsets=std::make_shared<IgbFieldOffsetTable>(IgbFieldOffsetTable{4});
    IgbFieldSchemaInvocation run(f.meta,names,globals,offsets,0);check(!run.resume(f.services).completed,"Missing global-table entry did not pause.");
    auto prior=f.meta->fields->backing;f.meta->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(1,changed);(*globals)[0]=IgbFieldGlobalBinding{};
    check(run.resume(f.services).completed && original->offset==4 && changed->offset==0,"Schema reread backing after reaching its global-table read.");
    Fixture wrapped;wrapped.meta->fields->count=static_cast<std::int32_t>(0x80000001u);auto target=field();(*wrapped.meta->fields->backing)[0]=target;
    IgbFieldSchemaInvocation negative(wrapped.meta,names,globals,offsets,0x80000000u);
    check(negative.resume(wrapped.services).completed && target->offset==4,"Schema did not preserve signed comparison and wrapped pointer index.");
    Fixture bad;bad.meta->fields->count=1;IgbFieldSchemaInvocation physical(bad.meta,names,globals,offsets,0xffffffffu);
    fails([&]{physical.resume(bad.services);},"Negative physical field offset was accepted.");
}
void adapter_and_reentry(){
    auto services=std::make_shared<IgbFieldSchemaServices>();auto adapter=make_native_igb_field_schema_factory(services);
    IgbFieldSchemaCall call;call.kind=SK::create_field;call.function=0x1234;check(!adapter(call),"Adapter manufactured an unknown specialized constructor.");
    call.function=0x80022a90;auto op=adapter(call);check(op && !op->resume().completed && !op->field_result,"Known constructor did not reach native required services.");
    services.reset();op.reset();fails([&]{adapter(call);},"Expired adapter silently recreated services.");
    Fixture f;auto table=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x1111});IgbFieldBatchInvocation run(f.meta,table,1);
    f.schema_action=[&](const auto&,auto&){fails([&]{run.resume(f.services);},"Recursive batch was accepted.");};
    fails([&]{run.resume(f.services);},"Outer batch survived recursive poisoning.");check(run.faulted() && f.meta->fields->count==0,"Recursive poisoning reached append.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_schema_tests");
    batch_order();batch_pending_and_failures();packed_release();configure_prefix();configure_physical_stores();configure_physical_pending();
    configure_physical_fault_prefix();configure_physical_identity();schema_live_loop();schema_physical_order();adapter_and_reentry();
    std::cout<<"igb_field_schema_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Field schema failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
