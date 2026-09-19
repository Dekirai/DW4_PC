#include "dw4/igb_thread_registration.hpp"

#include <iostream>

namespace {
using namespace dw4;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t flags){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;return result;}
struct Fixture {
    std::shared_ptr<IgbThreadRegistrationState> state=std::make_shared<IgbThreadRegistrationState>();
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    std::shared_ptr<IgbTypeRegistrationServices> types=std::make_shared<IgbTypeRegistrationServices>();
    std::shared_ptr<IgbFilePoolsServices> pools=std::make_shared<IgbFilePoolsServices>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbTypeRegistrationCall> calls;
    std::function<std::shared_ptr<IgbTypeRegistrationOperation>(const IgbTypeRegistrationCall&)> remaining;
    Fixture(){
        callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        callbacks->prerequisites->initialized=1;callbacks->prerequisites->started=1;
        pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();pools->memory_pools->selector=0;pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        types->factory=make_native_igb_registration_pool_factory(pools,[this](const IgbTypeRegistrationCall& call){
            calls.push_back(call);return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        });
        callbacks->factory=make_native_igb_thread_registration_function_factory(state,callbacks,types);
    }
};
void exact_arguments(){
    auto state=std::make_shared<IgbThreadRegistrationState>();const auto args=igb_thread_type_registration_arguments(state);
    check(args.metadata_slot.get()==&state->metadata && !*args.metadata_slot,"argument helper aliases unknown canonical slot without publishing it");
    check(*args.name=="igThreadManager" && args.flag==1 && args.native_size==0x10,"exact abstract class name and size");
    check(args.parent_registration==0x80066b08 && args.parent_getter==0x800237d0 && args.field_callback==0x80023920,"actual parent and getter functions");
    check(args.layout_probe==0 && args.defaults_callback==0x80023940 && args.trailing_callback==0x80023a30,"abstract class has no layout probe but real defaults and activation");
    check(*args.extra_callbacks==std::vector<std::uint32_t>({0x80023f28,0x80026098,0}),"exact extension table and terminator");
    auto meta=metadata(0);state->metadata=meta;check(args.metadata_slot->value()==meta,"later publication visible through original canonical alias");
    std::weak_ptr<IgbThreadRegistrationState> weak=state;state.reset();check(!weak.expired(),"argument slot retains its owner");
    rejects([]{igb_thread_type_registration_arguments({});},"missing canonical slot owner rejected");
    IgbTypeRegistrationServices types;check(!types.metadata_pool_handle,"metadata handle is initially unknown");
    publish_original_igb_metadata_pool_handle(types);check(types.metadata_pool_handle==5 && !types.runtime && !types.factory,"publication helper only performs proven handle store5");
    types.metadata_pool_handle=9;publish_original_igb_metadata_pool_handle(types);check(types.metadata_pool_handle==5,"explicit startup store overwrites previous word");
}
void reached_cold_registration(){
    Fixture f;f.callbacks->prerequisites->initialized=0;f.callbacks->prerequisites->started=0;
    f.state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    IgbRegistrationCallbacksInvocation entry(0x800238a8);
    check(!entry.resume(f.callbacks).completed,"cold entry pauses at unrepresented metadata pool handle");
    check(f.callbacks->prerequisites->initialized==1 && f.callbacks->prerequisites->started==1 && f.calls.empty(),"recursive prerequisite guard commits bytes without fake type registration");
    publish_original_igb_metadata_pool_handle(*f.types);
    const auto progress=entry.resume(f.callbacks);check(!progress.completed,"actual cold ThreadManager reaches missing metadata constructor");
    check(f.calls.size()==1 && f.calls[0].kind==TK::create_metadata && f.calls[0].pool==f.pool,"actual native handle5 lookup feeds metadata factory");
    check(f.calls[0].allocation_label && *f.calls[0].allocation_label=="igMetaObject: igThreadManager","actual registrar prepares concrete metadata allocation label");
    check(f.state->metadata && !*f.state->metadata && !entry.completed() && !entry.faulted(),"missing metadata construction remains truthful pending");
}
void ready_and_in_progress(){
    Fixture f;publish_original_igb_metadata_pool_handle(*f.types);auto ready=metadata(4);f.state->metadata=ready;
    IgbThreadTypeRegistrationInvocation invocation;
    check(invocation.resume(f.state,f.callbacks,f.types).completed && !invocation.result(),"represented ready metadata follows real registrar fast path");
    check(ready->class_flags==4 && f.calls.empty() && ready->native_size==0,"ready branch leaves schema untouched");
    check(invocation.resume({}, {}, {}).completed,"completed registration does not replay");
    f.manager->pools[5].reset();IgbThreadTypeRegistrationInvocation missing_pool;
    check(!missing_pool.resume(f.state,f.callbacks,f.types).completed,"ready metadata cannot bypass earlier physical pool read");
    f.manager->pools[5]=f.pool;check(missing_pool.resume(f.state,f.callbacks,f.types).completed,"ready metadata resumes after real pool binding");
    auto busy=metadata(8);f.state->metadata=busy;IgbThreadTypeRegistrationInvocation recursive;
    check(recursive.resume(f.state,f.callbacks,f.types).completed && !recursive.result() && busy->class_flags==8,"in-progress metadata avoids repeated registration without clearing busy bit");
}
void field_boundary(){
    // Prepared inherited metadata and name setter isolate the first real
    // ThreadManager field callback. They do not construct the actual schema.
    Fixture f;publish_original_igb_metadata_pool_handle(*f.types);const auto meta=metadata(1);f.state->metadata=meta;
    f.remaining=[&](const IgbTypeRegistrationCall& call){
        if(call.kind!=TK::set_name)return std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[call]{call.metadata->name=call.name;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbThreadTypeRegistrationInvocation invocation;check(!invocation.resume(f.state,f.callbacks,f.types).completed,"real field callback remains pending");
    check(f.calls.size()==2 && f.calls[0].kind==TK::set_name && f.calls[1].kind==TK::defaults && f.calls[1].function==0x80023940,"exact default body reached without null layout probe call");
    check(meta->class_flags==9 && meta->flag_1a==1 && meta->native_size==0x10 && meta->field_callback==0x80023920,"original stores before field callback preserved");
    check(!meta->fields && !invocation.completed(),"unimplemented field body does not become empty registered schema");
    rejects([&]{invocation.result();},"pending registration cannot expose result");
}
void lazy_getter(){
    auto state=std::make_shared<IgbThreadRegistrationState>();IgbThreadMetadataInvocation unknown(state);
    check(!unknown.resume().completed,"unrepresented global remains pending");
    const auto ready=metadata(4);state->metadata=ready;
    check(unknown.resume().completed && unknown.result()==ready,"ready metadata needs no registration services");
    state->metadata=metadata(0);check(unknown.resume().completed && unknown.result()==ready,"completed getter retains result");
    IgbThreadMetadataInvocation missing({});rejects([&]{missing.resume();},"null native cell owner faults at reached read");
    check(missing.faulted(),"getter cell fault recorded");

    for(const auto flags:{0u,1u,2u,8u,0xffffffffu}){
        Fixture f;f.state->metadata=metadata(flags);IgbThreadMetadataInvocation getter(f.state);
        const auto result=getter.resume(f.callbacks);
        check(result.completed==((flags&4u)!=0),"only ready bit gates the lazy full getter");
    }
    Fixture f;auto replacement=metadata(4);f.state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};unsigned selections=0,polls=0;
    f.callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){
        check(call.kind==CK::invoke_function && call.function==0x800238a8,"getter calls actual wrapper body");
        ++selections;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{
            if(++polls==1)return MessengerCallResult{false,0,"prepared registration"};
            f.state->metadata=replacement;return MessengerCallResult{true,0,{}};};return op;
    };
    IgbThreadMetadataInvocation getter(f.state);check(!getter.resume(f.callbacks).completed,"lazy registration pauses");
    f.state->metadata=metadata(4);f.callbacks->factory={};
    check(getter.resume(f.callbacks).completed && getter.result()==replacement && selections==1 && polls==2,"getter retains registration then reloads canonical result");

    Fixture reload;reload.state->metadata=metadata(0);unsigned calls=0;
    reload.callbacks->factory=[&](const IgbRegistrationCallbacksCall&){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
        op->resume=[&]{++calls;reload.state->metadata.reset();return MessengerCallResult{true,0,{}};};return op;};
    IgbThreadMetadataInvocation physical(reload.state);check(!physical.resume(reload.callbacks).completed && calls==1,"unknown final cache pauses after completed registration");
    reload.state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(physical.resume(reload.callbacks).completed && !physical.result() && calls==1,"known null final return is preserved without re-registration");
}
void adapters_and_lifetime(){
    Fixture f;publish_original_igb_metadata_pool_handle(*f.types);f.state->metadata=metadata(4);
    auto adapter=make_native_igb_thread_type_factory(f.state,f.callbacks,f.types);
    for(const auto address:{0x80023844u,0x80023920u}){
        IgbTypeRegistrationCall call;call.kind=TK::get_parent;call.function=address;
        auto op=adapter(call);check(op && op->resume().completed && op->metadata_result==*f.state->metadata,"exact getter adapter returns canonical owner");
    }
    IgbTypeRegistrationCall registration;registration.kind=TK::register_parent;registration.function=0x800238a8;
    auto op=adapter(registration);check(op && op->resume().completed && !op->callbacks_result,"parent registration adapter returns actual deferred-list result");
    registration.function=0x80023940;check(!adapter(registration),"unknown field/default function stays unbound");
    IgbRegistrationCallbacksCall wrapper;wrapper.kind=CK::invoke_function;wrapper.function=0x80023880;
    auto wrap=f.callbacks->factory(wrapper);check(wrap && wrap->resume().completed,"exact wrapper composes native runner and registrar");
    wrapper.function=0x80023a30;check(!f.callbacks->factory(wrapper),"thread activation remains explicit");

    Fixture held;held.state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    auto selected=held.callbacks->factory(IgbRegistrationCallbacksCall{CK::invoke_function,0x800238a8});
    check(!selected->resume().completed,"selected native registration can pause");
    auto weak_state=std::weak_ptr<IgbThreadRegistrationState>(held.state);auto weak_types=std::weak_ptr<IgbTypeRegistrationServices>(held.types);
    held.state.reset();held.types.reset();held.callbacks.reset();
    check(!weak_state.expired() && !weak_types.expired(),"selected operation retains services and canonical slot while pending");
    selected.reset();check(weak_state.expired() && weak_types.expired(),"weak adapter avoids permanent service cycle");

    auto bootstrap_factory=make_native_igb_bootstrap_pool_factory(f.pools);
    IgbBootstrapCall bootstrap_call;bootstrap_call.kind=IgbBootstrapServiceKind::pool_for_handle;bootstrap_call.handle=5;
    auto pool_op=bootstrap_factory(bootstrap_call);
    check(pool_op && pool_op->resume().completed && pool_op->pool_result==f.pool,"bootstrap pool adapter preserves actual handle5 owner");
    bootstrap_call.kind=IgbBootstrapServiceKind::allocation_pool;
    check(!bootstrap_factory(bootstrap_call),"bootstrap adapter does not replace distinct allocation-pool lookup");

    Fixture expired;auto weak=std::weak_ptr<IgbThreadRegistrationState>(expired.state);
    auto weak_adapter=make_native_igb_thread_type_factory(expired.state,expired.callbacks,expired.types);
    expired.state.reset();check(weak.expired(),"adapter alone does not own canonical state");
    registration.function=0x800238a8;rejects([&]{weak_adapter(registration);},"expired adapter binding fails explicitly");
}
void faults(){
    Fixture f;publish_original_igb_metadata_pool_handle(*f.types);f.state->metadata=metadata(4);
    IgbThreadTypeRegistrationInvocation invocation;f.manager->original_pool_access_bound=false;
    f.pools->factory=[&](const IgbFilePoolsCall&){auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[&]{
        try{invocation.resume(f.state,f.callbacks,f.types);}catch(const IgbThreadRegistrationError&){}
        return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{invocation.resume(f.state,f.callbacks,f.types);},"swallowed reentry still poisons registration");
    check(invocation.faulted(),"registration records permanent fault");
    rejects([&]{invocation.resume(f.state,f.callbacks,f.types);},"faulted registration cannot replay");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_thread_registration_tests");
    exact_arguments();reached_cold_registration();ready_and_in_progress();field_boundary();lazy_getter();adapters_and_lifetime();faults();
    std::cout<<"igb_thread_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Thread registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
