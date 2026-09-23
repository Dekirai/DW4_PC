#include "dw4/igb_field_clone.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbFieldCloneServiceKind;using BK=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t bytes=0x50){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();
    storage->bytes->bytes.assign(bytes,0xcc);storage->bytes->initialized.assign(bytes,1);return {storage,0};
}
struct Fixture {
    std::shared_ptr<IgbFieldCloneServices> services=std::make_shared<IgbFieldCloneServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbDecodeRuntimeField> source=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbDecodeRuntimeField> destination=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer storage=allocation(0x200000);
    std::vector<IgbFieldCloneCall> calls;std::vector<IgbBootstrapCall> allocations;
    IgbFieldCloneFactory remaining;IgbBootstrapFactory allocation_factory;
    Fixture(){
        storage.storage->bindings.push_back({0,{},{},{},{},destination});
        services->fields=std::make_shared<IgbMetaFieldFactoryServices>();services->fields->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->fields->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();services->fields->bootstrap->runtime->singleton=core;
        services->fields->bootstrap->default_pool_handle=3;
        services->values=std::make_shared<IgbFieldValuesServices>();services->values->fields=std::make_shared<IgbFieldOperationsServices>();
        services->values->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->fields->bootstrap->factory=[this](const IgbBootstrapCall& c){
            allocations.push_back(c);if(allocation_factory)return allocation_factory(c);
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==BK::pool_for_handle)out->pool_result=pool;
                else if(c.kind==BK::pool_allocate || c.kind==BK::heap_allocate)out->allocation_result=storage;
                else throw std::runtime_error("Unexpected clone allocation endpoint");
                return MessengerCallResult{true,0,{}};};return op;
        };
        services->factory=[this](const IgbFieldCloneCall& c){calls.push_back(c);if(remaining)return remaining(c);
            auto op=std::make_shared<IgbFieldCloneOperation>();op->resume=[this,c,out=op.get()]{if(c.kind==K::reflected_clone)out->allocation_result=storage;
                return MessengerCallResult{true,0,{}};};return op;
        };
    }
};
struct CopyFixture:Fixture {
    std::shared_ptr<IgbScratchBuffer> original_default=std::make_shared<IgbScratchBuffer>();
    std::shared_ptr<IgbScratchBuffer> copied_default=std::make_shared<IgbScratchBuffer>();
    std::shared_ptr<IgbRuntimeMetaType> declaring=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbDecodeRuntimeField> element=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeFieldSlot> global=std::make_shared<IgbRuntimeFieldSlot>();
    std::shared_ptr<IgbInternedString> name=std::make_shared<IgbInternedString>();
    std::function<MessengerCallResult(const IgbFieldCopyCall&,IgbFieldCopyOperation&)> copy_action;
    std::vector<IgbFieldCopyCall> copy_calls;
    CopyFixture(){
        services->copy=std::make_shared<IgbFieldCopyServices>();services->copy->fields=services->fields;services->copy->values=services->values;
        source->reference_word=0x12800005;source->vtable_word=0x80476088;source->offset=0x44;source->type_ordinal=0xff04;source->ordinal=321;source->size=4;
        source->flag_25=1;source->flag_26=1;source->default_mode=3;source->flag_27=2;source->flag_28=3;source->flag_29=4;
        source->flag_2a=5;source->flag_2b=6;source->flag_2c=7;source->word_30=0x12345678;source->flag_34=1;source->flag_35=2;
        source->word_38=0x80;source->element_field=std::weak_ptr<IgbDecodeRuntimeField>(element);source->flag_40=3;source->flag_41=4;
        source->flag_42=5;source->word_44=0xfffffff8;source->word_48=0x55555555;source->word_4c=0x66666666;
        source->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>(global);source->global_slot_address=0x600000;source->declaring_type=std::weak_ptr<IgbRuntimeMetaType>(declaring);
        name->value=std::make_shared<const std::string>("_data");name->container=pool;name->references=7;
        source->name=name->value;source->name_string=name;source->name_string_address.reset();services->values->strings.push_back({0x720000,name});
        services->values->fields->pointer_lists->values.push_back({0x710000,declaring,{}});
        services->values->fields->pointer_lists->values.push_back({0x730000,{},element});element->reference_word=99;
        original_default->bytes={1,2,3,4};original_default->initialized.assign(4,1);
        copied_default->bytes.assign(4,0xbb);copied_default->initialized.assign(4,1);
        source->default_bytes=IgbRuntimeFieldDefaultSource{original_default,0};source->default_bytes_address=0x740000;
        services->copy->factory=[this](const IgbFieldCopyCall& c){copy_calls.push_back(c);auto op=std::make_shared<IgbFieldCopyOperation>();
            op->resume=[this,c,out=op.get()]{if(copy_action)return copy_action(c,*out);
                if(c.kind==IgbFieldCopyServiceKind::tagged_pool)out->pool_result=pool;
                else if(c.kind==IgbFieldCopyServiceKind::allocate_default){check(c.bytes==4 && c.pool==pool,"Copy constructor changed actual681C4 allocation size/pool.");
                    out->buffer_result={copied_default,0};out->buffer_address=0x750000;}
                else throw std::runtime_error("Unexpected field copy service");return MessengerCallResult{true,0,{}};};return op;};
    }
};
void scalar_fixture(CopyFixture& f,IgbMetaFieldKind kind){
    f.storage.storage->bytes->bytes.resize(0x34);f.storage.storage->bytes->initialized.resize(0x34);
    f.source->vtable_word=igb_meta_field_profile(kind).vtable;f.source->size=1;
    f.source->flag_34.reset();f.source->flag_35.reset();f.source->element_field.reset();f.source->flag_42.reset();
    f.original_default->bytes.resize(1);f.original_default->initialized.resize(1);
    f.copied_default->bytes.resize(1);f.copied_default->initialized.resize(1);
    f.copy_action=[&f](const IgbFieldCopyCall& c,IgbFieldCopyOperation& out){
        if(c.kind==IgbFieldCopyServiceKind::tagged_pool)out.pool_result=f.pool;
        else{check(c.kind==IgbFieldCopyServiceKind::allocate_default && c.bytes==1 && c.pool==f.pool,
                "Scalar copy changed the one-byte default allocation.");out.buffer_result={f.copied_default,0};out.buffer_address=0x750000;}
        return MessengerCallResult{true,0,{}};
    };
}
void null_allocation_and_gates(){
    for(bool heap:{false,true})for(const auto argument:{0u,1u,2u,0xffffffffu}){
        Fixture f;if(heap)f.pool.reset();f.storage={};IgbFieldCloneInvocation call(f.source,argument);
        check(call.resume(f.services).completed && !call.result() && igb_bootstrap_address(call.allocation())==0,"Cold null allocation did not return original null.");
        check(f.calls.empty() && f.allocations.size()==2 && f.allocations[0].kind==BK::pool_for_handle && f.allocations[0].handle==3 &&
            f.allocations[1].kind==(heap?BK::heap_allocate:BK::pool_allocate) && f.allocations[1].bytes==0x50,"Cold clone used ordinary construction or another allocation profile.");
        check(call.resume().completed && f.allocations.size()==2,"Completed null clone replayed allocation.");
    }
    Fixture f;f.services->fields->bootstrap->runtime->singleton.reset();IgbFieldCloneInvocation missing(f.source);
    check(!missing.resume(f.services).completed && f.allocations.empty() && f.calls.empty(),"Unknown core selected a clone branch.");
    f.services->fields->bootstrap->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{missing.resume(f.services);},"Known null core was treated as disabled.");
    Fixture captured;bool ready{};unsigned polls{};
    captured.allocation_factory=[&](const IgbBootstrapCall& c){auto op=std::make_shared<IgbBootstrapOperation>();
        if(c.kind==BK::pool_for_handle)op->resume=[&,out=op.get()]{out->pool_result=captured.pool;return MessengerCallResult{true,0,{}};};
        else op->resume=[&]{++polls;return MessengerCallResult{ready,0,"Prepared null allocation"};};return op;};
    IgbFieldCloneInvocation call(captured.source);check(!call.resume(captured.services).completed && polls==1,"Pending raw allocation did not suspend.");
    captured.core->enabled=1;captured.services->fields->bootstrap->factory={};ready=true;
    check(call.resume(captured.services).completed && !call.result() && polls==2 && captured.calls.empty(),"Pending cold allocation reselected enabled cloning.");
}
void enabled_order_and_adapters(){
    for(const auto argument:{0u,1u,2u,0xffffffffu}){
        Fixture f;f.core->enabled=1;IgbFieldCloneInvocation call(f.source,argument);
        check(call.resume(f.services).completed && call.result()==f.destination,"Prepared reflected clone lost its typed result.");
        check(f.allocations.empty() && f.calls.size()==2 && f.calls[0].kind==K::register_type && f.calls[0].function==0x8002ae84 &&
            f.calls[1].kind==K::reflected_clone && f.calls[1].function==0x800680b4 && f.calls[1].source==f.source && f.calls[1].argument==argument,
            "Enabled clone changed registration, source or mode argument.");
        check(f.storage.storage->bytes->bytes==std::vector<std::uint8_t>(0x50,0xcc) && f.services->completed_allocations.size()==1,
            "Prepared enabled clone secretly executed cold copying or lost storage ownership.");
    }
    Fixture retained;retained.core->enabled=1;bool ready{};unsigned polls{};
    retained.remaining=[&](const IgbFieldCloneCall& c){auto op=std::make_shared<IgbFieldCloneOperation>();
        if(c.kind==K::register_type)op->resume=[&]{++polls;return MessengerCallResult{ready,0,"Prepared registration"};};
        else op->resume=[&,out=op.get()]{out->allocation_result=retained.storage;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldCloneInvocation pending(retained.source,0x1234);check(!pending.resume(retained.services).completed,"Registration did not retain its pending callback.");
    retained.core->enabled=0;ready=true;
    check(pending.resume(retained.services).completed && polls==2 && retained.calls.size()==2 && retained.calls.back().argument==0x1234 && retained.allocations.empty(),
        "Pending registration replayed gate selection or lost the clone argument.");
    Fixture f;f.core->enabled=1;unsigned delegated{};
    const auto factory=make_native_igb_list_field_clone_factory(f.services,[&](const IgbListDefaultsCall&){++delegated;return std::shared_ptr<IgbListDefaultsOperation>{};});
    IgbListDefaultsCall c;c.kind=IgbListDefaultsServiceKind::clone_field;c.field=f.source;c.vtable_word=0x80476088;c.virtual_slot=0x54;c.argument=2;
    const auto op=factory(c);check(op && op->resume().completed && op->field_result==f.destination && op->field_address==0x200000,"List clone adapter lost the result/address pair.");
    c.vtable_word=0x804762fc;check(!factory(c) && delegated==1,"Clone adapter consumed another field profile.");
    c.vtable_word=0x80476088;c.virtual_slot=0x58;check(!factory(c) && delegated==2,"Clone adapter consumed another virtual.");
    auto services=std::make_shared<IgbFieldCloneServices>();const auto dead=make_native_igb_list_field_clone_factory(services);std::weak_ptr<IgbFieldCloneServices> weak=services;
    services.reset();check(weak.expired(),"Clone adapter retained a permanent services cycle.");c.virtual_slot=0x54;rejects([&]{dead(c);},"Expired clone services were fabricated.");
}
void missing_base_and_constructor_lifetime(){
    Fixture f;IgbFieldCloneInvocation call(f.source);check(!call.resume(f.services).completed && !f.destination->construction_active_.expired(),"Cold clone did not reach and retain the actual base-copy boundary.");
    rejects([&]{call.result();},"Incomplete cold copy exposed a descriptor.");
    check(f.calls.empty() && f.allocations.size()==2,"Missing copy services substituted a completed ordinary constructor.");
    check(!call.resume(f.services).completed && f.allocations.size()==2,"Pending base copy repeated raw allocation.");
    Fixture abandoned;{IgbFieldCloneInvocation pending(abandoned.source);check(!pending.resume(abandoned.services).completed,"Abandonment did not reach base-copy suspension.");}
    check(abandoned.destination->construction_fault_ && abandoned.destination->construction_active_.expired() && !abandoned.services->unfinished.empty(),
        "Abandoned clone lost constructor failure or physical storage retention.");
    Fixture alias;alias.destination->physical_storage=IgbFieldStorageBinding{alias.storage.storage,4};IgbFieldCloneConstructorInvocation invalid(alias.storage,alias.source);
    rejects([&]{invalid.resume(alias.services);},"Copy constructor rebound an existing field to another offset.");
    Fixture active;auto token=std::make_shared<int>(0);active.destination->construction_active_=token;
    IgbFieldCloneConstructorInvocation competing(active.storage,active.source);rejects([&]{competing.resume(active.services);},"Copy constructor acquired an active field.");
    check(!active.destination->construction_fault_,"Rejected competing constructor invalidated another owner.");
}
void complete_cold_copy(){
    CopyFixture f;IgbFieldCloneInvocation clone(f.source,0xffffffffu);
    check(clone.resume(f.services).completed && clone.result()==f.destination,"Complete cold copy chain did not construct the destination.");
    check(f.calls.empty() && f.allocations.size()==2 && f.copy_calls.size()==2 && f.copy_calls[0].kind==IgbFieldCopyServiceKind::tagged_pool &&
        f.copy_calls[1].kind==IgbFieldCopyServiceKind::allocate_default,"Cold clone substituted ordinary/default construction or another buffer allocator.");
    check(f.destination->vtable_word==0x80476088 && f.destination->reference_word==0x00800001 && f.name->references==8 && f.element->reference_word==99,
        "Cold clone lost copied header bit, name retain or nonowning element semantics.");
    check(f.destination->offset==0x44 && f.destination->type_ordinal==0xff04 && f.destination->ordinal==321 && f.destination->size==4 &&
        f.destination->declaring_type->lock()==f.declaring && f.destination->global_slot->lock()==f.global && f.destination->name_string->get()==f.name.get(),
        "Base copy lost canonical scalar or pointer identities.");
    check(f.destination->default_bytes->buffer==f.copied_default && f.copied_default!=f.original_default && f.copied_default->bytes==f.original_default->bytes &&
        f.destination->default_bytes_address==0x750000,"Clone did not allocate and copy independent default storage.");
    check(f.destination->flag_34==1 && f.destination->flag_35==2 && f.destination->element_field->lock()==f.element &&
        f.destination->word_38==0x80 && f.destination->flag_40==3 && f.destination->flag_41==4 && f.destination->flag_42==5 &&
        f.destination->word_44==0xfffffff8 && f.destination->word_48==0 && f.destination->word_4c==0,"Derived copy replaced live members with ordinary defaults.");
    const auto bytes=f.storage.storage->bytes;
    check(igb_buffer_word(bytes,0x0c)==0x720000 && igb_buffer_word(bytes,0x18)==0x710000 && igb_buffer_word(bytes,0x1c)==0x600000 &&
        igb_buffer_word(bytes,0x20)==0x750000 && igb_buffer_word(bytes,0x3c)==0x730000,"Copy constructor did not publish original numeric pointer identities.");
    for(const auto at:{0x16u,0x17u,0x2du,0x2eu,0x2fu,0x36u,0x37u,0x43u})check(bytes->bytes[at]==0xcc,"Copy constructor overwrote untouched original padding.");
    check(f.destination->construction_active_.expired() && !f.destination->construction_fault_ && f.services->completed_allocations.size()==1,
        "Completed clone retained a constructor token or dropped its allocation.");
    check(clone.resume().completed && f.name->references==8 && f.copy_calls.size()==2,"Completed clone replayed ownership or default allocation.");
}
void live_derived_reads_and_pointer_binding(){
    CopyFixture f;bool ready{};unsigned polls{};
    f.copy_action=[&](const IgbFieldCopyCall& c,IgbFieldCopyOperation& out){
        if(c.kind==IgbFieldCopyServiceKind::tagged_pool){out.pool_result=f.pool;return MessengerCallResult{true,0,{}};}
        ++polls;if(!ready)return MessengerCallResult{false,0,"Prepared default allocation"};
        f.source->flag_34=8;f.source->flag_35=9;f.source->flag_42=10;f.source->word_38=0x90;f.source->default_mode=2;
        f.source->flag_41=11;f.source->flag_40=12;f.source->word_44=0xabcdef01;
        out.buffer_result={f.copied_default,0};out.buffer_address=0x750000;return MessengerCallResult{true,0,{}};
    };
    IgbFieldCloneInvocation clone(f.source);check(!clone.resume(f.services).completed && polls==1 && f.destination->vtable_word==0x80471914,
        "Pending base allocation prematurely published derived copy state.");
    ready=true;check(clone.resume(f.services).completed && polls==2 && f.allocations.size()==2,"Pending copy restarted allocation or lost its selected callback.");
    check(f.destination->flag_34==8 && f.destination->flag_35==9 && f.destination->flag_42==10 && f.destination->word_38==0x90 &&
        f.destination->default_mode==2 && f.destination->flag_41==11 && f.destination->flag_40==12 && f.destination->word_44==0xabcdef01,
        "Derived memory fields were snapshotted before the base allocator returned.");
    CopyFixture missing;missing.services->values->fields->pointer_lists->values.pop_back();IgbFieldCloneConstructorInvocation copy(missing.storage,missing.source);
    check(!copy.resume(missing.services).completed && missing.destination->flag_42==5 && missing.destination->word_48==0 && missing.destination->word_4c==0,
        "Missing element arithmetic binding did not pause at its original raw pointer store.");
    auto later=std::make_shared<IgbDecodeRuntimeField>();missing.source->element_field=std::weak_ptr<IgbDecodeRuntimeField>(later);missing.source->word_38=0x55;
    missing.services->values->fields->pointer_lists->values.push_back({0x730000,{},missing.element});
    check(copy.resume(missing.services).completed && missing.destination->element_field->lock()==missing.element && missing.destination->word_38==0x55 &&
        missing.name->references==8,"Late element binding changed its captured pointer or replayed base-copy ownership.");
    CopyFixture unknown;unknown.source->flag_35.reset();IgbFieldCloneConstructorInvocation paused(unknown.storage,unknown.source);
    check(!paused.resume(unknown.services).completed && unknown.destination->vtable_word==0x80471384 && unknown.destination->flag_34==1,
        "Unknown destruct flag skipped the completed reference constructor prefix.");
    unknown.source->flag_35=7;check(paused.resume(unknown.services).completed && unknown.destination->flag_35==7,"Late destruct flag could not resume copied state.");
}
void replaced_destination_binding(){
    CopyFixture f;f.source->flag_35.reset();auto clone=std::make_unique<IgbFieldCloneInvocation>(f.source);
    check(!clone->resume(f.services).completed && f.destination->vtable_word==0x80471384 && f.destination->flag_34==1,
        "Binding replacement test did not reach the suspended derived-copy prefix.");
    const auto prefix=f.storage.storage->bytes->bytes;
    auto replacement=std::make_shared<IgbDecodeRuntimeField>();f.storage.storage->bindings.front().field=replacement;f.source->flag_35=7;
    rejects([&]{clone->resume(f.services);},"Resumed copy accepted a replacement destination binding.");
    check(clone->faulted() && !clone->completed() && f.destination->construction_fault_ && !replacement->construction_fault_,
        "Binding replacement did not invalidate exactly the captured constructor owner.");
    check(f.storage.storage->bytes->bytes==prefix && f.services->completed_allocations.empty() && f.name->references==8 &&
        f.copy_calls.size()==2 && f.allocations.size()==2,"Binding replacement changed the physical prefix or replayed base-copy work.");
    rejects([&]{clone->result();},"Faulted clone exposed the replacement descriptor as its result.");
    f.storage.storage->bindings.front().field=f.destination;
    rejects([&]{clone->resume(f.services);},"Restoring the binding revived a faulted construction.");
    clone.reset();check(f.destination->construction_active_.expired() && !f.services->unfinished.empty(),
        "Faulted clone retained its construction token or dropped unfinished storage ownership.");
    for(unsigned mutation=0;mutation<3;++mutation){
        CopyFixture changed;changed.source->flag_35.reset();IgbFieldCloneInvocation pending(changed.source);
        check(!pending.resume(changed.services).completed,"Physical identity test did not suspend derived copying.");
        const auto before=changed.storage.storage->bytes->bytes;const auto other=allocation(0x300000);
        if(mutation==0)changed.destination->physical_storage.reset();
        else changed.destination->physical_storage=IgbFieldStorageBinding{mutation==1?other.storage:changed.storage.storage,mutation==1?0u:4u};
        changed.source->flag_35=7;
        rejects([&]{pending.resume(changed.services);},"Resumed copy accepted a changed physical storage identity.");
        check(pending.faulted() && changed.destination->construction_fault_ && changed.storage.storage->bytes->bytes==before &&
            other.storage->bytes->bytes==std::vector<std::uint8_t>(0x50,0xcc) && changed.services->completed_allocations.empty(),
            "Changed physical identity diverted stores or exposed a completed clone.");
    }
}
void scalar_cold_clones(){
    for(const auto kind:{IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::boolean})for(const auto argument:{0u,1u,2u,0xffffffffu}){
        CopyFixture f;scalar_fixture(f,kind);IgbFieldCloneInvocation run(f.source,argument,kind);
        check(run.resume(f.services).completed && run.result()==f.destination && f.destination->vtable_word==igb_meta_field_profile(kind).vtable,
            "Scalar clone did not publish its exact derived table after the shared copy.");
        check(f.allocations.size()==2 && f.allocations.back().bytes==0x34 && f.calls.empty() && f.copy_calls.size()==2 &&
            f.destination->reference_word==0x800001 && f.name->references==8 && f.element->reference_word==99,
            "Scalar cold wrapper changed its allocation, name ownership or nonowning source state.");
        check(f.destination->default_bytes->buffer==f.copied_default && f.copied_default->bytes==std::vector<std::uint8_t>{1} &&
            f.destination->size==1 && !f.destination->flag_34 && !f.destination->flag_35 && !f.destination->word_48,
            "Scalar copy used an ordinary reset or entered memory-reference derived copying.");
        for(const auto at:{0x16u,0x17u,0x2du,0x2eu,0x2fu})check(f.storage.storage->bytes->bytes[at]==0xcc,"Scalar copy changed unwritten padding.");
        check(run.resume().completed && f.name->references==8 && f.services->completed_allocations.size()==1 &&
            f.destination->construction_active_.expired(),"Completed scalar copy replayed ownership or retained its construction lease.");
        Fixture empty;empty.storage={};IgbFieldCloneInvocation null({},argument,kind);
        check(null.resume(empty.services).completed && !null.result() && empty.allocations.back().bytes==0x34 && empty.calls.empty(),
            "Scalar null allocation dereferenced the source or invoked a constructor.");
    }
    for(const auto kind:{IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::boolean}){
        CopyFixture direct;scalar_fixture(direct,kind);direct.storage.storage->bytes->bytes.resize(0x50,0x5a);
        direct.storage.storage->bytes->initialized.resize(0x50,1);IgbFieldCloneConstructorInvocation constructor(direct.storage,direct.source,kind);
        check(constructor.resume(direct.services).completed && direct.allocations.empty() &&
            std::all_of(direct.storage.storage->bytes->bytes.begin()+0x34,direct.storage.storage->bytes->bytes.end(),[](auto byte){return byte==0x5a;}),
            "Scalar copy constructor wrote beyond its original 0x34-byte object.");
    }
}
void scalar_pending_copy_and_faults(){
    for(const auto kind:{IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::boolean}){
        CopyFixture f;scalar_fixture(f,kind);bool ready{};unsigned polls{};
        f.copy_action=[&](const IgbFieldCopyCall& c,IgbFieldCopyOperation& out){
            if(c.kind==IgbFieldCopyServiceKind::tagged_pool){out.pool_result=f.pool;return MessengerCallResult{true,0,{}};}
            ++polls;if(!ready)return MessengerCallResult{false,0,"prepared scalar default allocation"};
            f.original_default->bytes[0]=9;out.buffer_result={f.copied_default,0};out.buffer_address=0x750000;return MessengerCallResult{true,0,{}};
        };
        IgbFieldCloneInvocation run(f.source,1,kind);
        check(!run.resume(f.services).completed && f.destination->vtable_word==0x80471914 && polls==1 && f.name->references==8,
            "Pending scalar base copy prematurely published its derived table.");
        f.core->enabled=1;f.services->copy->factory={};ready=true;
        check(run.resume(f.services).completed && polls==2 && f.copy_calls.size()==2 && f.allocations.size()==2 && f.calls.empty() &&
            f.copied_default->bytes[0]==9 && f.name->references==8,"Scalar copy resume repeated gate selection, allocation or retain.");
        CopyFixture fault;scalar_fixture(fault,kind);fault.original_default->initialized[0]=0;IgbFieldCloneInvocation invalid(fault.source,1,kind);
        rejects([&]{invalid.resume(fault.services);},"Scalar base-copy read accepted uninitialized default bytes.");
        check(invalid.faulted() && fault.destination->construction_fault_ && fault.destination->vtable_word==0x80471914 &&
            fault.services->completed_allocations.empty(),"Scalar copy failure published a derived table or completed descriptor.");
        rejects([&]{invalid.result();},"Faulted scalar clone exposed its destination.");
        Fixture abandoned;{IgbFieldCloneInvocation pending(abandoned.source,1,kind);
            check(!pending.resume(abandoned.services).completed,"Scalar abandonment did not reach shared-copy suspension.");}
        check(abandoned.destination->construction_fault_ && abandoned.destination->construction_active_.expired() &&
            !abandoned.services->unfinished.empty(),"Abandoned scalar clone lost its lease fault or allocation ownership.");
    }
}
void scalar_enabled_boundaries_and_adapters(){
    for(const auto kind:{IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::boolean}){
        const auto wrapper=kind==IgbMetaFieldKind::signed_byte?0x80037d00u:0x80038800u;
        const auto body=kind==IgbMetaFieldKind::signed_byte?0x80037d28u:0x80038828u;
        Fixture f;f.core->enabled=1;bool ready{};unsigned polls{};
        f.remaining=[&](const IgbFieldCloneCall& c)->std::shared_ptr<IgbFieldCloneOperation>{
            if(c.kind==K::reflected_clone)return {};
            check(c.kind==K::register_type && c.function==wrapper,"Scalar enabled wrapper selected another registration.");
            auto op=std::make_shared<IgbFieldCloneOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared scalar registration"};};return op;
        };
        IgbFieldCloneInvocation run(f.source,0x87654321,kind);check(!run.resume(f.services).completed && polls==1,"Scalar registration did not retain its pending call.");
        f.core->enabled=0;ready=true;
        check(!run.resume(f.services).completed && polls==2 && f.calls.size()==2 && f.calls.back().function==0x800680b4 &&
            f.calls.back().source==f.source && f.calls.back().argument==0x87654321 && f.allocations.empty() && !run.completed(),
            "Missing scalar reflected copy was replaced by cold construction or lost its captured arguments.");
        check(!run.resume(f.services).completed && polls==2 && f.allocations.empty(),"Repeated enabled boundary replayed registration or allocated a cold clone.");
        Fixture callback;callback.core->enabled=1;callback.services->fields->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
        unsigned registrations{};callback.services->fields->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
            check(c.kind==IgbRegistrationCallbacksServiceKind::invoke_function && c.function==body,"Scalar callback runner selected another registration body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++registrations;return MessengerCallResult{true,0,{}};};return op;
        };
        callback.remaining=[](const IgbFieldCloneCall&)->std::shared_ptr<IgbFieldCloneOperation>{return {};};
        IgbListDefaultsCall request;request.kind=IgbListDefaultsServiceKind::clone_field;request.field=callback.source;
        request.vtable_word=igb_meta_field_profile(kind).vtable;request.virtual_slot=0x54;request.argument=2;
        auto operation=make_native_igb_list_field_clone_factory(callback.services)(request);
        callback.source->vtable_word=0;
        check(operation && !operation->resume().completed && registrations==1 && callback.calls.size()==1 &&
            callback.calls.front().kind==K::reflected_clone && callback.calls.front().function==0x800680b4 &&
            callback.calls.front().argument==2 && callback.allocations.empty(),"Scalar adapter reloaded its captured table or bypassed registration/680B4.");
    }
    Fixture f;rejects([&]{IgbFieldCloneInvocation unsupported(f.source,1,IgbMetaFieldKind::string);},"Scalar clone extension accepted an unimplemented string clone.");
}
}
int main(){try{null_allocation_and_gates();enabled_order_and_adapters();missing_base_and_constructor_lifetime();complete_cold_copy();live_derived_reads_and_pointer_binding();replaced_destination_binding();
    scalar_cold_clones();scalar_pending_copy_and_faults();scalar_enabled_boundaries_and_adapters();
    std::cout<<"igb_field_clone_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Field clone failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
