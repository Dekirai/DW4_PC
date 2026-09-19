#include "dw4/igb_type_publication.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <limits>
#include <map>

using namespace dw4;
namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
template<class F>void fails(F run,const char* message){bool caught{};try{run();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
using K=IgbPointerListServiceKind;
struct Fixture {
    std::shared_ptr<IgbTypePublicationServices> services=std::make_shared<IgbTypePublicationServices>();
    std::shared_ptr<IgbRuntimeTypesServices> runtime=std::make_shared<IgbRuntimeTypesServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeTypeList> list=std::make_shared<IgbRuntimeTypeList>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<IgbPointerListServices> pointers=std::make_shared<IgbPointerListServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbPointerListCall> calls;
    std::map<std::uint32_t,std::uint32_t> sizes;
    std::function<void(const IgbPointerListCall&,IgbPointerListOperation&)> mutate;
    std::uint32_t next_address=0x20000;
    Fixture(){
        core->types=list;core->enabled=0;runtime->singleton=core;
        services->runtime=runtime;services->pointer_lists=pointers;
        pointers->values.push_back({0x80000,metadata,{}});
        pointers->factory=[this](const IgbPointerListCall& c){
            calls.push_back(c);auto operation=std::make_shared<IgbPointerListOperation>();
            operation->resume=[this,c,out=operation.get()]{
                if(c.kind==K::allocation_pool || c.kind==K::object_pool)out->pool_result=pool;
                else if(c.kind==K::allocation_size)out->size_result=sizes.at(c.backing.address);
                else{
                    auto b=backing(c.bytes/4,next_address);next_address+=0x10000;
                    if(c.backing.bytes){const auto copied=std::min(c.backing.bytes->bytes.size(),b.bytes->bytes.size());
                        std::copy_n(c.backing.bytes->bytes.begin(),copied,b.bytes->bytes.begin());
                        std::copy_n(c.backing.bytes->initialized.begin(),copied,b.bytes->initialized.begin());
                        if(c.backing.types)std::copy_n(c.backing.types->begin(),std::min(c.backing.types->size(),b.types->size()),b.types->begin());}
                    out->backing_result=std::move(b);
                }
                if(mutate)mutate(c,*out);return MessengerCallResult{true,0,{}};
            };return operation;
        };
    }
    IgbPointerListBacking backing(std::uint32_t slots,std::uint32_t address){
        IgbPointerListBacking b;b.address=address;b.types=std::make_shared<IgbRuntimeTypeList::Storage>(slots);
        b.bytes=std::make_shared<IgbScratchBuffer>();b.bytes->bytes.assign(slots*4,0xa5);b.bytes->initialized.assign(slots*4,0);
        sizes[address]=slots*4;return b;
    }
    void initial(std::uint32_t slots){const auto b=backing(slots,0x10000);
        list->backing=b.types;list->backing_bytes=b.bytes;list->backing_address=b.address;}
};
void identity_search(){
    auto list=std::make_shared<IgbRuntimeTypeList>();auto a=std::make_shared<IgbRuntimeMetaType>();auto b=std::make_shared<IgbRuntimeMetaType>();
    a->name=b->name=std::make_shared<const std::string>("same spelling");
    for(const auto count:{0,-1,std::numeric_limits<std::int32_t>::min()}){list->count=count;
        check(igb_runtime_type_index(list,a)==-1,"Nonpositive count dereferenced unavailable backing.");}
    list->count=1;fails([&]{igb_runtime_type_index(list,a);},"Positive count accepted null backing.");
    list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(IgbRuntimeTypeList::Storage{b,nullptr,a,a});list->count=4;list->capacity=-1;
    check(igb_runtime_type_index(list,a)==2,"Identity search used a name or returned a later duplicate.");
    check(igb_runtime_type_index(list,nullptr)==1,"Null pointer identity was rejected.");
    list->count=1;check(igb_runtime_type_index(list,a)==-1,"Identity search inferred logical count from capacity/backing length.");
    list->count=10;check(igb_runtime_type_index(list,b)==0,"First match read an unavailable trailing slot.");
    fails([&]{igb_runtime_type_index(list,std::make_shared<IgbRuntimeMetaType>());},"Unavailable reached physical slot was accepted.");
    fails([&]{igb_runtime_type_index({},a);},"Null list was accepted.");
}
void existing_and_new(){
    Fixture existing;existing.list->backing=std::make_shared<IgbRuntimeTypeList::Storage>(IgbRuntimeTypeList::Storage{nullptr,existing.metadata});
    existing.list->count=2;existing.metadata->word_0c=99;existing.metadata->reference_word=0xffffffff;
    existing.services->pointer_lists.reset();IgbTypePublicationInvocation found(existing.metadata);
    check(found.resume(existing.services).completed,"Existing identity required append services or enabled reflection.");
    check(existing.metadata->word_0c==1 && existing.metadata->reference_word==0xffffffff && existing.calls.empty(),"Existing publication changed ownership or lost the exact index.");
    check(found.resume().completed,"Completed publication did not remain complete.");
    for(const auto refs:{0u,1u,0x7fffffu,0x800000u,0xffffffffu}){
        Fixture f;f.metadata->reference_word=refs;IgbTypePublicationInvocation publish(f.metadata);
        check(publish.resume(f.services).completed,"Empty registry append failed.");
        check(f.metadata->reference_word==refs+1u && f.metadata->word_0c==0,"Publication did not use wrapping full-word retain/index.");
        check(f.list->count==1 && f.list->backing->at(0)==f.metadata && igb_buffer_word(f.list->backing_bytes,0)==0x80000,"Publication did not execute real pointer append.");
        check(f.calls.size()==2 && f.calls[0].kind==K::object_pool && f.calls[1].kind==K::reallocate,"Empty registry used an invented allocator shortcut.");
        check(publish.resume().completed && f.calls.size()==2,"Completed append replayed allocation.");
    }
    Fixture spare;spare.initial(4);spare.list->capacity=0;IgbTypePublicationInvocation append(spare.metadata);
    check(append.resume(spare.services).completed && spare.calls.size()==2 && spare.list->count==1,"Append did not consult actual allocation size.");
}
void pending_and_fresh_search(){
    Fixture f;f.initial(4);const auto original=f.pointers->factory;
    unsigned selected{},polls{};auto operation=std::make_shared<IgbPointerListOperation>();
    f.pointers->factory=[&](const IgbPointerListCall& call){if(call.kind!=K::allocation_pool)return original(call);++selected;return operation;};
    operation->resume=[&]{if(++polls==1)return MessengerCallResult{false,0,"prepared allocation pool"};operation->pool_result=f.pool;return MessengerCallResult{true,0,{}};};
    IgbTypePublicationInvocation publish(f.metadata);
    check(!publish.resume(f.services).completed && f.metadata->word_0c==0xffffffff && f.metadata->reference_word==2,"Suspension lost the initial index/retain prefix.");
    auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->types=std::make_shared<IgbRuntimeTypeList>();
    replacement->types->backing=std::make_shared<IgbRuntimeTypeList::Storage>(IgbRuntimeTypeList::Storage{nullptr,f.metadata});replacement->types->count=2;
    f.runtime->singleton=replacement;f.list->count=2;
    check(publish.resume(f.services).completed,"Retained append did not resume.");
    check(selected==1 && polls==2 && f.list->count==3 && f.list->backing->at(2)==f.metadata,"Pending append recaptured its list or replayed the retain.");
    check(f.metadata->word_0c==1 && f.metadata->reference_word==2 && replacement->types->count==2,"Post-append search did not reload the singleton/list.");
    Fixture absent;absent.initial(4);auto empty=std::make_shared<IgbRuntimeTypeRegistry>();empty->types=std::make_shared<IgbRuntimeTypeList>();
    absent.mutate=[&](const auto& c,auto&){if(c.kind==K::allocation_size)absent.runtime->singleton=empty;};
    IgbTypePublicationInvocation still_absent(absent.metadata);
    check(still_absent.resume(absent.services).completed && absent.metadata->word_0c==0xffffffff && absent.list->count==1,"Second missing result was replaced by the appended index or a retry.");
}
void missing_binding_and_failures(){
    Fixture late;late.initial(4);late.pointers->values.clear();IgbTypePublicationInvocation waiting(late.metadata);
    check(!waiting.resume().completed && !late.metadata->word_0c,"Unrepresented singleton modified metadata.");
    check(!waiting.resume(late.services).completed && late.metadata->reference_word==2 && late.metadata->word_0c==0xffffffff,"Missing identity did not preserve the reached retain.");
    check(!waiting.resume(late.services).completed && late.metadata->reference_word==2 && late.calls.empty(),"Missing identity replayed a retain or invoked allocation.");
    late.pointers->values.push_back({0x80000,late.metadata,{}});
    check(waiting.resume(late.services).completed && late.metadata->reference_word==2,"Late identity binding failed to resume the selected append.");
    Fixture conflict;conflict.pointers->values.push_back({0x81000,conflict.metadata,{}});IgbTypePublicationInvocation aliases(conflict.metadata);
    fails([&]{aliases.resume(conflict.services);},"Conflicting original identities were accepted.");
    check(conflict.metadata->word_0c==0xffffffff && conflict.metadata->reference_word==2 && aliases.faulted(),"Identity failure rolled back the first store/retain.");
    Fixture zero;zero.runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};IgbTypePublicationInvocation null_core(zero.metadata);
    fails([&]{null_core.resume(zero.services);},"Represented null singleton paused instead of faulting.");check(!zero.metadata->word_0c && zero.metadata->reference_word==1,"Null singleton modified metadata.");
    Fixture no_list;no_list.core->types.reset();IgbTypePublicationInvocation null_list(no_list.metadata);
    fails([&]{null_list.resume(no_list.services);},"Null registry list was accepted.");check(!no_list.metadata->word_0c,"Null list reached index publication.");
    Fixture no_meta;IgbTypePublicationInvocation null_metadata(nullptr);
    fails([&]{null_metadata.resume(no_meta.services);},"Null metadata index store was accepted.");
    check(null_metadata.faulted() && no_meta.calls.empty(),"Null metadata entered append or did not fault.");
    Fixture malformed;malformed.initial(4);malformed.list->backing_bytes->bytes.resize(3);malformed.list->backing_bytes->initialized.resize(3);
    IgbTypePublicationInvocation bad(malformed.metadata);fails([&]{bad.resume(malformed.services);},"Short physical pointer store was accepted.");
    check(malformed.metadata->word_0c==0xffffffff && malformed.metadata->reference_word==2 && malformed.list->count==0,"Malformed store lost preceding index/retain or incremented count.");
    const auto prior=malformed.calls.size();fails([&]{bad.resume(malformed.services);},"Faulted publication was reusable.");check(prior==malformed.calls.size(),"Faulted publication replayed an operation.");
    Fixture vanished;vanished.initial(4);vanished.mutate=[&](const auto& c,auto&){if(c.kind==K::allocation_size)vanished.runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};};
    IgbTypePublicationInvocation post_failure(vanished.metadata);fails([&]{post_failure.resume(vanished.services);},"Post-append null singleton was ignored.");
    check(vanished.list->count==1 && vanished.metadata->word_0c==0xffffffff && vanished.metadata->reference_word==2,"Post-append fault rolled back the completed append.");
}
void abandonment_and_recursion(){
    auto services=std::make_shared<IgbTypePublicationServices>();services->runtime=std::make_shared<IgbRuntimeTypesServices>();
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->types=std::make_shared<IgbRuntimeTypeList>();services->runtime->singleton=core;
    std::weak_ptr<IgbRuntimeMetaType> retained;
    {auto metadata=std::make_shared<IgbRuntimeMetaType>();retained=metadata;IgbTypePublicationInvocation stopped(metadata);
        check(!stopped.resume(services).completed && metadata->reference_word==2,"Lifetime fixture did not reach the unmatched retained pointer.");metadata.reset();}
    check(!retained.expired() && retained.lock()->reference_word==2,"Abandonment destroyed an acquired original metadata reference.");
    services.reset();check(retained.expired(),"Abandoned publication retention created a service cycle.");
    Fixture f;unsigned calls{};IgbTypePublicationInvocation recursive(f.metadata);
    f.pointers->factory=[&](const auto&){++calls;auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&]{
        try{recursive.resume(f.services);}catch(const IgbTypePublicationError&){}return MessengerCallResult{true,0,{}};};return op;};
    fails([&]{recursive.resume(f.services);},"Caught recursive publication did not invalidate the outer continuation.");
    check(recursive.faulted() && f.metadata->reference_word==2 && f.list->count==0 && calls==1,"Recursive failure replayed retain or published a slot.");
}
void adapter_ownership(){
    Fixture f;unsigned fallbacks{};auto factory=make_native_igb_type_publication_factory(f.services,[&](const auto&){++fallbacks;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall unknown;unknown.kind=IgbTypeRegistrationServiceKind::compute_layout;
    check(!factory(unknown) && fallbacks==1,"Publication adapter swallowed a different registration endpoint.");
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::publish_type;call.metadata=f.metadata;
    auto operation=factory(call);check(operation && operation->resume().completed && f.metadata->word_0c==0,"Registration adapter did not compose actual publication.");
    std::weak_ptr<IgbTypePublicationServices> weak=f.services;f.services.reset();check(!weak.expired(),"Selected operation lost its service lifetime.");
    operation.reset();check(weak.expired(),"Factory captured its services strongly.");
    fails([&]{factory(call);},"Expired publication service was silently treated as an unbound callback.");
}
}
int main(){try{identity_search();existing_and_new();pending_and_fresh_search();missing_binding_and_failures();abandonment_and_recursion();adapter_ownership();
    std::cout<<"Type publication checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Type publication failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
