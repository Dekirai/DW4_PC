#include "dw4/igb_field_defaults.hpp"
#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_prototype.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;
using FK=IgbFieldOperationKind;
constexpr std::uint32_t structure_table=0x80470e00;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto buffer(std::size_t size,std::uint8_t value=0xcd,std::uint8_t initialized=1){
    auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(size,value);out->initialized.assign(size,initialized);return out;
}
struct BoundField {
    IgbObjectInstance object;
    BoundField(){object.offset=0x10;object.storage=std::make_shared<IgbObjectStorage>();object.storage->original_address=0x100000;
        object.storage->bytes=buffer(0x54,0xcd,0);object.field=std::make_shared<IgbDecodeRuntimeField>();
        object.storage->bindings.push_back({object.offset,{},{},{},{},object.field});
        object.field->physical_storage=IgbFieldStorageBinding{object.storage,object.offset};
        igb_bootstrap_write_word({object.storage,object.offset},0,structure_table);
        igb_bootstrap_write_word({object.storage,object.offset},4,1);
    }
    // Direct physical mutation deliberately leaves the canonical view stale.
    void raw(std::uint32_t relative,std::uint32_t value,unsigned width=4){
        for(unsigned i=0;i<width;++i){const auto at=object.offset+relative+i;
            object.storage->bytes->bytes.at(at)=static_cast<std::uint8_t>(value>>(8*(width-1-i)));
            object.storage->bytes->initialized.at(at)=1;}
    }
    void copy(std::uint32_t relative,std::initializer_list<std::uint8_t> data,std::uint32_t table=0x80475b60){
        auto member=std::make_shared<IgbDecodeRuntimeField>();member->vtable_word=table;member->offset=relative;
        member->size=static_cast<std::uint16_t>(data.size());auto source=buffer(data.size());source->bytes=data;
        member->default_bytes=IgbRuntimeFieldDefaultSource{source,0};IgbObjectInitCall c;
        c.kind=IgbObjectInitServiceKind::field_virtual;c.field=member;c.object=object;c.virtual_slot=0x70;
        const auto op=make_native_igb_scalar_field_factory()(c);
        check(op && op->resume().completed,"Structure target did not receive its actual scalar/raw-reference default copy.");
    }
};
struct Values {
    std::shared_ptr<IgbFieldValuesServices> services=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbFieldOperationCall> calls;
    std::vector<std::shared_ptr<IgbScratchBuffer>> allocated;
    std::function<std::optional<MessengerCallResult>(const IgbFieldOperationCall&,IgbFieldOperation&)> action;
    unsigned polls{};
    Values(){services->fields=std::make_shared<IgbFieldOperationsServices>();services->struct_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->fields->factory=make_native_igb_field_values_factory(services,[this](const IgbFieldOperationCall& c){
            calls.push_back(c);auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
                ++polls;if(action){const auto result=action(c,*out);if(result)return *result;}
                if(c.kind==FK::allocation_pool || c.kind==FK::object_pool){out->pool_result=pool;return MessengerCallResult{true,0,{}};}
                if(c.kind==FK::allocation_size){check(c.buffer.buffer!=nullptr,"Capacity query lost its buffer.");
                    return MessengerCallResult{true,static_cast<std::uint32_t>(c.buffer.buffer->bytes.size()),{}};}
                if(c.kind==FK::free_default){check(c.pool==pool && c.buffer.buffer && !c.buffer.buffer->released,
                    "Default free changed its selected pool or repeated release.");c.buffer.buffer->released=true;return MessengerCallResult{true,0,{}};}
                check(c.kind==FK::allocate_default && c.pool==pool && c.alignment==1,"Struct default changed actual68254 alignment or pool.");
                auto result=buffer(c.bytes,0xa7,0);allocated.push_back(result);out->buffer_result={result,0};
                out->buffer_address=0x900000+static_cast<std::uint32_t>(allocated.size())*0x100;return MessengerCallResult{true,0,{}};
            };return op;
        });
    }
    auto select(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t slot){IgbFieldOperationCall c;
        c.kind=FK::field_virtual;c.field=field;c.vtable_word=structure_table;c.virtual_slot=slot;
        return services->fields->factory(c);
    }
};
auto alignment(const std::shared_ptr<IgbDecodeRuntimeField>& field){IgbMetadataLayoutCall c;
    c.kind=IgbMetadataLayoutKind::field_alignment;c.field=field;c.vtable_word=structure_table;c.virtual_slot=0x6c;
    return make_native_igb_field_alignment_factory()(c);
}
void independent_words_and_partial_stores(){
    BoundField f;const auto field=f.object.field;
    check(!field->struct_alignment && !field->struct_constructor && !field->struct_destructor,"Raw field owner fabricated struct defaults.");
    auto metadata=std::make_shared<IgbRuntimeMetaType>();auto element=std::make_shared<IgbDecodeRuntimeField>();
    field->method_function=0x12345678;field->flag_34=0x5a;field->flag_35=0x6b;field->word_38=0x11223344;
    field->flag_3c=0x7c;field->object_type=metadata;field->element_field=element;
    f.copy(0x34,{0,1,0,0x10});f.copy(0x38,{0x80,7,0x4a,0x1c},0x80476630);f.copy(0x3c,{0x80,7,0x4a,0x5c},0x80476630);
    check(field->struct_alignment==0x10010 && field->struct_constructor==0x80074a1c && field->struct_destructor==0x80074a5c,
        "Struct local words lost full width, byte order or their distinct canonical members.");
    check(field->method_function==0x12345678 && field->flag_34==0x5a && field->flag_35==0x6b && field->word_38==0x11223344 &&
        field->flag_3c==0x7c && field->object_type->lock()==metadata && field->element_field->lock()==element &&
        field->overwritten_constructor_owners_.empty(),"Struct function words acquired another field layout's pointer or reference semantics.");
    f.copy(0x34,{0,0,0,0});f.copy(0x38,{0xff,0xff,0xff,0xff},0x80476630);f.copy(0x3c,{0,0,0,0},0x80476630);
    check(field->struct_alignment==0 && field->struct_constructor==0xffffffff && field->struct_destructor==0,
        "Known zero or full-width struct function value became unknown.");
    for(const auto at:{0x34u,0x38u,0x3cu}){BoundField partial;
        auto member=at==0x34?&partial.object.field->struct_alignment:at==0x38?&partial.object.field->struct_constructor:&partial.object.field->struct_destructor;
        partial.copy(at,{0x80,1,2});check(!*member,"Incomplete struct word was fabricated from untouched bytes.");
        partial.copy(at+3,{3});check(*member==0x80010203,"Final partial store did not synchronize the completed struct word.");
        check(partial.object.storage->bytes->initialized[partial.object.offset+at-1]==0 &&
            partial.object.storage->bytes->initialized[partial.object.offset+at+4]==0,"Partial struct store initialized adjacent bytes.");
    }
    BoundField cross;cross.copy(0x34,{1,2,3});cross.copy(0x37,{4,5,6});
    check(cross.object.field->struct_alignment==0x01020304 && !cross.object.field->struct_constructor && !cross.object.field->struct_destructor,
        "Cross-word copy made an incomplete constructor word known.");
    cross.copy(0x3a,{7,8});check(cross.object.field->struct_constructor==0x05060708,"Cross-word constructor bytes lost publication.");
    for(const auto table:{0x804758e4u,0x804762fcu,0x80476088u,0x80476630u}){BoundField other;
        igb_bootstrap_write_word({other.object.storage,other.object.offset},0,table);
        other.copy(0x34,{0,0,0,1});other.copy(0x38,{0,0,0,0});other.copy(0x3c,{0,0,0,0});
        check(!other.object.field->struct_alignment && !other.object.field->struct_constructor && !other.object.field->struct_destructor,
            "Struct synchronization interpreted another concrete descriptor layout.");
    }
    check(f.object.storage->bytes->initialized[0xf]==0 && f.object.storage->bytes->initialized[0x50]==0,
        "Struct default copies changed the allocation prefix or suffix.");
}
void live_size_and_alignment(){
    Values values;auto canonical=std::make_shared<IgbDecodeRuntimeField>();canonical->vtable_word=structure_table;
    const auto canonical_alignment=alignment(canonical);
    check(canonical_alignment && !canonical_alignment->resume().completed,"Unknown canonical struct alignment defaulted to a constant.");
    canonical->struct_alignment=0;check(canonical_alignment->resume().completed && canonical_alignment->resume().value==0,
        "Unknown alignment did not resume to a represented zero value.");
    for(const auto value:{0u,1u,0x10010u,0xffffffffu}){canonical->struct_alignment=value;const auto selected=alignment(canonical);
        check(selected && selected->resume().completed && selected->resume().value==value,"Struct alignment was clamped or narrowed in its getter.");}
    for(const auto value:{0u,1u,12u,0xffffu}){canonical->size=static_cast<std::uint16_t>(value);const auto selected=values.select(canonical,0x64);
        check(selected && selected->resume().completed && selected->resume().value==value,"Canonical struct size became a fixed scalar width.");}
    BoundField physical;const auto field=physical.object.field;field->size=99;field->struct_alignment=1;
    physical.raw(0x14,12,2);physical.raw(0x34,0x12345678);
    const auto size=values.select(field,0x64);const auto align=alignment(field);
    check(size && size->resume().completed && size->resume().value==12 && field->size==12,
        "Struct size did not read its physical halfword before the stale canonical value.");
    check(align && align->resume().completed && align->resume().value==0x12345678 && field->struct_alignment==0x12345678,
        "Struct alignment did not read its physical full word.");
    physical.raw(0x14,0,2);physical.raw(0x34,0);field->size=99;field->struct_alignment=1;
    check(values.select(field,0x64)->resume().value==0 && alignment(field)->resume().value==0 && field->size==0 && field->struct_alignment==0,
        "A fresh struct getter cached old physical words or treated zero as unknown.");
    check(size->resume().value==12 && align->resume().value==0x12345678,
        "Completed struct getter replaced its captured result on another poll.");
    check(!field->struct_constructor && !field->struct_destructor && physical.object.storage->bytes->initialized[0x26]==0 &&
        physical.object.storage->bytes->initialized[0x27]==0,"Struct getters read padding or initialized unrelated function words.");
    auto held=std::make_shared<IgbDecodeRuntimeField>();const auto captured=alignment(held);std::weak_ptr<IgbDecodeRuntimeField> weak=held;
    check(!captured->resume().completed,"Absent held alignment did not suspend.");
    held->struct_alignment=16;held.reset();check(!weak.expired() && captured->resume().value==16,"Selected alignment lost its captured descriptor owner.");
}
void physical_read_failures_and_dispatch(){
    Values values;
    for(const auto byte:{0x14u,0x15u,0x34u,0x35u,0x36u,0x37u}){BoundField f;f.raw(0x14,12,2);f.raw(0x34,16);
        f.object.field->size=77;f.object.field->struct_alignment=88;f.object.storage->bytes->initialized[f.object.offset+byte]=0;
        if(byte<0x34){auto op=values.select(f.object.field,0x64);rejects([&]{op->resume();},"Physical unknown struct halfword fell back to canonical size.");
            f.object.storage->bytes->initialized[f.object.offset+byte]=1;
            rejects([&]{op->resume();},"Restored physical size bytes revived a faulted getter.");}
        else{auto op=alignment(f.object.field);rejects([&]{op->resume();},"Physical unknown struct word fell back to canonical alignment.");
            f.object.storage->bytes->initialized[f.object.offset+byte]=1;
            rejects([&]{op->resume();},"Restored physical alignment bytes revived a faulted getter.");}
        check(f.object.field->size==77 && f.object.field->struct_alignment==88,"Failed physical read published a guessed canonical value.");
    }
    for(unsigned kind=0;kind<5;++kind)for(const bool size:{false,true}){BoundField f;f.raw(0x14,12,2);f.raw(0x34,16);
        const auto field=f.object.field;
        if(kind==0)f.object.storage.reset();
        else if(kind==1)f.object.storage->bytes->released=true;
        else if(kind==2)f.object.storage->bindings.front().field=std::make_shared<IgbDecodeRuntimeField>();
        else if(kind==3)field->physical_storage->offset+=4;
        else field->construction_fault_=true;
        if(size){auto op=values.select(field,0x64);rejects([&]{op->resume();},"Invalid physical struct identity was accepted by its size getter.");}
        else{auto op=alignment(field);rejects([&]{op->resume();},"Invalid physical struct identity was accepted by its alignment getter.");}
    }
    auto null_size=values.select({},0x64);rejects([&]{null_size->resume();},"Null struct size receiver was treated as zero.");
    auto null_alignment=alignment({});rejects([&]{null_alignment->resume();},"Null struct alignment receiver was treated as unknown.");
    unsigned delegated{};auto exact=make_native_igb_field_values_factory(values.services,[&](const auto&){++delegated;return std::shared_ptr<IgbFieldOperation>{};});
    IgbFieldOperationCall c;c.kind=FK::field_virtual;c.vtable_word=structure_table;c.virtual_slot=0x60;
    check(!exact(c) && delegated==1,"Struct value adapter consumed an adjacent unsupported slot.");
    c.virtual_slot=0x64;c.vtable_word=structure_table+4;check(!exact(c) && delegated==2,"Struct value adapter accepted a neighboring table.");
    c.vtable_word=structure_table;c.kind=FK::allocation_size;check(!exact(c) && delegated==3,"Struct value adapter consumed a different service kind.");
    c.kind=FK::field_virtual;c.virtual_slot=0x8c;c.argument=1;
    check(!exact(c) && delegated==4,"Struct default adapter treated an unbound nonnull source as null.");
    IgbMetadataLayoutCall a;a.kind=IgbMetadataLayoutKind::field_alignment;a.vtable_word=structure_table;a.virtual_slot=0x64;
    check(!make_native_igb_field_alignment_factory()(a),"Struct alignment adapter accepted its size slot.");
}
void alignment_layout_narrowing(){
    for(const auto value:{0x10010u,0x10000u,0u}){
        auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=structure_table;field->size=7;
        field->offset=123;field->flag_25=1;field->struct_alignment=value;field->declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};
        auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->parent=std::shared_ptr<IgbRuntimeMetaType>{};metadata->native_size=0xffffffff;
        metadata->allocation_prefix=0x87654321;metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();metadata->fields->count=1;
        metadata->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(1,field);
        auto services=std::make_shared<IgbMetadataLayoutServices>();services->factory=make_native_igb_field_alignment_factory();
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();auto core=std::make_shared<IgbRuntimeTypeRegistry>();services->runtime->singleton=core;
        core->registration_callbacks=std::make_shared<IgbRegistrationCallbackList>();
        IgbMetadataLayoutInvocation run(metadata);
        check(run.resume(services).completed && metadata->native_alignment==(value==0x10010?16:4) &&
            field->offset==(value==0x10010?0xfffffff0u:0u) && metadata->allocation_prefix==0x87654321 &&
            metadata->native_size==0xffffffff,"Struct getter narrowed/clamped alignment before the original layout arithmetic.");
    }
}
void finalization_and_real_buffer_growth(){
    Values values;BoundField f;const auto field=f.object.field;
    auto prototype=std::make_shared<IgbDecodeRuntimeField>();prototype->ordinal=0x1234;values.services->struct_prototype=prototype;
    auto global=std::make_shared<IgbRuntimeFieldSlot>();field->global_slot=global;
    field->global_slot_address=0x830000;f.raw(0x1c,0x830000);
    // The raw bootstrap writer synchronizes only headers. Use the actual
    // ushort member copy to prepare a coherent physical/canonical ordinal.
    f.copy(0x12,{0x56,0x78},0x80477054);f.raw(0x14,1,2);f.raw(0x34,0x10010);
    check(field->ordinal==0x5678,"Struct finalization fixture has an incoherent instance ordinal.");
    field->default_bytes=IgbRuntimeFieldDefaultSource{};field->default_bytes_address=0;
    IgbFieldFinalizeInvocation initial(field);check(initial.resume(values.services->fields).completed,"Struct finalizer did not compose actual dynamic values and buffer allocation.");
    check(values.allocated.size()==1 && values.allocated.front()->bytes==std::vector<std::uint8_t>(1,0xa7) &&
        values.allocated.front()->initialized==std::vector<std::uint8_t>(1,0) && field->type_ordinal==0x1234 && field->ordinal==0x5678 &&
        global->value()==field && igb_buffer_word(f.object.storage->bytes,f.object.offset+0x10)==0x12345678,
        "Null struct default source zeroed storage, copied a prototype or changed the adjacent instance ordinal.");
    const auto old=values.allocated.front();const auto old_address=*field->default_bytes_address;
    values.calls.clear();values.polls=0;bool free_ready{};unsigned frees{};
    values.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation&)->std::optional<MessengerCallResult>{
        if(c.kind==FK::free_default){++frees;check(c.buffer.buffer==old && c.address==old_address && !old->released,
            "Struct growth lost its captured old allocation before free.");if(!free_ready)return MessengerCallResult{false,19,"Prepared struct default free."};}
        return {};
    };
    global->reset();f.raw(0x14,12,2);field->size=99;IgbFieldFinalizeInvocation growth(field);
    const auto first=growth.resume(values.services->fields);
    check(!first.completed && first.value==19 && field->size==12 && frees==1 && values.allocated.size()==1 && !global->has_value() && !old->released,
        "Struct size growth skipped the pending free or published its global before allocation.");
    auto replacement=std::make_shared<IgbDecodeRuntimeField>();replacement->ordinal=3;values.services->struct_prototype=replacement;
    free_ready=true;check(growth.resume(values.services->fields).completed && frees==2 && old->released && values.allocated.size()==2 &&
        field->size==12 && field->type_ordinal==0x1234 && field->default_bytes->buffer==values.allocated.back() && global->value()==field,
        "Resumed struct growth replayed completed virtuals or retained the one-byte buffer.");
    const std::vector<FK> expected{FK::allocation_pool,FK::allocation_size,FK::object_pool,FK::free_default,FK::object_pool,FK::allocate_default};
    std::vector<FK> observed;for(const auto& c:values.calls)observed.push_back(c.kind);
    check(observed==expected && values.calls.back().bytes==12 && values.calls.back().alignment==1 &&
        values.allocated.back()->bytes==std::vector<std::uint8_t>(12,0xa7) && values.allocated.back()->initialized==std::vector<std::uint8_t>(12,0),
        "Struct growth changed free-before-allocation ordering, allocation size or untouched bytes.");
    check(growth.resume().completed && values.calls.size()==expected.size() && values.allocated.size()==2,"Completed struct finalizer repeated default allocation.");
    values.action={};auto input=buffer(12);for(unsigned i=0;i<12;++i)input->bytes[i]=static_cast<std::uint8_t>(i+1);
    IgbFieldOperationCall c;c.kind=FK::field_virtual;c.field=field;c.vtable_word=structure_table;c.virtual_slot=0x8c;
    c.source=IgbRuntimeFieldDefaultSource{input,0};c.source_address=0x800000;
    const auto copy=values.services->fields->factory(c);check(copy && copy->resume().completed &&
        field->default_bytes->buffer->bytes==input->bytes && field->default_bytes->buffer->initialized==std::vector<std::uint8_t>(12,1),
        "Struct+8C did not execute the complete original twelve-byte source copy.");
}
void default_callback_fault_prefix(){
    for(unsigned change=0;change<3;++change){Values values;BoundField f;const auto field=f.object.field;
        f.raw(0x14,12,2);igb_field_store_default(field,{},0);
        auto alternate=std::make_shared<IgbObjectStorage>();alternate->original_address=0x200000;alternate->bytes=buffer(0x40,0xcd,0);
        alternate->bindings.push_back({0,{},{},{},{},field});
        igb_bootstrap_write_word({alternate,0},0,structure_table);igb_bootstrap_write_word({alternate,0},0x20,0);
        auto returned=buffer(12,0xa7,0),source=buffer(12,0x69,1);
        std::shared_ptr<IgbFieldOperation> outer;unsigned callbacks{};
        values.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation& result)->std::optional<MessengerCallResult>{
            if(c.kind!=FK::allocate_default)return {};
            ++callbacks;check(c.bytes==12 && c.alignment==1,"Fault-prefix allocation changed its original request.");
            if(change==0)rejects([&]{outer->resume();},"Recursive struct+8C unexpectedly succeeded.");
            else if(change==1)field->physical_storage=IgbFieldStorageBinding{alternate,0};
            else field->physical_storage.reset();
            result.buffer_result={returned,0};result.buffer_address=0x300000;return MessengerCallResult{true,0,{}};
        };
        IgbFieldOperationCall c;c.kind=FK::field_virtual;c.field=field;c.vtable_word=structure_table;c.virtual_slot=0x8c;
        c.source=IgbRuntimeFieldDefaultSource{source,0};c.source_address=0x400000;
        outer=values.services->fields->factory(c);
        rejects([&]{outer->resume();},"Struct+8C completed after a swallowed recursive failure or callback rebinding.");
        check(callbacks==1 && field->size==12 && field->default_bytes && !field->default_bytes->buffer && field->default_bytes_address==0 &&
            igb_buffer_word(f.object.storage->bytes,f.object.offset+0x20)==0 && igb_buffer_word(alternate->bytes,0x20)==0 &&
            returned->bytes==std::vector<std::uint8_t>(12,0xa7) && returned->initialized==std::vector<std::uint8_t>(12,0),
            "Invalidated struct+8C published its callback result or copied bytes after the fault.");
        field->physical_storage=IgbFieldStorageBinding{f.object.storage,f.object.offset};values.action={};
        rejects([&]{outer->resume();},"Restoring a struct physical binding revived a faulted default operation.");
        check(callbacks==1 && field->default_bytes_address==0,"Faulted struct+8C replayed its allocation or pointer publication.");
    }
    Values pending;BoundField f;const auto field=f.object.field;f.raw(0x14,1,2);igb_field_store_default(field,{},0);
    unsigned callbacks{};bool ready{};
    pending.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation&)->std::optional<MessengerCallResult>{
        if(c.kind==FK::allocate_default){++callbacks;if(!ready)return MessengerCallResult{false,0,"Prepared allocation."};}return {};
    };
    const auto outer=pending.select(field,0x8c);check(!outer->resume().completed && callbacks==1,"Physical binding fixture did not suspend in allocation.");
    field->physical_storage.reset();ready=true;rejects([&]{outer->resume();},"Pending struct+8C accepted removal of its captured physical binding.");
    check(callbacks==1 && pending.allocated.empty() && field->default_bytes_address==0,"Missing binding was checked only after resumed allocation.");
    field->physical_storage=IgbFieldStorageBinding{f.object.storage,f.object.offset};rejects([&]{outer->resume();},"Restored pending field binding erased its permanent fault.");
}
void default_callback_released_descriptor_bytes(){
    for(const bool remove:{false,true}){Values values;BoundField f;const auto field=f.object.field;
        f.raw(0x14,12,2);const auto physical=f.object.storage->bytes;
        auto payload=buffer(12,0xa7,1),source=buffer(12,0x69,1);
        igb_field_store_default(field,{payload,0},0x500000);unsigned callbacks{};
        values.action=[&](const IgbFieldOperationCall& c,IgbFieldOperation&)->std::optional<MessengerCallResult>{
            if(c.kind!=FK::allocation_size)return {};
            ++callbacks;check(c.buffer.buffer==payload && c.address==0x500000,
                "Released-byte regression lost its sufficient existing payload buffer.");
            if(remove)f.object.storage->bytes.reset();else physical->released=true;
            return MessengerCallResult{true,12,{}};
        };
        IgbFieldOperationCall c;c.kind=FK::field_virtual;c.field=field;c.vtable_word=structure_table;c.virtual_slot=0x8c;
        c.source=IgbRuntimeFieldDefaultSource{source,0};c.source_address=0x600000;
        const auto outer=values.services->fields->factory(c);
        rejects([&]{outer->resume();},"Struct+8C completed after its physical descriptor bytes were removed or released.");
        check(callbacks==1 && values.calls.size()==2 && values.allocated.empty() && field->size==12 &&
            field->default_bytes && field->default_bytes->buffer==payload && field->default_bytes_address==0x500000 &&
            payload->bytes==std::vector<std::uint8_t>(12,0xa7) && payload->initialized==std::vector<std::uint8_t>(12,1),
            "Struct+8C copied payload bytes or allocated after its physical descriptor became unavailable.");
        f.object.storage->bytes=physical;physical->released=false;values.action={};
        rejects([&]{outer->resume();},"Restoring the descriptor byte owner revived a faulted struct+8C operation.");
        check(callbacks==1 && payload->bytes==std::vector<std::uint8_t>(12,0xa7) && values.allocated.empty(),
            "Restored descriptor storage caused a faulted operation to replay or copy bytes.");
    }
}
void strict_object_leaves_and_prototype_lookup(){
    auto services=std::make_shared<IgbFieldInitServices>();services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();
    services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
    auto cell=std::make_shared<IgbRegistrationMetaSlot>();services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::structure)]=cell;
    unsigned delegated{};const auto adapter=make_native_igb_field_init_factory(services,[&](const auto&){++delegated;return std::shared_ptr<IgbObjectInitOperation>{};});
    IgbObjectInitCall c;c.kind=IgbObjectInitServiceKind::object_virtual;c.vtable_word=structure_table;
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u}){c.virtual_slot=slot;auto op=adapter(c);
        check(op && op->resume().completed && delegated==0,"Verified structure object BLR read an absent receiver.");}
    c.virtual_slot=0x58;const auto getter=adapter(c);check(getter && !getter->resume().completed,"Unknown struct metadata was inferred ready.");
    *cell=std::shared_ptr<IgbRuntimeMetaType>{};check(getter->resume().completed && !getter->metadata_result,"Plain struct metadata getter registered null state.");
    auto metadata=std::make_shared<IgbRuntimeMetaType>();*cell=metadata;
    check(getter->resume().completed && getter->metadata_result==metadata,"Plain structure metadata getter missed its canonical cell.");
    for(const auto slot:{0x38u,0x40u,0x54u}){c.virtual_slot=slot;check(!adapter(c),"Struct initializer admitted an unrelated object virtual.");}
    c.kind=IgbObjectInitServiceKind::field_virtual;c.field=std::make_shared<IgbDecodeRuntimeField>();c.field->vtable_word=structure_table;
    const auto members=make_native_igb_scalar_field_factory(adapter);
    for(const auto slot:{0x84u,0x7cu,0xf0u,0x70u,0x78u,0x80u}){c.virtual_slot=slot;
        check(!members(c),"Descriptor admission silently implemented the separate inline struct member path.");}
    rejects([]{IgbFieldPrototypeLookupInvocation invalid(IgbFieldTypeKind::structure);},"A nonexistent null-only structure prototype getter was invented.");
}
}
int main(){try{independent_words_and_partial_stores();live_size_and_alignment();physical_read_failures_and_dispatch();
    alignment_layout_narrowing();finalization_and_real_buffer_growth();default_callback_fault_prefix();default_callback_released_descriptor_bytes();strict_object_leaves_and_prototype_lookup();
    std::cout<<"igb_struct_field_runtime_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Struct descriptor runtime failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
