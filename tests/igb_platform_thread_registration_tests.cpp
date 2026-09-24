#include "dw4/igb_platform_thread_registration.hpp"
#include "dw4/igb_metadata_factory.hpp"

#include <iostream>

namespace {
using namespace dw4;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using BK=IgbBootstrapServiceKind;using MK=IgbMetadataFactoryServiceKind;using Lookup=IgbPlatformThreadLookup;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto metadata(std::uint32_t flags=0){auto out=std::make_shared<IgbRuntimeMetaType>();out->class_flags=flags;return out;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size,std::uint32_t prefix=0){
    auto out=std::make_shared<IgbObjectStorage>();out->original_address=address-prefix;out->bytes=std::make_shared<IgbScratchBuffer>();
    out->bytes->bytes.assign(size+prefix,0xcc);out->bytes->initialized.assign(size+prefix,1);return {out,prefix};
}
struct Fixture {
    std::shared_ptr<IgbPlatformThreadRegistrationServices> services=std::make_shared<IgbPlatformThreadRegistrationServices>();
    std::shared_ptr<IgbMetadataFactoryServices> factory=std::make_shared<IgbMetadataFactoryServices>();
    std::shared_ptr<IgbFilePoolsServices> pools=std::make_shared<IgbFilePoolsServices>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> constructed=metadata();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    IgbBootstrapPointer physical=allocation(0x100008,0x64,8),list=allocation(0x110008,16,8);
    IgbTypeRegistrationFactory remaining;std::vector<IgbTypeRegistrationCall> calls;unsigned allocations{},locals{};
    Fixture(){
        services->state=std::make_shared<IgbPlatformThreadRegistrationState>();services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
        services->parent=std::make_shared<IgbThreadRegistrationState>();services->parent->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->vtable_offset=0;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();pools->memory_pools->selector=0;pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        factory->bootstrap=std::make_shared<IgbBootstrapServices>();factory->bootstrap->runtime=services->types->runtime;
        factory->dependencies=std::make_shared<IgbDependenciesServices>();factory->dependencies->globals=std::make_shared<SystemStageServices>();
        factory->dependencies->globals->string_pool=pool;
        physical.storage->bindings.push_back({physical.offset,{},constructed});list.storage->bindings.push_back({list.offset,{},{},fields});
        factory->bootstrap->factory=[this](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x64 && c.pool==pool,
                "Cold platform metadata changed its original allocation arguments.");
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,out=op.get()]{++allocations;out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;};
        // Only the internal field-list allocation is a prepared endpoint. The
        // metadata header and members are initialized by the actual29E64 path.
        factory->factory=[this](const IgbMetadataFactoryCall& c){check(c.kind==MK::create_fields,"Cold platform metadata selected an unexpected prepared endpoint.");
            auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[this,out=op.get()]{out->allocation_result=list;out->fields_result=fields;return MessengerCallResult{true,0,{}};};return op;};
        services->types->factory=make_native_igb_platform_thread_registration_type_factory(services,
            make_native_igb_registration_pool_factory(pools,make_native_igb_metadata_factory(factory,[this](const IgbTypeRegistrationCall& c){
                calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};})));
        services->callbacks->factory=make_native_igb_platform_thread_registration_function_factory(services);
        services->local_storage=[this]{++locals;return allocation(0x200008,16,8);};
        services->physical_metadata.push_back(physical);
    }
};
void exact_arguments(){
    Fixture f;const auto args=igb_platform_thread_registration_arguments(f.services->state);
    check(args.metadata_slot.get()==&f.services->state->metadata && args.metadata_slot==igb_platform_thread_metadata_slot(f.services),
        "Platform manager metadata acquired a copied global cell.");
    check(*args.name=="igGamecubeThreadManager" && args.flag==0 && args.native_size==16,"Platform manager name, concrete flag or size differs.");
    check(args.parent_registration==0x800238a8 && args.parent_getter==0x8003af68 && args.field_callback==0x80023a10,
        "Platform manager parent or type callback differs.");
    check(args.layout_probe==0x8003ae84 && args.defaults_callback==0x8003af70 && !args.trailing_callback && !args.extra_callbacks,
        "Platform manager gained extension callbacks or lost its actual probe/defaults.");
    check(igb_platform_thread_metadata_slot(f.services,Lookup::plain_parent).get()==&f.services->parent->metadata,
        "Plain parent getter did not alias the existing abstract manager cell.");
    auto value=metadata();f.services->state->metadata=value;std::weak_ptr<IgbPlatformThreadRegistrationState> weak=f.services->state;
    f.services->state.reset();check(!weak.expired() && args.metadata_slot->value()==value,"Aliasing arguments lost the canonical state owner.");
    rejects([]{igb_platform_thread_registration_arguments({});},"Absent platform metadata storage was fabricated.");
    rejects([]{IgbPlatformThreadMetadataInvocation bad(static_cast<Lookup>(2));},"Unknown platform metadata lookup mode was accepted.");
}
void prerequisites_and_cold_registration(){
    Fixture f;auto old=f.services->state;old->metadata=metadata(4);f.services->callbacks->prerequisites->initialized.reset();
    IgbPlatformThreadRegistrationInvocation run;
    check(!run.resume(f.services).completed && f.allocations==0 && f.calls.empty(),"Ready platform metadata bypassed the earlier prerequisite read.");
    f.services->callbacks->prerequisites->initialized=0;f.services->callbacks->prerequisites->started=0;
    unsigned polls{};bool ready{};std::vector<std::uint32_t> functions;
    f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==CK::invoke_function,"Prerequisite selected a list operation.");
        functions.push_back(c.function);auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&]{++polls;return MessengerCallResult{ready,0x1234,"prepared prerequisite"};};return op;};
    auto status=run.resume(f.services);check(!status.completed && status.value==0x1234 && functions==std::vector<std::uint32_t>{0x80023880} &&
        f.services->callbacks->prerequisites->started==1,"Platform registration lost prerequisite order, state prefix or pending status.");
    auto selected=std::make_shared<IgbPlatformThreadRegistrationState>();selected->metadata=metadata(4);f.services->state=selected;
    old->metadata=std::shared_ptr<IgbRuntimeMetaType>{};f.services->types->metadata_pool_handle.reset();ready=true;
    check(!run.resume(f.services).completed && polls==6 && functions==std::vector<std::uint32_t>({0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}),
        "Platform prerequisite continuation repeated a selection or skipped later callbacks.");
    f.services->state=std::make_shared<IgbPlatformThreadRegistrationState>();f.services->types->metadata_pool_handle=5;
    check(run.resume(f.services).completed && !run.result() && !*old->metadata && !f.services->state->metadata && f.allocations==0,
        "Platform registration captured before prerequisites or recaptured its state after a pending pool lookup.");
    check(run.resume().completed && polls==6,"Completed platform registration repeated prerequisites.");

    Fixture cold;IgbPlatformThreadRegistrationInvocation construct;
    check(!construct.resume(cold.services).completed && cold.allocations==1 && *cold.services->state->metadata==cold.constructed &&
        cold.constructed->class_flags==8 && cold.constructed->fields==cold.fields && cold.calls.size()==1 &&
        cold.calls[0].kind==TK::register_parent && cold.calls[0].function==0x800238a8,
        "Cold platform body skipped actual metadata construction, busy publication or abstract parent registration.");
    check(!construct.resume(cold.services).completed && cold.allocations==1,"Pending abstract parent repeated metadata allocation.");
    for(const auto flags:{4u,8u,12u}){Fixture gate;gate.services->state->metadata=metadata(flags);IgbPlatformThreadRegistrationInvocation held;
        check(held.resume(gate.services).completed && !held.result() && (*gate.services->state->metadata)->class_flags==flags && gate.allocations==0,
            "Platform manager changed the original ready/busy gate.");}
}
void reached_probe_and_defaults(){
    Fixture f;auto value=metadata(1);value->reference_word=2;f.services->state->metadata=value;
    auto physical=allocation(0x310008,0x64,8);physical.storage->bindings.push_back({8,{},value});f.services->physical_metadata.push_back(physical);
    f.remaining=[](const IgbTypeRegistrationCall& c){if(c.kind!=TK::set_name)return std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[c]{c.metadata->name=c.name;return MessengerCallResult{true,0,{}};};return op;};
    IgbPlatformThreadRegistrationInvocation run;
    check(!run.resume(f.services).completed && f.locals==1 && f.allocations==0 && f.calls.size()==2 && f.calls.back().kind==TK::publish_type,
        "Platform registration skipped native probe/defaults or completed unrepresented publication.");
    check(value->class_flags==11 && value->native_size==16 && value->flag_1a==0 && value->type_word==0x804700a8 &&
        value->field_callback==0x80023a10 && value->word_40==0x80023920 && igb_bootstrap_read_word(physical,0x40)==0x80023920,
        "Platform registration lost original stores before the publish boundary.");
    check(!value->fields && !value->factory_redirect && !run.resume(f.services).completed && f.locals==1,
        "Concrete defaults fabricated inherited fields/redirect or pending publication replayed its probe.");
}
void metadata_getters(){
    Fixture f;f.services->state->metadata.reset();IgbPlatformThreadMetadataInvocation missing;
    check(!missing.resume(f.services).completed && f.calls.empty(),"Unknown concrete metadata became a represented null.");
    auto ready=metadata(4);f.services->state->metadata=ready;
    check(missing.resume(f.services).completed && missing.result()==ready && f.allocations==0,"Ready concrete metadata unnecessarily registered.");
    f.services->state->metadata=metadata();check(missing.resume().completed && missing.result()==ready,"Completed concrete getter reloaded its result.");
    Fixture plain;plain.services->parent->metadata.reset();IgbPlatformThreadMetadataInvocation parent(Lookup::plain_parent);
    check(!parent.resume(plain.services).completed,"Unknown parent metadata became null.");
    plain.services->parent->metadata=std::shared_ptr<IgbRuntimeMetaType>{};plain.services->callbacks.reset();plain.services->types.reset();
    check(parent.resume(plain.services).completed && !parent.result() && plain.allocations==0,"Plain parent null performed registration.");
    for(const auto flags:{0u,4u,8u,12u}){Fixture pending;pending.services->state->metadata=metadata(flags);
        const auto selected=pending.services->state;unsigned selections{},polls{};bool resume{};auto replacement=metadata();
        pending.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;
            check(c.kind==CK::invoke_function && c.function==0x8003aef8,"Concrete full getter selected a different registration body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(resume)selected->metadata=replacement;
                return MessengerCallResult{resume,0x57,"prepared body"};};return op;};
        IgbPlatformThreadMetadataInvocation getter;auto result=getter.resume(pending.services);
        if(flags&4u){check(result.completed && !selections,"Ready full getter executed the registration wrapper.");continue;}
        check(!result.completed && result.value==0x57 && selections==1 && polls==1,"Unready full getter lost its original body or pending status.");
        pending.services->state=std::make_shared<IgbPlatformThreadRegistrationState>();pending.services->state->metadata=ready;
        selected->metadata=ready;pending.services->callbacks->factory={};resume=true;
        check(getter.resume(pending.services).completed && getter.result()==replacement && selections==1 && polls==2,
            "Pending full getter changed its canonical owner, replayed registration or rechecked ready before the reload.");
    }
    Fixture reload;reload.services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};unsigned calls{};
    reload.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall&){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&]{++calls;reload.services->state->metadata.reset();return MessengerCallResult{true,0,{}};};return op;};
    IgbPlatformThreadMetadataInvocation getter;check(!getter.resume(reload.services).completed && calls==1,"Unrepresented post-registration reload became null.");
    reload.services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(getter.resume(reload.services).completed && !getter.result() && calls==1,"Null reload repeated the completed registration.");
}
void physical_probe(){
    for(const auto offset:{0u,4u,8u,12u}){Fixture f;auto local=allocation(0x500008,16,8);f.core->vtable_offset=offset;
        igb_bootstrap_write_word(local,8,0x12345678);igb_bootstrap_write_word(local,12,0x87654321);
        IgbPlatformThreadProbeInvocation run(local);const auto result=run.resume(f.services);
        const auto expected=offset==0?0x804700a8u:offset==4?1u:offset==8?0x12345678u:0x87654321u;
        check(result.completed && result.value==expected && run.value()==expected && igb_bootstrap_read_word(local,0)==0x804700a8,
            "Platform probe returned a guessed vtable or initialized untouched manager fields.");
        check(igb_bootstrap_read_word(local,4)==1 && local.storage->bytes->bytes[7]==0xcc && f.allocations==0,
            "Platform probe changed the original reset or performed a runtime allocation.");
        igb_bootstrap_write_word(local,0,0x11111111);check(run.resume().value==expected && igb_bootstrap_read_word(local,0)==0x11111111,
            "Completed platform probe repeated a vtable store.");
    }
    Fixture missing;auto local=allocation(0x500008,16,8);missing.services->types->runtime->singleton.reset();IgbPlatformThreadProbeInvocation wait(local);
    check(!wait.resume(missing.services).completed && igb_bootstrap_read_word(local,0)==0x804717f0 && igb_bootstrap_read_word(local,4)==1,
        "Platform probe stored its manager vtable before capturing the core pointer.");
    missing.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{wait.resume(missing.services);},"Platform probe accepted a known-null captured core.");
    check(wait.faulted() && igb_bootstrap_read_word(local,0)==0x804700a8,"Null core fault skipped preceding manager table publications.");
    Fixture captured;local=allocation(0x500008,16,8);captured.core->vtable_offset.reset();IgbPlatformThreadProbeInvocation offset(local);
    check(!offset.resume(captured.services).completed && igb_bootstrap_read_word(local,0)==0x804700a8,"Unknown offset skipped concrete table publication.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=4;captured.services->types->runtime->singleton=replacement;
    captured.core->vtable_offset=0;igb_bootstrap_write_word(local,0,0x10203040);
    check(offset.resume(captured.services).completed && offset.value()==0x10203040 && igb_bootstrap_read_word(local,0)==0x10203040,
        "Platform probe reloaded the core, repeated stores or invented a cleanup vtable.");
    for(const auto index:{8u,12u}){Fixture unknown;local=allocation(0x500008,16,8);unknown.core->vtable_offset=index;
        local.storage->bytes->initialized[local.offset+index]=0;IgbPlatformThreadProbeInvocation fault(local);
        rejects([&]{fault.resume(unknown.services);},"Platform probe fabricated a missing manager member.");
        check(fault.faulted() && local.storage->bytes->initialized[local.offset+index]==0 && igb_bootstrap_read_word(local,0)==0x804700a8,
            "Unknown physical member fault changed its bytes or preceding table prefix.");}
    for(const auto index:{1u,16u,0xffffffffu}){Fixture outside;local=allocation(0x500008,16,8);outside.core->vtable_offset=index;IgbPlatformThreadProbeInvocation fault(local);
        rejects([&]{fault.resume(outside.services);},"Platform probe escaped its aligned16-byte local profile.");}
    Fixture unreadable;local=allocation(0x500008,16,8);local.storage->bytes->initialized[local.offset+4]=0;IgbPlatformThreadProbeInvocation original(local);
    rejects([&]{original.resume(unreadable.services);},"Base reset invented an unreadable original reference word.");
    check(igb_bootstrap_read_word(local,0)==0x804717f0 && local.storage->bytes->initialized[local.offset+4]==0,
        "Base reset fault lost its preceding original vtable store.");
}
void defaults_publication(){
    Fixture f;f.services->state->metadata.reset();const auto captured=f.services->state;IgbPlatformThreadDefaultsInvocation run;
    check(!run.resume(f.services).completed,"Unknown defaults receiver became null metadata.");
    auto target=metadata(2);target->word_40=0x12345678;target->fields=std::make_shared<IgbDecodeFieldRegistry>();target->fields->count=7;
    auto local=allocation(0x600008,0x64,8);local.storage->bindings.push_back({8,{},target});f.services->physical_metadata.push_back(local);
    captured->metadata=target;f.services->state=std::make_shared<IgbPlatformThreadRegistrationState>();f.services->state->metadata=metadata(4);
    check(run.resume(f.services).completed && target->word_40==0x80023920 && igb_bootstrap_read_word(local,0x40)==0x80023920 &&
        target->class_flags==2 && target->fields->count==7 && !(*f.services->state->metadata)->word_40,
        "Defaults lost its canonical receiver, appended fields, or changed unrelated registration state.");
    check(local.storage->bytes->bytes[8+0x3f]==0xcc && local.storage->bytes->bytes[8+0x44]==0xcc,
        "Defaults +40 store overwrote adjacent physical bytes.");
    target->word_40=0;igb_bootstrap_write_word(local,0x40,0);
    check(run.resume().completed && target->word_40==0 && igb_bootstrap_read_word(local,0x40)==0,"Completed defaults replayed publication.");
    Fixture null;IgbPlatformThreadDefaultsInvocation invalid;rejects([&]{invalid.resume(null.services);},"Defaults accepted a represented null metadata pointer.");
    check(invalid.faulted(),"Null defaults store lost fault state.");
    Fixture truncated;truncated.services->state->metadata=truncated.constructed;truncated.constructed->word_40=0x76543210;
    truncated.physical.storage->bytes->bytes.resize(8+0x43);truncated.physical.storage->bytes->initialized.resize(8+0x43);
    IgbPlatformThreadDefaultsInvocation short_store;rejects([&]{short_store.resume(truncated.services);},"Defaults accepted a truncated physical +40 word.");
    check(truncated.constructed->word_40==0x76543210 && truncated.physical.storage->bytes->bytes[8+0x40]==0xcc,
        "Failed physical defaults store published canonical state or partial bytes.");
    rejects([&]{short_store.resume(truncated.services);},"Faulted defaults invocation resumed.");
    Fixture conflict;conflict.services->state->metadata=conflict.constructed;auto other=allocation(0x700008,0x64,8);
    other.storage->bindings.push_back({8,{},conflict.constructed});conflict.services->physical_metadata.push_back(other);
    IgbPlatformThreadDefaultsInvocation aliases;rejects([&]{aliases.resume(conflict.services);},"Defaults accepted conflicting physical metadata identities.");
    check(!conflict.constructed->word_40,"Conflicting defaults storage published its canonical word.");
    Fixture duplicate;duplicate.services->state->metadata=duplicate.constructed;duplicate.services->physical_metadata.push_back(duplicate.physical);
    IgbPlatformThreadDefaultsInvocation consistent;check(consistent.resume(duplicate.services).completed && duplicate.constructed->word_40==0x80023920,
        "Consistent duplicate physical metadata bindings were rejected.");
}
void adapters_and_lifetimes(){
    Fixture f;f.services->state->metadata=metadata(4);auto parent=metadata();f.services->parent->metadata=parent;
    const auto adapter=make_native_igb_platform_thread_registration_type_factory(f.services);
    for(const auto function:{0x8003ae48u,0x8003af68u}){IgbTypeRegistrationCall call;call.kind=TK::get_parent;call.function=function;auto op=adapter(call);
        check(op && op->resume().completed && op->metadata_result==(function==0x8003af68?parent:*f.services->state->metadata),
            "Exact platform getter adapter selected another canonical global.");}
    for(const auto function:{0x8003aed0u,0x8003aef8u}){IgbRegistrationCallbacksCall call;call.kind=CK::invoke_function;call.function=function;
        auto op=f.services->callbacks->factory(call);check(op && op->resume().completed,"Exact platform wrapper/body adapter missed the ready gate.");}
    IgbTypeRegistrationCall call;call.kind=TK::register_parent;call.function=0x8003aef8;
    check(adapter(call)->resume().completed,"Platform parent-registration adapter was not composed.");
    call.kind=TK::defaults;call.function=0x8003af70;call.metadata=parent;
    check(adapter(call)->resume().completed && (*f.services->state->metadata)->word_40==0x80023920 && !parent->word_40,
        "Platform defaults adapter used callback metadata instead of its canonical receiver.");
    call.kind=TK::layout_probe;call.function=0x8003ae84;f.services->local_storage={};auto probe=adapter(call);
    check(probe && !probe->resume().completed,"Platform probe adapter guessed local storage.");
    auto local=allocation(0x800008,16,8);unsigned selections{};
    f.services->local_storage=[&]{++selections;return local;};f.core->vtable_offset.reset();
    check(!probe->resume().completed && selections==1,"Platform probe adapter failed to retain its allocated local.");
    f.services->local_storage={};f.core->vtable_offset=0;
    check(probe->resume().value==0x804700a8 && selections==1,"Pending probe adapter reselected local storage.");
    std::weak_ptr<IgbObjectStorage> retained=local.storage;local={};probe.reset();
    check(!retained.expired(),"Probe discarded represented guest local ownership.");
    for(const auto function:{0x8003af84u,0x8003b0e0u,0x8003b15cu,0x800933f0u,0x800755c0u}){call.function=function;
        check(!adapter(call),"Manager-only platform adapter claimed another class or runtime body.");}
    auto owner=std::make_shared<IgbPlatformThreadRegistrationServices>();auto weak_adapter=make_native_igb_platform_thread_registration_type_factory(owner);
    owner.reset();call.kind=TK::get_parent;call.function=0x8003ae48;
    rejects([&]{weak_adapter(call);},"Expired platform adapter silently replaced services.");
    Fixture held;held.services->types->metadata_pool_handle.reset();IgbRegistrationCallbacksCall body;body.kind=CK::invoke_function;body.function=0x8003aef8;
    auto operation=held.services->callbacks->factory(body);check(!operation->resume().completed,"Retained body did not suspend at its missing handle.");
    std::weak_ptr<IgbPlatformThreadRegistrationServices> weak=held.services;held.services.reset();
    check(!weak.expired(),"Pending platform body lost its selected services.");operation.reset();check(weak.expired(),"Platform adapters created a permanent service cycle.");
}
void reflected_redirect(){
    Fixture f;const auto adapter=make_native_igb_platform_thread_redirect_factory(f.services);
    IgbFileFactoryCall call;call.kind=IgbFileFactoryServiceKind::redirect_type;call.redirect=0x80023a10;call.metadata=metadata(4);
    unsigned selections{},polls{};bool ready{};auto concrete=metadata();
    f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;
        check(c.kind==CK::invoke_function && c.function==0x8003aef8,"Reflected redirect skipped the full concrete metadata getter.");
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(ready)f.services->state->metadata=concrete;
            return MessengerCallResult{ready,0x3456,"prepared concrete registration"};};return op;};
    auto operation=adapter(call);auto result=operation->resume();
    check(!result.completed && result.value==0x3456 && !operation->metadata_result && selections==1 && polls==1,
        "Reflected manager redirect fabricated metadata while its registration was pending.");
    f.services->state->metadata=metadata(4);f.services->callbacks->factory={};ready=true;
    check(operation->resume().completed && operation->metadata_result==concrete && !operation->allocation_result && !operation->object_result &&
        selections==1 && polls==2 && f.allocations==0,"Reflected redirect changed its retained getter, returned an object or skipped the fresh metadata reload.");
    check(operation->resume().completed && polls==2,"Completed redirect repeated concrete registration.");
    for(const auto redirect:{0x80023e20u,0x80026160u,0x8003ae48u}){call.redirect=redirect;check(!adapter(call),"Manager redirect adapter claimed an unrelated redirect function.");}
    call.redirect=0x80023a10;call.kind=IgbFileFactoryServiceKind::allocate;
    check(!adapter(call),"Manager redirect adapter consumed an allocation request.");
}
}
int main(){try{exact_arguments();prerequisites_and_cold_registration();reached_probe_and_defaults();metadata_getters();physical_probe();defaults_publication();adapters_and_lifetimes();reflected_redirect();
    std::cout<<"igb_platform_thread_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Platform thread registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
