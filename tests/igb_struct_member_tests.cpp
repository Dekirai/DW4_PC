#include "dw4/igb_struct_member.hpp"
#include "dw4/igb_field_defaults.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Prepared guest callbacks prove dispatch and
// continuation semantics only; they do not implement the original functions.
namespace {
using namespace dw4;
using Kind=IgbStructMemberServiceKind;
constexpr std::uint32_t table=0x80470e00,constructor=0x81234560,destructor=0x82345670;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t fill=0xcd,std::uint8_t known=0){
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,fill);result->initialized.assign(size,known);return result;
}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.field==b.field && a.metadata==b.metadata && a.runtime_object==b.runtime_object;}
IgbObjectInstance receiver(){
    IgbObjectInstance result;result.offset=0x10;result.storage=std::make_shared<IgbObjectStorage>();
    result.storage->original_address=0x200000;result.storage->bytes=bytes(0x80);
    result.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::semaphore);
    result.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{result.storage,result.offset};
    IgbObjectBinding binding;binding.offset=result.offset;binding.runtime_object=result.runtime_object;
    result.storage->bindings.push_back(binding);igb_bootstrap_write_word(pointer(result),0,0x80470314);
    igb_object_set_reference_word(result,0xa5000003);return result;
}
struct Fixture {
    std::shared_ptr<IgbStructMemberServices> services=std::make_shared<IgbStructMemberServices>();
    IgbObjectInstance descriptor,target=receiver();
    std::vector<IgbStructMemberCall> calls;
    std::function<MessengerCallResult(const IgbStructMemberCall&)> action;
    unsigned polls{};
    Fixture(){
        descriptor.offset=0x10;descriptor.storage=std::make_shared<IgbObjectStorage>();
        descriptor.storage->original_address=0x100000;descriptor.storage->bytes=bytes(0x54);
        descriptor.field=std::make_shared<IgbDecodeRuntimeField>();
        descriptor.storage->bindings.push_back({descriptor.offset,{},{},{},{},descriptor.field});
        descriptor.field->physical_storage=IgbFieldStorageBinding{descriptor.storage,descriptor.offset};
        igb_bootstrap_write_word(pointer(descriptor),0,table);igb_object_set_reference_word(descriptor,1);
        services->values=std::make_shared<IgbFieldValuesServices>();
        services->factory=[this](const IgbStructMemberCall& call){
            calls.push_back(call);auto operation=std::make_shared<IgbStructMemberOperation>();
            operation->resume=[this,call]{++polls;if(action)return action(call);return MessengerCallResult{true,0xfedcba98,{}};};return operation;
        };
    }
    // Deliberately leave the host mirrors stale to require actual physical reads.
    void raw(std::uint32_t relative,std::uint32_t value,unsigned width=4){
        for(unsigned i=0;i<width;++i){const auto at=descriptor.offset+relative+i;
            descriptor.storage->bytes->bytes.at(at)=static_cast<std::uint8_t>(value>>(8*(width-1-i)));
            descriptor.storage->bytes->initialized.at(at)=1;}
    }
    void configure(std::uint32_t ctor,std::uint32_t dtor,std::uint32_t offset=0x18,std::uint16_t size=12){
        raw(8,offset);raw(0x14,size,2);raw(0x38,ctor);raw(0x3c,dtor);
    }
    void source(const std::shared_ptr<IgbScratchBuffer>& owner,std::uint32_t offset,std::uint32_t address){
        descriptor.field->default_bytes=IgbRuntimeFieldDefaultSource{owner,offset};descriptor.field->default_bytes_address=address;raw(0x20,address);
    }
    auto adapter(){return make_native_igb_struct_member_factory(services);}
};
auto canonical(){auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=table;return field;}

void leaves_and_live_predicate(){
    for(const auto slot:{0x84u,0x74u}){
        auto field=canonical();IgbStructMemberInvocation run(field,{},slot);
        check(run.resume().completed && !run.faulted(),"Struct BLR read the receiver or unknown descriptor operands.");
        field->struct_constructor=constructor;check(run.resume().completed,"Completed struct BLR changed state.");
    }
    for(const auto slot:{0x7cu,0x80u}){
        Fixture f;if(slot==0x7c)f.raw(0x38,0);else f.raw(0x3c,0);
        const auto before=f.target.storage->bytes->bytes,known=f.target.storage->bytes->initialized;
        IgbStructMemberInvocation run(f.descriptor.field,f.target,slot);
        check(run.resume(f.services).completed && f.calls.empty() && f.polls==0,
            "Zero optional struct function read unknown offset or called a guest endpoint.");
        check(f.target.storage->bytes->bytes==before && f.target.storage->bytes->initialized==known,
            "Zero optional struct function initialized inline bytes.");
    }
    auto field=canonical();IgbStructMemberInvocation pending(field,{},0xf0);
    check(!pending.resume().completed && !pending.faulted(),"Unknown canonical constructor became false.");
    field->struct_constructor=0;check(pending.resume().completed && pending.resume().value==0,"Known zero constructor did not resolve pending predicate.");
    for(const auto value:{0u,1u,0x80000000u,0xffffffffu}){
        Fixture f;f.raw(0x38,value);f.descriptor.field->struct_constructor=value?0:1;
        IgbStructMemberInvocation run(f.descriptor.field,{},0xf0);const auto status=run.resume(f.services);
        check(status.completed && status.value==(value!=0) && f.calls.empty(),"Constructor predicate ignored the full live physical word.");
        f.raw(0x38,value?0:1);check(run.resume().value==status.value,"Completed predicate reread changed descriptor bytes.");
    }
    for(unsigned byte=0;byte<4;++byte){Fixture f;f.configure(0,0);f.descriptor.field->struct_constructor=0;
        f.descriptor.storage->bytes->initialized[f.descriptor.offset+0x38+byte]=0;
        IgbStructMemberInvocation run(f.descriptor.field,{},0xf0);
        rejects([&]{run.resume(f.services);},"Physical unknown constructor byte used the known host mirror.");
        f.raw(0x38,0);rejects([&]{run.resume(f.services);},"Faulted predicate revived after physical repair.");
        check(run.faulted() && !run.completed(),"Predicate failure was not permanent.");
    }
}

void optional_callbacks_capture_and_late_factory(){
    for(const auto slot:{0x7cu,0x80u}){
        Fixture f;f.configure(constructor,destructor);bool ready{};
        const auto expected=slot==0x7c?constructor:destructor;
        f.action=[&](const IgbStructMemberCall& call){check(call.kind==Kind::guest_function && call.function==expected &&
            call.address==0x200028 && same(call.object,f.target) && call.field==f.descriptor.field,
            "Optional callback changed its captured function, root receiver or member address.");return MessengerCallResult{ready,0x73,"Prepared guest endpoint."};};
        IgbStructMemberInvocation run(f.descriptor.field,f.target,slot);
        check(!run.resume(f.services).completed && f.calls.size()==1 && f.polls==1,"Nonzero struct function was skipped or completed without its endpoint.");
        f.configure(0,0,0x34);f.services->factory={};ready=true;
        check(run.resume(f.services).completed && f.calls.size()==1 && f.polls==2,"Suspended optional call recaptured changed words or lost its operation.");
        check(run.resume().completed && f.polls==2,"Completed optional callback replayed.");
    }
    Fixture f;f.configure(constructor,0);const auto factory=f.services->factory;f.services->factory={};
    IgbStructMemberInvocation pending(f.descriptor.field,f.target,0x7c);
    check(!pending.resume(f.services).completed && !pending.faulted() && f.calls.empty(),"Unsupported nonzero constructor was silently skipped or made a host pointer call.");
    f.configure(0,0,0x30);f.services->factory=factory;
    check(pending.resume(f.services).completed && f.calls.size()==1 && f.calls[0].function==constructor && f.calls[0].address==0x200028,
        "Late guest factory changed operands captured before endpoint selection.");
}

void destruction_then_fresh_construction(){
    Fixture f;f.configure(constructor,destructor);bool destructor_ready{},constructor_ready{};
    f.action=[&](const IgbStructMemberCall& call){
        if(call.function==destructor)return MessengerCallResult{destructor_ready,0x75,"Prepared destructor."};
        check(call.function==0x83456780 && call.address==0x200038,"Construction did not reload its function and offset after destruction.");
        return MessengerCallResult{constructor_ready,0x76,"Prepared constructor."};
    };
    const auto original=f.target.storage->bytes->bytes;
    IgbStructMemberInvocation run(f.descriptor.field,f.target,0x70);
    check(!run.resume(f.services).completed && f.calls.size()==1 && f.calls[0].function==destructor && f.calls[0].address==0x200028,
        "Reset did not select the captured destructor before construction.");
    f.configure(0x83456780,0x84567890,0x28);destructor_ready=true;
    check(!run.resume(f.services).completed && f.calls.size()==2 && f.polls==3,"Reset replayed selection or skipped its fresh constructor.");
    f.configure(0,0,0x38);constructor_ready=true;f.services->factory={};
    check(run.resume(f.services).completed && f.polls==4 && f.target.storage->bytes->bytes==original,
        "Suspended reset changed selected constructor or substituted default bytes.");
    check(run.resume().completed && f.polls==4,"Completed reset replayed destruction or construction.");
}

void zero_unknown_and_failed_reset_callees(){
    auto field=canonical();field->offset=0x18;field->struct_constructor=constructor;
    Fixture f;IgbStructMemberInvocation pending(field,f.target,0x70);
    check(!pending.resume(f.services).completed && f.calls.empty(),"Unknown destructor was zero or bypassed in reset.");
    field->offset=0x30;field->struct_destructor=destructor;
    check(pending.resume(f.services).completed && f.calls.size()==2 && f.calls[0].address==0x200028 && f.calls[1].address==0x200040,
        "Pending destructor word changed the offset already loaded before it.");
    for(bool clear_constructor:{false,true}){
        Fixture failure;failure.configure(constructor,clear_constructor?destructor:0);
        failure.action=[&](const IgbStructMemberCall&){failure.raw(0x38,0);return MessengerCallResult{true,0,{}};};
        IgbStructMemberInvocation run(failure.descriptor.field,failure.target,0x70);
        rejects([&]{run.resume(failure.services);},"Reset silently skipped a null indirect destructor or fresh constructor.");
        check(run.faulted() && failure.calls.size()==(clear_constructor?1u:0u),"Reset null-call fault changed its completed callback prefix.");
        failure.configure(constructor,destructor);const auto count=failure.polls;
        rejects([&]{run.resume(failure.services);},"Null-call fault revived after descriptor repair.");
        check(failure.polls==count,"Failed reset replayed a completed destructor.");
    }
}

void fresh_virtual_dispatch(){
    Fixture f;f.configure(0,0);f.raw(0,0x80475b60);f.descriptor.field->vtable_word=table;
    bool ready{};f.action=[&](const IgbStructMemberCall& call){check(call.kind==Kind::field_virtual && call.vtable_word==0x80475b60 &&
        call.virtual_slot==0x70 && call.field==f.descriptor.field && same(call.object,f.target),"Struct +78 did not dispatch physical live vtable+70.");
        return MessengerCallResult{ready,0x77,"Prepared changed-table virtual."};};
    IgbStructMemberInvocation run(f.descriptor.field,f.target,0x78);
    check(!run.resume(f.services).completed && f.calls.size()==1,"Changed-table +70 was replaced by a direct struct default copy.");
    f.raw(0,table);f.configure(constructor,destructor);ready=true;f.services->factory={};
    check(run.resume(f.services).completed && f.calls.size()==1 && f.polls==2,"Suspended +78 recaptured the vtable or operation.");
    Fixture direct;direct.configure(constructor,destructor);IgbStructMemberInvocation same_table(direct.descriptor.field,direct.target,0x78);
    check(same_table.resume(direct.services).completed && direct.calls.size()==2 && direct.calls[0].kind==Kind::guest_function &&
        direct.calls[0].function==destructor && direct.calls[1].function==constructor,"Same-table +78 did not run actual native reset.");
}

void default_copy_overlap_and_zero_size(){
    for(const auto offsets:std::array<std::array<unsigned,2>,5>{{{0x28,0x2a},{0x2a,0x28},{0x28,0x28},{0x24,0x38},{0x38,0x24}}}){
        Fixture f;f.configure(0,0,offsets[1]-f.target.offset,8);const auto memory=f.target.storage->bytes;
        for(unsigned i=0x20;i<0x48;++i){memory->bytes[i]=static_cast<std::uint8_t>(i);memory->initialized[i]=1;}
        auto expected=memory->bytes;const auto snapshot=memory->bytes;
        for(unsigned i=0;i<8;++i)expected[offsets[1]+i]=snapshot[offsets[0]+i];
        f.source(memory,offsets[0],f.target.storage->original_address+offsets[0]);
        IgbStructMemberInvocation run(f.descriptor.field,f.target,0x70);
        check(run.resume(f.services).completed && memory->bytes==expected && f.calls.empty(),"Zero-constructor struct defaults lost actual overlapping copy semantics.");
        const auto completed=memory->bytes;f.configure(constructor,destructor);
        check(run.resume().completed && memory->bytes==completed && f.calls.empty(),"Completed struct byte copy replayed or switched constructor branches.");
    }
    Fixture empty;empty.configure(0,0,0x18,0);empty.raw(0x20,0xdeadbeef);
    const auto before=empty.target.storage->bytes->bytes,known=empty.target.storage->bytes->initialized;
    IgbStructMemberInvocation run(empty.descriptor.field,empty.target,0x70);
    check(run.resume(empty.services).completed && empty.target.storage->bytes->bytes==before && empty.target.storage->bytes->initialized==known,
        "Zero-size copy dereferenced an unbound source or initialized inline bytes.");
    Fixture unknown;unknown.configure(0,0,0x18,0);IgbStructMemberInvocation pointer_read(unknown.descriptor.field,unknown.target,0x70);
    rejects([&]{pointer_read.resume(unknown.services);},"Zero-size copy skipped its preceding physical default-pointer read.");
}

void copy_capture_and_late_bindings(){
    Fixture f;f.configure(0,0,0x18,3);f.raw(0x20,0x300002);f.descriptor.field->default_bytes.reset();
    IgbFieldDefaultCopyInvocation run(f.descriptor.field,f.target);
    check(!run.resume(f.services->values).completed && !run.faulted(),"Known nonzero source without an owner was fabricated or rejected before late binding.");
    f.configure(0,0,0x30,1);f.raw(0x20,0x400000);
    auto source=bytes(8,0,1);source->bytes={0,1,2,3,4,5,6,7};f.services->values->buffers.push_back({source,0x300000});
    check(run.resume(f.services->values).completed && f.target.storage->bytes->bytes[0x28]==2 && f.target.storage->bytes->bytes[0x29]==3 &&
        f.target.storage->bytes->bytes[0x2a]==4 && f.target.storage->bytes->initialized[0x40]==0,
        "Late source binding recaptured offset, half-size or already loaded arithmetic pointer.");
    check(f.descriptor.storage->bytes->initialized[f.descriptor.offset+0x16]==0 && f.descriptor.storage->bytes->initialized[f.descriptor.offset+0x17]==0,
        "Default copy required or initialized padding beyond size halfword.");
}

void late_source_preserves_destination_identity(){
    for(bool replace_bytes:{false,true}){
        Fixture f;f.configure(0,0,0x18,4);f.raw(0x20,0x300000);
        const auto original=f.target.storage->bytes;const auto original_address=f.target.storage->original_address;
        // Existing member effects must survive a later, failed default copy.
        original->bytes[0x28]=0x51;original->initialized[0x28]=1;
        const auto before=original->bytes,known=original->initialized;
        IgbFieldDefaultCopyInvocation run(f.descriptor.field,f.target);
        check(!run.resume(f.services->values).completed && original->bytes==before && original->initialized==known,
            "Unbound source changed destination bytes before copying.");
        const auto replacement=std::make_shared<IgbScratchBuffer>(*original);
        if(replace_bytes)f.target.storage->bytes=replacement;else f.target.storage->original_address+=0x1000;
        auto source=bytes(4,0x99,1);f.services->values->buffers.push_back({source,0x300000});
        rejects([&]{run.resume(f.services->values);},"Late source resolution redirected a captured destination allocation or arithmetic identity.");
        check(run.faulted() && !run.completed() && original->bytes==before && original->initialized==known &&
            replacement->bytes==before && replacement->initialized==known,
            "Destination identity fault changed an existing prefix or wrote into the replacement owner.");
        f.target.storage->bytes=original;f.target.storage->original_address=original_address;
        rejects([&]{run.resume(f.services->values);},"Failed destination binding revived after restoring the original allocation.");
        check(original->bytes==before && original->initialized==known,"Restoring destination identity replayed the failed byte copy.");
    }
    for(bool remove_bytes:{false,true}){
        Fixture f;f.configure(0,0,0x18,0);f.raw(0x20,0xdeadbeef);
        const auto original=f.target.storage->bytes;const auto before=original->bytes,known=original->initialized;
        if(remove_bytes)f.target.storage->bytes.reset();else original->released=true;
        IgbFieldDefaultCopyInvocation run(f.descriptor.field,f.target);
        check(run.resume(f.services->values).completed && !run.faulted() && original->bytes==before && original->initialized==known,
            "Zero-size copy dereferenced destination storage while validating its arithmetic operand.");
    }
}

void copy_partial_failure_and_no_replay(){
    for(bool backwards:{false,true}){
        Fixture f;f.configure(0,0,0x18,4);auto source=bytes(4,0,1);source->bytes={0x10,0x20,0x30,0x40};
        source->initialized[backwards?2:1]=0;f.source(source,0,backwards?0x100000:0x300000);
        IgbStructMemberInvocation run(f.descriptor.field,f.target,0x70);
        rejects([&]{run.resume(f.services);},"Struct default copy accepted an unknown reached source byte.");
        const auto at=backwards?0x2bu:0x28u;
        check(f.target.storage->bytes->bytes[at]==(backwards?0x40:0x10) && f.target.storage->bytes->initialized[at]==1,
            "Copy failure lost the prefix selected by unsigned guest pointer comparison.");
        for(unsigned i=0x28;i<0x2c;++i)if(i!=at)check(f.target.storage->bytes->bytes[i]==0xcd && f.target.storage->bytes->initialized[i]==0,
            "Copy failure wrote a later byte or prevalidated the whole range.");
        source->initialized.assign(4,1);source->bytes.assign(4,0xee);
        rejects([&]{run.resume(f.services);},"Faulted copy resumed after source bytes were repaired.");
        check(run.faulted() && f.target.storage->bytes->bytes[at]==(backwards?0x40:0x10),"Faulted copy replay changed completed prefix.");
    }
    Fixture f;f.configure(0,0,0x18,4);auto source=bytes(4,0x61,1);f.source(source,0,0x300000);
    f.target.storage->bytes->bytes.resize(0x2a);f.target.storage->bytes->initialized.resize(0x2a);
    IgbFieldDefaultCopyInvocation run(f.descriptor.field,f.target);
    rejects([&]{run.resume(f.services->values);},"Bounded target copy wrote outside represented storage.");
    check(run.faulted() && f.target.storage->bytes->bytes[0x28]==0x61 && f.target.storage->bytes->bytes[0x29]==0x61,
        "Target range failure discarded preceding byte stores.");
}

void copy_synchronizes_only_completed_target_words(){
    Fixture f,target;f.target=target.descriptor;f.configure(0,0,0x34,4);
    auto source=bytes(4,0,1);source->bytes={0x12,0x34,0x56,0x78};source->initialized[3]=0;
    f.source(source,0,0x300000);IgbFieldDefaultCopyInvocation partial(f.descriptor.field,f.target);
    rejects([&]{partial.resume(f.services->values);},"Derived-target partial copy accepted its unknown final byte.");
    check(!target.descriptor.field->struct_alignment && !target.descriptor.field->struct_constructor &&
        target.descriptor.storage->bytes->bytes[target.descriptor.offset+0x34]==0x12 &&
        target.descriptor.storage->bytes->bytes[target.descriptor.offset+0x36]==0x56,
        "Partial default stores fabricated a canonical target word or discarded preceding bytes.");
    f.configure(0,0,0x37,1);auto final=bytes(1,0x78,1);f.source(final,0,0x300010);
    IgbFieldDefaultCopyInvocation complete_word(f.descriptor.field,f.target);
    check(complete_word.resume(f.services->values).completed && target.descriptor.field->struct_alignment==0x12345678 &&
        !target.descriptor.field->struct_constructor && !target.descriptor.field->struct_destructor,
        "Final copied byte did not synchronize its exact derived target word.");
    check(target.descriptor.storage->bytes->initialized[target.descriptor.offset+0x33]==0 &&
        target.descriptor.storage->bytes->initialized[target.descriptor.offset+0x38]==0,
        "Derived target synchronization read or initialized adjacent padding.");
}

void callback_faults_reentry_and_identity(){
    for(const auto slot:{0x70u,0x78u})for(unsigned mutation=0;mutation<4;++mutation){
        Fixture f;f.configure(constructor,destructor);const auto descriptor_binding=f.descriptor.field->physical_storage;
        const auto target_bindings=f.target.storage->bindings;const auto owner=f.descriptor.storage->bytes;
        IgbStructMemberInvocation run(f.descriptor.field,f.target,slot);unsigned caught{};
        f.action=[&](const IgbStructMemberCall& call){
            check(call.function==destructor,"Fault regression unexpectedly reached the constructor.");
            f.target.storage->bytes->bytes[0x28]=0x5a;f.target.storage->bytes->initialized[0x28]=1;
            if(mutation==0){try{run.resume(f.services);}catch(const std::runtime_error&){++caught;}}
            else if(mutation==1)f.descriptor.field->physical_storage.reset();
            else if(mutation==2)f.target.storage->bindings.clear();
            else owner->released=true;
            return MessengerCallResult{true,0,{}};
        };
        rejects([&]{run.resume(f.services);},"Caught reentry or callback-invalidated identity reached subsequent constructor work.");
        check(run.faulted() && f.calls.size()==1 && f.polls==1 && (mutation!=0 || caught==1) &&
            f.target.storage->bytes->bytes[0x28]==0x5a,"Callback fault lost the completed destructor prefix or selected a constructor.");
        f.descriptor.field->physical_storage=descriptor_binding;f.target.storage->bindings=target_bindings;owner->released=false;f.action={};
        rejects([&]{run.resume(f.services);},"Faulted callback sequence revived after restoring owner identity.");
        check(f.polls==1,"Restored callback identity replayed a completed destructor.");
    }
    Fixture f;f.configure(constructor,destructor);f.action=[](const IgbStructMemberCall&)->MessengerCallResult{throw std::runtime_error("Prepared guest failure.");};
    IgbStructMemberInvocation run(f.descriptor.field,f.target,0x70);
    rejects([&]{run.resume(f.services);},"Guest destructor exception was swallowed.");f.action={};
    rejects([&]{run.resume(f.services);},"Thrown guest callback resumed after replacement.");
    check(run.faulted() && f.calls.size()==1 && f.polls==1,"Guest failure repeated selection or reached constructor.");
}

void exact_factory_admission(){
    Fixture f;f.configure(0,0);unsigned delegated{};
    const auto factory=make_native_igb_struct_member_factory(f.services,[&](const IgbObjectInitCall&){++delegated;return std::shared_ptr<IgbObjectInitOperation>{};});
    IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::field_virtual;call.field=f.descriptor.field;call.object=f.target;call.vtable_word=table;
    for(const auto slot:{0x84u,0x74u,0x7cu,0x80u,0xf0u}){call.virtual_slot=slot;const auto op=factory(call);
        check(op && op->resume().completed,"Struct member factory omitted an exact admitted leaf.");}
    //667F4 supplies a descriptor without a captured table in field requests.
    call.vtable_word=0;call.virtual_slot=0x7c;f.descriptor.field->vtable_word=0x80475b60;
    const auto live=factory(call);check(live && live->resume().completed,"Initializer field request did not select its live physical descriptor table.");
    call.vtable_word=table;
    for(const auto slot:{0x5cu,0x64u,0x6cu,0x8cu,0x90u,0x54u}){call.virtual_slot=slot;check(!factory(call),"Struct member factory claimed a descriptor or unrelated virtual.");}
    call.virtual_slot=0x84;call.kind=IgbObjectInitServiceKind::object_virtual;check(!factory(call),"Struct field adapter consumed object service.");
    call.kind=IgbObjectInitServiceKind::field_virtual;call.vtable_word=0x80475b60;f.raw(0,0x80475b60);
    check(!factory(call) && delegated==8,"Struct member adapter consumed another descriptor class.");
}
}
int main(){try{
    leaves_and_live_predicate();optional_callbacks_capture_and_late_factory();destruction_then_fresh_construction();
    zero_unknown_and_failed_reset_callees();fresh_virtual_dispatch();default_copy_overlap_and_zero_size();
    copy_capture_and_late_bindings();late_source_preserves_destination_identity();copy_partial_failure_and_no_replay();copy_synchronizes_only_completed_target_words();
    callback_faults_reentry_and_identity();exact_factory_admission();
    std::cout<<"igb_struct_member_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Struct member tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
