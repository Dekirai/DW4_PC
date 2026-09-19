#include "dw4/igb_bootstrap_object.hpp"

#include <iostream>
#include <limits>

namespace {
using namespace dw4;
using K=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::shared_ptr<ModelSceneObject> pool(){return std::make_shared<ModelSceneData>();}
IgbBootstrapPointer allocation(std::uint32_t offset=0){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=0x10000000;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(0x80,0xa5);
    storage->bytes->initialized.assign(0x80,1);return {storage,offset};
}
struct Fixture {
    IgbBootstrapPointer pointer=allocation();
    std::shared_ptr<IgbBootstrapServices> services=std::make_shared<IgbBootstrapServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<ModelSceneObject> selected=pool();
    std::vector<IgbBootstrapCall> trace;
    std::function<void(const IgbBootstrapCall&,IgbBootstrapOperation&)> after;
    Fixture(){
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;
        services->default_pool_handle=7;metadata->allocation_prefix=12;
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata);
        services->factory=[this](const IgbBootstrapCall& call){
            auto op=std::make_shared<IgbBootstrapOperation>();
            op->resume=[this,call,out=op.get()]{
                trace.push_back(call);out->pool_result=selected;out->allocation_result=pointer;out->metadata_result=metadata;
                if(after)after(call,*out);return MessengerCallResult{true,0x1fe,{}};
            };return op;
        };
    }
};
void raw_allocation_paths(){
    Fixture direct;IgbRawAllocationInvocation call(0x64,8,direct.selected);
    check(call.resume(direct.services).completed,"Explicit pool allocation did not complete.");
    check(direct.trace.size()==1 && direct.trace[0].kind==K::pool_allocate && direct.trace[0].bytes==0x6c,
        "Explicit pool changed allocation size or selected the default lookup.");
    check(call.result().storage==direct.pointer.storage && call.result().offset==8,"Pooled allocation lost prefix adjustment.");
    check(call.resume().value==0x10000008 && direct.trace.size()==1,"Completed allocation replayed callbacks.");
    Fixture defaults;IgbRawAllocationInvocation late(16,4,{});defaults.services->default_pool_handle.reset();
    check(!late.resume(defaults.services).completed && defaults.trace.empty(),"Missing handle silently became default zero.");
    defaults.services->default_pool_handle=0xfffffffeu;
    check(late.resume(defaults.services).completed && defaults.trace.size()==2,"Late default-handle binding did not resume.");
    check(defaults.trace[0].kind==K::pool_for_handle && defaults.trace[0].handle==0xfffffffeu &&
        defaults.trace[1].pool==defaults.selected,"Raw handle was filtered before actual lookup.");
    Fixture heap;heap.selected.reset();IgbRawAllocationInvocation fallback(4,8,{});
    check(fallback.resume(heap.services).completed,"Heap fallback did not complete.");
    check(heap.trace.size()==2 && heap.trace[1].kind==K::heap_allocate && heap.trace[1].bytes==12,
        "Null default pool did not select actual heap fallback.");
    check(fallback.result().offset==0 && igb_bootstrap_address(fallback.result())==0x10000000,
        "Heap fallback incorrectly added the original prefix.");
    Fixture null;null.pointer={};IgbRawAllocationInvocation null_pool(0xfffffffcu,8,null.selected);
    check(null_pool.resume(null.services).value==8 && null.trace[0].bytes==4,"Pooled null/wrapping arithmetic was normalized.");
    check(!null_pool.result().storage && null_pool.result().offset==8,"Null plus prefix manufactured an allocation.");
    rejects([&]{igb_bootstrap_read_word(null_pool.result(),0);},"Arithmetic pointer was treated as readable native memory.");
    Fixture heap_null;heap_null.pointer={};heap_null.selected.reset();IgbRawAllocationInvocation no_memory(8,12,{});
    check(no_memory.resume(heap_null.services).value==0 && igb_bootstrap_address(no_memory.result())==0,
        "Heap-null result received a prefix.");
}
void retained_allocation(){
    Fixture f;bool ready{};unsigned selected{},resumed{};const auto original=f.selected;
    f.services->factory=[&](const IgbBootstrapCall& request){
        ++selected;check(request.pool==original && request.bytes==32,"Pending allocator lost captured arguments.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->allocation_result=f.pointer;
        op->resume=[&]{++resumed;return MessengerCallResult{ready,0,"Prepared allocator waits."};};return op;
    };
    IgbRawAllocationInvocation call(28,4,original);check(!call.resume(f.services).completed,"Prepared allocation did not suspend.");
    rejects([&]{call.result();},"Pending allocation exposed a result.");
    f.services->factory=[](const auto&)->std::shared_ptr<IgbBootstrapOperation>{throw std::runtime_error("Replacement factory must not run.");};
    ready=true;check(call.resume(f.services).completed && selected==1 && resumed==2,"Pending allocator was recreated.");
    Fixture recursive;IgbRawAllocationInvocation invalid(16,0,recursive.selected);
    recursive.after=[&](const auto&,auto&){rejects([&]{invalid.resume(recursive.services);},"Recursive allocation was accepted.");};
    rejects([&]{invalid.resume(recursive.services);},"Outer allocator ignored swallowed recursive failure.");
    check(invalid.faulted(),"Recursive allocation did not poison the invocation.");
}
void physical_header_and_binding(){
    for(const auto old:{0u,1u,0x00800000u,0x7fffffffu,0xffffffffu}){
        auto pointer=allocation(16);auto metadata=std::make_shared<IgbRuntimeMetaType>();
        pointer.storage->bindings.push_back({16,{},metadata,{}});igb_bootstrap_write_word(pointer,4,old);
        IgbBootstrapBaseInvocation reset(pointer,false);check(reset.resume().completed,"Header reset did not complete without services.");
        check(igb_bootstrap_read_word(pointer,0)==0x804717f0u && igb_bootstrap_read_word(pointer,4)==1 && metadata->reference_word==1,
            "Header reset did not preserve actual packed-reference semantics.");
        check(pointer.storage->bytes->bytes[15]==0xa5 && pointer.storage->bytes->bytes[24]==0xa5,"Header reset modified adjacent bytes.");
        check(igb_bootstrap_metadata(pointer)==metadata,"Typed metadata binding was replaced.");
    }
    auto pointer=allocation();auto fields=std::make_shared<IgbDecodeFieldRegistry>();
    pointer.storage->bindings.push_back({0,{},{},fields});
    igb_bootstrap_write_word(pointer,4,0x12345678);igb_bootstrap_write_byte(pointer,4,0xab);
    igb_bootstrap_write_halfword(pointer,6,0xcdef);
    check(fields->reference_word==0xab34cdefu && igb_bootstrap_fields(pointer)==fields,"Physical subword writes lost typed header synchronization.");
    auto missing=allocation();rejects([&]{igb_bootstrap_metadata(missing);},"Unbound storage created a native metadata owner.");
    auto conflict=allocation();auto first=std::make_shared<IgbRuntimeMetaType>(),second=std::make_shared<IgbRuntimeMetaType>();
    conflict.storage->bindings={{0,{},first,{}},{0,{},second,{}}};
    rejects([&]{igb_bootstrap_metadata(conflict);},"Conflicting metadata identities were silently selected.");
    conflict.storage->bindings={{0,{},first,{}},{0,{}, {},fields}};
    rejects([&]{igb_bootstrap_metadata(conflict);},"Separate cross-kind bindings were silently accepted.");
    auto unknown=allocation();unknown.storage->bytes->initialized[6]=0;
    IgbBootstrapBaseInvocation fault(unknown,false);rejects([&]{fault.resume();},"Unknown old reference bytes were treated as zero.");
    check(fault.faulted() && unknown.storage->faulted() && igb_bootstrap_read_word(unknown,0)==0x804717f0u &&
        unknown.storage->bytes->bytes[4]==0xa5 && !unknown.storage->bytes->initialized[6],
        "Uninitialized-header failure lost the original vtable-store prefix.");
    auto short_storage=allocation();short_storage.storage->bytes->bytes.resize(6);short_storage.storage->bytes->initialized.resize(6);
    rejects([&]{igb_bootstrap_write_word(short_storage,4,1);},"Partial physical word storage was accepted.");
    check(short_storage.storage->bytes->bytes[4]==0xa5,"Invalid word store partially changed represented bytes.");
    rejects([&]{igb_bootstrap_write_byte(pointer,std::numeric_limits<std::uint32_t>::max(),1);},"Offset overflow wrote an unrelated byte.");
}
void base_pool_tag(){
    Fixture off;IgbBootstrapBaseInvocation disabled(off.pointer,true);
    check(disabled.resume(off.services).completed && off.trace.empty() && igb_bootstrap_read_word(off.pointer,4)==1,
        "Disabled ArkCore selected pool callbacks.");
    Fixture f;f.core->enabled=1;IgbBootstrapBaseInvocation enabled(f.pointer,true);
    check(enabled.resume(f.services).completed && f.trace.size()==2,"Enabled base construction did not finish pool selection.");
    check(f.trace[0].kind==K::allocation_pool && f.trace[0].address==0x0ffffff4u &&
        f.trace[1].kind==K::pool_index && f.trace[1].pool==f.selected,"Allocation-prefix subtraction or captured pool was lost.");
    check(igb_bootstrap_read_word(f.pointer,4)==0xfe000001u,"Pool tag did not use the returned low byte.");
    IgbBootstrapBaseInvocation reset(f.pointer,false);check(reset.resume().completed && igb_bootstrap_read_word(f.pointer,4)==1,
        "Second base reset retained the earlier pool tag.");
    Fixture no_pool;no_pool.core->enabled=1;no_pool.selected.reset();IgbBootstrapBaseInvocation no_tag(no_pool.pointer,true);
    check(no_tag.resume(no_pool.services).completed && no_pool.trace.size()==1 && igb_bootstrap_read_word(no_pool.pointer,4)==1,
        "Null allocation pool dereferenced its virtual table.");
    Fixture late;late.services->runtime->singleton.reset();IgbBootstrapBaseInvocation wait(late.pointer,true);
    check(!wait.resume(late.services).completed && igb_bootstrap_read_word(late.pointer,4)==1,"Unknown ArkCore did not pause after header reset.");
    igb_bootstrap_write_word(late.pointer,4,0x42000007);late.services->runtime->singleton=late.core;
    check(wait.resume(late.services).completed && igb_bootstrap_read_word(late.pointer,4)==7,"Late core binding replayed header reset.");
    Fixture null_core;null_core.services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    IgbBootstrapBaseInvocation bad(null_core.pointer,true);rejects([&]{bad.resume(null_core.services);},"Known-null ArkCore paused as missing state.");
    Fixture null_meta;null_meta.core->enabled=1;*null_meta.services->object_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    IgbBootstrapBaseInvocation bad_meta(null_meta.pointer,true);rejects([&]{bad_meta.resume(null_meta.services);},"Known-null base metadata was accepted.");
}
void live_vtable_and_lifetime(){
    Fixture f;f.services->runtime->singleton.reset();IgbBootstrapBaseInvocation call(f.pointer,true);
    check(!call.resume(f.services).completed,"Live-vtable fixture did not pause at core.");
    igb_bootstrap_write_word(f.pointer,0,0x12345678);f.core->enabled=1;f.services->runtime->singleton=f.core;
    f.after=[&](const auto& request,auto&){if(request.kind==K::base_metadata){
        check(request.vtable_word==0x12345678,"Metadata callback did not capture the changed live vtable.");
        f.metadata->allocation_prefix=0x10000020;
    }};
    check(call.resume(f.services).completed && f.trace.size()==3 && f.trace[0].kind==K::base_metadata &&
        f.trace[1].address==0xffffffe0u,"Unknown virtual or wrapping live-prefix subtraction was skipped.");
    Fixture abandoned;abandoned.services->runtime->singleton.reset();
    {IgbBootstrapBaseInvocation pending(abandoned.pointer,true);check(!pending.resume(abandoned.services).completed,"Abandon fixture did not suspend.");}
    check(abandoned.pointer.storage->faulted(),"Abandoned partial construction silently allowed reuse.");
    IgbBootstrapBaseInvocation reused(abandoned.pointer,false);rejects([&]{reused.resume();},"Abandoned allocation was reused.");
    Fixture active;active.services->runtime->singleton.reset();IgbBootstrapBaseInvocation first(active.pointer,true);
    check(!first.resume(active.services).completed,"Concurrent fixture did not suspend.");
    IgbBootstrapBaseInvocation second(active.pointer,false);rejects([&]{second.resume();},"Concurrent base constructors shared writable storage.");
    active.services->runtime->singleton=active.core;check(first.resume(active.services).completed,"Rejected second constructor poisoned the owner.");
    Fixture teardown;teardown.core->enabled=1;
    struct OnDestroy{IgbBootstrapPointer object;~OnDestroy(){igb_bootstrap_write_word(object,4,0x77000005);}};
    const auto ordinary=teardown.services->factory;
    teardown.services->factory=[&](const IgbBootstrapCall& request){
        if(request.kind!=K::pool_index)return ordinary(request);
        auto op=std::make_shared<IgbBootstrapOperation>();auto guard=std::make_shared<OnDestroy>();guard->object=teardown.pointer;
        op->resume=[guard]{return MessengerCallResult{true,0x42,{}};};return op;
    };
    IgbBootstrapBaseInvocation destruction(teardown.pointer,true);
    check(destruction.resume(teardown.services).completed && igb_bootstrap_read_word(teardown.pointer,4)==0x42000005,
        "Operation teardown overwrote the subsequently published captured pool tag.");
}
} // namespace
int main(){try{
    raw_allocation_paths();retained_allocation();physical_header_and_binding();base_pool_tag();live_vtable_and_lifetime();
    std::cout<<"IGB bootstrap object checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
