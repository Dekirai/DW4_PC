#include "dw4/igb_object_registration.hpp"
#include "dw4/igb_thread_registration.hpp"

#include <iostream>

namespace {
using namespace dw4;
using TK=IgbTypeRegistrationServiceKind;
using CK=IgbRegistrationCallbacksServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<IgbRuntimeMetaType> meta(std::uint32_t flags){auto value=std::make_shared<IgbRuntimeMetaType>();value->class_flags=flags;return value;}
struct Fixture {
    std::shared_ptr<IgbObjectRegistrationServices> services=std::make_shared<IgbObjectRegistrationServices>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    IgbTypeRegistrationFactory remaining;
    std::vector<IgbTypeRegistrationCall> calls;
    Fixture(){
        services->metadata=std::make_shared<IgbRegistrationMetaSlot>();
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->vtable_offset=0;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;
        manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->factory=make_native_igb_object_registration_function_factory(services);
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,
            make_native_igb_object_registration_type_factory(services,[this](const IgbTypeRegistrationCall& c){
                calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
    }
};
void arguments_and_plain_getters(){
    Fixture f;const auto args=igb_object_type_registration_arguments(f.services->metadata);
    check(args.metadata_slot==f.services->metadata && *args.name=="igObject" && args.native_size==8 && args.flag==0,"igObject arguments do not preserve canonical name/size/slot.");
    check(args.parent_registration==0x80066a60 && args.parent_getter==0x80066aa4 && args.field_callback==0x80066b74,"igObject parent and getter addresses differ.");
    check(args.layout_probe==0x80066aac && args.defaults_callback==0x80066b7c && !args.extra_callbacks && args.trailing_callback==0,"igObject callback table was fabricated.");
    rejects([]{igb_object_type_registration_arguments({});},"Null canonical metadata cell was accepted.");
    IgbObjectMetadataInvocation unknown(f.services->metadata);
    check(!unknown.resume().completed,"Unrepresented global should remain pending.");
    *f.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(unknown.resume().completed && !unknown.result(),"Plain getter should return represented null without allocating.");
    const auto unready=meta(0);*f.services->metadata=unready;
    check(unknown.resume().completed && !unknown.result(),"Completed getter reloaded replacement global.");
    IgbObjectMetadataInvocation owner(f.services->metadata);
    check(owner.resume().completed && owner.result()==unready && unready->class_flags==0,"Plain getter registered or required ready flags.");
    const auto adapter=make_native_igb_object_registration_type_factory(f.services);
    for(const auto address:{0x800237d0u,0x80066b74u,0x80042decu}){
        IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=address;const auto op=adapter(c);
        check(op && op->resume().completed && op->metadata_result==unready,"A plain getter alias lost its canonical owner.");
    }
    IgbObjectMetadataInvocation missing({});rejects([&]{missing.resume();},"Missing canonical cell did not fault.");
    check(missing.faulted() && !missing.completed(),"Invalid global read lost fault state.");
    IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=0x80066aa4;
    const auto null_parent=make_native_igb_object_registration_type_factory({})(c);
    check(null_parent && null_parent->resume().completed && !null_parent->metadata_result,"Literal-null parent getter required unrelated services.");
}
void root_allocation(){
    Fixture f;f.services->types->metadata_pool_handle.reset();IgbObjectRootRegistrationInvocation call;
    check(!call.resume(f.services).completed && f.calls.empty(),"Root callback skipped missing metadata handle.");
    f.services->types->metadata_pool_handle=5;f.manager->original_pool_access_bound=false;
    unsigned selections{},polls{},allocations{};bool pool_ready{},allocation_ready{};
    f.services->pools->factory=[&](const IgbFilePoolsCall& c){
        ++selections;check(c.handle==5,"Root callback changed captured metadata handle.");auto op=std::make_shared<IgbFilePoolsOperation>();
        op->resume=[&,out=op.get()]{++polls;if(!pool_ready)return MessengerCallResult{false,0,"prepared pool"};out->object_result=f.pool;return MessengerCallResult{true,0,{}};};return op;
    };
    check(!call.resume(f.services).completed && selections==1,"Selected pool did not suspend.");
    f.services->types->metadata_pool_handle=7;pool_ready=true;
    auto list=std::make_shared<IgbRegistrationCallbackList>();list->count=11;list->capacity=19;
    list->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(19,0x80000004);
    auto old=list->backing;std::weak_ptr<IgbRegistrationCallbackList::Storage> old_weak=old;old.reset();
    f.remaining=[&](const IgbTypeRegistrationCall& c){
        check(c.kind==TK::create_callbacks && c.pool==f.pool && c.bytes==12,"Root callback changed physical allocation arguments.");
        ++allocations;auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,out=op.get()]{
            if(!allocation_ready)return MessengerCallResult{false,0,"prepared allocation"};out->callbacks_result=list;return MessengerCallResult{true,0,{}};};return op;
    };
    check(!call.resume(f.services).completed && allocations==1 && list->count==11,"Pending allocation initialized callback header early.");
    f.remaining={};f.services->types->factory={};f.services->pools->factory={};allocation_ready=true;
    check(call.resume(f.services).completed && call.result()==list && allocations==1 && selections==1 && polls==2,"Root callback replayed lookup/allocation after suspension.");
    check(list->count==0 && list->capacity==0 && !list->backing && !old_weak.expired(),"Native3E848 did not preserve exact header stores and overwritten backing lifetime.");
    check(call.resume().completed,"Completed root callback replayed.");
    Fixture null;null.remaining=[](const IgbTypeRegistrationCall&){auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[]{return MessengerCallResult{true,0,{}};};return op;};
    IgbObjectRootRegistrationInvocation null_result;check(null_result.resume(null.services).completed && !null_result.result(),"Null allocation did not skip callback constructor.");
    Fixture teardown;auto dirty=std::make_shared<IgbRegistrationCallbackList>();dirty->count=7;
    struct OnDrop {std::function<void()> action;~OnDrop(){action();}};
    teardown.remaining=[&](const IgbTypeRegistrationCall&){auto token=std::make_shared<OnDrop>();token->action=[dirty]{dirty->count=42;dirty->capacity=99;};
        auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[token,dirty,out=op.get()]{out->callbacks_result=dirty;return MessengerCallResult{true,0,{}};};return op;};
    IgbObjectRootRegistrationInvocation after;check(after.resume(teardown.services).completed && dirty->count==0 && dirty->capacity==0,"Constructor ran before completed allocator teardown.");
}
void probe(){
    auto runtime=std::make_shared<IgbRuntimeTypesServices>();IgbObjectLayoutProbeInvocation call;
    check(!call.resume(runtime).completed,"Unrepresented ArkCore did not suspend.");
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();runtime->singleton=core;
    check(!call.resume(runtime).completed,"Unrepresented captured offset did not suspend.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;runtime->singleton=replacement;core->vtable_offset=4;
    check(call.resume(runtime).completed && call.value()==1,"Probe did not retain captured core while waiting for its live offset.");
    core->vtable_offset=0;check(call.resume().value==1,"Completed probe reread offset.");
    for(const auto offset:{0u,4u}){
        IgbObjectLayoutProbeInvocation value;replacement->vtable_offset=offset;
        check(value.resume(runtime).completed && value.value()==(offset==0?0x804717f0u:1u),"Original base constructor probe words differ.");
    }
    for(const auto offset:{1u,2u,3u,5u,7u,8u,0xfffffffcu,0xffffffffu}){
        IgbObjectLayoutProbeInvocation bad;replacement->vtable_offset=offset;
        rejects([&]{bad.resume(runtime);},"Unbound stack offset was accepted as a vtable.");
        check(bad.faulted() && !bad.completed(),"Malformed probe offset did not poison invocation.");
        rejects([&]{bad.value();},"Faulted probe exposed a completed value.");
    }
    runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbObjectLayoutProbeInvocation null;
    rejects([&]{null.resume(runtime);},"Represented null ArkCore did not fault at+394.");
}
void body_and_adapters(){
    for(const auto flags:{4u,8u,12u,0xffffffffu}){
        Fixture f;const auto value=meta(flags);*f.services->metadata=value;IgbObjectTypeRegistrationInvocation call;
        check(call.resume(f.services).completed && !call.result() && value->class_flags==flags && f.calls.empty(),"Native registrar ready/busy path differs.");
        check(!f.services->callbacks->prerequisites,"igObject body invented a prerequisite once guard.");
    }
    Fixture f;const auto value=meta(1);*f.services->metadata=value;
    // Prepared name assignment isolates wrapper/probe integration; the separate
    // cold integration executes the real interner and metadata-name caller.
    f.remaining=[](const IgbTypeRegistrationCall& c){if(c.kind!=TK::set_name)return std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[c]{c.metadata->name=c.name;return MessengerCallResult{true,0,{}};};return op;};
    IgbObjectTypeRegistrationInvocation call;check(!call.resume(f.services).completed,"Unknown specialized igObject defaults were reported complete.");
    check(value->class_flags==9 && value->native_size==8 && value->field_callback==0x80066b74 && value->type_word==0x804717f0 && *value->name=="igObject","Native wrapper/probe failed to publish its exact prefix.");
    check(f.calls.back().kind==TK::defaults && f.calls.back().function==0x80066b7c,"igObject did not reach its actual specialized defaults callback.");
    rejects([&]{call.result();},"Pending fields exposed completed registration.");
    Fixture ready;*ready.services->metadata=meta(4);IgbRegistrationCallbacksCall wrapper;wrapper.kind=CK::invoke_function;wrapper.function=0x80066ae0;
    const auto op=ready.services->callbacks->factory(wrapper);check(op && op->resume().completed,"66AE0 did not execute native66188 with66B08.");
    IgbTypeRegistrationCall unrelated;unrelated.kind=TK::defaults;unrelated.function=0x80066b7c;
    check(!make_native_igb_object_registration_type_factory(ready.services)(unrelated),"Adapter swallowed an unrecovered defaults body.");
    Fixture held;held.services->types->metadata_pool_handle.reset();*held.services->metadata=meta(4);
    auto selected=held.services->callbacks->factory({CK::invoke_function,0x80066b08});
    check(!selected->resume().completed,"Selected body did not pause at missing handle.");
    std::weak_ptr<IgbObjectRegistrationServices> weak=held.services;held.services.reset();
    check(!weak.expired(),"Selected operation lost its services.");selected.reset();check(weak.expired(),"Weak adapters formed an ownership cycle.");
    auto expired=std::make_shared<IgbObjectRegistrationServices>();auto adapter=make_native_igb_object_registration_type_factory(expired);expired.reset();
    unrelated.kind=TK::register_parent;unrelated.function=0x80066b08;rejects([&]{adapter(unrelated);},"Expired services were silently reconstructed.");
    Fixture recursive;IgbObjectRootRegistrationInvocation root;
    recursive.remaining=[&](const IgbTypeRegistrationCall&){auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[&]{
        try{root.resume(recursive.services);}catch(const std::runtime_error&){}return MessengerCallResult{true,0,{}};};return operation;};
    rejects([&]{root.resume(recursive.services);},"Caught recursive misuse allowed the outer root call to finish.");
    check(root.faulted() && !root.completed(),"Reentry lost persistent fault state.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_object_registration_tests");
    arguments_and_plain_getters();root_allocation();probe();body_and_adapters();
    std::cout<<"igb_object_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Object registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
