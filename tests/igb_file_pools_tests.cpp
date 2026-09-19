#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_stream.hpp"

#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& fn,const char* reason) {
    ++checks;try {fn();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
std::shared_ptr<ModelSceneObject> object(std::uint32_t refs=1) {
    auto result=std::make_shared<ModelSceneData>();result->reference_word=refs;return result;
}
std::shared_ptr<IgbMemoryPoolRegistry> manager() {
    auto result=make_original_igb_memory_pool_registry();result->original_pool_access_bound=true;
    for(auto& pool:result->pools)pool=std::shared_ptr<ModelSceneObject>{};return result;
}
void flags(const std::shared_ptr<IgbMemoryPoolRegistry>& owner,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)owner->inline_bytes->bytes[0x1c0+i]=static_cast<std::uint8_t>(value>>(24-i*8));
}
struct Fixture {
    std::shared_ptr<IgbFilePoolsServices> services=std::make_shared<IgbFilePoolsServices>();
    std::shared_ptr<IgbMemoryPoolRegistry> owner=manager();
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    Fixture() {
        services->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->memory_pools->selector=0;services->memory_pools->fallback=owner;
    }
    std::shared_ptr<ModelSceneObject> current() {
        IgbCurrentPoolInvocation call;check(call.resume(services).completed,"Represented current pool did not complete.");return call.result();
    }
};
struct OnDestroy {
    std::function<void()> action;
    ~OnDestroy() {if(action)action();}
};
void selection() {
    Fixture f;auto pool=object();f.owner->pools[2]=pool;
    check(f.current()==pool && pool->reference_word==1,"Current-pool lookup changed owner references.");
    auto first=manager(),second=manager();first->pools[2]=object();second->pools[2]=object();
    f.services->memory_pools->active={first,second,f.owner};
    for(std::int32_t index=1;index<=3;++index) {
        f.services->memory_pools->selector=index;
        check(f.current()==*(*f.services->memory_pools->active[static_cast<std::size_t>(index-1)])->pools[2],"Selector chose the wrong manager.");
    }
    for(auto selector:{-1,4,0x7fffffff}) {
        f.services->memory_pools->selector=selector;IgbCurrentPoolInvocation call;
        rejects([&]{call.resume(f.services);},"Invalid selector fabricated a current pool.");
        check(call.faulted(),"Reached null manager did not poison its lookup.");
    }
    IgbCurrentPoolInvocation missing;check(!missing.resume().completed,"Missing selector became zero.");
    f.services->memory_pools->selector=2;f.services->memory_pools->active[1].reset();
    check(!missing.resume(f.services).completed,"Unrepresented selected entry became null.");
    f.services->memory_pools->selector=0;f.services->memory_pools->active[1]=second;
    check(missing.resume(f.services).completed && missing.result()==*second->pools[2],"Pending owner read reloaded its captured selector.");
    IgbCurrentPoolInvocation field;f.owner->pools[2].reset();
    check(!field.resume(f.services).completed,"Unrepresented pool slot became null.");
    f.services->memory_pools->fallback=first;f.owner->pools[2]=pool;
    check(field.resume(f.services).completed && field.result()==pool,"Pending pool read replaced its captured manager.");
    f.owner->pools[2]=object();check(field.resume().completed && field.result()==pool,"Completed getter performed fresh reads.");
    check(!make_original_igb_memory_pool_registry()->pools[2],"Name-only registry construction invented initialized pool ownership.");
}
void fallback_and_flags() {
    Fixture f;auto first=manager(),second=manager(),third=manager();auto pool=object();first->pools[2]=pool;
    f.services->memory_pools->selector=3;f.services->memory_pools->active={first,second,third};
    check(f.current()==pool,"Pool fallback did not traverse prior managers.");
    f.services->memory_pools->active[1]=third;
    check(!f.current(),"Identical preceding manager should stop fallback.");
    f.services->memory_pools->active[1]=std::shared_ptr<IgbMemoryPoolRegistry>{};IgbCurrentPoolInvocation null;
    rejects([&]{null.resume(f.services);},"Null preceding manager did not fault at virtual dispatch.");
    f.services->memory_pools->active[1]=second;flags(third,0x04000000u);
    IgbCurrentPoolInvocation pending;
    check(!pending.resume(f.services).completed,"Missing pool preparation was replaced by a successful status leaf.");
    unsigned prepared{};
    f.services->factory=[&](const IgbFilePoolsCall& request) {
        check(request.kind==IgbFilePoolsServiceKind::prepare_pool && request.manager==third && request.handle==2,
            "Pool preparation lost its captured owner or handle.");
        auto op=std::make_shared<IgbFilePoolsOperation>();op->object_result=object();
        op->resume=[&] {++prepared;return MessengerCallResult{true,0xdeadbeefu,{}};};return op;
    };
    check(pending.resume(f.services).completed && !pending.result() && prepared==1,"Preparation result replaced the reloaded physical pool slot.");
    check(!f.current(),"Null prepared pool incorrectly fell through to previous managers.");
    IgbCurrentPoolInvocation reloaded;f.services->factory={};
    check(!reloaded.resume(f.services).completed,"Missing preparation unexpectedly completed.");
    f.services->factory=[&](const IgbFilePoolsCall&) {
        auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[&] {third->pools[2]=pool;return MessengerCallResult{true,0,{}};};return op;
    };
    check(reloaded.resume(f.services).completed && reloaded.result()==pool,"Preparation did not reload the captured pool slot.");
    third->pools[2]=std::shared_ptr<ModelSceneObject>{};flags(third,0);
    third->pools[2].reset();IgbCurrentPoolInvocation changed;
    check(!changed.resume(f.services).completed,"Missing slot unexpectedly completed.");
    third->pools[2]=std::shared_ptr<ModelSceneObject>{};f.services->memory_pools->selector=1;
    check(changed.resume(f.services).completed && !changed.result(),"Fallback reused the original selector instead of its fresh read.");
    f.services->memory_pools->selector=3;flags(third,0x08000000u);
    check(f.current()==pool,"Wrong lazy flag bit suppressed fallback.");
    first->pools[2]=std::shared_ptr<ModelSceneObject>{};f.services->memory_pools->active={second,third,second};
    IgbCurrentPoolInvocation cycle;rejects([&]{cycle.resume(f.services);},"Malformed original fallback cycle escaped the native depth guard.");
}
void virtual_boundaries() {
    Fixture f;f.owner->original_pool_access_bound=false;IgbCurrentPoolInvocation call;
    check(!call.resume(f.services).completed,"Unknown manager virtual fabricated a result.");
    auto original=f.owner,replacement=manager();auto pool=object();unsigned factories{},callbacks{};
    f.services->factory=[&](const IgbFilePoolsCall& request) {
        ++factories;check(request.kind==IgbFilePoolsServiceKind::pool_virtual && request.manager==original && request.handle==2,
            "Unknown virtual lost its captured owner or handle.");
        auto op=std::make_shared<IgbFilePoolsOperation>();op->object_result=pool;
        op->resume=[&]{++callbacks;return MessengerCallResult{callbacks>1,0,"wait"};};return op;
    };
    f.services->memory_pools->fallback=replacement;
    check(!call.resume(f.services).completed && factories==1,"Pending virtual did not suspend.");
    f.services->factory={};check(call.resume().completed && call.result()==pool && factories==1 && callbacks==2,
        "Pending virtual recreated its operation or lost captured services.");
}
void publication() {
    for(auto refs:{0u,1u,0x007fffffu,0x00800000u,0x01800000u,0xffffffffu}) {
        Fixture f;auto pool=object(refs);f.owner->pools[2]=pool;
        IgbFilePoolsInvocation call(f.file);check(call.resume(f.services).completed,"Three pool publications did not complete.");
        check(f.file->pool_118==pool && f.file->pool_11c==pool && f.file->pool_120==pool && pool->reference_word==refs+3u,
            "Pool publications lost identity or full-word wrapping retain.");
        check(call.resume().completed && pool->reference_word==refs+3u,"Completed callback repeated retains.");
    }
    Fixture same;auto pool=object(0x01800001u);same.owner->pools[2]=pool;
    same.file->pool_118=pool;same.file->pool_11c=pool;same.file->pool_120=pool;
    IgbFilePoolsInvocation alias(same.file);check(alias.resume(same.services).completed && pool->reference_word==0x01800001u,
        "Same-owner assignment released before acquiring incoming identity.");
    Fixture null;auto old=object(0);null.file->pool_118=old;IgbFilePoolsInvocation empty(null.file);
    check(empty.resume(null.services).completed && !null.file->pool_118 && old->reference_word==0xffffffffu,
        "Null assignment lost wrapped old decrement.");
    Fixture bad;auto retained=object();bad.owner->pools[2]=retained;IgbFilePoolsInvocation no_file({});
    rejects([&]{no_file.resume(bad.services);},"Null file was accepted.");
    check(retained->reference_word==2,"Null file failed before reached current-pool retain.");
}
void destructor_prefixes() {
    Fixture f;auto first=object(),second=object(),third=object(),old=object(0xa0800001u),replaced=object(17);
    f.owner->pools[2]=first;f.file->pool_118=old;f.file->pool_11c=object(1);f.file->pool_120=object(1);
    auto captured_second=f.file->pool_11c,captured_third=f.file->pool_120;unsigned visits{};
    f.services->factory=[&](const IgbFilePoolsCall& request) {
        check(request.kind==IgbFilePoolsServiceKind::destroy_object,"Unexpected pool callback.");
        auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[&,request] {
            ++visits;
            if(visits==1) {
                check(request.object==old && old->reference_word==0xa0800000u && first->reference_word==2 && f.file->pool_118==old,
                    "First destructor lost retained/decremented unpublished prefix.");
                f.file->pool_118=replaced;f.owner->pools[2]=second;
            }else if(visits==2) {
                check(request.object==captured_second && f.file->pool_118==first && second->reference_word==2,"Second request reused a stale pool.");
                f.owner->pools[2]=third;
            }else check(request.object==captured_third && f.file->pool_11c==second && third->reference_word==2,"Third request reused a stale pool.");
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    IgbFilePoolsInvocation call(f.file);check(call.resume(f.services).completed && visits==3,"Destructor sequence did not complete.");
    check(f.file->pool_118==first && f.file->pool_11c==second && f.file->pool_120==third && replaced->reference_word==17,
        "Raw publication released a callback replacement or lost captured incoming pools.");
    Fixture pending;auto incoming=object(),previous=object(1);pending.owner->pools[2]=incoming;pending.file->pool_118=previous;
    IgbFilePoolsInvocation delayed(pending.file);check(!delayed.resume(pending.services).completed && previous->reference_word==0 && incoming->reference_word==2,
        "Missing destructor failed to retain its original prefix.");
    check(!delayed.resume(pending.services).completed && previous->reference_word==0 && incoming->reference_word==2,
        "Pending destructor replayed reference arithmetic.");
    unsigned completions{};pending.services->factory=[&](const IgbFilePoolsCall& request) {
        check(request.object==previous,"Delayed destructor changed its old owner.");auto op=std::make_shared<IgbFilePoolsOperation>();
        op->resume=[&]{++completions;return MessengerCallResult{true,0,{}};};return op;
    };
    check(delayed.resume(pending.services).completed && completions==1 && incoming->reference_word==4,"Delayed destructor did not continue exact remaining fields.");
}
void abandonment_and_reentry() {
    Fixture f;auto incoming=object(),old=object();const std::weak_ptr<ModelSceneObject> wi=incoming,wo=old;
    f.owner->original_pool_access_bound=false;f.file->pool_118=old;
    f.services->factory=[&](const IgbFilePoolsCall& request) {
        auto op=std::make_shared<IgbFilePoolsOperation>();
        if(request.kind==IgbFilePoolsServiceKind::pool_virtual) {op->object_result=incoming;op->resume=[] {return MessengerCallResult{true,0,{}};};}
        else op->resume=[&] {f.file->pool_118.reset();incoming.reset();old.reset();return MessengerCallResult{false,0,"wait"};};
        return op;
    };
    {IgbFilePoolsInvocation abandoned(f.file);check(!abandoned.resume(f.services).completed,"Prepared destructor should remain pending.");}
    check(!wi.expired() && !wo.expired(),"Abandonment destroyed retained incoming or pending destructor owners.");
    IgbFilePoolsInvocation restart(f.file);rejects([&]{restart.resume(f.services);},"Abandoned prefix restarted.");
    Fixture recursive;recursive.owner->original_pool_access_bound=false;IgbFilePoolsInvocation call(recursive.file);
    recursive.services->factory=[&](const IgbFilePoolsCall&) {
        auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[&] {
            try {call.resume(recursive.services);}catch(const std::runtime_error&) {}
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    rejects([&]{call.resume(recursive.services);},"Swallowed recursive failure completed initialization.");
    check(call.faulted(),"Recursive initialization did not poison its invocation.");
}
void completed_call_teardown() {
    Fixture current;flags(current.owner,0x04000000u);auto result=object();unsigned prepared{};
    current.services->factory=[&](const IgbFilePoolsCall& request) {
        check(request.kind==IgbFilePoolsServiceKind::prepare_pool,"Unexpected current-pool operation.");
        auto latch=std::make_shared<OnDestroy>();latch->action=[&] {++prepared;current.owner->pools[2]=result;};
        auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[latch] {return MessengerCallResult{true,0,{}};};return op;
    };
    IgbCurrentPoolInvocation call;
    check(call.resume(current.services).completed && call.result()==result && prepared==1,
        "Completed preparation callback remained alive across the fresh pool read.");
    Fixture fields;auto first=object(),second=object(),old=object(),replacement=object(9);unsigned destroyed{};
    fields.owner->pools[2]=first;fields.file->pool_118=old;
    fields.services->factory=[&](const IgbFilePoolsCall& request) {
        check(request.kind==IgbFilePoolsServiceKind::destroy_object && request.object==old,"Unexpected post-default destructor.");
        auto latch=std::make_shared<OnDestroy>();latch->action=[&] {
            ++destroyed;fields.file->pool_118=replacement;fields.owner->pools[2]=second;
        };
        auto op=std::make_shared<IgbFilePoolsOperation>();op->resume=[latch] {return MessengerCallResult{true,0,{}};};return op;
    };
    IgbFilePoolsInvocation initialize(fields.file);
    check(initialize.resume(fields.services).completed && destroyed==1 && fields.file->pool_118==first &&
        fields.file->pool_11c==second && fields.file->pool_120==second && replacement->reference_word==9,
        "Completed destructor callback remained alive across raw publication or the next fresh lookup.");
}
}
int main(int argc,char**) {
    try {
        if(argc!=1)throw std::runtime_error("Usage: igb_file_pools_tests");
        selection();fallback_and_flags();virtual_boundaries();publication();destructor_prefixes();abandonment_and_reentry();completed_call_teardown();
        std::cout<<"igb_file_pools_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
