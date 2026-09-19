#include "dw4/igb_file_factory.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbFileFactoryServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
SystemStageString text(std::string value){return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<ModelSceneObject> object(){return std::make_shared<ModelSceneData>();}
std::shared_ptr<IgbRuntimeMetaType> metadata(){
    auto meta=std::make_shared<IgbRuntimeMetaType>();meta->name=text("Prepared file metadata");meta->factory_redirect=0;
    meta->allocation_prefix=0;meta->default_pool_name=SystemStageString{};meta->native_size=0x134;
    meta->type_word=0x804724c4;meta->instance_count=0;meta->instances=std::shared_ptr<IgbObjectInstanceList>{};
    meta->fields=std::make_shared<IgbDecodeFieldRegistry>();meta->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();return meta;
}
// Explicit zero-field metadata, allocator, registration and virtual endpoints.
// This fixture exercises real caller/init orchestration, not file defaults.
struct Fixture {
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(),base=metadata();
    std::shared_ptr<IgbFileFactoryServices> services=std::make_shared<IgbFileFactoryServices>();
    std::shared_ptr<ModelSceneObject> pool=object(),named=object();
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbObjectStorage> storage=std::make_shared<IgbObjectStorage>();
    std::vector<K> trace;
    std::vector<IgbObjectInitCall> initialization;
    std::function<void(const IgbFileFactoryCall&,IgbFileFactoryOperation&)> after;
    std::function<void(const IgbFileFactoryCall&,std::shared_ptr<IgbFileFactoryOperation>&)> select;
    Fixture(){
        storage->original_address=0x10000000;storage->bytes=std::make_shared<IgbScratchBuffer>();
        storage->bytes->bytes.resize(0x400);storage->bytes->initialized.resize(0x400);storage->bindings.push_back({0,file});
        services->file_metadata=meta;services->initializer=std::make_shared<IgbObjectInitServices>();
        services->initializer->runtime=std::make_shared<IgbRuntimeTypesServices>();
        auto registry=std::make_shared<IgbRuntimeTypeRegistry>();registry->enabled=1;registry->field_initialization_enabled=1;registry->vtable_offset=0;
        services->initializer->runtime->singleton=registry;services->initializer->object_metadata=base;
        services->initializer->factory=[this](const IgbObjectInitCall& call){
            auto operation=std::make_shared<IgbObjectInitOperation>();operation->resume=[this,call,out=operation.get()]{
                initialization.push_back(call);
                if(call.kind==IgbObjectInitServiceKind::object_virtual && call.virtual_slot==0x58)out->metadata_result=meta;
                return MessengerCallResult{true,0,{}};
            };return operation;
        };
        services->factory=[this](const IgbFileFactoryCall& call){
            auto operation=std::make_shared<IgbFileFactoryOperation>();operation->resume=[this,call,out=operation.get()]{
                trace.push_back(call.kind);
                switch(call.kind){
                case K::register_file_type:break;
                case K::redirect_type:out->metadata_result=meta;break;
                case K::named_pool:out->object_result=named;break;
                case K::current_pool:out->object_result=pool;break;
                case K::allocate:out->allocation_result=storage;break;
                }
                if(after)after(call,*out);return MessengerCallResult{true,0,{}};
            };
            if(select)select(call,operation);return operation;
        };
    }
};
void registration_and_metadata(){
    Fixture f;f.services->factory={};IgbFileFactoryInvocation missing(f.pool);
    const auto pending=missing.resume(f.services);
    check(!pending.completed && pending.diagnostic.find("8002F178")!=std::string::npos,"Existing metadata incorrectly skipped registration.");
    check(!f.file->igb_decode && f.initialization.empty(),"Missing registration invented file construction.");
    rejects([&]{missing.result();},"Pending factory exposed a result.");
    Fixture late;late.services->file_metadata.reset();unsigned calls{};
    late.after=[&](const auto& c,auto&){if(c.kind==K::register_file_type)++calls;};
    IgbFileFactoryInvocation call(late.pool);check(!call.resume(late.services).completed && calls==1,"Missing metadata-global binding did not pause after registration.");
    late.services->file_metadata=late.meta;
    check(call.resume(late.services).completed && call.result()==late.file && calls==1,"Late metadata binding replayed registration or failed initializer.");
    check(!late.file->igb_decode,"Caller manufactured a decoder while prepared field callbacks were empty.");
    Fixture replaced;auto abstract=metadata();abstract->flag_1a=1;
    replaced.after=[&](const auto& c,auto&){if(c.kind==K::register_file_type)replaced.services->file_metadata=abstract;};
    IgbFileFactoryInvocation live(replaced.pool);check(live.resume(replaced.services).completed && !live.result() && replaced.initialization.empty(),"Wrapper did not reload metadata after registration.");
    Fixture null;null.services->file_metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbFileFactoryInvocation bad(null.pool);
    rejects([&]{bad.resume(null.services);},"Represented null metadata-global silently paused.");
    check(bad.faulted() && null.trace==std::vector<K>{K::register_file_type},"Null metadata fault changed reached registration prefix.");
}
void redirect_and_abstract(){
    for(unsigned flag:{0u,1u,2u,255u}){
        Fixture f;f.meta->flag_1a=static_cast<std::uint8_t>(flag);IgbReflectedFactoryInvocation call(f.meta,f.pool);
        check(call.resume(f.services).completed,"Abstract-byte corpus did not complete.");
        check(bool(call.result())==(flag!=1),"Abstract flag rejects other nonzero values.");
        check(f.trace.empty()==(flag==1),"Abstract rejection selected an allocator.");
    }
    Fixture f;const auto original=metadata(),destination=f.meta;original->flag_1a=1;original->factory_redirect=0x12345678;
    const auto pool=f.pool;bool ready{};unsigned selected{},resumes{};
    f.select=[&](const auto& c,auto& op){if(c.kind==K::redirect_type){
        check(c.metadata==original && c.redirect==0x12345678,"Redirect call lost captured identity/tag.");++selected;
        op->metadata_result=destination;op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared redirect."};};
    }};
    f.after=[&](const auto& c,auto&){if(c.kind==K::allocate)check(c.pool==pool,"Redirect recursion changed original incoming pool.");};
    IgbReflectedFactoryInvocation call(original,pool);check(!call.resume(f.services).completed && f.initialization.empty(),"Redirect failed to pause before abstract flag.");
    original->factory_redirect=0;f.pool=object();ready=true;
    check(call.resume(f.services).completed && call.result()==f.file && selected==1 && resumes==2,"Redirect continuation was restarted or original abstract flag used.");
    Fixture absent;absent.meta->factory_redirect.reset();absent.meta->flag_1a=1;
    IgbReflectedFactoryInvocation unknown(absent.meta,absent.pool);check(!unknown.resume(absent.services).completed,"Unrepresented redirect field was skipped by abstract gate.");
    absent.meta->factory_redirect=0;check(unknown.resume(absent.services).completed && !unknown.result(),"Late represented-null redirect did not reach abstract gate.");
    Fixture cycle;cycle.meta->factory_redirect=9;IgbReflectedFactoryInvocation looping(cycle.meta,cycle.pool);
    rejects([&]{looping.resume(cycle.services);},"Infinite metadata redirect was not bounded.");check(looping.faulted(),"Redirect bound did not retain fault state.");
}
void pool_selection(){
    Fixture explicit_pool;explicit_pool.meta->default_pool_name.reset();
    IgbReflectedFactoryInvocation direct(explicit_pool.meta,explicit_pool.pool);
    check(direct.resume(explicit_pool.services).completed && explicit_pool.trace==std::vector<K>{K::allocate},"Supplied pool read unrepresented default name.");
    for(const auto name:{SystemStageString{},text(""),text(std::string("\0tail",5)),text("Named"),text(std::string(1,char(0xff)))}){
        Fixture f;f.meta->default_pool_name=name;IgbReflectedFactoryInvocation call(f.meta,{});
        check(call.resume(f.services).completed,"Pool-name corpus failed.");
        const bool nonempty=name && !name->empty() && name->front()!=0;
        check(f.trace==std::vector<K>{nonempty?K::named_pool:K::current_pool,K::allocate},"Pool-name zero/nonzero first-byte decision changed.");
    }
    Fixture fallback;fallback.meta->default_pool_name=text("Missing");fallback.named.reset();
    IgbReflectedFactoryInvocation call(fallback.meta,{});check(call.resume(fallback.services).completed,"Null named lookup did not fall back.");
    check(fallback.trace==std::vector<K>{K::named_pool,K::current_pool,K::allocate},"Named/current allocation order changed.");
    Fixture null;null.pool.reset();null.meta->allocation_prefix.reset();IgbReflectedFactoryInvocation reached(null.meta,{});
    check(!reached.resume(null.services).completed,"Null selected pool faulted before missing captured prefix was reached.");
    null.meta->allocation_prefix=0;rejects([&]{reached.resume(null.services);},"Represented null allocation pool did not fault.");
    check(null.trace==std::vector<K>{K::current_pool},"Null pool selected allocation virtual.");
}
void allocation_arithmetic_and_pending(){
    Fixture f;f.meta->allocation_prefix=16;f.meta->native_size=0xfffffff8;f.storage->bindings.front().offset=16;
    bool ready{};unsigned selected{},resumes{};
    f.select=[&](const auto& c,auto& op){if(c.kind==K::allocate){
        check(c.bytes==8 && c.pool==f.pool && c.metadata==f.meta,"Wrapped allocation size or argument capture changed.");++selected;
        op->allocation_result=f.storage;op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared allocation."};};
    }};
    IgbReflectedFactoryInvocation call(f.meta,f.pool);check(!call.resume(f.services).completed,"Pending allocation was completed.");
    f.meta->allocation_prefix=32;ready=true;f.services->factory={};
    check(call.resume(f.services).completed && call.result()==f.file && selected==1 && resumes==2,"Allocation reread changed prefix or selected operation.");
    check(!f.initialization.empty() && f.initialization.front().object.offset==16,"Initializer lost captured allocation adjustment.");
    check(call.resume().completed && resumes==2,"Completed factory replayed allocation.");
    Fixture empty;empty.storage.reset();IgbReflectedFactoryInvocation null(empty.meta,empty.pool);
    check(null.resume(empty.services).completed && !null.result() && empty.initialization.empty(),"Null allocation plus zero prefix reached initialization.");
    Fixture adjusted;adjusted.meta->allocation_prefix=16;adjusted.storage.reset();IgbReflectedFactoryInvocation invalid(adjusted.meta,adjusted.pool);
    rejects([&]{invalid.resume(adjusted.services);},"Null allocation plus nonzero prefix was treated as null result.");
    check(invalid.faulted(),"Invalid adjusted storage did not retain physical failure.");
    Fixture wrapped;wrapped.meta->allocation_prefix=16;wrapped.storage->original_address=0xfffffff0;
    IgbReflectedFactoryInvocation zero(wrapped.meta,wrapped.pool);
    check(zero.resume(wrapped.services).completed && !zero.result() && wrapped.initialization.empty(),"Wrapping adjusted pointer zero did not skip constructor.");
    Fixture pending_init;pending_init.services->initializer.reset();IgbReflectedFactoryInvocation paused(pending_init.meta,pending_init.pool);
    check(!paused.resume(pending_init.services).completed && pending_init.trace==std::vector<K>{K::allocate},"Missing initializer was treated as construction success.");
    rejects([&]{paused.result();},"Allocation alone exposed a typed completed file.");
}
void errors_and_lifetime(){
    Fixture f;f.select=[](const auto& c,auto& op){if(c.kind==K::allocate)op->resume=[]()->MessengerCallResult{throw std::runtime_error("Prepared allocation failure");};};
    IgbReflectedFactoryInvocation call(f.meta,f.pool);rejects([&]{call.resume(f.services);},"Endpoint failure did not escape.");
    check(call.faulted(),"Endpoint fault was not retained.");rejects([&]{call.resume(f.services);},"Faulted allocation prefix replayed.");
    Fixture recursive;IgbFileFactoryInvocation wrapper(recursive.pool);
    recursive.after=[&](const auto& c,auto&){if(c.kind==K::register_file_type)try{wrapper.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{wrapper.resume(recursive.services);},"Swallowed recursive wrapper call did not invalidate outer invocation.");
    Fixture abandoned;const std::weak_ptr<IgbObjectStorage> observed=abandoned.storage;
    abandoned.services->initializer.reset();
    {
        IgbReflectedFactoryInvocation pending(abandoned.meta,abandoned.pool);
        check(!pending.resume(abandoned.services).completed,"Abandonment fixture did not reach initializer pause.");
        abandoned.storage.reset();
    }
    check(!observed.expired(),"Abandoning a constructed allocation prefix released its unfinished storage.");
    Fixture late;late.services->initializer.reset();const std::weak_ptr<IgbObjectStorage> late_owner=late.storage;
    {
        IgbReflectedFactoryInvocation pending(late.meta,late.pool);
        check(!pending.resume().completed,"Missing allocation binding unexpectedly completed.");
        check(!pending.resume(late.services).completed,"Late allocation did not pause at initializer.");
        late.storage.reset();
    }
    check(!late_owner.expired(),"Late service binding did not retain an abandoned allocation.");
}
void native_adapters(){
    Fixture f;const auto dependency=make_native_igb_file_factory(f.services);
    LoaderDependencyCall c;c.kind=LoaderDependencyServiceKind::create_igb_file;c.object=f.pool;
    auto op=dependency(c);check(op && op->resume().completed && op->file_result==f.file,"Dependency adapter did not perform actual factory/init chain.");
    c.kind=LoaderDependencyServiceKind::intern_name;check(!dependency(c),"Dependency file factory consumed unrelated endpoint.");
    Fixture ordinary;const auto request=make_native_igb_request_file_factory(ordinary.services);
    LoaderRequestCall q;q.kind=LoaderRequestServiceKind::create_igb_file;q.object=ordinary.pool;
    const auto made=request(q);check(made && made->resume().completed && made->file_result==ordinary.file,"Ordinary request adapter did not use same canonical factory.");
    auto services=std::make_shared<IgbFileFactoryServices>();const auto expired=make_native_igb_file_factory(services);
    const std::weak_ptr<IgbFileFactoryServices> owner=services;services.reset();check(owner.expired(),"Factory adapter strongly retained recursive service graph.");
    c.kind=LoaderDependencyServiceKind::create_igb_file;rejects([&]{expired(c);},"Expired services were silently replaced with factory defaults.");
}
void concrete_file_virtuals(){
    Fixture f;auto pools=std::make_shared<IgbFilePoolsServices>();pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
    pools->memory_pools->selector=0;const auto registry=make_original_igb_memory_pool_registry();
    registry->original_pool_access_bound=true;registry->pools[2]=f.pool;pools->memory_pools->fallback=registry;
    unsigned delegated{};const auto native=make_native_igb_file_init_factory(f.services,pools,
        [&](const IgbObjectInitCall&)->std::shared_ptr<IgbObjectInitOperation>{++delegated;return {};});
    IgbObjectInitCall call;call.kind=IgbObjectInitServiceKind::object_virtual;call.vtable_word=0x804724c4;
    for(const auto slot:{0x0cu,0x24u,0x2cu}){
        call.virtual_slot=slot;const auto op=native(call);
        check(op && op->resume().completed,"Verified BLR binding incorrectly required object/global state.");
    }
    call.virtual_slot=0x58;f.services->file_metadata.reset();const auto getter=native(call);
    check(getter && !getter->resume().completed,"Absent metadata-global binding was defaulted.");
    auto changed=metadata();f.services->file_metadata=changed;
    check(getter->resume().completed && getter->metadata_result==changed,"Concrete getter did not reload canonical metadata.");
    f.services->file_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(native(call)->resume().completed,"Concrete getter incorrectly dereferenced returned null metadata.");
    f.services->file_metadata=f.meta;call.virtual_slot=0x34;call.object.file=f.file;
    const auto post=native(call);check(post && post->resume().completed,"Concrete post-default callback did not run native pool caller.");
    check(f.file->pool_118==f.pool && f.file->pool_11c==f.pool && f.file->pool_120==f.pool && f.pool->reference_word==4,
        "Native post-default adapter lost independent pool assignments/references.");
    call.virtual_slot=0x74;check(!native(call) && delegated==1,"Unknown file virtual was treated as no-op.");
    call.virtual_slot=0x58;call.vtable_word=0x12345678;check(!native(call) && delegated==2,"Unknown vtable was treated as igIGBFile.");
    // The actual initializer now uses the native getter/BLR/post-default bindings.
    Fixture composed;auto p=std::make_shared<IgbFilePoolsServices>();p->memory_pools=pools->memory_pools;
    composed.services->initializer->factory=make_native_igb_file_init_factory(composed.services,p,composed.services->initializer->factory);
    IgbFileFactoryInvocation complete(composed.pool);
    check(complete.resume(composed.services).completed && complete.result()==composed.file && composed.file->pool_118==f.pool,
        "Actual initializer did not compose the recovered concrete file virtuals.");
    check(!composed.file->igb_decode,"Concrete virtual adapter invented field defaults/decoder state.");
    Fixture fallback;fallback.meta->default_pool_name=text("Missing");fallback.named.reset();
    fallback.services->factory=make_native_igb_factory_pool_factory(pools,fallback.services->factory);
    fallback.after=[&](const auto& c,auto&){if(c.kind==K::allocate)check(c.pool==f.pool,"Native current-pool fallback selected a prepared replacement.");};
    IgbReflectedFactoryInvocation native_fallback(fallback.meta,{});
    check(native_fallback.resume(fallback.services).completed && native_fallback.result()==fallback.file,
        "Reflected caller did not compose actual80058C80 fallback after named miss.");
    check(fallback.trace==std::vector<K>{K::named_pool,K::allocate},"Current-pool adapter delegated its recovered endpoint.");
}
}
int main(){try{registration_and_metadata();redirect_and_abstract();pool_selection();allocation_arithmetic_and_pending();errors_and_lifetime();native_adapters();concrete_file_virtuals();
    std::cout<<"IGB file factory: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB file factory after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
