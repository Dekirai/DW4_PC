#include "dw4/igb_error_handler_registration.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbErrorHandlerServiceKind;using C=IgbRegistrationCallbacksServiceKind;
using T=IgbTypeRegistrationServiceKind;using O=IgbObjectInitServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
IgbBootstrapPointer raw(std::uint32_t address=0x100000,std::uint32_t size=8,std::uint32_t offset=8){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(offset+size,0xcc);storage->bytes->initialized.assign(offset+size,1);
    IgbBootstrapPointer value{storage,offset};if(size>=8){igb_bootstrap_write_word(value,0,0x804759d8);igb_bootstrap_write_word(value,4,1);}return value;
}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t flags=0){
    auto value=std::make_shared<IgbRuntimeMetaType>();value->class_flags=flags;value->reference_word=2;value->post_layout_callback=0;
    value->factory_redirect=0;value->allocation_prefix=0;value->native_size=8;value->type_word=0x804759d8;
    value->instance_count=0;value->instances=std::shared_ptr<IgbObjectInstanceList>{};
    value->fields=std::make_shared<IgbDecodeFieldRegistry>();value->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();return value;
}
// Parent/name/publication/layout bodies and host allocations are explicit
// prepared endpoints. The registrar, probe, both callback lists, singleton and
// teardown are actual native invocations, not ready metadata substitutions.
struct Fixture {
    std::shared_ptr<IgbErrorHandlerServices> services=std::make_shared<IgbErrorHandlerServices>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(),parent=metadata(4);
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer created=raw();
    std::vector<IgbTypeRegistrationCall> type_calls;
    std::vector<IgbRegistrationCallbacksCall> callback_calls;
    std::vector<IgbErrorHandlerCall> service_calls;
    std::function<void(const IgbErrorHandlerCall&)> during;
    std::function<void(const IgbTypeRegistrationCall&)> during_type;
    bool pool_ready=true,create_ready=true,destroy_ready=true;
    unsigned pool_polls{},create_polls{},destroy_polls{};
    Fixture(){
        services->metadata=std::make_shared<IgbRegistrationMetaSlot>(meta);services->state=std::make_shared<IgbErrorHandlerState>();
        services->state->singleton=IgbBootstrapPointer{};services->state->teardown_guard=0;
        services->pool_handle=std::make_shared<std::optional<std::uint32_t>>(7);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;core->registration_filter_enabled=0;core->registration_callback_pool=pool;
        core->bootstrap_callbacks=std::make_shared<IgbRegistrationCallbackList>();
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        services->callbacks->factory=make_native_igb_error_handler_function_factory(services,[this](const IgbRegistrationCallbacksCall& call){
            callback_calls.push_back(call);auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[call,out=op.get()]{
                if(call.kind==C::allocate_backing || call.kind==C::reallocate_backing){
                    check(call.bytes<=1024,"Prepared callback allocation exceeds its bound.");
                    auto result=call.backing?std::make_shared<IgbRegistrationCallbackList::Storage>(*call.backing):std::make_shared<IgbRegistrationCallbackList::Storage>();
                    result->resize(call.bytes/4);out->backing_result=result;
                }else if(call.kind!=C::free_backing && call.kind!=C::free_list)throw std::runtime_error("Unexpected prepared callback endpoint.");
                return MessengerCallResult{true,0,{}};
            };return op;});
        services->local_storage=[]{return raw(0x200000);};
        services->types->factory=make_native_igb_error_handler_type_factory(services,make_native_igb_registration_list_factory(services->callbacks,
            [this](const IgbTypeRegistrationCall& call){type_calls.push_back(call);auto op=std::make_shared<IgbTypeRegistrationOperation>();
                op->resume=[this,call,out=op.get()]{
                    switch(call.kind){
                    case T::pool_for_handle:out->object_result=pool;break;
                    case T::create_metadata:out->metadata_result=meta;break;
                    case T::get_parent:out->metadata_result=parent;break;
                    case T::set_name:call.metadata->name=call.name;break;
                    case T::compute_layout:call.metadata->class_flags|=4;break;
                    case T::create_callbacks:out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();break;
                    case T::register_parent:case T::attach_parent:case T::publish_type:break;
                    default:throw std::runtime_error("Unexpected prepared type endpoint.");
                    }
                    if(during_type)during_type(call);return MessengerCallResult{true,0,{}};
                };return op;}));
        services->factory=[this](const IgbErrorHandlerCall& call){service_calls.push_back(call);auto op=std::make_shared<IgbErrorHandlerOperation>();
            op->resume=[this,call,out=op.get()]{if(during)during(call);
                if(call.kind==K::pool_for_handle){++pool_polls;out->pool_result=pool;return MessengerCallResult{pool_ready,0,"Prepared runtime pool."};}
                if(call.kind==K::create_object){++create_polls;out->object_result=created;return MessengerCallResult{create_ready,0,"Prepared object creation."};}
                ++destroy_polls;return MessengerCallResult{destroy_ready,0,"Prepared object destruction."};
            };return op;};
    }
};
void profile_and_registration(){
    Fixture f;const auto args=igb_error_handler_type_registration_arguments(f.services);
    check(args.metadata_slot==f.services->metadata && *args.name=="igErrorHandler" && args.flag==0 && args.native_size==8,"Error-handler identity/size/global alias changed.");
    check(args.parent_registration==0x80066b08 && args.parent_getter==0x800237d0 && args.field_callback==0x800312f0,"Error-handler ancestry/field callback changed.");
    check(args.layout_probe==0x80031210 && args.defaults_callback==0x80031310 && args.trailing_callback==0x80031338 && !args.extra_callbacks,"Error-handler callback profile changed.");
    rejects([]{igb_error_handler_type_registration_arguments({});},"Absent canonical cell was fabricated.");
    IgbErrorHandlerRegistrationInvocation body;check(body.resume(f.services).completed,"Actual error-handler registrar failed.");
    auto immediate=body.result();auto deferred=*f.core->bootstrap_callbacks;
    check(immediate && immediate!=deferred && immediate->count==1 && immediate->backing->at(0)==0x80031338,"Immediate constructor callback was not returned separately.");
    check(deferred && deferred->count==1 && deferred->backing->at(0)==0x80031358 && !deferred->released,"Defaults did not append actual deferred teardown.");
    check(f.service_calls.empty() && f.meta->class_flags==7 && f.meta->native_size==8 && f.meta->type_word==0x804759d8,"Registration executed trailing creation early or lost probe publication.");
    check(f.meta->reference_word==1 && f.meta->name && *f.meta->name=="igErrorHandler","Registration ownership/name publication changed.");
    f.create_ready=false;IgbRegistrationCallbacksInvocation wrapper(0x80031278);
    // Feed the already executed body's returned list into actual66188. This
    // makes its cleanup visible without performing a second registration.
    const auto native=f.services->callbacks->factory;
    f.services->callbacks->factory=[&,native](const IgbRegistrationCallbacksCall& c){
        if(c.kind==C::invoke_function && c.function==0x80031278){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->callbacks_result=immediate;op->resume=[]{return MessengerCallResult{true,0,{}};};return op;}
        return native(c);};
    check(!wrapper.resume(f.services->callbacks).completed && f.create_polls==1 && !immediate->released && deferred->count==1,"Immediate callback failed to suspend independently of deferred teardown.");
    f.create_ready=true;check(wrapper.resume(f.services->callbacks).completed && immediate->released && !deferred->released,"Registrar cleanup freed the deferred list or failed to free its own.");
    check(igb_bootstrap_address(*f.services->state->singleton)==igb_bootstrap_address(f.created) && *f.services->state->teardown_guard==0,"Trailing creation did not publish singleton or prematurely tore it down.");
    IgbRegistrationCallbacksCall teardown;teardown.kind=C::invoke_function;teardown.function=deferred->backing->at(0);
    auto operation=native(teardown);check(operation && operation->resume().completed && f.destroy_polls==1 && !igb_bootstrap_address(*f.services->state->singleton) && *f.services->state->teardown_guard==1,"Deferred teardown did not perform original release and final globals.");
    Fixture pending;pending.services->callbacks->prerequisites->initialized.reset();IgbErrorHandlerRegistrationInvocation waiting;
    check(!waiting.resume(pending.services).completed && pending.type_calls.empty(),"Unknown prerequisites were bypassed by prepared metadata.");
    pending.services->callbacks->prerequisites->initialized=1;pending.meta->class_flags=4;
    check(waiting.resume(pending.services).completed && pending.type_calls.size()==1 && pending.type_calls.front().kind==T::pool_for_handle,"Ready registrar skipped actual prerequisite/pool order.");
}
void metadata_lookups(){
    Fixture unknown;unknown.services->metadata->reset();IgbErrorHandlerLookupInvocation plain(IgbErrorHandlerLookup::plain);
    check(!plain.resume(unknown.services).completed,"Unknown metadata became known null.");*unknown.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(plain.resume(unknown.services).completed && !plain.result() && unknown.type_calls.empty(),"Plain null getter registered a class.");
    for(const auto flags:{0u,4u,8u,0xffffffffu}){Fixture f;f.meta->class_flags=flags;unsigned selections{},polls{};bool ready{};
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==C::invoke_function && c.function==0x80031278,"Full getter selected another registration body.");++selections;
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"Prepared registration."};};return op;};
        IgbErrorHandlerLookupInvocation full;const auto first=full.resume(f.services);
        if(flags&4)check(first.completed && full.result()==f.meta && selections==0,"Ready getter executed registration.");
        else{check(!first.completed && selections==1,"Full getter skipped unready metadata.");auto replacement=metadata(4);*f.services->metadata=replacement;ready=true;f.services->callbacks->factory={};
            check(full.resume(f.services).completed && full.result()==replacement && polls==2 && selections==1,"Full getter lost selected continuation or fresh metadata reload.");}
    }
    rejects([]{IgbErrorHandlerLookupInvocation invalid(static_cast<IgbErrorHandlerLookup>(2));},"Invented lazy getter mode was admitted.");
    Fixture f;auto factory=make_native_igb_error_handler_type_factory(f.services);IgbTypeRegistrationCall c;c.kind=T::get_parent;c.function=0x80031198;
    check(!factory(c),"Address31198 from the preceding function was admitted as a getter.");
}
void layout_probe(){
    for(const auto offset:{0u,4u}){Fixture f;auto local=raw();f.core->vtable_offset=offset;IgbErrorHandlerLayoutProbeInvocation run(local);
        check(run.resume(f.services).completed && run.value()==(offset?1u:0x804759d8u),"Exact8-byte probe returned an invented word.");
        check(igb_bootstrap_read_word(local,0)==0x804759d8 && igb_bootstrap_read_word(local,4)==1,"Probe omitted base reset or derived vtable.");
        for(unsigned i=0;i<local.offset;++i)check(local.storage->bytes->bytes[i]==0xcc,"Probe damaged allocation prefix.");}
    Fixture f;auto local=raw();f.services->types->runtime->singleton.reset();IgbErrorHandlerLayoutProbeInvocation run(local);
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x804717f0,"Missing core did not preserve base reset before derived store.");
    f.core->vtable_offset.reset();f.services->types->runtime->singleton=f.core;
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x804759d8,"Missing offset skipped derived store.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;f.services->types->runtime->singleton=replacement;f.core->vtable_offset=4;
    igb_bootstrap_write_word(local,4,0xff800029);check(run.resume(f.services).completed && run.value()==0xff800029,"Probe reloaded core or replayed base reset after suspension.");
    for(const auto offset:{1u,8u,0xfffffffcu}){Fixture bad;auto storage=raw();bad.core->vtable_offset=offset;IgbErrorHandlerLayoutProbeInvocation invalid(storage);
        rejects([&]{invalid.resume(bad.services);},"Probe accepted an unaligned/outside word.");check(invalid.faulted(),"Probe fault was not permanent.");}
    Fixture null;auto storage=raw();null.services->types->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbErrorHandlerLayoutProbeInvocation bad(storage);
    rejects([&]{bad.resume(null.services);},"Known-null core was treated as missing.");check(igb_bootstrap_read_word(storage,0)==0x804759d8,"Null-core fault happened before derived store.");
}
void singleton_gates_and_fresh_reload(){
    Fixture f;f.services->state->singleton.reset();IgbErrorHandlerSingletonInvocation run;
    check(!run.resume(f.services).completed && f.service_calls.empty(),"Unknown singleton became zero.");f.services->state->singleton=IgbBootstrapPointer{};f.services->metadata->reset();
    check(!run.resume(f.services).completed,"Unknown metadata skipped creation gate.");*f.services->metadata=f.meta;f.services->state->teardown_guard.reset();
    check(!run.resume(f.services).completed && f.service_calls.empty(),"Unknown guard became zero.");f.services->state->teardown_guard=0;f.services->pool_handle->reset();
    check(!run.resume(f.services).completed && f.service_calls.empty(),"Unknown runtime handle became metadata-pool handle.");*f.services->pool_handle=0;
    check(run.resume(f.services).completed && f.service_calls.size()==2 && f.service_calls[0].handle==0,"Known-zero runtime handle was rejected.");
    for(const auto guard:{1u,2u,255u}){Fixture held;held.services->state->teardown_guard=static_cast<std::uint8_t>(guard);held.services->pool_handle.reset();IgbErrorHandlerSingletonInvocation skip;
        check(skip.resume(held.services).completed && !igb_bootstrap_address(skip.result()) && held.service_calls.empty(),"Nonzero guard created a singleton.");}
    Fixture null;*null.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};null.services->state->teardown_guard.reset();IgbErrorHandlerSingletonInvocation skip;
    check(skip.resume(null.services).completed && null.service_calls.empty(),"Null metadata read later guard/handle state.");
    Fixture existing;existing.services->state->singleton=existing.created;existing.services->metadata.reset();existing.services->state->teardown_guard.reset();
    auto replacement=raw(0x300000);unsigned reads{};IgbErrorHandlerSingletonInvocation fresh;
    check(fresh.resume(existing.services,[&]{if(++reads==2)existing.services->state->singleton=replacement;}).completed && fresh.result().storage==replacement.storage,"Nonnull singleton branch did not perform fresh return load.");
    Fixture publication;bool replace{};IgbErrorHandlerSingletonInvocation publish;
    check(publish.resume(publication.services,[&]{if(publication.services->state->singleton && publication.services->state->singleton->storage==publication.created.storage && !replace){replace=true;publication.services->state->singleton=replacement;}}).completed && publish.result().storage==replacement.storage,"Constructed singleton return bypassed its fresh global reload.");
}
void selected_calls_and_publication(){
    Fixture f;f.pool_ready=false;IgbErrorHandlerSingletonInvocation run;
    check(!run.resume(f.services).completed && f.service_calls.size()==1 && f.pool_polls==1,"Runtime pool did not suspend.");
    *f.services->pool_handle=99;f.services->state->teardown_guard=1;*f.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    const auto selected=f.services->factory;f.services->factory={};f.pool_ready=true;
    check(!run.resume(f.services).completed && f.pool_polls==2 && f.service_calls.size()==1,"Selected pool restarted or later create binding was guessed.");
    f.services->factory=selected;f.create_ready=false;
    check(!run.resume(f.services).completed && f.service_calls.size()==2 && f.service_calls[1].pool==f.pool,"Create call lost captured returned pool.");
    auto displaced=raw(0x400000);std::weak_ptr<IgbObjectStorage> weak=displaced.storage;f.services->state->singleton=displaced;f.services->factory={};f.create_ready=true;
    const auto canonical=f.services->state;f.services->state.reset();
    check(!run.resume(f.services).completed && f.create_polls==2,"Missing publication storage repeated creation or invented state.");
    f.services->state=canonical;displaced={};check(run.resume(f.services).completed && run.result().storage==f.created.storage && !weak.expired(),"Publication lost displaced owner or selected result.");
    check(f.service_calls[0].handle==7 && f.pool_polls==2 && f.create_polls==2 && run.resume().completed,"Completed singleton replayed selected calls.");
    Fixture zero;zero.created={};IgbErrorHandlerSingletonInvocation null;
    check(null.resume(zero.services).completed && !igb_bootstrap_address(null.result()) && zero.service_calls.size()==2,"Null constructor result was replaced by a ready singleton.");
    Fixture checked;unsigned checked_polls{},legacy_polls{};bool ready{};std::shared_ptr<IgbErrorHandlerOperation> selected_operation;
    const auto prepared=checked.services->factory;
    checked.services->factory=[&](const IgbErrorHandlerCall& c){if(c.kind!=K::pool_for_handle)return prepared(c);
        auto op=std::make_shared<IgbErrorHandlerOperation>();op->pool_result=checked.pool;op->resume=[&]{++legacy_polls;return MessengerCallResult{true,0,{}};};
        op->resume_checked=[&](const std::function<void()>& guard){++checked_polls;guard();return MessengerCallResult{ready,0,"Prepared checked pool."};};selected_operation=op;return op;};
    IgbErrorHandlerSingletonInvocation continuation;check(!continuation.resume(checked.services).completed && checked_polls==1 && legacy_polls==0,"Checked callback was not preferred.");
    selected_operation->resume_checked={};ready=true;
    check(continuation.resume(checked.services).completed && checked_polls==2 && legacy_polls==0,"Suspended checked callback choice was not retained.");
}
void teardown_order_and_faults(){
    for(const auto references:{0u,1u,2u,0x00800001u,0xff800001u,0xffffffffu}){Fixture f;f.services->state->singleton=f.created;f.services->state->teardown_guard.reset();igb_bootstrap_write_word(f.created,4,references);
        IgbErrorHandlerTeardownInvocation run;check(run.resume(f.services).completed,"Packed-reference teardown failed.");
        check(igb_bootstrap_read_word(f.created,4)==references-1 && f.destroy_polls==((((references-1)&0x7fffff)==0)?1u:0u),"Teardown changed full-word decrement/low23 predicate.");
        check(!igb_bootstrap_address(*f.services->state->singleton) && *f.services->state->teardown_guard==1,"Teardown final stores were omitted.");}
    Fixture f;f.services->state->singleton=f.created;f.destroy_ready=false;IgbErrorHandlerTeardownInvocation run;
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(f.created,4)==0 && f.services->state->singleton->storage==f.created.storage && *f.services->state->teardown_guard==0,"Pending destructor cleared singleton or published guard early.");
    auto replacement=raw(0x500000);f.services->state->singleton=replacement;f.services->factory={};f.destroy_ready=true;
    check(run.resume(f.services).completed && f.service_calls.size()==1 && f.service_calls[0].object.storage==f.created.storage && f.destroy_polls==2,"Destructor recaptured fresh singleton or selected service again.");
    check(igb_bootstrap_read_word(replacement,4)==1 && igb_bootstrap_read_word(f.created,4)==0,"Continuation decremented a pointer twice or touched replacement reference.");
    Fixture fresh;fresh.services->state->singleton=fresh.created;IgbErrorHandlerTeardownInvocation recheck;bool changed{};
    check(recheck.resume(fresh.services,[&]{if(!changed && igb_bootstrap_read_word(fresh.created,4)==0){changed=true;igb_bootstrap_write_word(fresh.created,4,2);}}).completed && fresh.destroy_polls==0,"Teardown tested captured decrement instead of fresh packed word.");
    Fixture empty;empty.services->state->singleton.reset();IgbErrorHandlerTeardownInvocation unknown;
    check(!unknown.resume(empty.services).completed,"Unknown teardown singleton was invented as zero.");empty.services->state->singleton=IgbBootstrapPointer{};
    check(unknown.resume(empty.services).completed && empty.service_calls.empty() && *empty.services->state->teardown_guard==1,"Null teardown required a destructor.");
    Fixture split;split.services->state->singleton=split.created;igb_bootstrap_write_word(split.created,4,2);IgbErrorHandlerTeardownInvocation stores;const auto original=split.services->state;
    check(!stores.resume(split.services,[&]{if(split.services->state && split.services->state->singleton && !igb_bootstrap_address(*split.services->state->singleton))split.services->state.reset();}).completed && !igb_bootstrap_address(*original->singleton) && *original->teardown_guard==0,"Guard storage absence rolled back prior singleton store.");
    split.services->state=original;check(stores.resume(split.services).completed && *original->teardown_guard==1 && igb_bootstrap_read_word(split.created,4)==1,"Late guard binding replayed reference decrement.");
    for(const auto stage:{K::pool_for_handle,K::create_object}){Fixture recursive;IgbErrorHandlerSingletonInvocation call;
        recursive.during=[&](const auto& c){if(c.kind==stage)try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
        rejects([&]{call.resume(recursive.services);},"Caught singleton recursion did not invalidate caller.");
        check(call.faulted() && !igb_bootstrap_address(*recursive.services->state->singleton),"Faulted creation published singleton.");rejects([&]{call.resume(recursive.services);},"Faulted singleton restarted.");}
    Fixture recursive;recursive.services->state->singleton=recursive.created;IgbErrorHandlerTeardownInvocation call;
    recursive.during=[&](const auto&){try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Caught destructor recursion did not invalidate teardown.");
    check(call.faulted() && igb_bootstrap_read_word(recursive.created,4)==0 && recursive.services->state->singleton->storage==recursive.created.storage && *recursive.services->state->teardown_guard==0,"Destructor fault lost the exact decrement prefix.");
    Fixture invalid;invalid.services->state->singleton=invalid.created;invalid.created.storage->bytes->initialized[invalid.created.offset+4]=0;IgbErrorHandlerTeardownInvocation bad;
    rejects([&]{bad.resume(invalid.services);},"Unknown physical reference word was fabricated.");check(bad.faulted() && *invalid.services->state->teardown_guard==0,"Reference read fault published guard.");
    Fixture truncated;truncated.services->state->singleton=truncated.created;igb_bootstrap_write_word(truncated.created,4,0x01000000);IgbErrorHandlerTeardownInvocation atomic;unsigned boundary{};
    rejects([&]{atomic.resume(truncated.services,[&]{if(++boundary==3){truncated.created.storage->bytes->bytes.resize(truncated.created.offset+7);truncated.created.storage->bytes->initialized.resize(truncated.created.offset+7);}});},"Truncated reference store was silently completed.");
    check(atomic.faulted() && truncated.service_calls.empty() && *truncated.services->state->teardown_guard==0 && truncated.created.storage->bytes->bytes[truncated.created.offset+4]==1 && truncated.created.storage->bytes->bytes[truncated.created.offset+6]==0,"Failed reference store changed its prefix or advanced to destruction.");
}
// Real6546C ->667F4 with an explicit typed error-handler allocation and the
// class's actual literal virtual leaves. Only allocation/tag services are
// prepared; no completed instance or instance count is injected.
struct Construction : Fixture {
    std::shared_ptr<IgbRuntimeObject> owner=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::error_handler);
    std::shared_ptr<IgbObjectStorage> storage=std::make_shared<IgbObjectStorage>();
    std::function<void()> during_allocate,during_init;
    unsigned allocations{},init_callbacks{},registrations{};
    bool allocate_ready=true;
    Construction(){
        meta->class_flags=4;meta->allocation_prefix=8;storage->original_address=0x600000;storage->bytes=std::make_shared<IgbScratchBuffer>();
        storage->bytes->bytes.assign(16,0xd5);storage->bytes->initialized.assign(16,0);IgbObjectBinding binding;binding.offset=8;binding.runtime_object=owner;storage->bindings.push_back(binding);
        services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){check(c.kind==C::invoke_function && c.function==0x80031278,"3119C skipped its unconditional registration body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[this]{++registrations;return MessengerCallResult{true,0,{}};};return op;};
        services->reflected=std::make_shared<IgbFileFactoryServices>();services->reflected->initializer=std::make_shared<IgbObjectInitServices>();
        auto init=services->reflected->initializer;init->runtime=services->types->runtime;init->object_metadata=parent;
        const auto native=make_native_igb_error_handler_object_init_factory(services);
        init->factory=[this,native](const IgbObjectInitCall& c){
            if(c.kind==O::object_virtual){auto op=native(c);if(c.virtual_slot==0x0c && during_init){auto body=op->resume;op->resume=[this,body]{++init_callbacks;during_init();return body();};}return op;}
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[this,c,out=op.get()]{++init_callbacks;
                if(c.kind==O::allocation_pool){check(c.address==storage->original_address,"Initializer changed allocation-base arithmetic.");out->pool_result=pool;return MessengerCallResult{true,0,{}};}
                check(c.kind==O::pool_index,"Unexpected prepared initialization endpoint.");return MessengerCallResult{true,0x12a,{}};};return op;};
        services->reflected->factory=[this](const IgbFileFactoryCall& c){check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==pool && c.bytes==16,"Reflected factory lost captured pool/size/prefix.");++allocations;
            auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[this,out=op.get()]{if(during_allocate)during_allocate();out->allocation_result=storage;return MessengerCallResult{allocate_ready,0,"Prepared physical allocation."};};return op;};
        const auto native_service=make_native_igb_error_handler_service_factory(services);
        const auto prepared=services->factory;services->factory=[native_service,prepared](const IgbErrorHandlerCall& c){return c.kind==K::create_object?native_service(c):prepared(c);};
    }
};
void reflected_construction_and_guards(){
    Construction f;IgbErrorHandlerSingletonInvocation run;check(run.resume(f.services).completed,"Singleton did not compose actual reflected construction.");
    check(run.result().storage==f.storage && run.result().offset==8 && f.registrations==1 && f.allocations==1 && *f.meta->instance_count==1,"Construction skipped actual registration/allocation/instance publication.");
    check(igb_bootstrap_read_word(run.result(),0)==0x804759d8 && igb_bootstrap_read_word(run.result(),4)==0x2a000001,"Actual initializer header or low-byte pool tag changed.");
    for(unsigned i=0;i<8;++i)check(f.storage->bytes->initialized[i]==0 && f.storage->bytes->bytes[i]==0xd5,"Initializer changed allocation prefix.");
    Construction pending;pending.allocate_ready=false;IgbErrorHandlerObjectFactoryInvocation factory(pending.pool);
    check(!factory.resume(pending.services).completed && pending.allocations==1,"Object factory did not suspend its allocation.");
    pending.services->reflected->factory={};pending.allocate_ready=true;
    check(factory.resume(pending.services).completed && factory.result().storage==pending.storage && pending.allocations==1 && pending.registrations==1,"Object factory repeated registration/allocation across pending.");
    Construction fresh;auto abstract=metadata(4);abstract->flag_1a=1;
    fresh.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.function==0x80031278,"Factory skipped original registrar.");auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&]{*fresh.services->metadata=abstract;return MessengerCallResult{true,0,{}};};return op;};
    IgbErrorHandlerObjectFactoryInvocation reloaded(fresh.pool);
    check(reloaded.resume(fresh.services).completed && !igb_bootstrap_address(reloaded.result()) && fresh.allocations==0,"Factory failed to freshly load metadata after registration.");
    for(const bool in_initializer:{false,true}){Construction bad;IgbErrorHandlerSingletonInvocation recursive;
        auto reenter=[&]{try{recursive.resume(bad.services);}catch(const std::runtime_error&){};};
        if(in_initializer)bad.during_init=reenter;else bad.during_allocate=reenter;
        rejects([&]{recursive.resume(bad.services);},"Caught nested recursion did not reach borrowed parent guard.");
        check(recursive.faulted() && *bad.meta->instance_count==0 && !igb_bootstrap_address(*bad.services->state->singleton),"Nested fault incremented instances or published singleton.");
        if(in_initializer){IgbBootstrapPointer pointer{bad.storage,8};check(igb_bootstrap_read_word(pointer,0)==0x804759d8 && igb_bootstrap_read_word(pointer,4)==0x2a000001 && bad.storage->faulted(),"Initializer fault lost reached header/tag or failed to latch storage fault.");}
        else check(std::all_of(bad.storage->bytes->initialized.begin(),bad.storage->bytes->initialized.end(),[](auto byte){return byte==0;}) && bad.init_callbacks==0,"Allocation callback fault still initialized returned storage.");
        rejects([&]{recursive.resume(bad.services);},"Nested fault resumed after callback returned.");
    }
}
void exact_leaf_admission(){
    Fixture f;auto factory=make_native_igb_error_handler_object_init_factory(f.services);
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u,0x58u})for(unsigned representation=0;representation<3;++representation){
        IgbObjectInitCall c;c.kind=O::object_virtual;c.vtable_word=0x804759d8;c.virtual_slot=slot;
        if(representation){c.object.storage=raw().storage;c.object.offset=0xffffffff;c.object.storage->bytes.reset();if(representation==2)c.object.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::thread);}
        auto op=factory(c);check(op && op->resume().completed,"No-read literal/getter leaf validated an irrelevant receiver.");
        if(slot==0x58)check(op->metadata_result==f.meta,"Plain object metadata getter returned another canonical cell.");
    }
    IgbObjectInitCall c;c.kind=O::object_virtual;c.vtable_word=0x804759d8;c.virtual_slot=0x70;check(!factory(c),"Unrecovered virtual was admitted.");
    c.virtual_slot=0x58;c.kind=O::field_virtual;check(!factory(c),"Class adapter admitted an unrelated service.");
    c.kind=O::object_virtual;c.vtable_word=0x804759d4;check(!factory(c),"Class adapter admitted another vtable.");
}
}
int main(){try{profile_and_registration();metadata_lookups();layout_probe();singleton_gates_and_fresh_reload();selected_calls_and_publication();teardown_order_and_faults();reflected_construction_and_guards();exact_leaf_admission();
    std::cout<<"IGB error-handler registration: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB error-handler registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
