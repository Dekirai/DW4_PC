#include "dw4/igb_memory_member_copy.hpp"

#include <iostream>

namespace {
using namespace dw4;using BK=IgbBufferServiceKind;using MK=IgbMemoryMemberCopyServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
auto bytes(unsigned size,std::uint8_t value=0){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
void word(const std::shared_ptr<IgbScratchBuffer>& b,unsigned offset,std::uint32_t value){for(unsigned i=0;i<4;++i)b->bytes[offset+i]=static_cast<std::uint8_t>(value>>(24-i*8));}
IgbObjectInstance object(std::uint32_t address,std::uint32_t offset=4,bool typed=true){
    IgbObjectInstance o;o.storage=std::make_shared<IgbObjectStorage>();o.storage->original_address=address;o.offset=offset;o.storage->bytes=bytes(0x60,0xcc);
    if(typed){o.field=std::make_shared<IgbDecodeRuntimeField>();o.field->vtable_word=0x80473f24;o.field->physical_storage=IgbFieldStorageBinding{o.storage,offset};
        o.storage->bindings.push_back({offset,{},{},{},{},o.field});igb_bootstrap_write_word({o.storage,offset},0,0x80473f24);igb_object_set_reference_word(o,1);}
    return o;
}
struct Allocation {std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t address{},offset{},size{};};
struct Fixture {
    std::shared_ptr<IgbMemoryMemberCopyServices> services=std::make_shared<IgbMemoryMemberCopyServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    IgbObjectInstance source=object(0x100000),destination=object(0x200000);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> object_type=std::make_shared<IgbRuntimeMetaType>(),memory_type=std::make_shared<IgbRuntimeMetaType>();
    std::vector<Allocation> allocations;std::vector<IgbBufferCall> buffer_calls;std::vector<IgbMemoryMemberCopyCall> member_calls;
    std::vector<IgbObjectInstance> cloned;unsigned frees{},copies{},sizes{},destroyed{},clone_calls{};
    std::function<MessengerCallResult(const IgbBufferCall&,IgbBufferOperation&)> buffer_action;
    std::function<MessengerCallResult(const IgbMemoryMemberCopyCall&,IgbMemoryMemberCopyOperation&)> member_action;
    Fixture(){
        services->buffers=std::make_shared<IgbBufferServices>();services->buffers->values=std::make_shared<IgbFieldValuesServices>();
        services->objects=std::make_shared<IgbObjectFieldConstructServices>();services->objects->values=services->buffers->values;
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        object_type->parent=std::shared_ptr<IgbRuntimeMetaType>{};memory_type->parent=std::shared_ptr<IgbRuntimeMetaType>{};
        auto& slots=services->registration->meta_fields->metadata_slots;
        slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>(object_type);
        slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=std::make_shared<IgbRegistrationMetaSlot>(memory_type);
        field->offset=0x20;field->flag_42=std::uint8_t{1};field->flag_40=std::uint8_t{1};field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};
        set_member(source,{});set_member(destination,{});
        services->buffers->factory=[this](const IgbBufferCall& c){buffer_calls.push_back(c);auto op=std::make_shared<IgbBufferOperation>();
            op->resume=[this,c,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(buffer_action){const auto status=buffer_action(c,*out);if(!status.completed)return status;}
                if(c.kind==BK::allocation_pool)out->pool_result=pool;
                else if(c.virtual_slot==0x150){++sizes;done=true;return MessengerCallResult{true,find(c.address).size,{}};}
                else if(c.virtual_slot==0x108){++frees;find(c.address).bytes->released=true;}
                else if(c.virtual_slot==0xd8){++copies;const auto a=allocate(0x800000+copies*0x100,c.bytes,3,0xcc);
                    a.bytes->initialized.assign(a.bytes->bytes.size(),0);out->buffer_result={a.bytes,a.offset};out->buffer_address=a.address;}
                else throw std::runtime_error("Unexpected buffer slot in memory caller fixture.");
                done=true;return MessengerCallResult{true,0,{}};};return op;};
        auto native=make_native_igb_memory_member_virtual_factory(services->registration,[this](const IgbMemoryMemberCopyCall& c){
            auto op=std::make_shared<IgbMemoryMemberCopyOperation>();op->resume=[this,c,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(member_action){const auto status=member_action(c,*out);if(!status.completed)return status;}
                if(c.kind==MK::destroy_object){++destroyed;check(c.function==0x80066e1c,"Element destructor target changed.");}
                else if(c.kind==MK::clone_object){++clone_calls;check(c.virtual_slot==0x54 && c.argument==1,"Element clone changed its original virtual arguments.");
                    auto result=object(0xa00000+clone_calls*0x100);cloned.push_back(result);out->object_result=result;}
                else throw std::runtime_error("Unexpected metadata getter in memory caller fixture.");
                done=true;return MessengerCallResult{true,0,{}};};return op;});
        services->factory=[this,native](const IgbMemoryMemberCopyCall& c){member_calls.push_back(c);return native(c);};
    }
    Allocation allocate(std::uint32_t address,unsigned size,unsigned offset=0,std::uint8_t value=0){
        Allocation a{bytes(size+offset+2,value),address,offset,size};allocations.push_back(a);
        services->buffers->values->buffers.push_back({a.bytes,address-offset});return a;
    }
    Allocation& find(std::uint32_t address){for(auto& a:allocations)if(a.address==address)return a;throw std::runtime_error("Unbound allocation in synthetic pool service.");}
    void set_member(const IgbObjectInstance& object,const Allocation& allocation){
        if(object.field){object.field->default_bytes=IgbRuntimeFieldDefaultSource{allocation.bytes,allocation.offset};object.field->default_bytes_address=allocation.address;}
        igb_bootstrap_write_word({object.storage,object.offset},0x20,allocation.address);
    }
    std::shared_ptr<IgbDecodeRuntimeField> element(bool memory=false){auto e=std::make_shared<IgbDecodeRuntimeField>();e->vtable_word=memory?0x80476088u:0x804762fcu;field->element_field=std::weak_ptr(e);return e;}
    IgbObjectInstance entry(std::uint32_t address,std::uint32_t references){auto o=object(address);igb_object_set_reference_word(o,references);igb_bind_object_field_instance(services->objects,o);return o;}
};
void cold_null_element(){
    Fixture f;const auto original=f.allocate(0x300003,1,3,1);f.set_member(f.source,original);
    f.source.storage->bytes->initialized[f.source.offset+0x20]=0; // Canonical member remains authoritative.
    IgbMemoryMemberCopyInvocation call(f.field,f.destination,f.source);
    check(call.resume(f.services).completed && f.copies==1 && f.sizes==2 && f.frees==0 && f.member_calls.empty(),"Cold null-element copy changed its actual helper sequence.");
    const auto result=*f.destination.field->default_bytes;
    check(result.buffer!=original.bytes && result.offset==3 && result.buffer->bytes[3]==1 && result.buffer->initialized[3]==1,
        "Cold copy did not allocate and populate an independent one-byte default.");
    check(f.destination.field->default_bytes_address==0x800100 && igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x20)==0x800100,
        "Memory publication lost physical or canonical pointer identity.");
    check(original.bytes->bytes[3]==1 && !original.bytes->released && f.field->offset==0x20 && f.field->flag_42==1 && f.field->flag_40==1,
        "Cold memory clone changed its source allocation or descriptor policy.");
    check(f.buffer_calls.size()==6 && f.buffer_calls[0].kind==BK::allocation_pool && f.buffer_calls[1].kind==BK::allocation_pool &&
        f.buffer_calls[2].virtual_slot==0x150 && f.buffer_calls[3].virtual_slot==0xd8 && f.buffer_calls[4].kind==BK::allocation_pool && f.buffer_calls[5].virtual_slot==0x150,
        "Whole memory caller omitted independent source owner/size queries.");
    const auto trace=f.buffer_calls.size();check(call.resume(f.services).completed && trace==f.buffer_calls.size(),"Completed memory copy replayed callbacks.");
}
void live_offsets_and_pending_free(){
    Fixture f;f.source=object(0x100000,4,false);f.destination=object(0x200000,4,false);f.field->offset=0x10;
    const auto old=f.allocate(0x300000,2),a=f.allocate(0x400000,2,0,0x11),b=f.allocate(0x500002,2,2,0x22);
    igb_bootstrap_write_word({f.destination.storage,f.destination.offset},0x10,old.address);
    igb_bootstrap_write_word({f.source.storage,f.source.offset},0x10,a.address);igb_bootstrap_write_word({f.source.storage,f.source.offset},0x14,b.address);
    bool free_ready{};unsigned free_polls{};
    f.buffer_action=[&](const IgbBufferCall& c,IgbBufferOperation&){
        if(c.virtual_slot==0x108){++free_polls;if(!free_ready)return MessengerCallResult{false,0,"prepared old free"};f.field->offset=0x14;}
        if(c.virtual_slot==0xd8){f.field->offset=0x18;igb_bootstrap_write_word({f.source.storage,f.source.offset},0x14,a.address);}
        return MessengerCallResult{true,0,{}};
    };
    IgbMemoryMemberCopyInvocation call(f.field,f.destination,f.source);
    check(!call.resume(f.services).completed && f.frees==0 && f.copies==0 && !old.bytes->released &&
        igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x10)==old.address,"Pending free cleared the old member or allocated too early.");
    free_ready=true;check(call.resume(f.services).completed && free_polls==2 && f.frees==1 && old.bytes->released && f.copies==1,
        "Resumed free replayed completed work or lost the original source-offset reload.");
    check(igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x10)==old.address &&
        igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x18)==0x800100 && f.find(0x800100).bytes->bytes[3]==0x22,
        "Memory caller cached a destination offset or recaptured the source after allocation.");
}
void null_gates_and_invalid_physical_members(){
    Fixture f;f.field->flag_42=std::uint8_t{0};f.field->element_field.reset();IgbMemoryMemberReleaseInvocation skipped(f.field,{});
    check(skipped.resume().completed,"Disabled ownership gate accessed an absent element or target.");
    f.field->flag_42=std::uint8_t{1};f.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};IgbMemoryMemberReleaseInvocation null_element(f.field,{});
    check(null_element.resume().completed,"Null element accessed an unused target allocation.");
    const auto old=f.allocate(0x300000,1);f.set_member(f.destination,old);f.field->flag_40=std::uint8_t{0};
    IgbMemoryMemberCopyInvocation clear(f.field,f.destination,f.source);
    check(clear.resume(f.services).completed && f.buffer_calls.empty() && !old.bytes->released && f.destination.field->default_bytes_address==0,
        "Null source or disabled free gate entered an allocation/free helper.");
    for(unsigned kind=0;kind<3;++kind){Fixture bad;bad.field->flag_40=std::uint8_t{0};const auto src=bad.allocate(0x300000,1);bad.set_member(bad.source,src);
        if(kind==0)bad.source.storage->bytes->released=true;
        if(kind==1)bad.source.storage->bytes->bytes.resize(bad.source.offset+0x23);
        if(kind==2)bad.source.storage->bytes->initialized.resize(bad.source.offset+0x23);
        IgbMemoryMemberCopyInvocation invalid(bad.field,bad.destination,bad.source);
        rejects([&]{invalid.resume(bad.services);},"Canonical default pointer bypassed unavailable physical source member storage.");
        check(invalid.faulted() && bad.buffer_calls.empty() && bad.destination.field->default_bytes_address==0,"Physical source fault entered later allocator callbacks.");
    }
    Fixture alias;const auto shared=alias.allocate(0x300000,1,0,0x55);alias.set_member(alias.source,shared);
    IgbMemoryMemberCopyInvocation same(alias.field,alias.source,alias.source);
    rejects([&]{same.resume(alias.services);},"Self-copy incorrectly skipped free-before-read behavior.");
    check(shared.bytes->released && alias.frees==1 && alias.source.field->default_bytes_address==shared.address,"Self-copy fault published a replacement before reading released bytes.");
}
void release_elements_and_retained_destructor(){
    Fixture f;auto element=f.element();const auto backing=f.allocate(0x300001,16,1);f.set_member(f.destination,backing);
    const auto first=f.entry(0x400000,0xa5800001),second=f.entry(0x500000,2),zero=f.entry(0x600000,0);
    word(backing.bytes,1,0);word(backing.bytes,5,0x400004);word(backing.bytes,9,0x500004);word(backing.bytes,13,0x600004);
    bool ready{};unsigned polls{};f.member_action=[&](const IgbMemoryMemberCopyCall& c,IgbMemoryMemberCopyOperation&){
        check(c.kind==MK::destroy_object && c.object.field==first.field,"Release loop selected a different captured element.");++polls;
        if(!ready)return MessengerCallResult{false,0,"prepared element destructor"};
        f.field->offset=0;f.field->flag_42=std::uint8_t{0};f.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};return MessengerCallResult{true,0,{}};
    };
    IgbMemoryMemberReleaseInvocation release(f.field,f.destination);
    check(!release.resume(f.services).completed && igb_object_reference_word(first)==0xa5800000 && igb_object_reference_word(second)==2,
        "Pending element destructor lost the completed full-word decrement prefix.");
    ready=true;f.services->factory={};check(release.resume(f.services).completed && polls==2 && f.destroyed==1 && f.sizes==1,
        "Release resumption recaptured descriptor gates/backing/count or discarded its callback.");
    check(igb_object_reference_word(second)==1 && igb_object_reference_word(zero)==0xffffffff &&
        igb_buffer_word(backing.bytes,5)==0x400004 && !backing.bytes->released,"Element release changed slots, freed backing, or used the wrong reference mask.");
}
void object_element_clones(){
    Fixture f;auto element=f.element();const auto source=f.allocate(0x300000,12),old=f.allocate(0x400000,4);
    const auto a=f.entry(0x500000,9),b=f.entry(0x600000,4),displaced=f.entry(0x700000,1);
    word(source.bytes,0,0x500004);word(source.bytes,4,0);word(source.bytes,8,0x600004);word(old.bytes,0,0x700004);
    f.set_member(f.source,source);f.set_member(f.destination,old);
    bool clone_ready{};unsigned clone_polls{};f.member_action=[&](const IgbMemoryMemberCopyCall& c,IgbMemoryMemberCopyOperation&){
        if(c.kind==MK::clone_object && c.object.field==a.field){++clone_polls;if(!clone_ready)return MessengerCallResult{false,0,"prepared object clone"};}
        return MessengerCallResult{true,0,{}};
    };
    IgbMemoryMemberCopyInvocation call(f.field,f.destination,f.source);
    check(!call.resume(f.services).completed && f.destroyed==1 && f.frees==1 && old.bytes->released && f.copies==1 &&
        f.destination.field->default_bytes_address==0x800100 && igb_object_reference_word(displaced)==0,
        "Object clone boundary skipped old element release/free or delayed buffer publication.");
    clone_ready=true;check(call.resume(f.services).completed && clone_polls==2 && f.clone_calls==2 && f.sizes==4,
        "Object element loop lost its captured count or cloned the null slot.");
    const auto result=*f.destination.field->default_bytes;
    check(igb_buffer_word(result.buffer,result.offset)==0xa00104 && igb_buffer_word(result.buffer,result.offset+4)==0 &&
        igb_buffer_word(result.buffer,result.offset+8)==0xa00204,"Object element clone results were not stored in the captured new backing.");
    check(igb_object_reference_word(a)==9 && igb_object_reference_word(b)==4 && igb_object_reference_word(f.cloned[0])==1,
        "Object element copying invented source or clone reference adjustments.");
}
void nested_memory_elements(){
    Fixture f;auto element=f.element(true);f.field->flag_42=std::uint8_t{0};f.field->flag_40=std::uint8_t{0};
    const auto outer=f.allocate(0x300000,12),a=f.allocate(0x400002,2,2,0x13),b=f.allocate(0x500001,1,1,0x27);
    word(outer.bytes,0,a.address);word(outer.bytes,4,0);word(outer.bytes,8,b.address);f.set_member(f.source,outer);
    IgbMemoryMemberCopyInvocation call(f.field,f.destination,f.source);
    check(call.resume(f.services).completed && f.copies==3 && f.sizes==7 && f.clone_calls==0 && f.frees==0,
        "Nested memory copy omitted independent inner clones or performed object ownership operations.");
    const auto result=*f.destination.field->default_bytes;
    check(igb_buffer_word(result.buffer,result.offset)==0x800200 && igb_buffer_word(result.buffer,result.offset+4)==0 &&
        igb_buffer_word(result.buffer,result.offset+8)==0x800300,"Nested memory loop changed raw pointer publication order.");
    check(f.find(0x800200).bytes->bytes[3]==0x13 && f.find(0x800200).bytes->bytes[4]==0x13 && f.find(0x800300).bytes->bytes[3]==0x27 &&
        !a.bytes->released && !b.bytes->released,"Nested memory clones shared or changed source storage.");
    check(f.member_calls.size()==2 && f.member_calls[0].kind==MK::element_metadata && f.member_calls[1].kind==MK::element_metadata,
        "Nested memory ancestry reused a getter instead of performing the second original test.");
}
void live_element_reloads_and_adapter(){
    Fixture f;f.field->flag_42=std::uint8_t{0};f.field->flag_40=std::uint8_t{0};auto element=f.element(true);
    const auto source=f.allocate(0x300000,4);f.set_member(f.source,source);unsigned getters{};
    f.services->factory=[&](const IgbMemoryMemberCopyCall& c){check(c.kind==MK::element_metadata,"Unexpected live-element operation.");++getters;
        auto op=std::make_shared<IgbMemoryMemberCopyOperation>();op->resume=[&,out=op.get()]{out->metadata_result=f.memory_type;
            f.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};return MessengerCallResult{true,0,{}};};return op;};
    IgbMemoryMemberCopyInvocation changed(f.field,f.destination,f.source);
    rejects([&]{changed.resume(f.services);},"Reloaded null memory element skipped the original second68128 dereference.");
    check(changed.faulted() && getters==1 && f.destination.field->default_bytes_address==0x800100,
        "Post-copy ancestry fault discarded completed buffer publication or repeated its getter.");
    Fixture adapter;const auto a=adapter.allocate(0x300000,1,0,0x7a);adapter.set_member(adapter.source,a);
    auto members=std::make_shared<IgbFieldMemberCopyServices>();members->factory=make_native_igb_memory_member_copy_factory(adapter.services);
    IgbFieldMemberCopyInvocation call(adapter.field,adapter.destination,adapter.source,0x80062824);
    check(call.resume(members).completed && adapter.destination.field->default_bytes->buffer->bytes[3]==0x7a,"Member-copy adapter did not compose actual62824.");
}
void overlapping_typed_publication(){
    Fixture f;f.field->flag_42=std::uint8_t{0};f.field->flag_40=std::uint8_t{0};auto element=f.element();
    f.destination=object(0x100000,0);f.source=object(0x100000,4);
    f.source.storage=f.destination.storage;f.source.field->physical_storage=IgbFieldStorageBinding{f.source.storage,4};
    f.source.storage->bindings.push_back({4,{},{},{},{},f.source.field});
    const auto original=f.allocate(0x300000,4),old=f.allocate(0x400000,4);const auto entry=f.entry(0x500000,3);
    word(original.bytes,0,0x500004);f.set_member(f.destination,old);f.set_member(f.source,original);
    f.buffer_action=[&](const IgbBufferCall& c,IgbBufferOperation&){if(c.virtual_slot==0xd8)f.field->offset=0x24;return MessengerCallResult{true,0,{}};};
    const auto native=f.services->factory;bool observed{};
    f.services->factory=[&](const IgbMemoryMemberCopyCall& c){const auto op=native(c);if(c.kind!=MK::element_metadata)return op;
        const auto resume=op->resume;op->resume=[&,resume]{const auto status=resume();if(status.completed){
            check(f.source.field->default_bytes_address==0x800100 && f.source.field->default_bytes && f.source.field->default_bytes->buffer==f.find(0x800100).bytes,
                "Publication did not synchronize the overlapping source default owner.");
            observed=true;f.field->offset=0x20;word(original.bytes,0,0xdeadbeec);}
            return status;};return op;};
    IgbMemoryMemberCopyInvocation call(f.field,f.destination,f.source);
    check(call.resume(f.services).completed && observed && f.clone_calls==1 && igb_buffer_word(old.bytes,0)==0xa00104,
        "Element loop read stale canonical source backing after an overlapping pointer publication.");
    check(f.destination.field->default_bytes->buffer==old.bytes && igb_object_reference_word(entry)==3 &&
        igb_bootstrap_read_word({f.destination.storage,0},0x24)==0x800100,"Aliased publication changed an untouched canonical member or reference.");

    Fixture partial;partial.field->flag_42=std::uint8_t{0};partial.field->flag_40=std::uint8_t{0};
    partial.destination=object(0x100000,0);partial.source=object(0x100000,1);
    partial.source.storage=partial.destination.storage;partial.source.field->physical_storage=IgbFieldStorageBinding{partial.source.storage,1};
    partial.source.storage->bindings.push_back({1,{},{},{},{},partial.source.field});
    const auto source=partial.allocate(0x30003b,1,0,0x61);partial.set_member(partial.destination,{});partial.set_member(partial.source,source);
    partial.source.field->global_slot_address=0x11223344;partial.source.storage->bytes->bytes[0x24]=0xee;
    IgbMemoryMemberCopyInvocation unaligned(partial.field,partial.destination,partial.source);
    check(unaligned.resume(partial.services).completed && partial.destination.field->default_bytes_address==0x800100,
        "An unaligned alias prevented the reached original word publication.");
    check(partial.source.field->default_bytes_address==0x8001003b && !partial.source.field->default_bytes &&
        partial.source.field->global_slot_address==0x11223300 && partial.source.storage->bytes->bytes[0x24]==0xee,
        "Partial alias publication imported stale physical neighbors or invented a pointer owner.");
}
void adjacent_buffer_bindings(){
    Fixture f;auto element=f.element();const auto previous=f.allocate(0x300000,4),backing=f.allocate(0x300006,4);
    const auto entry=f.entry(0x500000,2);word(backing.bytes,0,0x500004);f.set_member(f.destination,backing);
    IgbMemoryMemberReleaseInvocation release(f.field,f.destination);
    check(release.resume(f.services).completed && igb_object_reference_word(entry)==1 && f.sizes==1 && !previous.bytes->released,
        "Adjacent allocation start conflicted with the preceding allocation's one-past address.");

    Fixture copied;const auto source=copied.allocate(0x300000,1,0,0x71),preceding=copied.allocate(0x8000fa,4);
    copied.set_member(copied.source,source);IgbMemoryMemberCopyInvocation copy(copied.field,copied.destination,copied.source);
    check(copy.resume(copied.services).completed && copied.destination.field->default_bytes_address==0x800100 &&
        copied.destination.field->default_bytes && copied.destination.field->default_bytes->buffer==copied.find(0x800100).bytes &&
        copied.destination.field->default_bytes->buffer!=preceding.bytes,
        "Canonical publication chose a preceding one-past allocation instead of the actual interior owner.");
}
void base_derived_publication_boundary(){
    Fixture f;f.field->flag_42=std::uint8_t{0};f.field->flag_40=std::uint8_t{0};f.field->offset=0x32;
    f.source=object(0x100000,4,false);igb_bootstrap_write_word({f.source.storage,f.source.offset},0x32,0);
    auto& target=*f.destination.field;target.vtable_word=0x80476088;target.word_30=0x11223344;
    target.flag_34=std::uint8_t{7};target.flag_35=std::uint8_t{9};
    igb_bootstrap_write_word({f.destination.storage,f.destination.offset},0x30,0xaabbccdd);
    IgbMemoryMemberCopyInvocation copy(f.field,f.destination,f.source);
    check(copy.resume(f.services).completed && target.word_30==0x11220000 && target.flag_34==0 && target.flag_35==0,
        "Publication crossing the base/derived boundary lost canonical byte merges or derived physical synchronization.");
    check(igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x30)==0xaabb0000 &&
        igb_bootstrap_read_word({f.destination.storage,f.destination.offset},0x32)==0 && f.buffer_calls.empty(),
        "Cross-boundary publication changed untouched physical bytes or entered an unnecessary allocator.");
}
}
int main(){try{cold_null_element();live_offsets_and_pending_free();null_gates_and_invalid_physical_members();release_elements_and_retained_destructor();
    object_element_clones();nested_memory_elements();live_element_reloads_and_adapter();overlapping_typed_publication();adjacent_buffer_bindings();base_derived_publication_boundary();
    std::cout<<"igb_memory_member_copy_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Memory member copy failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
