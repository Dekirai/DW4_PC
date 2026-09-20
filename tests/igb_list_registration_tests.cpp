#include "dw4/igb_list_registration.hpp"
#include "dw4/igb_thread_registration.hpp"

#include <iostream>

namespace {
using namespace dw4;using E=IgbTypeListFactoryEntry;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using MK=IgbMetadataFactoryServiceKind;using BK=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto meta(std::uint32_t flags=0){auto out=std::make_shared<IgbRuntimeMetaType>();out->class_flags=flags;return out;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t bytes){auto out=std::make_shared<IgbObjectStorage>();out->original_address=address;
    out->bytes=std::make_shared<IgbScratchBuffer>();out->bytes->bytes.assign(bytes,0xcc);out->bytes->initialized.assign(bytes,1);return {out,0};}
struct Fixture {
    std::shared_ptr<IgbListRegistrationServices> services=std::make_shared<IgbListRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    IgbBootstrapPointer object=allocation(0x100000,0x64),field_list=allocation(0x110000,16);
    std::vector<IgbTypeRegistrationCall> calls;
    IgbTypeRegistrationFactory remaining;IgbMetadataFactory metadata_remaining;
    unsigned allocations{},locals{};
    Fixture(){
        services->lists=std::make_shared<IgbTypeListServices>();services->field_registries=std::make_shared<IgbFieldRegistryServices>();
        services->field_registries->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
        services->lists->registry_metadata=std::shared_ptr<IgbRegistrationMetaSlot>(services->field_registries,&services->field_registries->metadata);
        services->lists->children_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->types=std::make_shared<IgbTypeRegistrationServices>();
        services->types->metadata_pool_handle=5;services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->bootstrap->runtime=services->types->runtime;services->lists->bootstrap=services->metadata->bootstrap;
        services->field_registries->bootstrap=services->metadata->bootstrap;
        services->metadata->dependencies=std::make_shared<IgbDependenciesServices>();services->metadata->dependencies->globals=std::make_shared<SystemStageServices>();
        services->metadata->dependencies->globals->string_pool=pool;
        object.storage->bindings.push_back({0,{},constructed,{}});field_list.storage->bindings.push_back({0,{},{},fields});
        services->metadata->bootstrap->factory=[this](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x64 && c.pool==pool,"Metadata factory changed its original pool/size.");
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,out=op.get()]{++allocations;out->allocation_result=object;return MessengerCallResult{true,0,{}};};return op;};
        // Prepared internal-list allocation isolates the actual metadata factory.
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){if(metadata_remaining)return metadata_remaining(c);
            check(c.kind==MK::create_fields,"Unexpected prepared metadata operation.");auto op=std::make_shared<IgbMetadataFactoryOperation>();
            op->resume=[this,out=op.get()]{out->allocation_result=field_list;out->fields_result=fields;return MessengerCallResult{true,0,{}};};return op;};
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,make_native_igb_metadata_factory(services->metadata,
            [this](const IgbTypeRegistrationCall& c){calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_list_registration_function_factory(services);
        services->local_storage=[this](E entry){++locals;auto out=allocation(0x200000+locals*0x100,16);
            if(entry==E::registry)out.storage->bindings.push_back({0,{},{},std::make_shared<IgbDecodeFieldRegistry>()});
            else out.storage->bindings.push_back({0,{},{},{},std::make_shared<IgbRuntimeTypeList>()});return out;};
    }
    auto slot(E entry){return entry==E::registry?services->lists->registry_metadata:services->lists->children_metadata;}
};
void arguments_and_aliases(){
    for(const auto entry:{E::registry,E::children}){Fixture f;const bool internal=entry==E::registry;
        const auto args=igb_list_type_registration_arguments(f.services,entry);
        check(args.metadata_slot==f.slot(entry) && args.flag==0 && args.native_size==16,"List registration lost canonical cell or size/flag.");
        check(*args.name==(internal?"__internalObjectList":"__internalNonRefCountedObjectList"),"List registration name differs from original literal.");
        check(args.parent_registration==(internal?0x8002e07cu:0x80066b08u) && args.parent_getter==(internal?0x8002ddccu:0x800237d0u),"List parent body/getter differs.");
        check(args.field_callback==(internal?0x8002ddacu:0x8002e0e8u) && args.layout_probe==(internal?0x8002dce4u:0x8002e020u) &&
            args.defaults_callback==(internal?0x8002ddd4u:0x8002e108u) && args.trailing_callback==0 && !args.extra_callbacks,"List registration changed callback identities.");
        auto value=meta();*f.slot(entry)=value;check(*args.metadata_slot==value,"List arguments copied the canonical cell.");
        if(internal){std::weak_ptr<IgbFieldRegistryServices> retained=f.services->field_registries;f.services.reset();
            check(!retained.expired() && args.metadata_slot.get()==&retained.lock()->metadata,"Canonical alias lost its field-list owner.");}
    }
    rejects([]{igb_list_type_registration_arguments({},E::registry);},"Arguments accepted missing canonical storage.");
}
void bodies_and_gates(){
    for(const auto entry:{E::registry,E::children}){
        Fixture f;IgbListTypeRegistrationInvocation body(entry);check(!body.resume(f.services).completed,"Unbound parent registration was completed.");
        check(f.allocations==1 && *f.slot(entry)==f.constructed && f.constructed->class_flags==8 && f.constructed->fields==f.fields,
            "List body skipped native metadata construction or busy publication.");
        check(!f.services->callbacks->prerequisites && f.calls.size()==1 && f.calls[0].kind==TK::register_parent &&
            f.calls[0].function==(entry==E::registry?0x8002e07cu:0x80066b08u),"List body invented a prerequisite guard or another parent.");
        check(!body.resume(f.services).completed && f.allocations==1,"Pending parent repeated list metadata construction.");
        for(const auto flags:{4u,8u,12u,0xffffffffu}){Fixture ready;auto value=meta(flags);*ready.slot(entry)=value;IgbListTypeRegistrationInvocation run(entry);
            check(run.resume(ready.services).completed && !run.result() && value->class_flags==flags && ready.allocations==0 && ready.calls.empty(),"Ready/busy list metadata bypassed original registrar gates.");}
        Fixture pool_first;*pool_first.slot(entry)=meta(4);pool_first.manager->pools[5].reset();IgbListTypeRegistrationInvocation waiting(entry);
        check(!waiting.resume(pool_first.services).completed,"Ready list metadata skipped earlier actual pool lookup.");
    }
}
void actual_probes_and_defaults_boundary(){
    for(const auto entry:{E::registry,E::children}){Fixture f;auto value=meta(1);*f.slot(entry)=value;
        f.services->types->factory=make_native_igb_list_registration_type_factory(f.services,f.services->types->factory);
        f.remaining=[&](const IgbTypeRegistrationCall& c){if(c.kind!=TK::set_name)return std::shared_ptr<IgbTypeRegistrationOperation>{};
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[c]{c.metadata->name=c.name;return MessengerCallResult{true,0,{}};};return op;};
        IgbListTypeRegistrationInvocation body(entry);check(!body.resume(f.services).completed && f.locals==1,"Missing probe offset did not suspend after its actual reset.");
        check(value->class_flags==9 && value->native_size==16 && value->field_callback==(entry==E::registry?0x8002ddacu:0x8002e0e8u) && !value->type_word,
            "Probe pause skipped caller stores or published a guessed vtable.");
        f.core->vtable_offset=4;check(!body.resume(f.services).completed && f.locals==1 && value->type_word==1 && f.allocations==0,"Probe did not return the actual selected local reference word.");
        check(f.calls.back().kind==TK::defaults && f.calls.back().function==(entry==E::registry?0x8002ddd4u:0x8002e108u) && value->class_flags==9,
            "List registration skipped its reached specialized defaults.");
    }
}
void getter_semantics(){
    for(const auto entry:{E::registry,E::children}){
        for(const auto mode:{IgbListLookup::plain,IgbListLookup::allocate_if_null,IgbListLookup::fully_registered}){Fixture f;auto ready=meta(mode==IgbListLookup::fully_registered?4:0);*f.slot(entry)=ready;
            f.services->types.reset();f.services->pools.reset();IgbListLookupInvocation run(entry,mode);
            check(run.resume(f.services).completed && run.result()==ready,"Existing list metadata getter required unrelated services or changed ready gate.");
            *f.slot(entry)=meta();check(run.resume().completed && run.result()==ready,"Completed getter reread canonical metadata.");}
        Fixture plain;IgbListLookupInvocation null_plain(entry,IgbListLookup::plain);
        check(null_plain.resume(plain.services).completed && !null_plain.result() && plain.allocations==0,"Plain getter constructed known-null metadata.");
        Fixture pending;*pending.slot(entry)=meta(8);auto replacement=meta(0);unsigned selections{},polls{};
        pending.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;
            check(c.kind==CK::invoke_function && c.function==(entry==E::registry?0x8002dd40u:0x8002e07cu),"Full getter called another wrapper body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{if(++polls==1)return MessengerCallResult{false,0,"prepared registration"};
                *pending.slot(entry)=replacement;return MessengerCallResult{true,0,{}};};return op;};
        IgbListLookupInvocation full(entry);check(!full.resume(pending.services).completed && selections==1,"Full getter skipped busy-but-not-ready registration.");
        *pending.slot(entry)=meta(4);pending.services->callbacks->factory={};
        check(full.resume(pending.services).completed && full.result()==replacement && selections==1 && polls==2,"Full getter replaced retained registration or rechecked ready after return.");
        Fixture late;*late.slot(entry)=meta();unsigned count{};late.services->callbacks->factory=[&](const auto&){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[&]{++count;late.slot(entry)->reset();return MessengerCallResult{true,0,{}};};return op;};
        IgbListLookupInvocation reload(entry);check(!reload.resume(late.services).completed && count==1,"Getter did not pause at missing reloaded cell state.");
        *late.slot(entry)=std::shared_ptr<IgbRuntimeMetaType>{};check(reload.resume(late.services).completed && !reload.result() && count==1,"Late null publication replayed registration.");
    }
}
void lazy_getters(){
    for(const auto entry:{E::registry,E::children}){
        Fixture f;f.services->types->metadata_pool_handle.reset();IgbListLookupInvocation run(entry,IgbListLookup::allocate_if_null);
        check(!run.resume(f.services).completed && f.allocations==0,"Lazy list getter skipped its handle read.");
        f.services->types->metadata_pool_handle=5;
        check(run.resume(f.services).completed && run.result()==f.constructed && *f.slot(entry)==f.constructed && f.constructed->class_flags==0 && f.allocations==1,
            "Lazy list getter failed actual metadata construction and canonical raw publication.");
        Fixture replaced;std::weak_ptr<IgbRuntimeMetaType> displaced;
        replaced.metadata_remaining=[&](const IgbMetadataFactoryCall& c){check(c.kind==MK::create_fields,"Unexpected lazy metadata endpoint.");auto op=std::make_shared<IgbMetadataFactoryOperation>();
            op->resume=[&,out=op.get()]{auto value=meta(17);value->reference_word=19;displaced=value;*replaced.slot(entry)=value;
                out->allocation_result=replaced.field_list;out->fields_result=replaced.fields;return MessengerCallResult{true,0,{}};};return op;};
        {IgbListLookupInvocation overwrite(entry,IgbListLookup::allocate_if_null);check(overwrite.resume(replaced.services).completed && overwrite.result()==replaced.constructed,"Lazy result did not overwrite callback replacement.");}
        check(!displaced.expired() && displaced.lock()->reference_word==19,"Lazy raw publication destroyed or decremented the displaced metadata owner.");
        Fixture held;held.manager->original_pool_access_bound=false;unsigned selections{},polls{};
        held.services->pools->factory=[&](const IgbFilePoolsCall& c){++selections;check(c.handle==5,"Lazy getter changed captured metadata handle.");auto op=std::make_shared<IgbFilePoolsOperation>();
            op->resume=[&,out=op.get()]{if(++polls==1)return MessengerCallResult{false,0,"prepared pool"};out->object_result=held.pool;return MessengerCallResult{true,0,{}};};return op;};
        IgbListLookupInvocation waiting(entry,IgbListLookup::allocate_if_null);check(!waiting.resume(held.services).completed,"Lazy pool call failed to suspend.");
        held.services->types->metadata_pool_handle=9;held.services->pools->factory={};
        check(waiting.resume(held.services).completed && selections==1 && polls==2 && held.allocations==1,"Lazy lookup reselected pool after suspension.");
    }
}
void probe_physical_reads(){
    for(const auto entry:{E::registry,E::children})for(const auto offset:{0u,4u,8u,12u}){Fixture f;auto local=f.services->local_storage(entry);f.core->vtable_offset=offset;
        IgbListLayoutProbeInvocation run(local,entry);check(run.resume(f.services).completed,"List local probe failed.");
        const auto expected=offset==0?(entry==E::registry?0x80475cb0u:0x80475c54u):offset==4?1u:0u;
        check(run.value()==expected && f.allocations==0 && igb_bootstrap_read_word(local,8)==0 && igb_bootstrap_read_word(local,12)==0,"Probe returned a constant, allocated backing or skipped reset stores.");
        check(run.resume().completed && run.value()==expected,"Completed probe replayed its reset.");}
    Fixture f;auto local=f.services->local_storage(E::registry);auto first=f.core;IgbListLayoutProbeInvocation captured(local,E::registry);
    check(!captured.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x80475cb0,"Probe skipped reset before missing core offset.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;f.services->types->runtime->singleton=replacement;first->vtable_offset=4;
    check(captured.resume(f.services).completed && captured.value()==1,"Probe redirected a captured core while waiting for its offset.");
    Fixture live;auto live_local=live.services->local_storage(E::children);live.services->types->runtime->singleton.reset();IgbListLayoutProbeInvocation before_core(live_local,E::children);
    check(!before_core.resume(live.services).completed,"Missing live core was replaced by a guessed probe result.");
    replacement->vtable_offset=8;live.services->types->runtime->singleton=replacement;igb_bootstrap_write_word(live_local,8,0x12345678);
    check(before_core.resume(live.services).completed && before_core.value()==0x12345678 && igb_bootstrap_read_word(live_local,8)==0x12345678,"Probe replayed reset or invented cleanup after its read.");
    for(const auto offset:{1u,16u,0xfffffffcu}){Fixture bad;bad.core->vtable_offset=offset;auto storage=bad.services->local_storage(E::registry);IgbListLayoutProbeInvocation invalid(storage,E::registry);
        rejects([&]{invalid.resume(bad.services);},"Invalid local word offset was accepted.");check(igb_bootstrap_read_word(storage,0)==0x80475cb0,"Bad offset fault skipped reset effects.");}
    Fixture null;null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};auto storage=null.services->local_storage(E::children);IgbListLayoutProbeInvocation invalid(storage,E::children);
    rejects([&]{invalid.resume(null.services);},"Probe dereferenced represented-null core without failure.");check(igb_bootstrap_read_word(storage,0)==0x80475c54,"Null core fault preceded reset.");
}
void adapters_and_lifetime(){
    Fixture f;auto registry=meta(4),children=meta(4);*f.slot(E::registry)=registry;*f.slot(E::children)=children;
    auto types=make_native_igb_list_registration_type_factory(f.services);
    for(const auto address:{0x8002dc6cu,0x8002dca8u,0x8002ddacu,0x8002ddccu,0x8002dfa8u,0x8002dfe4u,0x8002e0e8u}){
        IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=address;auto op=types(c);
        check(op && op->resume().completed && op->metadata_result==(address<0x8002ddccu?registry:children),"List getter adapter selected the wrong canonical cell.");}
    for(const auto address:{0x8002dd18u,0x8002e054u,0x8002dd40u,0x8002e07cu}){IgbRegistrationCallbacksCall c;c.kind=CK::invoke_function;c.function=address;
        auto op=f.services->callbacks->factory(c);check(op && op->resume().completed,"Exact list registration body/wrapper adapter failed.");}
    auto list_factory=make_native_igb_list_registration_factory(f.services);
    for(const auto address:{0x8002dd18u,0x8002e054u}){IgbTypeListCall c;c.kind=IgbTypeListServiceKind::register_type;c.function=address;
        auto op=list_factory(c);check(op && op->resume().completed,"Enabled type-list factory missed its concrete registration wrapper.");}
    IgbFieldRegistryCall field_call;field_call.kind=IgbFieldRegistryServiceKind::register_type;
    const auto field_op=make_native_igb_field_list_registration_factory(f.services)(field_call);check(field_op && field_op->resume().completed,"Enabled field-list factory missed its wrapper.");
    IgbTypeRegistrationCall unknown;unknown.kind=TK::defaults;unknown.function=0x8002e108;check(!types(unknown),"Registration adapter completed unimplemented defaults.");
    Fixture held;held.services->types->metadata_pool_handle.reset();*held.slot(E::registry)=meta(4);
    IgbRegistrationCallbacksCall body;body.kind=CK::invoke_function;body.function=0x8002dd40;auto selected=held.services->callbacks->factory(body);
    check(!selected->resume().completed,"Selected ready registration did not pause at missing handle.");std::weak_ptr<IgbListRegistrationServices> weak=held.services;
    held.services.reset();check(!weak.expired(),"Pending adapter lost its service owner.");selected.reset();check(weak.expired(),"Weak registration adapter created a permanent service cycle.");
    auto owner=std::make_shared<IgbListRegistrationServices>();auto stale=make_native_igb_list_registration_function_factory(owner);owner.reset();
    rejects([&]{stale(body);},"Expired registration adapter silently recreated services.");
    Fixture recursive;*recursive.slot(E::children)=meta(1);IgbListTypeRegistrationInvocation run(E::children);
    recursive.remaining=[&](const auto&){rejects([&]{run.resume(recursive.services);},"Recursive registration was accepted.");return std::shared_ptr<IgbTypeRegistrationOperation>{};};
    rejects([&]{run.resume(recursive.services);},"Recursive failure did not poison outer registration.");check(run.faulted(),"Recursive registration did not retain fault state.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_list_registration_tests");
    arguments_and_aliases();bodies_and_gates();actual_probes_and_defaults_boundary();getter_semantics();lazy_getters();probe_physical_reads();adapters_and_lifetime();
    std::cout<<"igb_list_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"List registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
