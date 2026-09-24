#include "dw4/igb_memory_member_defaults.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using BK=IgbBufferServiceKind;using MK=IgbMemoryMemberCopyServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(unsigned size,std::uint8_t value=0){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(size,value);out->initialized.assign(size,1);return out;}
void word(const std::shared_ptr<IgbScratchBuffer>& b,unsigned at,std::uint32_t value){for(unsigned i=0;i<4;++i)b->bytes[at+i]=static_cast<std::uint8_t>(value>>(24u-i*8u));}
IgbObjectInstance object(std::uint32_t address,IgbRuntimeObjectKind kind=IgbRuntimeObjectKind::thread_list){
    IgbObjectInstance out;out.storage=std::make_shared<IgbObjectStorage>();out.storage->original_address=address;out.offset=8;out.storage->bytes=bytes(0x40,0xcc);
    out.runtime_object=std::make_shared<IgbRuntimeObject>(kind);out.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{out.storage,out.offset};
    out.storage->bindings.push_back({out.offset,{},{},{},{},{},out.runtime_object});
    igb_bootstrap_write_word({out.storage,out.offset},0,0x80476ce0);igb_object_set_reference_word(out,1);return out;
}
struct Allocation {std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t address{},offset{},size{};};
struct Fixture {
    std::shared_ptr<IgbMemoryMemberCopyServices> services=std::make_shared<IgbMemoryMemberCopyServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    IgbObjectInstance target=object(0x100000);
    std::shared_ptr<IgbScratchBuffer> default_pointer=bytes(4);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> object_type=std::make_shared<IgbRuntimeMetaType>();
    std::vector<Allocation> allocations;std::vector<IgbBufferCall> buffer_calls;std::vector<IgbMemoryMemberCopyCall> member_calls;
    unsigned frees{},copies{},sizes{},destroyed{};
    std::function<MessengerCallResult(const IgbBufferCall&,IgbBufferOperation&)> buffer_action;
    std::function<MessengerCallResult(const IgbMemoryMemberCopyCall&,IgbMemoryMemberCopyOperation&)> member_action;
    Fixture(){
        services->buffers=std::make_shared<IgbBufferServices>();services->buffers->values=std::make_shared<IgbFieldValuesServices>();
        services->objects=std::make_shared<IgbObjectFieldConstructServices>();services->objects->values=services->buffers->values;
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();services->registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        object_type->parent=std::shared_ptr<IgbRuntimeMetaType>{};
        services->registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>(object_type);
        field->vtable_word=0x80476088;field->offset=8;field->flag_40=1;field->flag_41=1;field->flag_42=1;
        field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};field->default_bytes=IgbRuntimeFieldDefaultSource{default_pointer,0};set_member(0);
        services->buffers->factory=[this](const IgbBufferCall& call){buffer_calls.push_back(call);auto op=std::make_shared<IgbBufferOperation>();
            op->resume=[this,call,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(buffer_action){const auto status=buffer_action(call,*out);if(!status.completed)return status;}
                if(call.kind==BK::allocation_pool)out->pool_result=pool;
                else if(call.virtual_slot==0x150){++sizes;done=true;return MessengerCallResult{true,find(call.address).size,{}};}
                else if(call.virtual_slot==0x108){++frees;find(call.address).bytes->released=true;}
                else if(call.virtual_slot==0xd8){++copies;const auto allocation=allocate(0x800000+copies*0x100,call.bytes,3,0xcc);
                    allocation.bytes->initialized.assign(allocation.bytes->bytes.size(),0);out->buffer_result={allocation.bytes,allocation.offset};out->buffer_address=allocation.address;}
                else throw std::runtime_error("Unexpected memory-default buffer virtual.");
                done=true;return MessengerCallResult{true,0,{}};};return op;};
        auto native=make_native_igb_memory_member_virtual_factory(services->registration,[this](const IgbMemoryMemberCopyCall& call){
            auto op=std::make_shared<IgbMemoryMemberCopyOperation>();op->resume=[this,call,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(member_action){const auto status=member_action(call,*out);if(!status.completed)return status;}
                check(call.kind==MK::destroy_object && call.function==0x80066e1c,"Memory defaults selected an unexpected object body.");
                ++destroyed;done=true;return MessengerCallResult{true,0,{}};};return op;});
        services->factory=[this,native](const IgbMemoryMemberCopyCall& call){member_calls.push_back(call);return native(call);};
    }
    Allocation allocate(std::uint32_t address,unsigned size,unsigned offset=0,std::uint8_t value=0){
        Allocation allocation{bytes(size+offset+2,value),address,offset,size};allocations.push_back(allocation);
        services->buffers->values->buffers.push_back({allocation.bytes,address-offset});return allocation;
    }
    Allocation& find(std::uint32_t address){for(auto& allocation:allocations)if(allocation.address==address)return allocation;throw std::runtime_error("Unbound synthetic allocation.");}
    void set_member(std::uint32_t pointer,unsigned offset=8){igb_bootstrap_write_word({target.storage,target.offset},offset,pointer);}
    std::uint32_t member(unsigned offset=8){return igb_bootstrap_read_word({target.storage,target.offset},offset);}
    auto element(){auto out=std::make_shared<IgbDecodeRuntimeField>();out->vtable_word=0x804762fc;field->element_field=std::weak_ptr(out);return out;}
    auto entry(std::uint32_t address,std::uint32_t references){auto out=object(address,IgbRuntimeObjectKind::thread);igb_object_set_reference_word(out,references);igb_bind_object_field_instance(services->objects,out);return out;}
};
void null_thread_list_and_live_free_gate(){
    Fixture empty;IgbMemoryMemberDefaultsInvocation cold(empty.field,empty.target);
    check(cold.resume(empty.services).completed && empty.member()==0 && empty.buffer_calls.empty() && empty.member_calls.empty(),
        "Null thread-list defaults invented allocation, element or pool operations.");
    check(empty.target.runtime_object->reference_word==1 && empty.target.runtime_object->vtable_word==0x80476ce0 &&
        igb_bootstrap_read_word({empty.target.storage,8},12)==0xcccccccc,"Thread-list pointer defaults changed an unrelated physical member.");
    check(cold.resume().completed,"Completed memory defaults required services or replayed work.");
    Fixture skipped;auto old=skipped.allocate(0x300000,4);skipped.set_member(old.address);skipped.field->flag_40=1;skipped.field->flag_41=0;
    IgbMemoryMemberDefaultsInvocation keep(skipped.field,skipped.target);
    check(keep.resume(skipped.services).completed && !old.bytes->released && skipped.frees==0 && skipped.member()==0,
        "Defaults used copy flag+40 instead of default free flag+41.");
    Fixture pending;old=pending.allocate(0x300000,4);pending.set_member(old.address);pending.field->flag_40=0;pending.field->flag_41.reset();
    IgbMemoryMemberDefaultsInvocation missing(pending.field,pending.target);
    check(!missing.resume(pending.services).completed && pending.member()==old.address && pending.buffer_calls.empty(),
        "Unknown default free flag was treated as zero or entered a pool callback.");
    pending.field->flag_41=1;bool ready{};unsigned polls{};
    pending.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){if(call.virtual_slot==0x108){++polls;return MessengerCallResult{ready,0x91,"prepared free"};}return MessengerCallResult{true,0,{}};};
    const auto status=missing.resume(pending.services);
    check(!status.completed && status.value==0x91 && !old.bytes->released && pending.member()==old.address,"Pending default free lost its status or physical prefix.");
    ready=true;pending.services->buffers->factory={};
    check(missing.resume(pending.services).completed && polls==2 && pending.frees==1 && old.bytes->released && pending.member()==0,
        "Default free lost its selected callback or used flag+40 to skip a reached operation.");
}
void release_before_free(){
    Fixture f;auto element=f.element();const auto old=f.allocate(0x300000,8);f.set_member(old.address);
    const auto first=f.entry(0x400000,0xa5800001),second=f.entry(0x500000,2);
    word(old.bytes,0,0x400008);word(old.bytes,4,0x500008);f.field->flag_41=0;
    bool ready{};unsigned polls{};f.member_action=[&](const IgbMemoryMemberCopyCall& call,IgbMemoryMemberCopyOperation&){
        check(call.object.runtime_object==first.runtime_object && f.frees==0 && !old.bytes->released,"Defaults freed backing before its old element destructor.");
        ++polls;if(!ready)return MessengerCallResult{false,0x92,"prepared element destruction"};f.field->flag_41=1;return MessengerCallResult{true,0,{}};};
    f.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){if(call.virtual_slot==0x108)check(first.runtime_object->reference_word==0xa5800000 && second.runtime_object->reference_word==1,
        "Default free ran before the complete old element release loop.");return MessengerCallResult{true,0,{}};};
    IgbMemoryMemberDefaultsInvocation run(f.field,f.target);const auto status=run.resume(f.services);
    check(!status.completed && status.value==0x92 && polls==1 && f.frees==0 && first.runtime_object->reference_word==0xa5800000 && second.runtime_object->reference_word==2,
        "Pending default destruction lost the preceding decrement or reached a later element.");
    ready=true;check(run.resume(f.services).completed && polls==2 && f.destroyed==1 && f.frees==1 && old.bytes->released && f.member()==0,
        "Default continuation did not reload flag+41 after the retained release helper.");
    check(f.member_calls.size()==3 && f.member_calls.front().kind==MK::element_metadata && f.member_calls.back().kind==MK::element_metadata,
        "Defaults omitted the independent retain ancestry test after publication.");
}
void copied_defaults_reload_offset_and_retain(){
    Fixture f;auto element=f.element();f.field->flag_42=0;f.field->flag_41=0;const auto original=f.allocate(0x300002,8,2);
    const auto first=f.entry(0x400000,0xffffffff),second=f.entry(0x500000,0xa57fffff);word(original.bytes,2,0x400008);word(original.bytes,6,0x500008);
    word(f.default_pointer,0,original.address);f.set_member(0x12345678,12);
    f.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){if(call.virtual_slot==0xd8){f.field->offset=12;f.field->flag_42=1;word(f.default_pointer,0,0xdeadbeef);}return MessengerCallResult{true,0,{}};};
    IgbMemoryMemberDefaultsInvocation run(f.field,f.target);
    check(run.resume(f.services).completed && f.copies==1 && f.sizes==3 && f.frees==0 && f.member()==0 && f.member(12)==0x800100,
        "Defaults cached the pre-copy member offset or recaptured the changed default pointer.");
    const auto copied=f.find(0x800100);
    check(igb_buffer_word(copied.bytes,3)==0x400008 && igb_buffer_word(copied.bytes,7)==0x500008 && first.runtime_object->reference_word==0 &&
        second.runtime_object->reference_word==0xa5800000 && igb_object_reference_word(second)==0xa5800000 && !original.bytes->released,
        "Default retain failed full-word wrap/carry or changed source element pointers.");
    check(f.buffer_calls.size()==8 && f.buffer_calls[0].kind==BK::allocation_pool && f.buffer_calls[1].kind==BK::allocation_pool &&
        f.buffer_calls[2].virtual_slot==0x150 && f.buffer_calls[3].virtual_slot==0xd8 && f.buffer_calls[4].kind==BK::allocation_pool &&
        f.buffer_calls[5].virtual_slot==0x150 && f.buffer_calls[6].kind==BK::allocation_pool && f.buffer_calls[7].virtual_slot==0x150,
        "Default copy did not execute native5647C and the independent retain size query.");
}
void retain_pending_preserves_captured_backing(){
    Fixture f;auto element=f.element();const auto backing=f.allocate(0x300001,15,1);f.set_member(backing.address);
    const auto first=f.entry(0x400000,0xffffffff);word(backing.bytes,1,0x400008);word(backing.bytes,5,0);word(backing.bytes,9,0x500008);
    IgbMemoryMemberRetainInvocation run(f.field,f.target);
    check(!run.resume(f.services).completed && first.runtime_object->reference_word==0 && f.sizes==1,
        "Retain did not preserve the preceding increment at a missing element owner.");
    const auto second=f.entry(0x500000,0xa5ffffff);word(backing.bytes,9,0xdeadbeef);f.field->flag_42=0;f.field->offset=12;f.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};
    f.set_member(0,8);f.services->factory={};f.services->buffers->factory={};
    check(run.resume(f.services).completed && first.runtime_object->reference_word==0 && second.runtime_object->reference_word==0xa6000000 && f.sizes==1,
        "Retain resumed from mutable flags, count, member or pointer instead of its reached element identity.");
    check(run.resume().completed && second.runtime_object->reference_word==0xa6000000,"Completed retain repeated its reference increment.");
    Fixture gates;gates.field->flag_42.reset();IgbMemoryMemberRetainInvocation unknown(gates.field,{});
    check(!unknown.resume().completed,"Unknown ownership flag was treated as disabled.");gates.field->flag_42=0;
    check(unknown.resume().completed,"Disabled ownership gate dereferenced unused target storage.");
    gates.field->flag_42=1;gates.field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};IgbMemoryMemberRetainInvocation null_element(gates.field,{});
    check(null_element.resume().completed,"Null element gate dereferenced unused target storage.");
    auto expired=std::make_shared<IgbDecodeRuntimeField>();gates.field->element_field=std::weak_ptr(expired);expired.reset();IgbMemoryMemberRetainInvocation stale(gates.field,{});
    rejects([&]{stale.resume(gates.services);},"Expired retain element was treated as null.");check(stale.faulted(),"Expired element did not fault its retain invocation.");
}
void malformed_default_storage_and_publication(){
    for(unsigned mode=0;mode<4;++mode){Fixture f;const auto old=f.allocate(0x300000,4);f.set_member(old.address);
        if(mode==0)f.field->default_bytes=IgbRuntimeFieldDefaultSource{};
        if(mode==1)f.default_pointer->bytes.resize(3);
        if(mode==2)f.default_pointer->initialized[2]=0;
        if(mode==3)f.default_pointer->released=true;
        IgbMemoryMemberDefaultsInvocation bad(f.field,f.target);rejects([&]{bad.resume(f.services);},"Malformed default pointer storage was accepted.");
        check(bad.faulted() && f.frees==1 && old.bytes->released && f.member()==old.address && f.copies==0,
            "Default storage fault lost the completed old free or published a fabricated null.");
        rejects([&]{bad.resume(f.services);},"Faulted defaults invocation resumed.");
    }
    Fixture late;late.field->flag_41=0;late.field->default_bytes.reset();IgbMemoryMemberDefaultsInvocation missing(late.field,late.target);
    check(!missing.resume(late.services).completed && late.member()==0 && late.buffer_calls.empty(),"Unknown descriptor default pointer did not remain pending.");
    late.field->default_bytes=IgbRuntimeFieldDefaultSource{late.default_pointer,0};check(missing.resume(late.services).completed,"Late default pointer binding did not resume.");
    Fixture short_target;short_target.field->flag_41=0;const auto original=short_target.allocate(0x300000,1,0,0x5a);word(short_target.default_pointer,0,original.address);
    short_target.target.storage->bytes->bytes.resize(short_target.target.offset+11);
    IgbMemoryMemberDefaultsInvocation short_write(short_target.field,short_target.target);
    rejects([&]{short_write.resume(short_target.services);},"Short target accepted a four-byte default publication.");
    check(short_write.faulted() && short_target.copies==1 && short_target.find(0x800100).bytes->bytes[3]==0x5a,
        "Publication fault discarded the already completed default copy.");
}
void late_source_binding_and_retained_owners(){
    Fixture f;f.field->flag_41=0;const auto original=f.allocate(0x300000,1,0,0x6c);word(f.default_pointer,0,original.address);
    f.services->buffers->values->buffers.clear();IgbMemoryMemberDefaultsInvocation run(f.field,f.target);
    check(!run.resume(f.services).completed && f.copies==1 && f.sizes==2 && f.member()==0,"Unbound source bytes skipped native copy or published an incomplete buffer.");
    word(f.default_pointer,0,0);f.services->buffers->values->buffers.push_back({original.bytes,original.address});
    check(run.resume(f.services).completed && f.copies==1 && f.member()==0x800100 && f.find(0x800100).bytes->bytes[3]==0x6c,
        "Late source binding recaptured the descriptor pointer or allocated again.");
    Fixture abandoned;auto element=abandoned.element();abandoned.field->flag_42=0;abandoned.field->flag_41=0;
    const auto source=abandoned.allocate(0x300000,4);word(source.bytes,0,0x400008);word(abandoned.default_pointer,0,source.address);
    abandoned.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){if(call.virtual_slot==0xd8)abandoned.field->flag_42=1;return MessengerCallResult{true,0,{}};};
    {IgbMemoryMemberDefaultsInvocation unfinished(abandoned.field,abandoned.target);check(!unfinished.resume(abandoned.services).completed && abandoned.member()==0x800100,
        "Missing retained element owner did not preserve completed buffer publication.");}
    const auto copied=abandoned.find(0x800100).bytes;
    check(std::find(abandoned.services->unfinished.begin(),abandoned.services->unfinished.end(),copied)!=abandoned.services->unfinished.end() &&
        std::find(abandoned.services->unfinished.begin(),abandoned.services->unfinished.end(),abandoned.target.storage)!=abandoned.services->unfinished.end(),
        "Abandoned defaults lost its copied backing or runtime target allocation.");
}
void nested_reentry_preserves_prefixes(){
    for(const bool callback_completes:{false,true}){
        Fixture f;auto element=f.element();const auto backing=f.allocate(0x300000,8);f.set_member(backing.address);
        const auto first=f.entry(0x400000,1),second=f.entry(0x500000,2);word(backing.bytes,0,0x400008);word(backing.bytes,4,0x500008);
        IgbMemoryMemberDefaultsInvocation run(f.field,f.target);unsigned callbacks{};
        f.member_action=[&](const IgbMemoryMemberCopyCall& call,IgbMemoryMemberCopyOperation&){
            check(call.kind==MK::destroy_object && call.object.runtime_object==first.runtime_object,"Nested release selected another object.");++callbacks;
            rejects([&]{run.resume(f.services);},"Default recursion from an old element destructor was accepted.");
            return MessengerCallResult{callback_completes,0x93,"prepared recursive destructor"};};
        rejects([&]{run.resume(f.services);},"Release helper ignored its enclosing default fault.");
        check(run.faulted() && callbacks==1 && first.runtime_object->reference_word==0 && second.runtime_object->reference_word==2 &&
            f.frees==0 && f.copies==0 && !backing.bytes->released && f.member()==backing.address,
            "Recursive destruction changed a later element or freed backing beyond the completed decrement.");
    }
    Fixture release;auto release_element=release.element();const auto old=release.allocate(0x300000,4);release.set_member(old.address);
    const auto old_object=release.entry(0x400000,2);word(old.bytes,0,0x400008);IgbMemoryMemberDefaultsInvocation release_run(release.field,release.target);
    release.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){
        check(call.kind==BK::allocation_pool,"Release helper reached a later pool virtual after enclosing recursion.");
        rejects([&]{release_run.resume(release.services);},"Default recursion from release size lookup was accepted.");return MessengerCallResult{true,0,{}};};
    rejects([&]{release_run.resume(release.services);},"Nested release size query ignored the enclosing fault.");
    check(release_run.faulted() && release.buffer_calls.size()==1 && release.sizes==0 && release.frees==0 && old_object.runtime_object->reference_word==2,
        "Release size helper continued past an invalidated parent into its pool virtual or element loop.");

    for(const bool during_size:{false,true}){
        Fixture f;auto element=f.element();f.field->flag_42=0;f.field->flag_41=0;
        const auto original=f.allocate(0x300000,8);const auto first=f.entry(0x400000,2),second=f.entry(0x500000,3);
        word(original.bytes,0,0x400008);word(original.bytes,4,0x500008);word(f.default_pointer,0,original.address);
        IgbMemoryMemberDefaultsInvocation run(f.field,f.target);unsigned owner_queries{},metadata_callbacks{};
        f.buffer_action=[&](const IgbBufferCall& call,IgbBufferOperation&){
            if(call.virtual_slot==0xd8)f.field->flag_42=1;
            if(call.kind==BK::allocation_pool && ++owner_queries==4 && during_size)
                rejects([&]{run.resume(f.services);},"Default recursion from retain size lookup was accepted.");
            return MessengerCallResult{true,0,{}};};
        const auto native=f.services->factory;
        f.services->factory=[&](const IgbMemoryMemberCopyCall& call){auto operation=native(call);
            if(call.kind==MK::element_metadata){const auto resume=operation->resume;operation->resume=[&,resume]{++metadata_callbacks;const auto status=resume();
                if(!during_size)rejects([&]{run.resume(f.services);},"Default recursion from retain metadata was accepted.");return status;};}return operation;};
        rejects([&]{run.resume(f.services);},"Retain helper ignored its enclosing default fault.");
        check(run.faulted() && f.copies==1 && f.sizes==2 && f.member()==0x800100 && metadata_callbacks==1 &&
            first.runtime_object->reference_word==2 && second.runtime_object->reference_word==3 && !original.bytes->released &&
            owner_queries==(during_size?4u:3u),"Recursive retain continued into a size virtual or changed references after completed publication.");
    }
    Fixture selected;selected.field->flag_41=0;const auto source=selected.allocate(0x300000,1);word(selected.default_pointer,0,source.address);
    IgbMemoryMemberDefaultsInvocation selected_run(selected.field,selected.target);unsigned selections{},polls{};
    selected.services->buffers->factory=[&](const IgbBufferCall&){++selections;
        rejects([&]{selected_run.resume(selected.services);},"Default recursion from buffer factory selection was accepted.");
        auto operation=std::make_shared<IgbBufferOperation>();operation->resume=[&]{++polls;return MessengerCallResult{true,0,{}};};return operation;};
    rejects([&]{selected_run.resume(selected.services);},"Selected buffer helper ignored its enclosing fault.");
    check(selected_run.faulted() && selections==1 && polls==0 && selected.member()==0,"Buffer helper executed a selected operation after its enclosing invocation faulted.");
}
void reentry_and_adapter(){
    Fixture f;f.field->flag_41=0;const auto original=f.allocate(0x300000,1);word(f.default_pointer,0,original.address);
    IgbMemoryMemberDefaultsInvocation run(f.field,f.target);f.buffer_action=[&](const IgbBufferCall&,IgbBufferOperation&){rejects([&]{run.resume(f.services);},"Recursive defaults invocation was accepted.");return MessengerCallResult{true,0,{}};};
    rejects([&]{run.resume(f.services);},"Swallowed recursive default fault was ignored by the outer invocation.");
    check(run.faulted() && f.copies==0 && f.member()==0,"Recursive default failure ran later allocation or publication.");
    Fixture adapter;unsigned delegated{};auto factory=make_native_igb_memory_member_defaults_factory(adapter.services,[&](const IgbObjectInitCall&){++delegated;return std::shared_ptr<IgbObjectInitOperation>{};});
    IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::field_virtual;call.virtual_slot=0x70;call.field=adapter.field;call.object=adapter.target;
    auto selected=factory(call);check(selected && selected->resume().completed && delegated==0,"Exact memory default virtual did not compose actual62798.");
    call.virtual_slot=0x74;check(!factory(call) && delegated==1,"Wrong field slot did not delegate.");call.virtual_slot=0x70;adapter.field->vtable_word=0x804762fc;
    check(!factory(call) && delegated==2,"Other field type was swallowed by memory default adapter.");adapter.field->vtable_word=0x80476088;call.kind=IgbObjectInitServiceKind::object_virtual;
    check(!factory(call) && delegated==3,"Object virtual was treated as a memory field default.");call.kind=IgbObjectInitServiceKind::field_virtual;call.field.reset();
    check(!factory(call) && delegated==4,"Null descriptor was swallowed by memory default adapter.");
    auto lifetime=std::make_shared<IgbMemoryMemberCopyServices>();auto weak=std::weak_ptr(lifetime);auto expired=make_native_igb_memory_member_defaults_factory(lifetime);
    call.field=adapter.field;auto retained=expired(call);lifetime.reset();check(!weak.expired() && retained->resume().completed,"Selected memory default adapter did not retain its services.");
    retained.reset();check(weak.expired(),"Completed adapter leaked its service owner after disposal.");
    rejects([&]{expired(call);},"Expired memory default adapter selected a new invocation.");
}
}
int main(){try{null_thread_list_and_live_free_gate();release_before_free();copied_defaults_reload_offset_and_retain();retain_pending_preserves_captured_backing();
    malformed_default_storage_and_publication();late_source_binding_and_retained_owners();nested_reentry_preserves_prefixes();reentry_and_adapter();
    std::cout<<"igb_memory_member_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Memory member defaults failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
