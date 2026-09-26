#include "dw4/igb_data_list_registration.hpp"

#include <array>
#include <iostream>

namespace {
using namespace dw4;using E=IgbDataListKind;using L=IgbDataListLookup;
using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;using MK=IgbMetadataFactoryServiceKind;
constexpr std::array entries{E::data_list,E::nonref_object,E::thread_list,E::ref_object,E::ref_thread};
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
    std::shared_ptr<IgbDataListRegistrationServices> services=std::make_shared<IgbDataListRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta();IgbBootstrapPointer physical=allocation(0x100000,0x64,8);
    IgbTypeRegistrationFactory remaining;IgbRegistrationCallbacksFactory callback_remaining;
    std::vector<IgbTypeRegistrationCall> calls;std::vector<std::uint32_t> functions;
    std::function<void()> during_allocation;unsigned allocations{},allocation_polls{},locals{};bool allocation_ready=true;
    Fixture(){
        services->data_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->ref_object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->ref_thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        services->dependencies->thread_list=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
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
        // Explicit reflected allocation isolates the getter/registrar. Actual
        // nested field-list construction belongs to cold-bootstrap integration.
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==MK::register_type)op->resume=[] {return MessengerCallResult{true,0,{}};};
            else{check(c.kind==MK::reflected_factory && c.pool==pool,"Lazy metadata changed its actual factory or pool.");++allocations;
                op->resume=[this,out=op.get()]{++allocation_polls;if(during_allocation)during_allocation();
                    if(!allocation_ready)return MessengerCallResult{false,0,"prepared reflected allocation"};out->allocation_result=physical;return MessengerCallResult{true,0,{}};};}
            return op;};
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,make_native_igb_metadata_factory(services->metadata,
            [this](const IgbTypeRegistrationCall& c){calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_data_list_registration_function_factory(services,[this](const IgbRegistrationCallbacksCall& c){
            functions.push_back(c.function);return callback_remaining?callback_remaining(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};});
        services->dependencies->types=services->types;services->dependencies->pools=services->pools;services->dependencies->metadata=services->metadata;
        services->local_storage=[this](E){++locals;return allocation(0x200000+locals*0x100,20,8);};
    }
    auto slot(E kind){return igb_data_list_metadata_slot(services,kind);}
};
void exact_profiles(){
    constexpr std::array<std::uint32_t,5> globals{0x80561d3c,0x80561730,0x80561530,0x80561708,0x80561534},bodies{0x80033a14,0x80029694,0x80023f50,0x8002907c,0x800240f4};
    constexpr std::array<std::uint32_t,5> parents{0x80066b08,0x80033a14,0x80029694,0x80033a14,0x8002907c},getters{0x800237d0,0x80024d1c,0x80023fdc,0x80024d1c,0x80024180};
    constexpr std::array<std::uint32_t,5> probes{0x800339ac,0x80029614,0x80023eb8,0x80028ffc,0x8002405c},defaults{0x80033aa4,0x80029724,0,0x8002910c,0};
    for(unsigned i=0;i<entries.size();++i){Fixture f;const auto kind=entries[i];const auto& p=igb_data_list_profile(kind);
        const auto args=igb_data_list_type_registration_arguments(f.services,kind);
        check(p.global==globals[i] && p.body==bodies[i] && args.metadata_slot==f.slot(kind),"Profile lost its original identity or canonical cell.");
        check(*args.name==p.name && args.flag==0 && args.native_size==20 && args.parent_registration==parents[i] && args.parent_getter==getters[i],"Original name/flags/layout/ancestry changed.");
        check(args.layout_probe==probes[i] && args.defaults_callback==defaults[i] && args.field_callback==p.field_callback && args.trailing_callback==0,"Original callback identities changed.");
        if(kind==E::thread_list || kind==E::ref_thread)check(args.extra_callbacks && *args.extra_callbacks==std::vector<std::uint32_t>({0x80023c40,0}) &&
            args.metadata_slot==(kind==E::thread_list?f.services->dependencies->thread_list:f.services->ref_thread_metadata),"Thread list duplicated the lazy-getter cell or extension table.");
        else check(!args.extra_callbacks,"Parent list invented deferred callbacks.");
        auto value=meta();*f.slot(kind)=value;check(*args.metadata_slot==value,"Arguments copied rather than shared the original cell.");
    }
    rejects([]{igb_data_list_profile(static_cast<E>(7));},"Unknown class profile was accepted.");
    rejects([]{igb_data_list_type_registration_arguments({},E::data_list);},"Unknown canonical storage was manufactured.");
    rejects([]{IgbDataListLookupInvocation bad(E::nonref_object,L::allocate_if_null);},"Unrecovered lazy getter was invented.");
}
void prerequisites_and_registration(){
    for(const auto kind:entries){Fixture f;*f.slot(kind)=meta(4);f.services->callbacks->prerequisites->initialized.reset();
        // This fixture explicitly prepares prerequisite leaves. Keep240CC at
        // that boundary now that its independent full registrar is admitted.
        const auto native=f.services->callbacks->factory;
        f.services->callbacks->factory=[&,native](const IgbRegistrationCallbacksCall& c){
            if(c.kind==CK::invoke_function && c.function==0x800240cc){f.functions.push_back(c.function);
                return f.callback_remaining?f.callback_remaining(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};}
            return native(c);};
        IgbDataListTypeRegistrationInvocation call(kind);check(!call.resume(f.services).completed && f.calls.empty(),"Ready metadata bypassed earlier prerequisite state.");
        f.services->callbacks->prerequisites->initialized=std::uint8_t{0};unsigned polls{};bool ready{};
        f.callback_remaining=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==CK::invoke_function,"Prerequisite selected an unrelated service.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared prerequisite"};};return op;};
        check(!call.resume(f.services).completed && f.services->callbacks->prerequisites->started==1 && f.functions==std::vector<std::uint32_t>{0x80023880},"Actual prerequisite started prefix or first callback changed.");
        f.services->types->metadata_pool_handle.reset();ready=true;
        check(!call.resume(f.services).completed && f.functions==std::vector<std::uint32_t>({0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}) && polls==6,"Prerequisite replayed a selection or skipped ordered calls.");
        f.services->types->metadata_pool_handle=5;check(call.resume(f.services).completed && !call.result() && f.calls.empty(),"Ready registrar did not complete after actual prerequisite/pool work.");
        check(call.resume().completed && polls==6,"Completed body replayed prerequisites.");
        Fixture cold;IgbDataListTypeRegistrationInvocation construction(kind);
        check(!construction.resume(cold.services).completed && cold.allocations==1 && *cold.slot(kind)==cold.constructed && cold.constructed->class_flags==8,"Cold body lost actual metadata factory/publication/busy prefix.");
        check(cold.calls.size()==1 && cold.calls[0].kind==TK::register_parent && cold.calls[0].function==igb_data_list_profile(kind).parent,"Cold body reached another parent or completed unknown inheritance.");
        check(!construction.resume(cold.services).completed && cold.allocations==1,"Pending parent repeated metadata construction.");
        for(const auto flags:{4u,8u,12u}){Fixture held;auto original=meta(flags);*held.slot(kind)=original;IgbDataListTypeRegistrationInvocation fast(kind);
            check(fast.resume(held.services).completed && original->class_flags==flags && held.allocations==0,"Ready/busy original gate changed.");}
    }
}
void getters_and_retained_calls(){
    for(const auto kind:entries){
        Fixture plain;plain.slot(kind)->reset();IgbDataListLookupInvocation read(kind,L::plain);
        check(!read.resume(plain.services).completed,"Unknown plain global became null.");
        *plain.slot(kind)=std::shared_ptr<IgbRuntimeMetaType>{};check(read.resume(plain.services).completed && !read.result() && plain.allocations==0,"Plain null getter allocated metadata.");
        for(const auto flags:{0u,4u,8u,0xffffffffu}){Fixture f;auto original=meta(flags);*f.slot(kind)=original;unsigned selections{},polls{};bool ready{};
            f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;check(c.function==igb_data_list_profile(kind).body,"Full getter selected another wrapper body.");
                auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared registration"};};return op;};
            IgbDataListLookupInvocation full(kind);const auto first=full.resume(f.services);
            if(flags&4u)check(first.completed && full.result()==original && selections==0,"Ready full getter called registration.");
            else{check(!first.completed && selections==1,"Full getter skipped null/not-ready wrapper.");
                auto replacement=meta();*f.slot(kind)=replacement;f.services->callbacks->factory={};ready=true;
                check(full.resume(f.services).completed && full.result()==replacement && selections==1 && polls==2,"Full getter reselected or failed to reload after its captured call.");}
        }
    }
    for(const auto kind:{E::data_list,E::thread_list,E::ref_object,E::ref_thread}){Fixture f;f.allocation_ready=false;const auto selected=f.slot(kind);
        IgbDataListLookupInvocation lazy(kind,L::allocate_if_null);check(!lazy.resume(f.services).completed && f.allocations==1,"Lazy actual metadata allocation failed to suspend.");
        auto displaced=meta(17);displaced->reference_word=73;std::weak_ptr<IgbRuntimeMetaType> weak=displaced;*selected=displaced;
        auto alternate=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        if(kind==E::data_list)f.services->data_metadata=alternate;else if(kind==E::thread_list)f.services->dependencies->thread_list=alternate;
        else if(kind==E::ref_object)f.services->ref_object_metadata=alternate;else f.services->ref_thread_metadata=alternate;
        f.allocation_ready=true;f.services->metadata.reset();
        check(lazy.resume(f.services).completed && lazy.result()==f.constructed && *selected==f.constructed && !**alternate && f.allocation_polls==2,"Lazy publication replaced captured canonical cell or factory.");
        displaced.reset();check(!weak.expired() && weak.lock()->reference_word==73,"Raw lazy publication destroyed or released displaced owner.");
        check((kind==E::thread_list?f.services->dependencies->allocations:f.services->allocations).size()==1,"Lazy result lost its physical allocation lifetime.");
        check(lazy.resume().completed && f.allocations==1,"Completed lazy getter repeated allocation.");
    }
}
std::uint32_t final_word(E kind){return kind==E::data_list?0x80472fa0u:kind==E::nonref_object?0x80476da8u:
    kind==E::thread_list?0x80476ce0u:kind==E::ref_object?0x80476c7cu:0x80476bb4u;}
void physical_probes(){
    for(const auto kind:entries)for(const auto offset:{0u,4u,8u,12u,16u}){Fixture f;auto local=f.services->local_storage(kind);auto before=local.storage->bytes->bytes;
        for(unsigned n=8;n<20;++n)local.storage->bytes->bytes[local.offset+n]=static_cast<std::uint8_t>(n+1);
        f.core->vtable_offset=offset;const auto expected=offset==0?final_word(kind):offset==4?1u:igb_bootstrap_read_word(local,offset);
        IgbDataListLayoutProbeInvocation probe(local,kind);check(probe.resume(f.services).completed && probe.value()==expected && f.allocations==0,"Probe guessed word or constructed a list instead of preserving stack bytes.");
        check(igb_bootstrap_read_word(local,0)==final_word(kind) && igb_bootstrap_read_word(local,4)==1,"Probe skipped actual6665C or final vtable.");
        for(unsigned n=8;n<20;++n)check(local.storage->bytes->bytes[local.offset+n]==n+1,"Probe overwrote uninitialized list members.");
        for(unsigned n=0;n<local.offset;++n)check(local.storage->bytes->bytes[n]==before[n],"Probe overwrote local prefix padding.");
        check(probe.resume().completed && probe.value()==expected,"Completed probe replayed stores.");
    }
    for(const auto kind:entries){Fixture f;auto local=f.services->local_storage(kind);f.services->types->runtime->singleton.reset();
        IgbDataListLayoutProbeInvocation probe(local,kind);check(!probe.resume(f.services).completed,"Missing ArkCore was replaced with a guess.");
        const auto prefix=kind==E::data_list?0x804717f0u:(kind==E::nonref_object || kind==E::ref_object)?0x80472fa0u:
            kind==E::thread_list?0x80476da8u:0x80476c7cu;
        check(igb_bootstrap_read_word(local,0)==prefix,"Probe captured ArkCore after the wrong number of stores.");
        f.services->types->runtime->singleton=f.core;check(!probe.resume(f.services).completed && igb_bootstrap_read_word(local,0)==final_word(kind),"Missing offset skipped remaining stores.");
        auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;f.services->types->runtime->singleton=replacement;f.core->vtable_offset=16;
        igb_bootstrap_write_word(local,16,0x12345678);
        check(probe.resume(f.services).completed && probe.value()==0x12345678,"Probe reloaded captured ArkCore or reset live bytes after suspension.");
        Fixture null;auto null_local=null.services->local_storage(kind);null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
        IgbDataListLayoutProbeInvocation bad(null_local,kind);rejects([&]{bad.resume(null.services);},"Known-null ArkCore did not fault.");
        check(bad.faulted() && igb_bootstrap_read_word(null_local,0)==final_word(kind),"Null core fault occurred before later vtable stores.");
    }
    for(const auto offset:{1u,20u,0xfffffffcu}){Fixture f;auto local=f.services->local_storage(E::data_list);f.core->vtable_offset=offset;
        IgbDataListLayoutProbeInvocation bad(local,E::data_list);rejects([&]{bad.resume(f.services);},"Probe read beyond original aligned20-byte object.");}
    Fixture unknown;auto local=unknown.services->local_storage(E::data_list);local.storage->bytes->initialized[local.offset+8]=0;unknown.core->vtable_offset=8;
    IgbDataListLayoutProbeInvocation data(local,E::data_list);rejects([&]{data.resume(unknown.services);},"Unknown untouched local word was silently initialized.");
    Fixture header;auto raw=header.services->local_storage(E::thread_list);raw.storage->bytes->initialized[raw.offset+4]=0;
    IgbDataListLayoutProbeInvocation reset(raw,E::thread_list);rejects([&]{reset.resume(header.services);},"Unknown old packed reference word was fabricated.");
    check(igb_bootstrap_read_word(raw,0)==0x804717f0 && reset.faulted(),"Reset read fault lost prior base vtable store.");
}
void adapters_and_faults(){
    Fixture f;for(const auto kind:entries)*f.slot(kind)=meta(4);auto type=make_native_igb_data_list_registration_type_factory(f.services);
    for(const auto kind:entries){const auto& p=igb_data_list_profile(kind);
        for(const auto fn:{p.full_getter,p.field_callback,p.plain_getter,p.lazy_getter})if(fn){IgbTypeRegistrationCall call;call.kind=TK::get_parent;call.function=fn;
            const auto op=type(call);check(op && op->resume().completed && op->metadata_result==**f.slot(kind),"Getter adapter selected another canonical class.");}
        for(const auto fn:{p.body,p.wrapper}){IgbRegistrationCallbacksCall call;call.kind=CK::invoke_function;call.function=fn;
            auto op=f.services->callbacks->factory(call);check(op && op->resume().completed,"Exact body/wrapper adapter failed.");}
        f.core->vtable_offset=0;IgbTypeRegistrationCall call;call.kind=TK::layout_probe;call.function=p.probe;auto op=type(call);
        check(op && op->resume().value==final_word(kind),"Probe adapter selected another vtable chain.");
    }
    IgbTypeRegistrationCall defaults;defaults.kind=TK::defaults;defaults.function=0x80033aa4;check(!type(defaults),"Registration adapter invented DataList defaults.");
    Fixture held;*held.slot(E::data_list)=meta(4);held.services->types->metadata_pool_handle.reset();
    IgbRegistrationCallbacksCall body;body.kind=CK::invoke_function;body.function=0x80033a14;auto selected=held.services->callbacks->factory(body);
    check(!selected->resume().completed,"Adapter did not retain reached pending pool operation.");std::weak_ptr<IgbDataListRegistrationServices> weak=held.services;held.services.reset();
    check(!weak.expired(),"Selected adapter discarded its service lifetime.");selected.reset();check(weak.expired(),"Weak factory created a service cycle.");
    auto owner=std::make_shared<IgbDataListRegistrationServices>();auto stale=make_native_igb_data_list_registration_function_factory(owner);owner.reset();
    rejects([&]{stale(body);},"Expired factory manufactured services.");
    Fixture recursion;*recursion.slot(E::data_list)=meta(1);IgbDataListTypeRegistrationInvocation run(E::data_list);
    recursion.remaining=[&](const auto&){try{run.resume(recursion.services);}catch(const std::runtime_error&){}return std::shared_ptr<IgbTypeRegistrationOperation>{};};
    rejects([&]{run.resume(recursion.services);},"Swallowed recursive body did not poison outer invocation.");check(run.faulted(),"Reentrant fault was lost.");
}
void reference_profiles_and_separate_lazy_cells(){
    struct Expected{E kind;const char* name;std::uint32_t wrapper,body,field,full,lazy,plain;};
    constexpr std::array<Expected,2> expected{{{E::ref_object,"igObjectList",0x80029054,0x8002907c,0x800290ec,0x80028fc0,0x80028f84,0x80024180},
        {E::ref_thread,"igThreadList",0x800240cc,0x800240f4,0x80024160,0x80024020,0x80023fe4,0}}};
    for(const auto& e:expected){Fixture f;const auto& p=igb_data_list_profile(e.kind);
        check(std::string(p.name)==e.name && p.wrapper==e.wrapper && p.body==e.body && p.field_callback==e.field &&
            p.full_getter==e.full && p.lazy_getter==e.lazy && p.plain_getter==e.plain,
            "Reference-counted list's exact wrapper/getter/field callback identity changed.");
        auto old_object=meta(4),old_thread=meta(4);*f.services->object_metadata=old_object;*f.services->dependencies->thread_list=old_thread;
        const auto original_object=f.services->object_metadata,original_thread=f.services->dependencies->thread_list;
        f.services->dependencies.reset();IgbDataListLookupInvocation lazy(e.kind,L::allocate_if_null);
        check(lazy.resume(f.services).completed && lazy.result()==f.constructed && **original_object==old_object && **original_thread==old_thread &&
            f.allocations==1 && f.services->allocations.size()==1 && f.slot(e.kind)!=original_object && f.slot(e.kind)!=original_thread,
            "Reference list used the old nonreference-thread dependency getter or shared its metadata cell.");
    }
    Fixture missing;missing.services->ref_thread_metadata.reset();*missing.services->dependencies->thread_list=meta(4);
    IgbDataListLookupInvocation wait(E::ref_thread,L::allocate_if_null);check(!wait.resume(missing.services).completed && missing.allocations==0,
        "Missing80561534 storage fell back to ready80561530 metadata.");
    missing.services->ref_thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
    check(wait.resume(missing.services).completed && wait.result()==missing.constructed,"Late reference-thread canonical storage did not resume.");
}

void reference_probe_unknown_bytes_and_capture_order(){
    for(const auto kind:{E::ref_object,E::ref_thread}){Fixture f;auto local=f.services->local_storage(kind);
        for(unsigned at=8;at<20;++at)local.storage->bytes->initialized[local.offset+at]=0;
        const auto before=local.storage->bytes->bytes,mask=local.storage->bytes->initialized;
        f.services->types->runtime->singleton.reset();IgbDataListLayoutProbeInvocation run(local,kind);
        check(!run.resume(f.services).completed && igb_bootstrap_read_word(local,0)==(kind==E::ref_object?0x80472fa0u:0x80476c7cu) &&
            igb_bootstrap_read_word(local,4)==1,"Reference probe crossed the wrong vtable boundary before ArkCore capture.");
        f.services->types->runtime->singleton=f.core;
        check(!run.resume(f.services).completed && igb_bootstrap_read_word(local,0)==final_word(kind),
            "Reference probe did not finish its post-capture vtable stores before reading the unknown offset.");
        for(unsigned at=8;at<20;++at)check(local.storage->bytes->bytes[local.offset+at]==before[local.offset+at] &&
            local.storage->bytes->initialized[local.offset+at]==mask[local.offset+at],"Reference probe initialized unused count/capacity/backing bytes.");
        auto different=std::make_shared<IgbRuntimeTypeRegistry>();different->vtable_offset=0;f.services->types->runtime->singleton=different;
        f.core->vtable_offset=8;rejects([&]{run.resume(f.services);},"Captured core's unknown local count was replaced with a fresh-core vtable result.");
        igb_bootstrap_write_word(local,8,7);rejects([&]{run.resume(f.services);},"Reference probe unknown-byte fault revived after repair.");
    }
}

void reference_thread_extra_callback_composition(){
    Fixture f;auto object=meta();object->reference_word=2;object->post_layout_callback=0;*f.slot(E::ref_thread)=object;
    f.core->registration_filter_enabled=0;f.core->registration_callback_pool=f.pool;auto parent=meta(4);
    f.remaining=[&](const IgbTypeRegistrationCall& c){auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==TK::get_parent){check(c.function==0x80024180,"Reference thread requested the nonreference parent getter.");out->metadata_result=parent;}
        else if(c.kind==TK::register_parent)check(c.function==0x8002907c,"Reference thread registered the nonreference object parent.");
        else if(c.kind==TK::set_name)c.metadata->name=c.name;
        else if(c.kind==TK::layout_probe){check(c.function==0x8002405c,"Reference thread selected another probe.");return MessengerCallResult{true,0x80476bb4,{}};}
        else if(c.kind==TK::compute_layout)c.metadata->class_flags|=4;
        else if(c.kind==TK::create_callbacks)out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();
        else check(c.kind==TK::attach_parent || c.kind==TK::publish_type,"Reference thread invented defaults or an unrelated registrar operation.");
        return MessengerCallResult{true,0,{}};};return op;};
    f.services->types->factory=make_native_igb_registration_list_factory(f.services->callbacks,f.services->types->factory);
    bool ready{};unsigned selections{},polls{},backing_frees{},list_frees{};
    f.callback_remaining=[&](const IgbRegistrationCallbacksCall& c){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(c.kind==CK::invoke_function){check(c.function==0x80023c40,"Extra callback table selected another function.");++selections;}
        op->resume=[&,c,out=op.get()]{
            if(c.kind==CK::allocate_backing || c.kind==CK::reallocate_backing){out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(c.bytes/4);return MessengerCallResult{true,0,{}};}
            if(c.kind==CK::invoke_function){++polls;return MessengerCallResult{ready,0,"Prepared deferred thread callback."};}
            if(c.kind==CK::free_backing)++backing_frees;else if(c.kind==CK::free_list)++list_frees;else throw std::runtime_error("Unexpected callback cleanup.");
            return MessengerCallResult{true,0,{}};};return op;};
    const auto factory=f.services->callbacks->factory;std::shared_ptr<IgbRegistrationCallbackList> produced;
    f.services->callbacks->factory=[&,factory](const IgbRegistrationCallbacksCall& c){auto op=factory(c);
        if(c.kind==CK::invoke_function && c.function==0x800240f4 && op){const auto resume=op->resume;
            op->resume=[&,resume,out=op.get()]{const auto result=resume();
                //66188 moves the returned list into its own continuation.
                // Retain the observation before that ownership transfer.
                if(result.completed)produced=out->callbacks_result;return result;};}return op;};
    IgbRegistrationCallbacksInvocation wrapper(0x800240f4);check(!wrapper.resume(f.services->callbacks).completed && produced &&
        produced->count==1 && produced->backing->at(0)==0x80023c40 && selections==1 && polls==1 &&
        (object->class_flags&4)!=0 && object->native_size==20 && object->field_callback==0x80024160 && backing_frees==0 && list_frees==0,
        "Actual reference-thread registrar did not append its extra callback before deferred execution/cleanup.");
    const auto list=produced;ready=true;check(wrapper.resume(f.services->callbacks).completed && selections==1 && polls==2 &&
        backing_frees==1 && list_frees==1 && list->released,"Deferred reference-thread callback was reselected or omitted original list cleanup.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_data_list_registration_tests");
    exact_profiles();prerequisites_and_registration();getters_and_retained_calls();physical_probes();adapters_and_faults();
    reference_profiles_and_separate_lazy_cells();reference_probe_unknown_bytes_and_capture_order();reference_thread_extra_callback_composition();
    std::cout<<"igb_data_list_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Data-list registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
