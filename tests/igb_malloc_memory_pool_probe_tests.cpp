#include "dw4/igb_memory_pool_registration.hpp"
#include "dw4/igb_object_field_construct.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){bool failed{};try{f();}catch(const std::runtime_error&){failed=true;}check(failed,message);}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
struct Fixture {
    IgbBootstrapPointer local=raw(0x100000,0xa0);
    std::shared_ptr<IgbMemoryPoolRegistrationServices> services=std::make_shared<IgbMemoryPoolRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    Fixture(){
        igb_bootstrap_write_word(local,4,0);
        services->types=std::make_shared<IgbTypeRegistrationServices>();
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->object_cleanup=std::make_shared<IgbObjectFieldConstructServices>();
    }
    IgbObjectInstance object(std::uint32_t address,std::uint32_t references,bool bind=true){
        auto storage=raw(address,0x10);IgbObjectInstance instance;instance.storage=storage.storage;
        instance.field=std::make_shared<IgbDecodeRuntimeField>();instance.field->physical_storage=IgbFieldStorageBinding{storage.storage,0};
        storage.storage->bindings.push_back({0,{},{},{},{},instance.field});
        igb_bootstrap_write_word(storage,4,references);
        if(bind)services->object_cleanup->objects.push_back(instance);
        return instance;
    }
};

void constructor_prefix_and_captured_core(){
    Fixture f;f.services->types->runtime->singleton.reset();IgbMallocMemoryPoolProbeInvocation probe(f.local);
    check(!probe.resume(f.services).completed && !probe.faulted(),"Unknown core did not retain the completed constructor prefix.");
    check(igb_bootstrap_read_word(f.local,0)==0x80473790 && igb_bootstrap_read_word(f.local,4)==1 &&
        igb_bootstrap_read_word(f.local,0x74)==0 && igb_bootstrap_read_word(f.local,0x94)==0,"Malloc constructor stores changed.");
    for(std::size_t i=0;i<0xa0;++i){const bool known=i<8 || (i>=0x74 && i<0x78) || (i>=0x94 && i<0x98);
        check(f.local.storage->bytes->initialized[i]==(known?1:0) && (known || f.local.storage->bytes->bytes[i]==0xcc),
            "Malloc construction initialized bytes outside its sixteen-byte original prefix.");}
    f.services->types->runtime->singleton=f.core;
    check(!probe.resume(f.services).completed,"Missing captured core offset was guessed.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;
    f.services->types->runtime->singleton=replacement;f.core->vtable_offset=4;igb_bootstrap_write_word(f.local,4,0x81234567);
    check(probe.resume(f.services).completed && probe.value()==0x81234567 && igb_bootstrap_read_word(f.local,4)==0x81234567,
        "Probe recaptured the global core or replayed its constructor.");
    check(probe.resume().completed && probe.value()==0x81234567,"Completed probe did not retain its result.");

    Fixture null;null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    IgbMallocMemoryPoolProbeInvocation null_probe(null.local);rejects([&]{null_probe.resume(null.services);},"Null captured core was treated as unknown.");
    check(igb_bootstrap_read_word(null.local,0x94)==0 && null_probe.faulted(),"Null core fault lost the constructor prefix.");

    Fixture unknown;unknown.local.storage->bytes->initialized.assign(0xa0,0);
    IgbMallocMemoryPoolProbeInvocation unknown_probe(unknown.local);
    rejects([&]{unknown_probe.resume(unknown.services);},"Base reset guessed the incoming packed reference word.");
    check(igb_bootstrap_read_word(unknown.local,0)==0x804717f0 && unknown.local.storage->bytes->initialized[4]==0 &&
        unknown.local.storage->bytes->initialized[0x74]==0,"Unknown header fault executed later constructor stores.");
}

void selected_word_and_unknown_padding(){
    Fixture f;IgbMallocMemoryPoolProbeInvocation probe(f.local);check(!probe.resume(f.services).completed,"Offset should be pending.");
    igb_bootstrap_write_word(f.local,0,0x12345678);f.core->vtable_offset=8;
    rejects([&]{probe.resume(f.services);},"Probe guessed an unknown selected local word.");
    check(probe.faulted() && igb_bootstrap_read_word(f.local,0)==0x12345678,"Failed selection restored the derived table too early.");
    igb_bootstrap_write_word(f.local,8,9);rejects([&]{probe.resume(f.services);},"Repair revived a faulted probe.");
    for(const auto offset:{0u,4u,0x74u,0x94u}){Fixture known;known.core->vtable_offset=offset;
        IgbMallocMemoryPoolProbeInvocation call(known.local);check(call.resume(known.services).completed &&
            call.value()==(offset==0?0x80473790u:offset==4?1u:0u),"Known original layout selection changed.");}
}

void ordered_cleanup_retains_callbacks_and_fresh_second_member(){
    Fixture f;auto first=f.object(0x200000,0x80000001),old_second=f.object(0x210000,2),fresh=f.object(0x220000,1);
    IgbMallocMemoryPoolProbeInvocation probe(f.local);check(!probe.resume(f.services).completed,"Cleanup injection requires the actual post-constructor boundary.");
    igb_bootstrap_write_word(f.local,0x94,0x200000);igb_bootstrap_write_word(f.local,0x74,0x210000);f.core->vtable_offset=0;
    unsigned selections{},first_polls{},second_polls{};bool first_ready{};
    f.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& call){
        check(call.kind==IgbObjectFieldConstructServiceKind::destroy_object,"Probe requested an unrelated cleanup service.");
        ++selections;auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
        if(call.object.field==first.field){operation->resume=[&]{++first_polls;if(!first_ready)return MessengerCallResult{false,0,"Prepared first cleanup."};
            first.storage->bytes->released=true;igb_bootstrap_write_word(f.local,0x74,0x220000);igb_bootstrap_write_word(f.local,0,0x87654321);
            return MessengerCallResult{true,0,{}};};
        }else{check(call.object.field==fresh.field,"Second cleanup failed to read its member after the first destruction.");
            operation->resume=[&]{++second_polls;return MessengerCallResult{true,0,{}};};}
        return operation;
    };
    check(!probe.resume(f.services).completed && selections==1 && first_polls==1 &&
        igb_object_reference_word(first)==0x80000000 && igb_object_reference_word(old_second)==2,
        "Probe changed the original94-before74 release order or packed reference arithmetic.");
    first.storage->bytes->initialized[4]=0;
    check(!probe.resume(f.services).completed && first_polls==2 && selections==1 && second_polls==0,
        "Polling the selected destructor added an outer header read or repeated selection.");
    first_ready=true;check(probe.resume(f.services).completed && first_polls==3 && second_polls==1 && selections==2 &&
        probe.value()==0x80473790 && igb_bootstrap_read_word(f.local,0)==0x87654321 &&
        igb_object_reference_word(old_second)==2 && igb_object_reference_word(fresh)==0 &&
        igb_bootstrap_read_word(f.local,0x94)==0x200000 && igb_bootstrap_read_word(f.local,0x74)==0x220000,
        "Cleanup reread a released first object, lost the captured return, or cleared original members.");
}

void late_mapping_and_full_word_reference_arithmetic(){
    Fixture f;auto first=f.object(0x200000,0,false),replacement=f.object(0x210000,0x800002);
    IgbMallocMemoryPoolProbeInvocation probe(f.local);check(!probe.resume(f.services).completed,"Offset should be pending.");
    igb_bootstrap_write_word(f.local,0x94,0x200000);f.core->vtable_offset=0;
    check(!probe.resume(f.services).completed && igb_object_reference_word(first)==0,"Missing numeric mapping performed a reference access.");
    igb_bootstrap_write_word(f.local,0x94,0x210000);f.services->object_cleanup->objects.push_back(first);
    igb_bootstrap_write_word(f.local,0x74,0x210000);
    check(probe.resume(f.services).completed && igb_object_reference_word(first)==0xffffffff &&
        igb_object_reference_word(replacement)==0x800001,"Late mapping changed the captured pointer or discarded packed high bits.");
}

void caught_reentry_and_abandoned_ownership(){
    Fixture f;auto first=f.object(0x200000,1),second=f.object(0x210000,2);IgbMallocMemoryPoolProbeInvocation probe(f.local);
    check(!probe.resume(f.services).completed,"Offset should be pending.");
    igb_bootstrap_write_word(f.local,0x94,0x200000);igb_bootstrap_write_word(f.local,0x74,0x210000);f.core->vtable_offset=0;
    f.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall&){auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
        operation->resume=[&]{rejects([&]{probe.resume(f.services);},"Recursive probe was accepted.");return MessengerCallResult{true,0,{}};};return operation;};
    rejects([&]{probe.resume(f.services);},"Caught recursion allowed the later cleanup.");
    check(probe.faulted() && igb_object_reference_word(first)==0 && igb_object_reference_word(second)==2,
        "Faulted cleanup lost its reached reference prefix or touched the second object.");

    Fixture owner;std::weak_ptr<IgbScratchBuffer> captured=owner.local.storage->bytes;
    {IgbMallocMemoryPoolProbeInvocation unfinished(owner.local);check(!unfinished.resume(owner.services).completed,"Offset should be pending.");
        owner.local.storage->bytes=raw(0x100000,0xa0).storage->bytes;}
    check(!captured.expired(),"Abandoned probe lost its captured local bytes.");owner.services->retained.clear();
    check(captured.expired(),"Probe owner retention formed a services ownership cycle.");
}

void adapter_selects_local_once_and_guards_reentry(){
    Fixture f;auto factory=make_native_igb_memory_pool_registration_type_factory(f.services);
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::layout_probe;call.function=0x80039cc0;
    auto operation=factory(call);check(operation && !operation->resume().completed,"Unbound local storage was fabricated.");
    unsigned selections{};f.services->malloc_probe_storage=[&]{++selections;return f.local;};
    check(!operation->resume().completed && selections==1,"Probe did not retain its local selection across missing offset.");
    f.core->vtable_offset=0;f.services->malloc_probe_storage={};
    check(operation->resume().completed && operation->resume().value==0x80473790 && selections==1,"Probe adapter repeated local allocation.");
    call.function=0x80039e28;call.kind=IgbTypeRegistrationServiceKind::defaults;auto defaults=factory(call);
    check(defaults && !defaults->resume().completed,"Malloc defaults fabricated their missing canonical metadata input.");

    Fixture recursive;auto selected=make_native_igb_memory_pool_registration_type_factory(recursive.services);
    call.function=0x80039cc0;call.kind=IgbTypeRegistrationServiceKind::layout_probe;auto target=selected(call);
    recursive.services->malloc_probe_storage=[&]{rejects([&]{target->resume();},"Recursive storage selection was accepted.");return recursive.local;};
    rejects([&]{target->resume();},"Caught storage reentry still constructed the local.");
    check(recursive.local.storage->bytes->initialized[0]==0,"Faulted storage callback was followed by a native constructor store.");
}
}
int main(){try{constructor_prefix_and_captured_core();selected_word_and_unknown_padding();
    ordered_cleanup_retains_callbacks_and_fresh_second_member();late_mapping_and_full_word_reference_arithmetic();
    caught_reentry_and_abandoned_ownership();adapter_selects_local_once_and_guards_reentry();
    std::cout<<"Malloc memory-pool probe checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Malloc probe failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
