#include "dw4/igb_named_type_registration.hpp"

#include <array>
#include <iostream>

namespace {
using namespace dw4;using E=IgbNamedTypeKind;using L=IgbNamedTypeLookup;
using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;using MK=IgbMetadataFactoryServiceKind;
constexpr std::array entries{E::named_object,E::thread,E::semaphore};
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto meta(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;return result;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size,std::uint32_t prefix=0){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-prefix;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size+prefix+8,0xcc);storage->bytes->initialized.assign(size+prefix+8,1);
    return {storage,prefix};
}
struct Fixture {
    std::shared_ptr<IgbNamedTypeRegistrationServices> services=std::make_shared<IgbNamedTypeRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta();IgbBootstrapPointer physical=allocation(0x100000,0x64,8);
    IgbTypeRegistrationFactory remaining;
    std::vector<IgbTypeRegistrationCall> calls;
    std::function<void()> during_allocation;unsigned allocations{},allocation_polls{},locals{};bool allocation_ready=true;
    Fixture(){
        services->named_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        services->dependencies->semaphore=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->enabled=1;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->initialized=std::uint8_t{1};services->callbacks->prerequisites->started=std::uint8_t{1};
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->bootstrap->runtime=services->types->runtime;services->metadata->metadata=meta(4);
        physical.storage->bindings.push_back({physical.offset,{},constructed});
        // Prepared reflected allocation isolates the new getter. Construction
        // of the actual metadata/field lists is covered by cold integration.
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==MK::register_type)op->resume=[] {return MessengerCallResult{true,0,{}};};
            else{check(c.kind==MK::reflected_factory && c.pool==pool,"Lazy metadata changed its actual factory or captured pool.");++allocations;
                op->resume=[this,out=op.get()]{++allocation_polls;if(during_allocation)during_allocation();
                    if(!allocation_ready)return MessengerCallResult{false,0x1234,"prepared reflected allocation"};out->allocation_result=physical;return MessengerCallResult{true,0,{}};};}
            return op;};
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,make_native_igb_metadata_factory(services->metadata,
            [this](const IgbTypeRegistrationCall& c){calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_named_type_registration_function_factory(services);
        services->dependencies->types=services->types;services->dependencies->pools=services->pools;services->dependencies->metadata=services->metadata;
        services->local_storage=[this]{++locals;return allocation(0x200000+locals*0x100,12,8);};
    }
    auto slot(E kind){return igb_named_type_metadata_slot(services,kind);}
    void replace_slot(E kind,std::shared_ptr<IgbRegistrationMetaSlot> cell){
        if(kind==E::named_object)services->named_metadata=std::move(cell);
        else if(kind==E::thread)services->thread_metadata=std::move(cell);
        else services->dependencies->semaphore=std::move(cell);
    }
};
void exact_profiles(){
    constexpr std::array<std::uint32_t,3> globals{0x80561744,0x80561504,0x805615ec},bodies{0x80029d58,0x80023c68,0x800260c0};
    constexpr std::array<std::uint32_t,3> parents{0x80066b08,0x80029d58,0x80029d58},getters{0x800237d0,0x80023cf4,0x80023cf4};
    constexpr std::array<std::uint32_t,3> probes{0x80029ca8,0,0},defaults{0x80029de8,0x80023cfc,0x8002614c};
    for(unsigned i=0;i<entries.size();++i){Fixture f;const auto kind=entries[i];const auto& p=igb_named_type_profile(kind);
        const auto args=igb_named_type_registration_arguments(f.services,kind);
        check(p.global==globals[i] && p.body==bodies[i] && args.metadata_slot==f.slot(kind),"Profile lost its original identity or canonical cell.");
        check(*args.name==p.name && args.flag==(i?1u:0u) && args.native_size==(kind==E::thread?48u:12u),"Original name/abstract flag/native size changed.");
        check(args.parent_registration==parents[i] && args.parent_getter==getters[i] && args.layout_probe==probes[i],"Original ancestry or layout probe changed.");
        check(args.defaults_callback==defaults[i] && args.field_callback==p.field_callback && args.trailing_callback==0 && !args.extra_callbacks,"Original callbacks changed or invented trailing work.");
        if(kind==E::semaphore)check(args.metadata_slot==f.services->dependencies->semaphore,"Semaphore duplicated the existing lazy-getter cell.");
        auto value=meta();*f.slot(kind)=value;check(*args.metadata_slot==value,"Arguments copied rather than shared the original cell.");
    }
    rejects([]{igb_named_type_profile(static_cast<E>(3));},"Unknown class profile was accepted.");
    rejects([]{igb_named_type_registration_arguments({},E::named_object);},"Unknown canonical storage was manufactured.");
    rejects([]{IgbNamedTypeLookupInvocation bad(E::thread,L::plain);},"Unrecovered plain thread getter was invented.");
    rejects([]{IgbNamedTypeLookupInvocation bad(E::named_object,static_cast<L>(3));},"Unknown lookup mode was accepted.");
}
void prerequisites_and_registration(){
    for(const auto kind:entries){Fixture f;auto old_cell=f.slot(kind);*old_cell=meta(4);f.services->callbacks->prerequisites->initialized.reset();
        IgbNamedTypeRegistrationInvocation call(kind);check(!call.resume(f.services).completed && f.calls.empty(),"Ready metadata bypassed earlier prerequisite state.");
        f.services->callbacks->prerequisites->initialized=std::uint8_t{0};unsigned polls{};bool ready{};std::vector<std::uint32_t> functions;
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==CK::invoke_function,"Prerequisite selected an unrelated service.");functions.push_back(c.function);
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0x2468,"prepared prerequisite"};};return op;};
        const auto pending=call.resume(f.services);
        check(!pending.completed && pending.value==0x2468 && f.services->callbacks->prerequisites->started==1 && functions==std::vector<std::uint32_t>{0x80023880},"Prerequisite status, started prefix or first callback changed.");
        auto selected=std::make_shared<IgbRegistrationMetaSlot>(meta(4));f.replace_slot(kind,selected);*old_cell=std::shared_ptr<IgbRuntimeMetaType>{};
        f.services->types->metadata_pool_handle.reset();ready=true;
        check(!call.resume(f.services).completed && functions==std::vector<std::uint32_t>({0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}) && polls==6,"Prerequisite replayed a selection or skipped ordered calls.");
        f.replace_slot(kind,std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{}));
        f.services->types->metadata_pool_handle=5;
        check(call.resume(f.services).completed && !call.result() && f.calls.empty() && f.allocations==0 && !**old_cell,"Body captured its canonical argument before prerequisites or recaptured it after pool suspension.");
        check(call.resume().completed && polls==6,"Completed body replayed prerequisites.");
        Fixture cold;IgbNamedTypeRegistrationInvocation construction(kind);
        check(!construction.resume(cold.services).completed && cold.allocations==1 && *cold.slot(kind)==cold.constructed && cold.constructed->class_flags==8,"Cold body lost actual metadata factory/publication/busy prefix.");
        check(cold.calls.size()==1 && cold.calls[0].kind==TK::register_parent && cold.calls[0].function==igb_named_type_profile(kind).parent,"Cold body reached another parent or completed unknown inheritance.");
        check(!construction.resume(cold.services).completed && cold.allocations==1,"Pending parent repeated metadata construction.");
        for(const auto flags:{4u,8u,12u}){Fixture held;auto original=meta(flags);*held.slot(kind)=original;IgbNamedTypeRegistrationInvocation fast(kind);
            check(fast.resume(held.services).completed && original->class_flags==flags && held.allocations==0,"Ready/busy original gate changed.");}
    }
    for(const auto kind:entries){Fixture f;auto current=meta(1);*f.slot(kind)=current;f.core->vtable_offset=0;
        const auto native=make_native_igb_named_type_registration_type_factory(f.services);
        f.remaining=[&](const IgbTypeRegistrationCall& c){if(c.kind==TK::set_name){auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[] {return MessengerCallResult{true,0,{}};};return op;}
            return native(c);};
        IgbNamedTypeRegistrationInvocation call(kind);check(!call.resume(f.services).completed,"Unknown defaults silently completed registration.");
        check(current->native_size==(kind==E::thread?48u:12u) && current->flag_1a==(kind==E::named_object?0u:1u) && current->field_callback==igb_named_type_profile(kind).field_callback,"Registration did not publish the actual native layout before defaults.");
        check(f.calls.back().kind==TK::defaults && f.calls.back().function==igb_named_type_profile(kind).defaults,"Registration reached another defaults body.");
        check(f.locals==(kind==E::named_object?1u:0u) && (kind!=E::named_object || current->type_word==0x8047650c),"Abstract registration probed a nonexistent object or named registration skipped its physical probe.");
    }
}
void full_and_plain_getters(){
    Fixture plain;plain.slot(E::named_object)->reset();IgbNamedTypeLookupInvocation read(E::named_object,L::plain);
    check(!read.resume(plain.services).completed,"Unknown plain global became null.");
    *plain.slot(E::named_object)=std::shared_ptr<IgbRuntimeMetaType>{};check(read.resume(plain.services).completed && !read.result() && plain.allocations==0,"Plain null getter allocated metadata.");
    for(const auto kind:entries)for(const auto flags:{0u,4u,8u,0xffffffffu}){Fixture f;auto original=meta(flags);*f.slot(kind)=original;unsigned selections{},polls{};bool ready{};
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;check(c.function==igb_named_type_profile(kind).body,"Full getter selected another wrapper body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,91,"prepared registration"};};return op;};
        IgbNamedTypeLookupInvocation full(kind);const auto first=full.resume(f.services);
        if(flags&4u)check(first.completed && full.result()==original && selections==0,"Ready full getter called registration.");
        else{check(!first.completed && first.value==91 && selections==1,"Full getter skipped not-ready wrapper or changed pending status.");
            auto replacement=meta();*f.slot(kind)=replacement;f.services->callbacks->factory={};ready=true;
            check(full.resume(f.services).completed && full.result()==replacement && selections==1 && polls==2,"Full getter reselected or failed to reload after its captured call.");}
    }
    for(const auto kind:entries){Fixture f;f.slot(kind)->reset();unsigned calls{};
        f.services->callbacks->factory=[&](const auto&){++calls;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[&]{f.slot(kind)->reset();return MessengerCallResult{true,0,{}};};return op;};
        IgbNamedTypeLookupInvocation full(kind);check(!full.resume(f.services).completed && calls==0,"Unknown metadata triggered registration.");
        *f.slot(kind)=std::shared_ptr<IgbRuntimeMetaType>{};check(!full.resume(f.services).completed && calls==1,"Known-null lookup skipped wrapper or manufactured its reloaded result.");
        *f.slot(kind)=std::shared_ptr<IgbRuntimeMetaType>{};check(full.resume(f.services).completed && !full.result() && calls==1,"Full getter retried registration instead of returning a reloaded null pointer.");
    }
}
void lazy_getters(){
    for(const auto kind:entries){Fixture f;f.allocation_ready=false;const auto selected=f.slot(kind);
        IgbNamedTypeLookupInvocation lazy(kind,L::allocate_if_null);const auto first=lazy.resume(f.services);
        check(!first.completed && first.value==0x1234 && f.allocations==1,"Lazy actual metadata allocation failed to suspend with the original status.");
        auto displaced=meta(17);displaced->reference_word=73;std::weak_ptr<IgbRuntimeMetaType> weak=displaced;*selected=displaced;
        auto alternate=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});f.replace_slot(kind,alternate);
        f.allocation_ready=true;f.services->metadata.reset();f.services->dependencies->metadata.reset();f.services->types->metadata_pool_handle=99;
        check(lazy.resume(f.services).completed && lazy.result()==f.constructed && *selected==f.constructed && !**alternate && f.allocation_polls==2,"Lazy publication changed its captured canonical cell, pool or metadata factory.");
        displaced.reset();check(!weak.expired() && weak.lock()->reference_word==73,"Raw lazy publication destroyed or released displaced owner.");
        const auto& allocations=kind==E::semaphore?f.services->dependencies->allocations:f.services->allocations;
        check(allocations.size()==1 && allocations[0].storage==f.physical.storage && allocations[0].offset==f.physical.offset,"Lazy result lost its physical allocation identity.");
        check(lazy.resume().completed && f.allocations==1,"Completed lazy getter repeated allocation.");
    }
    for(const auto kind:entries)for(const auto flags:{0u,4u,8u}){Fixture f;auto current=meta(flags);*f.slot(kind)=current;f.services->types->metadata_pool_handle.reset();
        IgbNamedTypeLookupInvocation lazy(kind,L::allocate_if_null);
        check(lazy.resume(f.services).completed && lazy.result()==current && f.allocations==0,"Lazy getter tested readiness or looked up a pool for a nonnull metadata pointer.");
    }
    for(const auto kind:entries){Fixture f;f.physical={};IgbNamedTypeLookupInvocation lazy(kind,L::allocate_if_null);
        check(lazy.resume(f.services).completed && !lazy.result() && *f.slot(kind)==std::shared_ptr<IgbRuntimeMetaType>{},"Lazy getter replaced an actual null factory result with metadata.");
        Fixture bad;bad.physical.storage->bindings.clear();IgbNamedTypeLookupInvocation malformed(kind,L::allocate_if_null);
        rejects([&]{malformed.resume(bad.services);},"Factory returned an unbound physical identity without faulting.");
        check(malformed.faulted() && !**bad.slot(kind),"Malformed allocation was published before physical identity validation.");
    }
}
void physical_probe(){
    for(const auto offset:{0u,4u,8u}){Fixture f;auto local=f.services->local_storage();const auto before=local.storage->bytes->bytes;f.core->vtable_offset=offset;
        IgbNamedObjectProbeInvocation probe(local);const auto result=probe.resume(f.services);const auto expected=offset==0?0x8047650cu:offset==4?1u:0u;
        check(result.completed && result.value==expected && probe.value()==expected && f.allocations==0,"Probe returned a guessed vtable instead of its selected physical word.");
        check(igb_bootstrap_read_word(local,0)==0x8047650c && igb_bootstrap_read_word(local,4)==1 && igb_bootstrap_read_word(local,8)==0,"Probe skipped base reset or derived stores.");
        for(std::size_t n=0;n<before.size();++n)if(n<local.offset || n>=local.offset+12)check(local.storage->bytes->bytes[n]==before[n],"Probe overwrote local padding.");
        igb_bootstrap_write_word(local,0,7);check(probe.resume().value==expected && igb_bootstrap_read_word(local,0)==7,"Completed probe repeated cleanup stores.");
    }
    Fixture f;auto local=f.services->local_storage();f.services->types->runtime->singleton.reset();IgbNamedObjectProbeInvocation probe(local);
    check(!probe.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x804717f0 && igb_bootstrap_read_word(local,4)==1 && igb_bootstrap_read_word(local,8)==0xcccccccc,"Missing core did not preserve the exact pre-derived prefix.");
    f.services->types->runtime->singleton=f.core;
    check(!probe.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x8047650c && igb_bootstrap_read_word(local,8)==0,"Missing offset paused before derived initialization.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;f.services->types->runtime->singleton=replacement;
    igb_bootstrap_write_word(local,0,0x12345678);igb_bootstrap_write_word(local,4,9);igb_bootstrap_write_word(local,8,0x87654321);f.core->vtable_offset=0;
    check(probe.resume(f.services).completed && probe.value()==0x12345678 && igb_bootstrap_read_word(local,0)==0x8047650c,"Probe reloaded core, replayed initialization or skipped derived cleanup.");
    check(igb_bootstrap_read_word(local,4)==9 && igb_bootstrap_read_word(local,8)==0x87654321,"Probe released or cleared the mutated name despite saved literal-zero cleanup.");
    Fixture name;auto named=name.services->local_storage();IgbNamedObjectProbeInvocation live(named);check(!live.resume(name.services).completed,"Unknown offset unexpectedly completed.");
    igb_bootstrap_write_word(named,8,0x23456789);name.core->vtable_offset=8;
    check(live.resume(name.services).value==0x23456789 && igb_bootstrap_read_word(named,8)==0x23456789,"Late selected name word was replaced by its earlier zero initialization.");
    Fixture null;auto null_local=null.services->local_storage();null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    IgbNamedObjectProbeInvocation bad(null_local);rejects([&]{bad.resume(null.services);},"Known-null captured ArkCore did not fault.");
    check(bad.faulted() && igb_bootstrap_read_word(null_local,0)==0x8047650c && igb_bootstrap_read_word(null_local,8)==0,"Null-core fault lost the prior derived VT/name stores.");
    rejects([&]{bad.resume(f.services);},"Faulted probe resumed after an external core repair.");rejects([&]{bad.value();},"Faulted probe exposed a result.");
    for(const auto offset:{1u,12u,0xfffffffcu}){Fixture invalid;auto raw=invalid.services->local_storage();invalid.core->vtable_offset=offset;IgbNamedObjectProbeInvocation run(raw);
        rejects([&]{run.resume(invalid.services);},"Probe read beyond the aligned12-byte object profile.");}
    Fixture unknown;auto raw=unknown.services->local_storage();raw.storage->bytes->initialized[raw.offset+4]=0;IgbNamedObjectProbeInvocation reset(raw);
    rejects([&]{reset.resume(unknown.services);},"Unknown old packed reference word was fabricated.");
    check(igb_bootstrap_read_word(raw,0)==0x804717f0 && igb_bootstrap_read_word(raw,8)==0xcccccccc,"Reset fault did not retain only its completed base VT store.");
    Fixture short_storage;auto short_local=short_storage.services->local_storage();short_local.storage->bytes->bytes.resize(short_local.offset+8);short_local.storage->bytes->initialized.resize(short_local.offset+8);
    short_storage.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbNamedObjectProbeInvocation short_probe(short_local);
    rejects([&]{short_probe.resume(short_storage.services);},"Missing name storage was silently skipped before core dereference.");
    check(igb_bootstrap_read_word(short_local,0)==0x8047650c && igb_bootstrap_read_word(short_local,4)==1,"Short local fault lost the reset and derived VT prefix.");
}
void adapters_and_faults(){
    Fixture f;for(const auto kind:entries)*f.slot(kind)=meta(4);auto type=make_native_igb_named_type_registration_type_factory(f.services);
    for(const auto kind:entries){const auto& p=igb_named_type_profile(kind);
        for(const auto fn:{p.full_getter,p.field_callback,p.plain_getter,p.lazy_getter})if(fn){IgbTypeRegistrationCall call;call.kind=TK::get_parent;call.function=fn;
            const auto op=type(call);check(op && op->resume().completed && op->metadata_result==**f.slot(kind),"Getter adapter selected another canonical class.");}
        for(const auto fn:{p.body,p.wrapper}){IgbRegistrationCallbacksCall call;call.kind=CK::invoke_function;call.function=fn;
            auto op=f.services->callbacks->factory(call);check(op && op->resume().completed,"Exact body/wrapper adapter failed.");}
        IgbTypeRegistrationCall body;body.kind=TK::register_parent;body.function=p.body;auto op=type(body);
        check(op && op->resume().completed,"Parent registration adapter missed a recovered body.");
    }
    IgbTypeRegistrationCall plain;plain.kind=TK::get_parent;plain.function=0x800940b8;auto get=type(plain);
    check(get && get->resume().completed && get->metadata_result==**f.slot(E::named_object),"Virtual plain getter940B8 did not share80561744.");
    IgbTypeRegistrationCall call;call.kind=TK::layout_probe;call.function=0x80029ca8;f.services->local_storage={};auto probe=type(call);
    check(probe && !probe->resume().completed && f.locals==0,"Unrepresented local storage became a constructed object.");
    f.services->local_storage=[&]{++f.locals;return allocation(0x250000,12,8);};check(!probe->resume().completed && f.locals==1,"Probe did not retain its first local while waiting for offset.");
    f.core->vtable_offset=4;check(probe->resume().value==1 && f.locals==1,"Probe adapter rebuilt its retained stack local.");
    IgbTypeRegistrationCall defaults;defaults.kind=TK::defaults;defaults.function=0x80029de8;check(!type(defaults),"Registration adapter invented named defaults.");
    call.function=0;check(!type(call),"Zero probe callback matched an abstract profile.");
    Fixture held;*held.slot(E::named_object)=meta(4);held.services->types->metadata_pool_handle.reset();
    IgbRegistrationCallbacksCall body;body.kind=CK::invoke_function;body.function=0x80029d58;auto selected=held.services->callbacks->factory(body);
    check(!selected->resume().completed,"Adapter did not retain reached pending pool operation.");std::weak_ptr<IgbNamedTypeRegistrationServices> weak=held.services;held.services.reset();
    check(!weak.expired(),"Selected adapter discarded its service lifetime.");selected.reset();check(weak.expired(),"Weak factory created a service cycle.");
    auto owner=std::make_shared<IgbNamedTypeRegistrationServices>();auto stale=make_native_igb_named_type_registration_function_factory(owner);owner.reset();
    rejects([&]{stale(body);},"Expired factory manufactured services.");
    Fixture recursion;*recursion.slot(E::named_object)=meta(1);IgbNamedTypeRegistrationInvocation run(E::named_object);
    recursion.remaining=[&](const auto&){try{run.resume(recursion.services);}catch(const std::runtime_error&){}return std::shared_ptr<IgbTypeRegistrationOperation>{};};
    rejects([&]{run.resume(recursion.services);},"Swallowed recursive body did not poison outer invocation.");check(run.faulted(),"Reentrant fault was lost.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_named_type_registration_tests");
    exact_profiles();prerequisites_and_registration();full_and_plain_getters();lazy_getters();physical_probe();adapters_and_faults();
    std::cout<<"igb_named_type_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Named-type registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
