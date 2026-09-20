#include "dw4/igb_metadata_probe.hpp"

#include <iostream>

namespace {
using namespace dw4;using S=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<ModelSceneObject> owner(){return std::make_shared<ModelSceneData>();}
struct Fixture {
    std::shared_ptr<IgbMetadataProbeServices> services=std::make_shared<IgbMetadataProbeServices>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    IgbBootstrapPointer local;
    std::vector<IgbDependenciesCall> calls;
    std::function<void(const IgbDependenciesCall&,IgbDependenciesOperation&,MessengerCallResult&)> on_call;
    Fixture(){
        local.storage=std::make_shared<IgbObjectStorage>();local.storage->original_address=0x900008;
        local.storage->bytes=std::make_shared<IgbScratchBuffer>();local.storage->bytes->bytes.assign(0x64,0xcc);
        local.storage->bytes->initialized.assign(0x64,1);local.storage->bindings.push_back({0,{},metadata,{}});
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->dependencies=std::make_shared<IgbDependenciesServices>();
        services->metadata->dependencies->globals=std::make_shared<SystemStageServices>();
        services->metadata->dependencies->globals->string_pool=owner();
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;core->vtable_offset=0;
        services->local_storage=[this]{return local;};
        services->metadata->dependencies->factory=[this](const IgbDependenciesCall& c){
            auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
                calls.push_back(c);MessengerCallResult result{true,0,{}};
                if(c.kind==S::create_string_pool || c.kind==S::initialize_string_pool)out->pool_result=owner();
                if(on_call)on_call(c,*out,result);return result;};return op;
        };
    }
    void pause_after_constructor(IgbMetadataProbeInvocation& call){
        services->runtime->singleton.reset();check(!call.resume(services).completed,"Absent ArkCore did not retain completed constructor.");
        check(metadata->native_size==0xffffffffu && metadata->reference_word==1 && !metadata->fields,"Probe did not run reset-only constructor.");
        services->runtime->singleton=core;
    }
};
std::shared_ptr<IgbInternedString> string(std::uint32_t references){auto result=std::make_shared<IgbInternedString>();
    result->value=std::make_shared<const std::string>("probe");result->references=references;result->container=owner();return result;}
void actual_constructor_and_offsets(){
    for(const auto offset:{0u,4u,0x10u,0x18u,0x28u,0x48u,0x58u,0x60u}){
        Fixture f;f.core->vtable_offset=offset;IgbMetadataProbeInvocation call(f.local);
        check(call.resume(f.services).completed,"Actual reset probe did not complete.");
        check(call.value()==igb_bootstrap_read_word(f.local,offset),"Probe did not return actual constructed local word.");
        check(f.calls.empty() && !f.metadata->fields,"Reset-only probe manufactured list construction or string callbacks.");
        check(call.resume().completed,"Completed probe failed without services.");
    }
    Fixture f;f.services->metadata->dependencies->globals->string_pool.reset();
    auto next=std::make_shared<IgbRuntimeTypeRegistry>();next->vtable_offset=0x10;
    f.on_call=[&](const auto& c,auto&,auto&){if(c.kind==S::initialize_string_pool)f.services->runtime->singleton=next;};
    IgbMetadataProbeInvocation call(f.local);
    check(call.resume(f.services).completed && call.value()==1 && f.calls.size()==2,"Probe read ArkCore before its actual constructor callback.");
}
void captured_core_and_read_prefix(){
    Fixture f;f.core->vtable_offset.reset();IgbMetadataProbeInvocation call(f.local);
    check(!call.resume(f.services).completed,"Missing offset was fabricated.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;f.services->runtime->singleton=replacement;
    f.core->vtable_offset=0;igb_bootstrap_write_word(f.local,0,0x12345678);
    check(call.resume(f.services).completed && call.value()==0x12345678 && igb_bootstrap_read_word(f.local,0)==0x804761d8,
        "Probe reloaded captured core, replayed constructor or lost pre-reset vtable value.");
    Fixture invalid;IgbMetadataProbeInvocation wrong(invalid.local);invalid.pause_after_constructor(wrong);invalid.core->vtable_offset=0x64;
    igb_bootstrap_write_word(invalid.local,0,0xabcdef01);
    rejects([&]{wrong.resume(invalid.services);},"Out-of-range local read was accepted.");
    check(igb_bootstrap_read_word(invalid.local,0)==0xabcdef01,"Rejected read executed later vtable store.");
    Fixture padding;padding.local.storage->bytes->initialized[0x1b]=0;padding.core->vtable_offset=0x18;
    IgbMetadataProbeInvocation missing(padding.local);rejects([&]{missing.resume(padding.services);},"Unknown stack padding became a fabricated value.");
    Fixture null;null.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbMetadataProbeInvocation dereference(null.local);
    rejects([&]{dereference.resume(null.services);},"Null ArkCore was treated as missing global.");
}
void ordered_cleanup_and_continuation(){
    Fixture f;IgbMetadataProbeInvocation call(f.local);f.pause_after_constructor(call);
    auto pool_name=string(1),old_name=string(9),new_name=string(1);bool ready{};unsigned releases{};
    igb_metadata_set_pool_name(f.local,pool_name,0xa008);igb_metadata_set_name(f.local,old_name,0xb008);
    igb_bootstrap_write_word(f.local,0,0x11223344);
    f.on_call=[&](const auto& c,auto&,auto& status){check(c.kind==S::release_string,"Unexpected probe callback.");++releases;
        if(c.string==pool_name){check(c.pool==pool_name->container && pool_name->references==0,"Pool-name release lost captured decrement/container.");
            igb_metadata_set_name(f.local,new_name,0xc008);igb_bootstrap_write_word(f.local,0,0x55667788);status.completed=ready;}
        else check(c.string==new_name && new_name->references==0,"Second destructor did not read the changed live name.");
    };
    check(!call.resume(f.services).completed && pool_name->references==0 && new_name->references==1,"Pending first release ran the second destructor.");
    ready=true;check(call.resume(f.services).completed && call.value()==0x11223344,"Release continuation changed captured return word.");
    check(releases==3 && old_name->references==9 && new_name->references==0 && pool_name->references==0,"Cleanup order, full-word count or exactly-once decrement differs.");
    check(igb_bootstrap_read_word(f.local,0x60)==0xa008 && igb_bootstrap_read_word(f.local,0x1c)==0xc008,"Stack destructors invented null stores.");
    Fixture wrapping;IgbMetadataProbeInvocation decrement(wrapping.local);wrapping.pause_after_constructor(decrement);
    auto zero=string(0),tagged=string(0x800001);igb_metadata_set_pool_name(wrapping.local,zero,0x1108);igb_metadata_set_name(wrapping.local,tagged,0x1208);
    check(decrement.resume(wrapping.services).completed && zero->references==0xffffffffu && tagged->references==0x800000 && wrapping.calls.empty(),
        "String destruction used masked object references or failed unsigned wrap.");
}
void pointer_binding_and_faults(){
    Fixture f;IgbMetadataProbeInvocation call(f.local);f.pause_after_constructor(call);
    igb_bootstrap_write_word(f.local,0x60,0xd008);check(!call.resume(f.services).completed,"Unknown captured string pointer was skipped.");
    auto replacement=string(5),captured=string(2);igb_metadata_set_pool_name(f.local,replacement,0xe008);
    check(!call.resume(f.services).completed && replacement->references==5,"Late live pointer replacement redirected a captured destructor.");
    f.services->strings.emplace(0xd008,captured);
    check(call.resume(f.services).completed && captured->references==1 && replacement->references==5,"Late address binding did not resume the captured destructor.");
    Fixture invalid;IgbMetadataProbeInvocation bad(invalid.local);invalid.pause_after_constructor(bad);auto released=string(1);released->released=true;
    igb_metadata_set_name(invalid.local,released,0xf008);rejects([&]{bad.resume(invalid.services);},"Released name header was read.");
    check(bad.faulted() && released->references==1,"Fault changed released header reference word.");
    Fixture recursive;IgbMetadataProbeInvocation reentry(recursive.local);recursive.pause_after_constructor(reentry);
    auto name=string(1);igb_metadata_set_name(recursive.local,name,0xf108);
    recursive.on_call=[&](const auto&,auto&,auto&){rejects([&]{reentry.resume(recursive.services);},"Recursive probe was accepted.");};
    rejects([&]{reentry.resume(recursive.services);},"Caught recursive fault allowed outer completion.");
    check(reentry.faulted() && name->references==0,"Recursive release replayed prefix.");
}
void adapter_and_retention(){
    Fixture f;unsigned storage_calls{};f.services->local_storage=[&]{++storage_calls;return f.local;};
    auto factory=make_native_igb_metadata_probe_factory(f.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80029ffc;check(!factory(request),"Probe adapter claimed wrong operation kind.");
    request.kind=IgbTypeRegistrationServiceKind::layout_probe;auto operation=factory(request);f.services->runtime->singleton.reset();
    check(operation && !operation->resume().completed && storage_calls==1,"Probe adapter failed to retain its actual local constructor.");
    f.services->runtime->singleton=f.core;check(operation->resume().completed && storage_calls==1,"Adapter recreated stack local on resume.");
    std::weak_ptr<IgbObjectStorage> retained=f.local.storage;operation.reset();f.local={};
    check(!retained.expired(),"Host local destruction discarded original residual owners.");
    Fixture absent;absent.services->local_storage={};auto late=make_native_igb_metadata_probe_factory(absent.services)(request);
    check(!late->resume().completed,"Unrepresented stack local became completed probe.");
    absent.services->local_storage=[&]{return absent.local;};check(late->resume().completed,"Late stack representation did not resume adapter.");
    rejects([&]{igb_metadata_set_name(absent.local,string(1),0);},"Name owner and null physical address mismatch was accepted.");
}
}
int main(){try{actual_constructor_and_offsets();captured_core_and_read_prefix();ordered_cleanup_and_continuation();pointer_binding_and_faults();adapter_and_retention();
    std::cout<<"Metadata probe tests passed: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
