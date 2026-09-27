#include "dw4/igb_event_tracker_registration.hpp"
#include "dw4/igb_object_field_construct.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Expectations come from the sealed original registration/probe contract.
// Parent registration, name services and object destruction are explicit
// prepared endpoints. This does not implement EventTracker defaults or runtime.
namespace {
using namespace dw4;
using T=IgbTypeRegistrationServiceKind;
using C=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
constexpr std::array<std::uint32_t,12> members{0x08,0x30,0x34,0x38,0x3c,0x40,0x44,0x48,0x4c,0x50,0x54,0x5c};
constexpr std::array<std::uint32_t,12> cleanup{0x5c,0x54,0x50,0x4c,0x48,0x44,0x40,0x3c,0x38,0x34,0x30,0x08};
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
auto metadata(std::uint32_t flags=0){auto value=std::make_shared<IgbRuntimeMetaType>();
    value->class_flags=flags;value->reference_word=2;value->post_layout_callback=0;return value;}
struct Fixture {
    std::shared_ptr<IgbEventTrackerRegistrationServices> services=std::make_shared<IgbEventTrackerRegistrationServices>();
    std::shared_ptr<IgbMemoryPoolRegistrationServices> pool=std::make_shared<IgbMemoryPoolRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> allocation_pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(),parent=metadata(4);
    IgbBootstrapPointer local=raw(0x100000,0x2530);
    std::vector<IgbTypeRegistrationCall> calls;
    unsigned local_selections{};
    Fixture(){
        services->memory_pool=pool;pool->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>(target);
        pool->types=std::make_shared<IgbTypeRegistrationServices>();pool->types->metadata_pool_handle=5;
        pool->types->runtime=std::make_shared<IgbRuntimeTypesServices>();pool->types->runtime->singleton=core;
        pool->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();pool->callbacks->runtime=pool->types->runtime;
        pool->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        pool->callbacks->prerequisites->initialized=1;pool->callbacks->prerequisites->started=1;
        pool->object_cleanup=std::make_shared<IgbObjectFieldConstructServices>();
        core->registration_filter_enabled=0;core->registration_callback_pool=allocation_pool;
        igb_bootstrap_write_word(local,4,0xe5812345);
        services->local_storage=[this]{++local_selections;return local;};
        auto remaining=[this](const IgbTypeRegistrationCall& call){
            calls.push_back(call);
            if(call.kind==T::defaults)return std::shared_ptr<IgbTypeRegistrationOperation>{};
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[this,call,out=op.get()]{
                switch(call.kind){
                case T::pool_for_handle:out->object_result=allocation_pool;break;
                case T::register_parent:check(call.function==0x80066b08,"Registrar selected another parent body.");break;
                case T::get_parent:check(call.function==0x800237d0,"Registrar selected another parent getter.");out->metadata_result=parent;break;
                case T::attach_parent:check(call.parent==parent && call.metadata==target,"Registrar attached another parent or receiver.");break;
                case T::set_name:call.metadata->name=call.name;break;
                default:throw std::runtime_error("Prepared registrar crossed the explicit defaults boundary.");
                }
                return MessengerCallResult{true,0,{}};
            };return op;
        };
        pool->types->factory=make_native_igb_event_tracker_registration_type_factory(services,remaining);
        pool->callbacks->factory=make_native_igb_event_tracker_registration_function_factory(services);
    }
    IgbObjectInstance object(std::uint32_t address,std::uint32_t reference,bool bind=true){
        auto pointer=raw(address,0x10);IgbObjectInstance value;value.storage=pointer.storage;
        value.field=std::make_shared<IgbDecodeRuntimeField>();value.field->physical_storage=IgbFieldStorageBinding{pointer.storage,0};
        pointer.storage->bindings.push_back({0,{},{},{},{},value.field});igb_bootstrap_write_word(pointer,4,reference);
        if(bind)pool->object_cleanup->objects.push_back(value);return value;
    }
};

void exact_profile_and_actual_defaults_boundary(){
    Fixture f;const auto args=igb_event_tracker_registration_arguments(f.services);
    check(args.metadata_slot==f.pool->event_tracker_metadata && args.metadata_slot==igb_event_tracker_metadata_slot(f.services),
        "EventTracker duplicated the allocation-only getter's80561BA4 cell.");
    check(args.flag==0 && args.native_size==0x2530 && args.name && *args.name=="igEventTracker" &&
        args.parent_registration==0x80066b08 && args.parent_getter==0x800237d0 && args.field_callback==0x80030df0 &&
        args.layout_probe==0x80030cc8 && args.defaults_callback==0x80030e10 && !args.trailing_callback,
        "EventTracker registrar arguments differ from original30D78.");
    check(args.extra_callbacks && *args.extra_callbacks==std::vector<std::uint32_t>({
        0x800249f4,0x80038478,0x8002d584,0x800368d4,0x800364c4,0x80038244,0x80026098,0}),
        "EventTracker lost the seven ordered deferred callbacks or terminator.");
    f.core->vtable_offset=0;IgbEventTrackerRegistrationInvocation run;
    check(!run.resume(f.services).completed && !run.faulted(),"Unimplemented30E10 was fabricated as a completed defaults body.");
    const std::vector<T> expected{T::pool_for_handle,T::register_parent,T::get_parent,T::attach_parent,T::set_name,T::defaults};
    check(f.calls.size()==expected.size(),"Actual registrar skipped or added a lower call before defaults.");
    for(std::size_t i=0;i<expected.size();++i)check(f.calls[i].kind==expected[i],"Registrar changed the original parent/name/probe/defaults sequence.");
    check(f.calls.back().function==0x80030e10 && f.target->class_flags==9 && f.target->native_size==0x2530 &&
        f.target->flag_1a==0 && f.target->field_callback==0x80030df0 && f.target->type_word==0x80472a58 && f.local_selections==1,
        "Reached defaults boundary lost the concrete layout/busy prefix or published readiness too soon.");
    check(!run.resume(f.services).completed && f.local_selections==1 && f.target->class_flags==9,
        "Repeated missing defaults replayed the physical probe or published extra callbacks.");
    rejects([&]{run.result();},"Pending registrar exposed a completed callback result.");
}

void prerequisite_before_cell_capture(){
    Fixture f;auto initial=f.pool->event_tracker_metadata;*initial=metadata(4);
    f.pool->callbacks->prerequisites->initialized.reset();f.pool->event_tracker_metadata.reset();
    IgbEventTrackerRegistrationInvocation run;
    check(!run.resume(f.services).completed && f.calls.empty(),"Body captured its canonical cell before21B94.");
    f.pool->callbacks->prerequisites->initialized=0;unsigned polls{};bool ready{};std::vector<std::uint32_t> functions;
    f.pool->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){
        check(call.kind==C::invoke_function,"Prerequisite selected another service.");functions.push_back(call.function);
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,17,"Prepared prerequisite."};};return op;
    };
    check(!run.resume(f.services).completed && polls==1 && functions==std::vector<std::uint32_t>{0x80023880} &&
        f.pool->callbacks->prerequisites->started==1,"Prerequisite lost its committed started flag or first callback.");
    auto captured=std::make_shared<IgbRegistrationMetaSlot>(metadata(4));f.pool->event_tracker_metadata=captured;
    f.pool->types->metadata_pool_handle.reset();ready=true;
    check(!run.resume(f.services).completed && polls==6 && functions==std::vector<std::uint32_t>({
        0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}) && f.calls.empty(),
        "Prerequisite continuation repeated selection or skipped original dependencies.");
    f.pool->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata(0));f.pool->types->metadata_pool_handle=5;
    check(run.resume(f.services).completed && !run.result() && f.calls.size()==1 && f.calls[0].kind==T::pool_for_handle && polls==6,
        "Body recaptured its cell after the registrar pool suspension or bypassed the ready-class pool lookup.");
    check(run.resume().completed && polls==6,"Completed registrar replayed prerequisites.");
}

void lookup_modes_and_captured_reload(){
    for(const auto mode:{IgbEventTrackerLookup::plain,IgbEventTrackerLookup::fully_registered}){
        Fixture f;f.pool->event_tracker_metadata->reset();IgbEventTrackerLookupInvocation run(mode);
        check(!run.resume(f.services).completed && !run.faulted(),"Unknown metadata became known-null.");
        auto ready=metadata(4);*f.pool->event_tracker_metadata=ready;
        check(run.resume(f.services).completed && run.result()==ready && f.calls.empty(),"Ready getter invoked an irrelevant lower endpoint.");
        *f.pool->event_tracker_metadata=metadata();check(run.resume().completed && run.result()==ready,"Completed getter recaptured its result.");
    }
    Fixture plain;*plain.pool->event_tracker_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    IgbEventTrackerLookupInvocation read(IgbEventTrackerLookup::plain);
    check(read.resume(plain.services).completed && !read.result() && plain.calls.empty(),"Plain93D1C registered a null class.");
    for(const bool null_input:{false,true})for(const bool null_result:{false,true}){
        Fixture f;auto captured=f.pool->event_tracker_metadata;*captured=null_input?std::shared_ptr<IgbRuntimeMetaType>{}:metadata(8);
        unsigned selections{},polls{};bool ready{};auto replacement=metadata(4);
        f.pool->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){
            check(call.kind==C::invoke_function && call.function==0x80030d78,"Full getter selected another wrapper's body.");
            ++selections;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{
                ++polls;if(!ready)return MessengerCallResult{false,23,"Prepared registration body."};
                *captured=null_result?std::shared_ptr<IgbRuntimeMetaType>{}:replacement;return MessengerCallResult{true,0,{}};
            };return op;
        };
        IgbEventTrackerLookupInvocation run;check(!run.resume(f.services).completed && selections==1 && polls==1,
            "Full getter used busy flag8 as the ready flag4 or skipped a null input.");
        f.pool->event_tracker_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata(4));f.pool->callbacks->factory={};ready=true;
        check(run.resume(f.services).completed && run.result()==(null_result?std::shared_ptr<IgbRuntimeMetaType>{}:replacement) &&
            selections==1 && polls==2,"Full getter lost its captured cell, fresh post-call read or selected continuation.");
    }
    Fixture shared;auto native=make_native_igb_event_tracker_registration_type_factory(shared.services);
    for(const auto fn:{0x80030c8cu,0x80030df0u,0x80093d1cu}){
        *shared.pool->event_tracker_metadata=metadata(4);IgbTypeRegistrationCall call;call.kind=T::get_parent;call.function=fn;
        auto op=native(call);check(op && op->resume().completed && op->metadata_result==**shared.pool->event_tracker_metadata,
            "Getter/field callback adapter did not share80561BA4.");
    }
    IgbEventTrackerMetadataLookupInvocation lazy;auto existing=metadata(0);*shared.pool->event_tracker_metadata=existing;
    check(lazy.resume(shared.pool).completed && lazy.result()==existing,"Existing allocation-only getter forced full EventTracker registration.");
}

void base_prefix_core_capture_and_unknown_padding(){
    Fixture f;f.pool->types->runtime->singleton.reset();IgbEventTrackerProbeInvocation run(f.local);
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(f.local,0)==0x804717f0 &&
        igb_bootstrap_read_word(f.local,4)==1 && f.local.storage->bytes->initialized[8]==0,
        "Unknown ArkCore did not stop after base reset and before the derived stores.");
    f.pool->types->runtime->singleton=f.core;
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(f.local,0)==0x80472a58,"Missing captured offset did not preserve derived construction.");
    std::size_t known{};for(std::size_t i=0;i<0x2530;++i){bool expected=i<8;
        for(const auto offset:members)expected=expected || (i>=offset && i<offset+4);
        const auto actual=f.local.storage->bytes->initialized[i];known+=actual!=0;
        check(actual==(expected?1:0) && (expected || f.local.storage->bytes->bytes[i]==0xcc),
            "EventTracker probe initialized untouched class padding.");
    }
    check(known==56,"EventTracker construction did not establish exactly56 known bytes.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;
    f.pool->types->runtime->singleton=replacement;f.core->vtable_offset=4;igb_bootstrap_write_word(f.local,4,0x81234567);
    check(run.resume(f.services).completed && run.value()==0x81234567 && igb_bootstrap_read_word(f.local,4)==0x81234567,
        "Probe recaptured ArkCore or replayed base reset after offset suspension.");
    check(run.resume().completed && run.value()==0x81234567,"Completed probe did not retain its captured result.");

    Fixture absent;absent.pool->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    IgbEventTrackerProbeInvocation null_core(absent.local);rejects([&]{null_core.resume(absent.services);},"Known-null ArkCore was treated as unknown.");
    check(null_core.faulted() && igb_bootstrap_read_word(absent.local,0)==0x80472a58 &&
        igb_bootstrap_read_word(absent.local,0x5c)==0,"Null core fault skipped preceding derived stores.");
    Fixture header;header.local.storage->bytes->initialized[4]=0;IgbEventTrackerProbeInvocation bad(header.local);
    rejects([&]{bad.resume(header.services);},"Base reset guessed an unknown incoming reference word.");
    check(igb_bootstrap_read_word(header.local,0)==0x804717f0 && header.local.storage->bytes->initialized[8]==0 &&
        header.local.storage->bytes->bytes[5]==0x81,"Base reference failure ran later byte or member stores.");
}

void selected_word_and_atomic_store_prefixes(){
    Fixture unknown;IgbEventTrackerProbeInvocation run(unknown.local);check(!run.resume(unknown.services).completed,"Offset should remain unknown.");
    igb_bootstrap_write_word(unknown.local,0,0x12345678);unknown.core->vtable_offset=0x58;
    rejects([&]{run.resume(unknown.services);},"Probe guessed untouched+58 selected bytes.");
    check(run.faulted() && igb_bootstrap_read_word(unknown.local,0)==0x12345678,"Selection failure executed cleanup's later derived VT store.");
    igb_bootstrap_write_word(unknown.local,0x58,9);rejects([&]{run.resume(unknown.services);},"Repair revived a faulted probe.");
    Fixture tail;tail.core->vtable_offset=0x252c;igb_bootstrap_write_word(tail.local,0x252c,0xa1b2c3d4);
    IgbEventTrackerProbeInvocation selected(tail.local);
    check(selected.resume(tail.services).completed && selected.value()==0xa1b2c3d4,"Class-sized probe rejected a represented final word.");
    Fixture short_local;short_local.local.storage->bytes->bytes.resize(0x56);short_local.local.storage->bytes->initialized.resize(0x56);
    short_local.core->vtable_offset=0;IgbEventTrackerProbeInvocation short_run(short_local.local);
    rejects([&]{short_run.resume(short_local.services);},"Partial+54 destination was treated as a complete original store.");
    check(igb_bootstrap_read_word(short_local.local,0x50)==0 && short_local.local.storage->bytes->initialized[0x54]==0 &&
        short_local.local.storage->bytes->initialized[0x55]==0,"Failed member store modified a partial word or erased its earlier prefix.");
    Fixture tiny;tiny.local.storage->bytes->bytes.resize(0x60);tiny.local.storage->bytes->initialized.resize(0x60);
    tiny.core->vtable_offset=0x252c;IgbEventTrackerProbeInvocation out_of_range(tiny.local);
    rejects([&]{out_of_range.resume(tiny.services);},"Declared class size fabricated absent selected-word storage.");
    check(igb_bootstrap_read_word(tiny.local,0x5c)==0,"Selection bounds failure lost completed construction.");
}

void all_fresh_cleanup_members_and_captured_result(){
    Fixture f;std::array<IgbObjectInstance,12> objects;std::array<std::uint32_t,12> addresses{};
    for(unsigned i=0;i<objects.size();++i){addresses[i]=0x200000+i*0x100;objects[i]=f.object(addresses[i],0x80000001);}
    auto stale=f.object(0x300000,2);IgbEventTrackerProbeInvocation run(f.local);
    check(!run.resume(f.services).completed,"Cleanup injection requires the real captured-core offset boundary.");
    igb_bootstrap_write_word(f.local,cleanup[0],addresses[0]);igb_bootstrap_write_word(f.local,cleanup[1],0x300000);f.core->vtable_offset=0;
    unsigned selections{},polls{};bool ready{};
    f.pool->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& call){
        check(call.kind==IgbObjectFieldConstructServiceKind::destroy_object && selections<12 && call.object.field==objects[selections].field,
            "Cleanup changed original reverse member order or used a stale later pointer.");
        const auto index=selections++;auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&,index]{
            ++polls;if(index==0 && !ready)return MessengerCallResult{false,31,"Prepared first destructor."};
            objects[index].storage->bytes->released=true;
            if(index+1<objects.size())igb_bootstrap_write_word(f.local,cleanup[index+1],addresses[index+1]);
            igb_bootstrap_write_word(f.local,0,0x11223344);return MessengerCallResult{true,0,{}};
        };return op;
    };
    check(!run.resume(f.services).completed && selections==1 && polls==1 && igb_object_reference_word(objects[0])==0x80000000 &&
        igb_object_reference_word(stale)==2,"Cleanup lost full-word arithmetic or reached a later member before destruction completed.");
    objects[0].storage->bytes->initialized[4]=0;
    check(!run.resume(f.services).completed && polls==2 && selections==1 && !run.faulted(),
        "Retained destructor polling introduced another outer reference read.");
    f.pool->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& call){
        check(call.kind==IgbObjectFieldConstructServiceKind::destroy_object && selections<12 && call.object.field==objects[selections].field,
            "Later fresh member did not select its corresponding object.");
        const auto index=selections++;auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&,index]{
            ++polls;objects[index].storage->bytes->released=true;
            if(index+1<objects.size())igb_bootstrap_write_word(f.local,cleanup[index+1],addresses[index+1]);
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    ready=true;check(run.resume(f.services).completed && selections==12 && polls==14 && run.value()==0x80472a58 &&
        igb_bootstrap_read_word(f.local,0)==0x11223344 && igb_object_reference_word(stale)==2,
        "Cleanup lost its captured result, reread a destroyed header or restored a base table.");
    for(unsigned i=0;i<addresses.size();++i)check(igb_bootstrap_read_word(f.local,cleanup[i])==addresses[i],
        "Cleanup cleared an original member instead of retaining its released pointer.");
}

void captured_pointer_late_mapping_and_fresh_reference_test(){
    Fixture f;auto captured=f.object(0x200000,0,false),replacement=f.object(0x210000,0x800002);
    IgbEventTrackerProbeInvocation run(f.local);check(!run.resume(f.services).completed,"Offset should be pending.");
    igb_bootstrap_write_word(f.local,0x5c,0x200000);f.core->vtable_offset=0;
    check(!run.resume(f.services).completed && igb_object_reference_word(captured)==0,"Unknown object mapping performed a guessed reference read.");
    igb_bootstrap_write_word(f.local,0x5c,0x210000);igb_bootstrap_write_word(f.local,0x54,0x210000);f.pool->object_cleanup->objects.push_back(captured);
    check(run.resume(f.services).completed && igb_object_reference_word(captured)==0xffffffff &&
        igb_object_reference_word(replacement)==0x800001,"Late mapping recaptured member pointer or lost full32-bit decrement.");
    // The enclosing checker is an explicit synthetic observation boundary,
    // not an assertion that the original runtime schedules a callback here.
    for(const bool make_zero:{false,true}){
        Fixture fresh;auto object=fresh.object(0x220000,make_zero?2u:1u);unsigned destroys{};bool mutated{};
        IgbEventTrackerProbeInvocation call(fresh.local);check(!call.resume(fresh.services).completed,"Offset should be pending.");
        igb_bootstrap_write_word(fresh.local,0x5c,0x220000);fresh.core->vtable_offset=0;
        fresh.pool->object_cleanup->factory=[&](const auto& request){check(request.object.field==object.field,"Reference reload destroyed another object.");
            ++destroys;auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;};
        const auto observe=[&]{if(!mutated && igb_object_reference_word(object)==(make_zero?1u:0u)){
            mutated=true;igb_bootstrap_write_word({object.storage,0},4,make_zero?0x800000u:0x800002u);
        }};
        check(call.resume(fresh.services,observe).completed && mutated && destroys==(make_zero?1u:0u),
            "Cleanup tested stale low23 bits instead of freshly reading the physical post-decrement word.");
    }
}

void local_identity_reentry_and_retained_owners(){
    Fixture f;auto first=f.object(0x200000,1),second=f.object(0x210000,2);IgbEventTrackerProbeInvocation run(f.local);
    check(!run.resume(f.services).completed,"Offset should be pending.");
    igb_bootstrap_write_word(f.local,0x5c,0x200000);igb_bootstrap_write_word(f.local,0x54,0x210000);f.core->vtable_offset=0;
    f.pool->object_cleanup->factory=[&](const auto&){auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{
        rejects([&]{run.resume(f.services);},"Recursive EventTracker probe was accepted.");return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{run.resume(f.services);},"Caught recursion allowed the next native member release.");
    check(run.faulted() && igb_object_reference_word(first)==0 && igb_object_reference_word(second)==2,
        "Reentry failure lost the reached reference prefix or touched the next member.");
    rejects([&]{run.resume(f.services);},"Faulted cleanup resumed after its recursive callback.");
    Fixture direct;IgbEventTrackerProbeInvocation enclosed(direct.local);bool entered{};
    rejects([&]{enclosed.resume(direct.services,[&]{if(!entered){entered=true;rejects([&]{enclosed.resume(direct.services);},
        "Recursive enclosing check was accepted.");}});},"Caught enclosing recursion allowed a later physical action.");
    check(direct.local.storage->bytes->initialized[0]==0,"Faulted enclosing checker was followed by base construction.");
    for(const bool owner_change:{false,true}){Fixture identity;IgbEventTrackerProbeInvocation selected(identity.local);
        check(!selected.resume(identity.services).completed,"Offset should be pending.");
        const auto old_bytes=identity.local.storage->bytes;
        if(owner_change)identity.local.storage->bytes=raw(0x100000,0x2530).storage->bytes;
        else identity.local.storage->original_address+=0x1000;
        identity.core->vtable_offset=0;rejects([&]{selected.resume(identity.services);},"Captured local identity mutation was ignored.");
        check(selected.faulted() && old_bytes->bytes[0]==0x80,"Identity failure altered the captured prefix.");
    }
    Fixture abandoned;std::weak_ptr<IgbScratchBuffer> old_local=abandoned.local.storage->bytes;
    {IgbEventTrackerProbeInvocation pending(abandoned.local);check(!pending.resume(abandoned.services).completed,"Offset should be pending.");
        abandoned.local.storage->bytes=raw(0x100000,0x2530).storage->bytes;}
    check(!old_local.expired(),"Abandoned probe dropped its captured local byte owner.");abandoned.services->retained.clear();
    check(old_local.expired(),"Captured local ownership formed a services cycle.");
    Fixture held;auto victim=held.object(0x230000,1);std::weak_ptr<IgbScratchBuffer> old_object=victim.storage->bytes;
    {IgbEventTrackerProbeInvocation pending(held.local);check(!pending.resume(held.services).completed,"Offset should be pending.");
        igb_bootstrap_write_word(held.local,0x5c,0x230000);held.core->vtable_offset=0;
        held.pool->object_cleanup->factory=[](const auto&){auto op=std::make_shared<IgbObjectFieldConstructOperation>();
            op->resume=[] {return MessengerCallResult{false,0,"Prepared destruction."};};return op;};
        check(!pending.resume(held.services).completed && igb_object_reference_word(victim)==0,"Destruction should retain its decremented object.");
        victim.storage->bytes=raw(0x230000,0x10).storage->bytes;}
    check(!old_object.expired(),"Abandoned destruction dropped the captured pointee bytes.");held.services->retained.clear();
    check(old_object.expired(),"Pointee-byte retention created an ownership cycle.");
}

void adapters_delegate_and_retain_local_selection(){
    for(const auto function:{0x80030d50u,0x80030d78u}){Fixture ready;*ready.pool->event_tracker_metadata=metadata(4);
        IgbRegistrationCallbacksCall call;call.kind=C::invoke_function;call.function=function;
        auto selected=ready.pool->callbacks->factory(call);
        check(selected && selected->resume().completed && ready.calls.size()==1 && ready.calls[0].kind==T::pool_for_handle,
            "Exact EventTracker wrapper/body adapter did not preserve the ready registrar's pool lookup.");
    }
    Fixture parent;*parent.pool->event_tracker_metadata=metadata(4);IgbTypeRegistrationCall parent_call;
    parent_call.kind=T::register_parent;parent_call.function=0x80030d78;
    auto parent_op=parent.pool->types->factory(parent_call);
    check(parent_op && parent_op->resume().completed && !parent_op->callbacks_result,
        "Parent-body adapter did not publish the completed null callback-list result.");
    Fixture f;unsigned delegated{};auto token=std::make_shared<IgbTypeRegistrationOperation>();
    auto type=make_native_igb_event_tracker_registration_type_factory(f.services,[&](const auto&){++delegated;return token;});
    IgbTypeRegistrationCall call;call.kind=T::defaults;call.function=0x80030e10;
    check(type(call)==token && delegated==1,"Adapter swallowed the unimplemented33-field defaults boundary.");
    call.kind=T::layout_probe;call.function=0;check(type(call)==token && delegated==2,"Zero layout function accidentally matched EventTracker.");
    call.kind=T::get_parent;call.function=0x80030c50;check(type(call)==token && delegated==3,"Class adapter replaced the existing allocation-only getter.");
    call.kind=T::layout_probe;call.function=0x80030cc8;f.services->local_storage={};auto op=type(call);
    check(op && !op->resume().completed && f.local_selections==0,"Missing local provider fabricated initialized class storage.");
    f.services->local_storage=[&]{++f.local_selections;return f.local;};
    check(!op->resume().completed && f.local_selections==1,"Probe did not select one local before waiting for the offset.");
    f.services->local_storage={};f.core->vtable_offset=0;
    check(op->resume().completed && op->resume().value==0x80472a58 && f.local_selections==1,"Pending probe repeated local allocation.");
    Fixture recursive;auto native=make_native_igb_event_tracker_registration_type_factory(recursive.services);auto target=native(call);
    recursive.services->local_storage=[&]{rejects([&]{target->resume();},"Recursive local provider was accepted.");return recursive.local;};
    rejects([&]{target->resume();},"Caught local-provider reentry still ran constructor stores.");
    check(recursive.local.storage->bytes->initialized[0]==0,"Reentered local provider published base construction.");
    auto weak_services=std::make_shared<IgbEventTrackerRegistrationServices>();auto remaining=std::make_shared<IgbRegistrationCallbacksOperation>();
    unsigned callbacks{};auto function=make_native_igb_event_tracker_registration_function_factory(weak_services,[&](const auto&){++callbacks;return remaining;});
    IgbRegistrationCallbacksCall other;other.kind=C::invoke_function;other.function=0x80030e10;
    check(function(other)==remaining && callbacks==1,"Function adapter claimed unimplemented defaults.");
}
} // namespace
int main(){try{
    exact_profile_and_actual_defaults_boundary();prerequisite_before_cell_capture();lookup_modes_and_captured_reload();
    base_prefix_core_capture_and_unknown_padding();selected_word_and_atomic_store_prefixes();
    all_fresh_cleanup_members_and_captured_result();captured_pointer_late_mapping_and_fresh_reference_test();
    local_identity_reentry_and_retained_owners();adapters_delegate_and_retain_local_selection();
    std::cout<<"EventTracker registration/probe checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"EventTracker registration/probe failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
