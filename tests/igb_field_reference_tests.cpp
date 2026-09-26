#include "dw4/igb_field_reference.hpp"
#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_object_defaults.hpp"
#include "dw4/igb_string_pool.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
using FK=IgbFieldOperationKind;using SK=IgbFieldSchemaServiceKind;using PK=IgbPointerListServiceKind;
using MK=IgbMetadataOperationKind;using BK=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& run,const char* why){++checks;try{run();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
auto bytes(std::size_t size){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,0xcc);b->initialized.assign(size,1);return b;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){auto s=std::make_shared<IgbObjectStorage>();s->original_address=address;s->bytes=bytes(size);return {s,0};}
// Deliberately bypass synchronization to represent a physical guest mutation and
// a stale host cache independently. This helper is fixture-only.
void raw_word(const IgbBootstrapPointer& p,std::uint32_t relative,std::uint32_t value){
    const auto at=std::size_t(p.offset)+relative;for(unsigned i=0;i<4;++i){p.storage->bytes->bytes.at(at+i)=std::uint8_t(value>>(24-8*i));p.storage->bytes->initialized.at(at+i)=1;}}
std::uint32_t raw_reference(const IgbBootstrapPointer& p){return igb_bootstrap_read_word(p,4);}
auto descriptor(){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=0x80476e6c;f->name_string=std::shared_ptr<IgbInternedString>{};
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4),0};f->default_bytes_address=0x220000;f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;return f;}
IgbBootstrapPointer physical_field(const std::shared_ptr<IgbDecodeRuntimeField>& f,std::uint32_t reference=1,std::uint32_t address=0x300000){
    auto p=allocation(address,0x48);p.offset=8;p.storage->bindings.push_back({8,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{p.storage,8};
    raw_word(p,0,0x80476e6c);raw_word(p,4,reference);raw_word(p,0x1c,0);return p;}
auto metadata(std::uint32_t address=0x900000){auto m=std::make_shared<IgbRuntimeMetaType>();m->type_word=0x804717f0;
    m->native_alignment=8;m->native_size=24;m->allocation_prefix=12;m->parent=std::shared_ptr<IgbRuntimeMetaType>{};
    m->fields=std::make_shared<IgbDecodeFieldRegistry>();m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);
    m->fields->backing_bytes=bytes(64);m->fields->backing_address=address;return m;}

void physical_words_and_atomicity(){
    auto f=descriptor();auto p=physical_field(f,0xa5000041);f->reference_word=0x11223344;IgbFieldReferenceAccess access(f);
    check(access.read()==0xa5000041 && f->reference_word==0x11223344,"Physical read used or refreshed the canonical cache.");
    access.write(access.read()+1);check(raw_reference(p)==0xa5000042 && f->reference_word==0xa5000042,"Retain did not preserve the full physical packed word.");
    raw_word(p,4,0xffffffff);f->reference_word=17;access.write(access.read()+1);
    check(raw_reference(p)==0 && f->reference_word==0,"Full32 retain did not wrap through zero.");
    access.write(access.read()-1);check(raw_reference(p)==0xffffffff && f->reference_word==0xffffffff,"Full32 release did not wrap through all bits.");
    std::fill(p.storage->bytes->initialized.begin(),p.storage->bytes->initialized.end(),0);
    access.write(0x80000000);check(access.read()==0x80000000 && f->reference_word==0x80000000,"Writing an unknown represented word required old contents or known padding.");
    check(p.storage->bytes->initialized[0]==0 && p.storage->bytes->initialized[p.offset+8]==0,"Reference store initialized unrelated padding.");
    auto unknown=descriptor();auto q=physical_field(unknown,9);q.storage->bytes->initialized[q.offset+5]=0;IgbFieldReferenceAccess unread(unknown);
    rejects([&]{unread.read();},"An unknown packed byte was read as a value.");q.storage->bytes->initialized[q.offset+5]=1;
    rejects([&]{unread.write(8);},"A failed physical read was revived by restoring knownness.");check(raw_reference(q)==9 && unread.faulted(),"Sticky read failure changed the word.");
    for(unsigned malformed=0;malformed<4;++malformed){auto field=descriptor();auto physical=physical_field(field,0x34567890);field->reference_word=0x76543210;
        auto& b=*physical.storage->bytes;
        if(malformed==0)b.initialized.pop_back();else if(malformed==1)b.initialized.push_back(1);
        else if(malformed==2){b.bytes.resize(physical.offset+7);b.initialized.resize(b.bytes.size());}
        else b.released=true;
        const auto old_bytes=b.bytes,old_mask=b.initialized;IgbFieldReferenceAccess bad(field);
        rejects([&]{bad.write(0xfedcba98);},"Malformed physical storage accepted a packed word store.");
        check(b.bytes==old_bytes && b.initialized==old_mask && field->reference_word==0x76543210 && bad.faulted(),"Failed word store changed a byte, mask or cache.");
    }
}

void identity_and_lifetime(){
    IgbFieldReferenceAccess null(nullptr);check(!null.faulted(),"Reference capture dereferenced a null descriptor.");
    rejects([&]{null.read();},"Null descriptor access succeeded.");
    auto canonical=descriptor();canonical->reference_word=0xffffffff;IgbFieldReferenceAccess host(canonical);
    host.write(host.read()+1);check(canonical->reference_word==0,"Canonical-only legacy profile lost full32 arithmetic.");
    auto acquired=physical_field(canonical,55);rejects([&]{host.write(1);},"Initially canonical access silently acquired a physical binding.");
    check(raw_reference(acquired)==55 && canonical->reference_word==0,"Binding acquisition failure wrote either representation.");
    for(unsigned mutation=0;mutation<7;++mutation){auto f=descriptor();auto p=physical_field(f,0x81000003);f->reference_word=13;
        const auto old_binding=f->physical_storage;const auto old_bytes=p.storage->bytes;const auto old_address=p.storage->original_address;
        IgbFieldReferenceAccess captured(f);
        if(mutation==0)f->physical_storage.reset();
        if(mutation==1)++f->physical_storage->offset;
        if(mutation==2)++p.storage->original_address;
        if(mutation==3)p.storage->bytes=bytes(0x48);
        if(mutation==4)p.storage->bindings.clear();
        if(mutation==5)old_bytes->released=true;
        if(mutation==6)f->construction_fault_=true;
        rejects([&]{captured.write(4);},"Captured physical identity change was accepted.");
        f->physical_storage=old_binding;p.storage->bytes=old_bytes;p.storage->original_address=old_address;
        p.storage->bindings={{8,{},{},{},{},f}};old_bytes->released=false;f->construction_fault_=false;
        rejects([&]{captured.read();},"Restored identity revived a permanently faulted reference access.");
        check(raw_reference(p)==0x81000003 && f->reference_word==13,"Identity failure altered the retained allocation or canonical cache.");
    }
    auto expired=descriptor();{auto temporary=physical_field(expired,4);}IgbFieldReferenceAccess late(expired);
    check(!late.faulted(),"Capturing an expired physical allocation failed eagerly.");rejects([&]{late.read();},"Expired physical binding fell back to the canonical cache.");
    auto f=descriptor();auto p=physical_field(f,5);std::weak_ptr<IgbScratchBuffer> weak=p.storage->bytes;
    {IgbFieldReferenceAccess captured(f);p.storage->bytes=bytes(0x48);check(!weak.expired(),"Capture failed to retain its original byte owner.");
        rejects([&]{captured.read();},"Replacement byte owner redirected the captured read.");}
    check(weak.expired(),"Leaf reference access introduced a permanent owner cycle.");
}

// Prepared allocation endpoints surround the actual native retain, raw append,
// finalization and creator-release callers. They do not perform reference work.
struct Fixture {
    std::shared_ptr<IgbFieldSchemaServices> schema=std::make_shared<IgbFieldSchemaServices>();
    std::shared_ptr<IgbFieldOperationsServices> fields=std::make_shared<IgbFieldOperationsServices>();
    std::shared_ptr<IgbPointerListServices> pointers=std::make_shared<IgbPointerListServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata();
    std::shared_ptr<IgbDecodeRuntimeField> field=descriptor();
    IgbBootstrapPointer physical=physical_field(field);
    std::shared_ptr<IgbFieldFactoryTable> factories=std::make_shared<IgbFieldFactoryTable>(IgbFieldFactoryTable{0x80022a90});
    bool pointer_ready=true,destroy_ready=true,null_result{};
    unsigned creates{},destroy_selections{},destroy_polls{},pointer_selections{},pointer_polls{};
    std::function<void(const IgbPointerListCall&)> pointer_action;
    Fixture(){
        schema->types=std::make_shared<IgbTypeRegistrationServices>();schema->types->metadata_pool_handle=5;
        schema->pools=std::make_shared<IgbFilePoolsServices>();schema->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        auto manager=make_original_igb_memory_pool_registry();manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema->pools->memory_pools->selector=0;schema->pools->memory_pools->fallback=manager;
        schema->fields=fields;fields->pointer_lists=pointers;fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        pointers->values.push_back({igb_bootstrap_address(physical),{},field});
        pointers->factory=[this](const IgbPointerListCall& c){++pointer_selections;auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{++pointer_polls;if(c.kind==PK::allocation_pool)out->pool_result=pool;
                else if(c.kind==PK::allocation_size){if(!pointer_ready)return MessengerCallResult{false,0,"prepared registry size"};out->size_result=64;}
                else throw std::runtime_error("Unexpected registry growth in reference fixture.");
                if(pointer_action)pointer_action(c);return MessengerCallResult{true,0,{}};};return op;};
        fields->factory=make_native_igb_unsigned_int_field_factory(fields,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==FK::allocation_pool)out->pool_result=pool;
                else if(c.kind==FK::allocation_size)return MessengerCallResult{true,4,{}};
                else throw std::runtime_error("Unexpected field buffer allocation in reference fixture.");return MessengerCallResult{true,0,{}};};return op;});
        schema->factory=[this](const IgbFieldSchemaCall& c){auto op=std::make_shared<IgbFieldSchemaOperation>();
            if(c.kind==SK::create_field){++creates;op->field_result=null_result?nullptr:field;op->field_address=null_result?0:igb_bootstrap_address(physical);
                op->resume=[]{return MessengerCallResult{true,0,{}};};}
            else{++destroy_selections;check(c.field==field,"Creator release selected another descriptor.");op->resume=[this]{++destroy_polls;return MessengerCallResult{destroy_ready,0,"prepared destructor"};};}
            return op;};
    }
};

void batch_captured_append_and_fresh_release(){
    Fixture f;raw_word(f.physical,4,0xa5000001);f.field->reference_word=23;f.pointer_ready=false;IgbFieldBatchInvocation call(f.meta,f.factories,1);
    check(!call.resume(f.schema).completed && raw_reference(f.physical)==0xa5000002 && f.field->reference_word==0xa5000002 && f.meta->fields->count==0,
        "Pending raw append did not preserve the actual physical retain prefix.");
    const auto captured=f.meta->fields;f.meta->fields=metadata(0x910000)->fields;f.pointer_ready=true;f.destroy_ready=false;
    f.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size){raw_word(f.physical,4,0x87800001);f.field->reference_word=99;}};
    check(!call.resume(f.schema).completed && captured->count==1 && f.meta->fields->count==0 && f.creates==1,
        "Suspended raw append changed the captured registry or repeated construction.");
    check(raw_reference(f.physical)==0x87800000 && f.field->reference_word==0x87800000 && f.destroy_selections==1 && f.destroy_polls==1,
        "Creator release did not freshly read the physical full32 word and test low23.");
    raw_word(f.physical,4,0x12345678);f.field->reference_word=12;f.destroy_ready=true;
    f.schema->factory=[](const auto&)->std::shared_ptr<IgbFieldSchemaOperation>{throw std::runtime_error("Pending destructor was reselected.");};
    check(call.resume(f.schema).completed && raw_reference(f.physical)==0x12345678 && f.field->reference_word==12 && f.destroy_polls==2,
        "Retained destructor continuation repeated creator release or refreshed its cache.");
    check(call.resume().completed && f.pointer_selections==2 && f.creates==1,"Completed batch replayed a lower operation.");
    Fixture fresh;raw_word(fresh.physical,4,0x80000000);fresh.field->reference_word=7;bool changed{};IgbFieldBatchInvocation low(fresh.meta,fresh.factories,1);
    const auto mutate_after_store=[&]{if(!changed && fresh.meta->fields->count==1 && raw_reference(fresh.physical)==0x80000000){
        changed=true;raw_word(fresh.physical,4,0x80000002);}};
    check(low.resume(fresh.schema,mutate_after_store).completed && changed && fresh.destroy_selections==0 && raw_reference(fresh.physical)==0x80000002 && fresh.field->reference_word==0x80000000,
        "Low23 branch reused the decrement result or wrote its fresh-read cache.");
    Fixture wrap;raw_word(wrap.physical,4,0xffffffff);wrap.pointer_ready=false;IgbFieldBatchInvocation full(wrap.meta,wrap.factories,1);
    check(!full.resume(wrap.schema).completed && raw_reference(wrap.physical)==0,"Batch retain masked away full32 wrap.");wrap.pointer_ready=true;
    check(full.resume(wrap.schema).completed && raw_reference(wrap.physical)==0xffffffff && wrap.destroy_selections==0,"Batch release did not wrap the retained zero.");
}

void batch_failure_order_and_owner_retention(){
    Fixture null;null.null_result=true;IgbFieldBatchInvocation no_field(null.meta,null.factories,1);
    rejects([&]{no_field.resume(null.schema);},"Null constructor result skipped its later creator-release fault.");
    check(no_field.faulted() && null.meta->fields->count==1 && !null.meta->fields->backing->at(0) && igb_buffer_word(null.meta->fields->backing_bytes,0)==0,
        "Null result was not appended before the late release dereference.");
    Fixture no_meta;raw_word(no_meta.physical,4,10);IgbFieldBatchInvocation absent(nullptr,no_meta.factories,1);
    rejects([&]{absent.resume(no_meta.schema);},"Null metadata completed.");check(raw_reference(no_meta.physical)==10,"Null metadata fault performed a premature retain.");
    Fixture no_list;raw_word(no_list.physical,4,10);no_list.meta->fields.reset();IgbFieldBatchInvocation list(no_list.meta,no_list.factories,1);
    rejects([&]{list.resume(no_list.schema);},"Null field list completed.");check(raw_reference(no_list.physical)==11,"Null list failure omitted its reached retain.");
    Fixture f;f.pointer_ready=false;std::weak_ptr<IgbScratchBuffer> weak=f.physical.storage->bytes;auto old=f.physical.storage->bytes;
    {IgbFieldBatchInvocation pending(f.meta,f.factories,1);check(!pending.resume(f.schema).completed,"Batch owner fixture did not suspend.");
        f.physical.storage->bytes=bytes(0x48);const auto replacement=f.physical.storage->bytes;f.pointer_ready=true;
        rejects([&]{pending.resume(f.schema);},"Creator release followed a replacement physical byte owner.");
        check(f.meta->fields->count==1 && igb_buffer_word(old,12)==2 && replacement->bytes==std::vector<std::uint8_t>(0x48,0xcc),
            "Identity fault occurred before raw append or changed the completed retain/replacement bytes.");
        f.physical.storage->bytes=old;rejects([&]{pending.resume(f.schema);},"Restoring descriptor bytes revived a failed batch.");f.physical.storage->bytes=replacement;old.reset();}
    check(!weak.expired(),"Abandoned batch failed to retain the overwritten original byte owner.");
    f.schema->unfinished.clear();check(weak.expired(),"Batch byte retention created an owner cycle.");
}

void finalized_append_reference_order(){
    Fixture f;raw_word(f.physical,4,0x99000020);f.field->reference_word=0;auto published=std::make_shared<IgbRuntimeFieldSlot>();
    f.field->global_slot=published;f.field->global_slot_address=0x805622a8;raw_word(f.physical,0x1c,0x805622a8);
    f.pointer_ready=false;IgbFieldAppendInvocation call(f.meta,f.field);
    check(!call.resume(f.fields).completed && *published==f.field && f.field->type_ordinal==0xffff && f.field->size==4 && raw_reference(f.physical)==0x99000021,
        "Finalized append did not publish defaults/global before the physical retain.");
    auto captured=f.meta->fields;f.meta->fields=metadata(0x910000)->fields;f.pointer_ready=true;
    check(call.resume(f.fields).completed && captured->count==1 && f.meta->fields->count==0 && raw_reference(f.physical)==0x99000021,
        "Finalized append repeated its retain, changed registry or released the caller's reference.");
    Fixture unknown;auto cell=std::make_shared<IgbRuntimeFieldSlot>();unknown.field->global_slot=cell;
    unknown.field->global_slot_address=0x805622a8;raw_word(unknown.physical,0x1c,0x805622a8);
    unknown.physical.storage->bytes->initialized[unknown.physical.offset+4]=0;
    const auto cache=unknown.field->reference_word;IgbFieldAppendInvocation bad(unknown.meta,unknown.field);
    rejects([&]{bad.resume(unknown.fields);},"Unknown reference byte completed finalized append.");
    check(*cell==unknown.field && unknown.field->size==4 && unknown.meta->fields->count==0 && unknown.field->reference_word==cache,
        "Reference failure moved before finalization or appended an unretained field.");
    Fixture captured_owner;auto before=captured_owner.physical.storage->bytes;IgbFieldAppendInvocation rebound(captured_owner.meta,captured_owner.field);
    captured_owner.physical.storage->bytes=std::make_shared<IgbScratchBuffer>(*before);
    rejects([&]{rebound.resume(captured_owner.fields);},"Constructor-captured append identity was recaptured after finalization.");
    check(captured_owner.meta->fields->count==0 && igb_buffer_word(before,12)==1,"Append identity failure changed the original reference or appended.");
    Fixture held;held.pointer_ready=false;std::weak_ptr<IgbScratchBuffer> weak=held.physical.storage->bytes;
    {IgbFieldAppendInvocation pending(held.meta,held.field);check(!pending.resume(held.fields).completed && raw_reference(held.physical)==2,
        "Finalized append owner fixture did not retain before waiting.");held.physical.storage->bytes=bytes(0x48);}
    check(!weak.expired(),"Abandoned finalized append dropped its original physical byte owner.");
    held.fields->unfinished.clear();check(weak.expired(),"Finalized append retention created a byte-owner cycle.");
}

void inherited_physical_retains(){
    Fixture f;auto parent=metadata(0x920000);auto second=descriptor();auto second_bytes=physical_field(second,17,0x310000);second->reference_word=80;
    raw_word(f.physical,4,0xffffffff);f.field->reference_word=99;parent->fields->count=2;parent->fields->backing->at(0)=f.field;
    auto children=std::make_shared<IgbRuntimeTypeList>();parent->children=children;
    auto services=std::make_shared<IgbMetadataOperationsServices>();bool ready{};unsigned selections{},polls{},child_calls{};
    services->factory=[&](const IgbMetadataOperationCall& c){auto op=std::make_shared<IgbMetadataOperation>();
        if(c.kind==MK::append_child){op->resume=[&]{++child_calls;return MessengerCallResult{true,0,{}};};return op;}
        check(c.kind==MK::append_field,"Inheritance selected an unexpected prepared operation.");++selections;
        op->resume=[&,c]{++polls;if(!ready)return MessengerCallResult{false,0,"prepared inherited raw append"};
            if(!c.fields)throw std::runtime_error("Prepared append dereferences null field list.");
            c.fields->backing->at(std::size_t(c.fields->count))=c.field;++c.fields->count;return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataParentInvocation call(f.meta,parent);check(!call.resume(services).completed && raw_reference(f.physical)==0 && f.field->reference_word==0 && child_calls==1,
        "Inherited physical retain did not precede the pending append.");
    auto first_destination=f.meta->fields;f.meta->fields=metadata(0x930000)->fields;auto fresh_source=metadata(0x940000)->fields;
    fresh_source->count=-20;fresh_source->backing->at(1)=second;parent->fields=fresh_source;ready=true;
    check(call.resume(services).completed && first_destination->count==1 && first_destination->backing->at(0)==f.field && f.meta->fields->count==1 && f.meta->fields->backing->at(0)==second,
        "Inheritance did not retain captured count/destination while freshly selecting the next descriptor.");
    check(raw_reference(f.physical)==0 && raw_reference(second_bytes)==18 && second->reference_word==18 && selections==2 && polls==3,
        "Inherited references were released, recomputed from cache or retained twice.");
    check(call.resume().completed && child_calls==1,"Completed inheritance replayed child registration.");
    Fixture null;auto one=metadata();one->children=children;one->fields->count=1;auto service=std::make_shared<IgbMetadataOperationsServices>();unsigned null_appends{};
    service->factory=[&](const IgbMetadataOperationCall& c){auto op=std::make_shared<IgbMetadataOperation>();op->resume=[&,c]{if(c.kind==MK::append_field){
        check(!c.field,"Null inheritance fixture selected a field.");++null_appends;c.fields->backing->at(0)=c.field;++c.fields->count;}return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataParentInvocation null_field(null.meta,one);check(null_field.resume(service).completed && null_appends==1 && null.meta->fields->count==1,"Null inherited field was rejected instead of skipping retain.");
    Fixture missing;missing.meta->fields.reset();auto failed_parent=metadata();failed_parent->children=children;
    failed_parent->fields->count=1;failed_parent->fields->backing->at(0)=missing.field;raw_word(missing.physical,4,23);missing.field->reference_word=90;
    auto fail_services=std::make_shared<IgbMetadataOperationsServices>();fail_services->factory=[](const IgbMetadataOperationCall& c){
        auto op=std::make_shared<IgbMetadataOperation>();op->resume=[c]{if(c.kind==MK::append_field && !c.fields)throw std::runtime_error("Prepared append dereferences null list.");
            return MessengerCallResult{true,0,{}};};return op;};
    IgbMetadataParentInvocation bad(missing.meta,failed_parent);rejects([&]{bad.resume(fail_services);},"Missing inherited destination completed.");
    check(raw_reference(missing.physical)==24 && missing.field->reference_word==24 && missing.meta->parent && *missing.meta->parent==failed_parent,
        "Failed inherited append rolled back its earlier retain or parent store.");
    missing.meta->fields=metadata()->fields;rejects([&]{bad.resume(fail_services);},"Restoring inherited destination replayed a faulted retain.");
    Fixture held;auto source=metadata();source->children=children;source->fields->count=1;source->fields->backing->at(0)=held.field;
    auto wait=std::make_shared<IgbMetadataOperationsServices>();wait->factory=[](const IgbMetadataOperationCall& c){auto op=std::make_shared<IgbMetadataOperation>();
        op->resume=[c]{return MessengerCallResult{c.kind==MK::append_child,0,"prepared pending inherited append"};};return op;};
    std::weak_ptr<IgbScratchBuffer> weak=held.physical.storage->bytes;
    {IgbMetadataParentInvocation pending(held.meta,source);check(!pending.resume(wait).completed && raw_reference(held.physical)==2,"Inheritance owner fixture did not retain before waiting.");held.physical.storage->bytes=bytes(0x48);}
    check(!weak.expired(),"Pending inheritance lost its captured byte owner after registry replacement.");wait->unfinished.clear();check(weak.expired(),"Inheritance retention created a byte-owner cycle.");
}

// Adapted from the existing ObjectDefaults fixture. Both specialized native
// constructors, naming, finalization and pointer-list append stay in this path.
struct ObjectFixture {
    std::shared_ptr<IgbObjectDefaultsServices> services=std::make_shared<IgbObjectDefaultsServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(0x900000);
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbStringPoolServices> strings=std::make_shared<IgbStringPoolServices>();
    std::shared_ptr<IgbStringPoolState> string_state=std::make_shared<IgbStringPoolState>();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> physical;
    std::vector<IgbFieldOperationCall> field_calls;
    unsigned allocations{},name_allocations{},name_polls{},buffer_allocations{},buffer_polls{},size_queries{};
    bool name_ready=true,buffer_ready=true;
    std::function<void(const IgbPointerListCall&)> pointer_action;
    std::function<void(const IgbFieldOperationCall&,IgbFieldOperation&)> field_action;
    ObjectFixture(){
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(meta);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->dependencies=std::make_shared<IgbDependenciesServices>();services->dependencies->globals=std::make_shared<SystemStageServices>();services->dependencies->globals->string_pool=pool;
        strings->mutex=std::shared_ptr<LoaderDirectoryMutex>{};string_state->table=std::make_shared<IgbStringTable>();string_state->table->capacity=4;
        string_state->table->storage=std::make_shared<IgbStringTable::Storage>(4);string_state->arena=pool;igb_bind_string_pool(strings,pool,string_state);
        strings->factory=[this](const IgbStringPoolCall& c){check(c.kind==IgbStringPoolServiceKind::allocate_string,"Unexpected string-table growth in fixture.");++name_allocations;
            auto op=std::make_shared<IgbStringPoolOperation>();op->resume=[this,c,out=op.get()]{++name_polls;if(!name_ready)return MessengerCallResult{false,0,"prepared name arena"};
                out->entry_result=std::make_shared<IgbStringEntry>();out->entry_result->owner=std::make_shared<IgbInternedString>();
                out->entry_result->bytes.buffer=std::make_shared<IgbScratchBuffer>();out->entry_result->bytes.buffer->bytes.resize(c.bytes+4);
                out->entry_result->bytes.buffer->initialized.assign(c.bytes+4,0);out->container_result=pool;return MessengerCallResult{true,0,{}};};return op;};
        services->dependencies->strings=strings->strings;services->dependencies->factory=make_native_igb_string_factory(strings);
        services->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();services->meta_fields->dependencies=services->dependencies;
        services->meta_fields->bootstrap=std::make_shared<IgbBootstrapServices>();services->meta_fields->bootstrap->runtime=services->types->runtime;
        services->meta_fields->bootstrap->factory=[this](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x34,"Specialized factory allocation changed.");
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{check(bool(c.pool),"Factory lost selected allocation pool.");++allocations;
                auto field=std::make_shared<IgbDecodeRuntimeField>();created.push_back(field);auto bytes=allocation(0x100000+allocations*0x100,0x34);
                bytes.storage->bindings.push_back({0,{},{},{},{},field});physical.push_back(bytes);out->allocation_result=bytes;return MessengerCallResult{true,0,{}};};return op;};
        services->fields=std::make_shared<IgbFieldOperationsServices>();services->fields->dependencies=services->dependencies;
        services->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool)out->pool_result=pool;
            else if(c.kind==PK::allocation_size){++size_queries;out->size_result=16;}
            else throw std::runtime_error("Prepared registry unexpectedly grew");if(pointer_action)pointer_action(c);return MessengerCallResult{true,0,{}};};return op;};
        services->fields->factory=make_native_igb_unsigned_int_field_factory(services->fields,[this](const IgbFieldOperationCall& c){
            field_calls.push_back(c);if(c.kind==FK::allocate_default)++buffer_allocations;
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==FK::object_pool){check(c.handle==0,"Specialized reset did not leave pool tag zero.");out->pool_result=pool;}
                else if(c.kind==FK::allocate_default){++buffer_polls;check(c.pool==pool && c.bytes==4 && c.alignment==1,"Native default allocation arguments differ.");
                    if(!buffer_ready)return MessengerCallResult{false,0,"prepared default allocator"};
                    auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(4,0);bytes->initialized.assign(4,1);
                    out->buffer_result={bytes,0};out->buffer_address=0x200000+buffer_allocations*0x100;
                }else throw std::runtime_error("Unexpected field virtual or default-buffer operation");
                if(field_action)field_action(c,*out);return MessengerCallResult{true,0,{}};};return op;});
    }
};

void object_defaults_physical_creator_release(){
    ObjectFixture f;bool injected{},ready{};unsigned selections{},polls{};
    f.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size && !injected){
        check(f.created.size()==1 && raw_reference(f.physical[0])==2 && f.created[0]->reference_word==2,
            "Real ObjectDefaults finalized append did not physically retain the constructor result.");
        injected=true;raw_word(f.physical[0],4,0x95800001);f.created[0]->reference_word=73;}};
    f.services->factory=[&](const IgbObjectDefaultsCall& c){++selections;check(c.field==f.created[0],"ObjectDefaults selected the wrong low23-zero field.");
        auto op=std::make_shared<IgbObjectDefaultsOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared field destruction"};};return op;};
    IgbObjectDefaultsInvocation call;check(!call.resume(f.services).completed && selections==1 && polls==1 && f.meta->fields->count==1 && f.allocations==1,
        "ObjectDefaults did not suspend after first physical creator release and append.");
    check(raw_reference(f.physical[0])==0x95800000 && f.created[0]->reference_word==0x95800000 && *f.services->ref_count==f.created[0],
        "ObjectDefaults creator release used stale canonical bits or omitted physical/global publication.");
    raw_word(f.physical[0],4,0xf0000027);f.created[0]->reference_word=42;ready=true;
    f.services->factory=[](const auto&)->std::shared_ptr<IgbObjectDefaultsOperation>{throw std::runtime_error("Pending creator destructor was reselected.");};
    check(call.resume(f.services).completed && f.allocations==2 && f.meta->fields->count==2 && selections==1 && polls==2,
        "ObjectDefaults replayed the first creator/destructor or skipped the second real field.");
    check(raw_reference(f.physical[0])==0xf0000027 && f.created[0]->reference_word==42 && raw_reference(f.physical[1])==1 && f.created[1]->reference_word==1,
        "Resumed destruction repeated the first decrement or second creator release did not synchronize physical storage.");
    check(call.resume().completed && f.allocations==2,"Completed ObjectDefaults repeated its two-field sequence.");
    ObjectFixture wrap;bool wrapped{};wrap.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size && !wrapped){
        wrapped=true;raw_word(wrap.physical[0],4,0);wrap.created[0]->reference_word=1;}};
    IgbObjectDefaultsInvocation underflow;check(underflow.resume(wrap.services).completed && raw_reference(wrap.physical[0])==0xffffffff && wrap.created[0]->reference_word==0xffffffff,
        "ObjectDefaults physical zero release failed to wrap full32 or incorrectly entered destruction.");
}

void object_defaults_capture_and_unfinished_owner(){
    ObjectFixture f;bool replaced{};std::weak_ptr<IgbScratchBuffer> weak;
    f.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size && !replaced){replaced=true;
        check(raw_reference(f.physical[0])==2,"ObjectDefaults owner fixture did not reach its physical retain.");weak=f.physical[0].storage->bytes;
        f.physical[0].storage->bytes=bytes(0x34);}};
    {IgbObjectDefaultsInvocation call;rejects([&]{call.resume(f.services);},"ObjectDefaults creator release used a replacement physical byte owner.");
        check(call.faulted() && f.meta->fields->count==1 && f.allocations==1 && f.created[0]->reference_word==2 && !weak.expired(),
            "ObjectDefaults identity fault lost its append/retain prefix or constructed the second field.");
        check(igb_buffer_word(weak.lock(),4)==2 && f.physical[0].storage->bytes->bytes==std::vector<std::uint8_t>(0x34,0xcc),
            "Faulted creator release changed original retained bytes or replacement allocation.");}
    check(!weak.expired(),"Abandoned ObjectDefaults omitted its captured original byte owner.");
    f.services->unfinished.clear();check(weak.expired(),"ObjectDefaults unfinished reference owns a service cycle.");
}
}

int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_reference_tests");
    physical_words_and_atomicity();identity_and_lifetime();batch_captured_append_and_fresh_release();batch_failure_order_and_owner_retention();
    finalized_append_reference_order();inherited_physical_retains();object_defaults_physical_creator_release();object_defaults_capture_and_unfinished_owner();
    std::cout<<"igb_field_reference_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Packed field references failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}

