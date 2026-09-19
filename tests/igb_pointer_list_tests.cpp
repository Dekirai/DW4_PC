#include "dw4/igb_pointer_list.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <map>

using namespace dw4;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void fails(F run,const char* message){bool caught{};try{run();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
using K=IgbPointerListServiceKind;
IgbPointerListBacking allocation(unsigned slots,bool fields=false,std::uint32_t address=0x10000){
    IgbPointerListBacking b;b.address=address;b.bytes=std::make_shared<IgbScratchBuffer>();
    b.bytes->bytes.assign(slots*4,0xa5);b.bytes->initialized.assign(slots*4,0);
    if(fields)b.fields=std::make_shared<IgbDecodeFieldRegistry::Storage>(slots);
    else b.types=std::make_shared<IgbRuntimeTypeList::Storage>(slots);return b;
}
template<class T>void install(const std::shared_ptr<T>& list,const IgbPointerListBacking& b){
    list->backing_bytes=b.bytes;list->backing_address=b.address;list->backing_offset=b.offset;
    if constexpr(std::is_same_v<T,IgbRuntimeTypeList>)list->backing=b.types;else list->backing=b.fields;
}
struct Fixture {
    std::shared_ptr<IgbRuntimeTypeList> list=std::make_shared<IgbRuntimeTypeList>();
    std::shared_ptr<IgbPointerListServices> services=std::make_shared<IgbPointerListServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeMetaType> item=std::make_shared<IgbRuntimeMetaType>();
    std::vector<IgbPointerListCall> calls;
    std::map<std::uint32_t,std::uint32_t> sizes;
    std::function<void(const IgbPointerListCall&,IgbPointerListOperation&)> mutate;
    std::uint32_t next=0x20000;
    Fixture(){list->reference_word=0x07000001;services->values.push_back({0x90000,item,{}});
        services->factory=[this](const IgbPointerListCall& c){calls.push_back(c);auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==K::allocation_pool || c.kind==K::object_pool)out->pool_result=pool;
                else if(c.kind==K::allocation_size)out->size_result=sizes.at(c.backing.address);
                else{auto b=allocation(c.bytes/4,bool(c.fields),next);next+=0x10000;sizes[b.address]=c.bytes;
                    if(c.backing.bytes){const auto copied=std::min(c.backing.bytes->bytes.size(),b.bytes->bytes.size());
                        std::copy_n(c.backing.bytes->bytes.begin(),copied,b.bytes->bytes.begin());std::copy_n(c.backing.bytes->initialized.begin(),copied,b.bytes->initialized.begin());
                        if(b.types && c.backing.types)std::copy_n(c.backing.types->begin(),std::min(b.types->size(),c.backing.types->size()),b.types->begin());
                        if(b.fields && c.backing.fields)std::copy_n(c.backing.fields->begin(),std::min(b.fields->size(),c.backing.fields->size()),b.fields->begin());}
                    out->backing_result=b;}
                if(mutate)mutate(c,*out);return MessengerCallResult{true,0,{}};
            };return op;};
    }
    void initial(unsigned n){const auto b=allocation(n);install(list,b);sizes[b.address]=n*4;}
    IgbPointerListValue value()const{return {0x90000,item,{}};}
};
void append_and_growth(){
    Fixture f;IgbPointerListInvocation first(f.list,f.value());check(first.resume(f.services).completed,"Empty append failed.");
    check(f.calls.size()==2 && f.calls[0].kind==K::object_pool && f.calls[0].handle==7 && f.calls[1].bytes==16,"Empty growth did not use actual tag and first capacity.");
    check(f.list->count==1 && f.list->backing->at(0)==f.item && igb_buffer_word(f.list->backing_bytes,0)==0x90000,"Append did not publish pointer before count.");
    check(igb_buffer_word(f.list->backing_bytes,12)==0 && f.item->reference_word==1,"Raw append retained metadata or failed zero tail.");
    const auto calls=f.calls.size();check(first.resume().completed && f.calls.size()==calls,"Completed append replayed.");
    for(const auto [old,request,expected]:{std::tuple{4u,4,8u},std::tuple{256u,511,512u},std::tuple{512u,512,1024u},std::tuple{1024u,1024,1536u}}){
        Fixture x;x.initial(old);IgbPointerListInvocation grow(x.list,request);check(grow.resume(x.services).completed,"Reserve failed.");
        check(x.calls.size()==6 && x.calls.back().bytes==expected*4 && x.list->count==0,"Reserve query count or nonlinear growth differs.");
        check(x.list->backing_bytes->initialized[old*4]==1 && x.list->backing_bytes->initialized[0]==0,"Reserve fabricated old backing initialization.");
    }
    Fixture spare;spare.initial(8);spare.list->capacity=-123;IgbPointerListInvocation append(spare.list,spare.value());
    check(append.resume(spare.services).completed && spare.calls.size()==2 && spare.list->capacity==-123,"Append inferred allocator capacity from cached view.");
}
void captured_queries_and_wrap(){
    Fixture f;f.initial(4);f.list->count=4;unsigned queries{};
    f.mutate=[&](const IgbPointerListCall& c,IgbPointerListOperation& op){if(c.kind==K::allocation_size){++queries;if(queries==1)f.list->count=1;if(queries==2)op.size_result=32;}};
    IgbPointerListInvocation call(f.list,f.value());check(call.resume(f.services).completed,"Live second-capacity append failed.");
    check(queries==2 && f.calls.size()==4 && f.list->count==2 && f.list->backing->at(1)==f.item,"Captured append count or live post-reserve count lost.");
    Fixture second;second.initial(4);unsigned reads{};
    second.mutate=[&](const IgbPointerListCall& c,IgbPointerListOperation& op){if(c.kind==K::allocation_size && ++reads==2)op.size_result=32;};
    IgbPointerListInvocation equal(second.list,4);check(equal.resume(second.services).completed && second.calls.back().bytes==32,"Second size did not determine captured old capacity.");
    check(second.list->backing_bytes->initialized[16]==0,"Zero-length tail was initialized.");
    for(const auto raw:{0x80000000u,0xc0000000u}){Fixture x;x.initial(4);x.list->count=std::bit_cast<std::int32_t>(raw);
        IgbPointerListInvocation wrap(x.list,x.value());check(wrap.resume(x.services).completed,"Wrapped negative count did not reach physical slot zero.");
        check(std::bit_cast<std::uint32_t>(x.list->count)==raw+1 && x.list->backing->at(0)==x.item,"Wrapped count/store differs.");}
    Fixture negative;negative.initial(4);negative.list->count=-1;IgbPointerListInvocation bad(negative.list,negative.value());
    fails([&]{bad.resume(negative.services);},"Unavailable negative physical store accepted.");check(negative.list->count==-1,"Failed pointer store changed count.");
}
void failure_prefix_and_lifetime(){
    Fixture null;null.initial(4);null.mutate=[](const auto& c,auto& op){if(c.kind==K::reallocate)op.backing_result={};};
    IgbPointerListInvocation failed(null.list,4);fails([&]{failed.resume(null.services);},"Null grown backing was accepted.");
    check(null.list->backing_address==0 && !null.list->backing && null.list->count==0,"Null publication prefix rolled back.");
    fails([&]{failed.resume(null.services);},"Faulted grow was reusable.");
    Fixture short_buffer;short_buffer.mutate=[](const auto& c,auto& op){if(c.kind==K::reallocate){op.backing_result.bytes->bytes.resize(7);op.backing_result.bytes->initialized.resize(7);}};
    IgbPointerListInvocation short_call(short_buffer.list,3);fails([&]{short_call.resume(short_buffer.services);},"Short clear range accepted.");
    check(std::all_of(short_buffer.list->backing_bytes->bytes.begin(),short_buffer.list->backing_bytes->bytes.end(),[](auto v){return v==0;}),"Byte memset failure lost preceding stores.");
    Fixture word_buffer;word_buffer.initial(8);word_buffer.mutate=[](const auto& c,auto& op){if(c.kind==K::reallocate){op.backing_result.bytes->bytes.resize(39);op.backing_result.bytes->initialized.resize(39);}};
    IgbPointerListInvocation word_call(word_buffer.list,8);fails([&]{word_call.resume(word_buffer.services);},"Short word memset accepted.");
    check(igb_buffer_word(word_buffer.list->backing_bytes,32)==0 && word_buffer.list->backing_bytes->bytes[36]==0xa5 && !word_buffer.list->backing_bytes->initialized[36],"Failing word store partially wrote or lost prior word.");
    Fixture overwritten;overwritten.initial(4);std::weak_ptr<IgbRuntimeMetaType> old;{auto owner=std::make_shared<IgbRuntimeMetaType>();old=owner;overwritten.list->backing->at(0)=owner;}
    IgbPointerListInvocation replace(overwritten.list,overwritten.value());check(replace.resume(overwritten.services).completed && !old.expired(),"Raw pointer overwrite destroyed displaced owner.");
    Fixture zero;auto op=std::make_shared<IgbPointerListOperation>();unsigned polls{},selections{};zero.services->factory=[&](const auto& c){++selections;check(c.kind==K::object_pool,"Pending pool progressed unexpectedly.");return op;};
    std::weak_ptr<IgbPointerListInvocation> recursive;auto active=std::make_shared<IgbPointerListInvocation>(zero.list,0);recursive=active;
    op->resume=[&]{++polls;try{recursive.lock()->resume(zero.services);}catch(const IgbPointerListError&){}return MessengerCallResult{true,0,{}};};
    fails([&]{active->resume(zero.services);},"Caught recursive failure did not poison outer continuation.");check(polls==1 && selections==1,"Recursive call replayed pool selection.");
}
void retained_services_and_adapters(){
    Fixture missing;missing.initial(4);const auto saved=missing.list->backing;missing.list->backing.reset();
    IgbPointerListInvocation mapping(missing.list,missing.value());check(!mapping.resume(missing.services).completed,"Missing typed store mapping did not pause.");
    missing.list->count=2;missing.list->backing=saved;
    check(mapping.resume(missing.services).completed && saved->at(0)==missing.item && !saved->at(2) && missing.list->count==3,"Late mapping redirected captured store or lost live count increment.");
    Fixture f;f.initial(4);unsigned selections{},polls{};auto selected=std::make_shared<IgbPointerListOperation>();
    f.services->factory=[&](const IgbPointerListCall& c){++selections;if(c.kind==K::allocation_pool){check(c.backing.address==0x10000,"Wrong captured backing.");return selected;}
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[out=op.get()]{out->size_result=16;return MessengerCallResult{true,0,{}};};return op;};
    selected->resume=[&]{if(++polls==1)return MessengerCallResult{false,0,"prepared pool"};selected->pool_result=f.pool;return MessengerCallResult{true,0,{}};};
    IgbPointerListInvocation held(f.list,f.value());check(!held.resume(f.services).completed,"Pool did not suspend.");f.list->count=2;
    check(held.resume(f.services).completed && selections==2 && polls==2 && f.list->count==3 && f.list->backing->at(2)==f.item,"Retained size call lost captured/live state.");
    Fixture adapter;adapter.initial(4);auto factory=make_native_igb_pointer_list_metadata_factory(adapter.services);
    IgbMetadataOperationCall request;request.kind=IgbMetadataOperationKind::append_child;request.children=adapter.list;request.metadata=adapter.item;
    const auto append=factory(request);check(append && append->resume().completed && adapter.list->count==1,"Metadata append adapter did not execute actual raw list.");
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->reference_word=0xffffffffu;auto fields=std::make_shared<IgbDecodeFieldRegistry>();
    const auto fb=allocation(4,true,0x30000);install(fields,fb);adapter.sizes[fb.address]=16;adapter.services->values.push_back({0xa0000,{},field});
    auto parent=std::make_shared<IgbRuntimeMetaType>();parent->children=adapter.list;parent->type_word=7;parent->allocation_prefix=0;
    parent->fields=std::make_shared<IgbDecodeFieldRegistry>();parent->fields->count=1;parent->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(1,field);
    adapter.item->fields=fields;auto parent_services=std::make_shared<IgbMetadataOperationsServices>();parent_services->factory=factory;
    IgbMetadataParentInvocation inherit(adapter.item,parent);check(inherit.resume(parent_services).completed,"Parent did not compose actual raw child/field appends.");
    check(field->reference_word==0 && fields->count==1 && fields->backing->at(0)==field && adapter.list->count==2,"Inherited retain/append order differs.");
    auto bootstrap=make_native_igb_pointer_list_bootstrap_factory(adapter.services);IgbArkCoreCall size;size.kind=IgbArkCoreServiceKind::backing_size;size.types=adapter.list;size.type_backing=adapter.list->backing;
    size.type_backing_bytes=adapter.list->backing_bytes;size.type_backing_address=adapter.list->backing_address;size.type_backing_offset=adapter.list->backing_offset;
    auto sizing=bootstrap(size);adapter.list->backing_address=0xdead;
    const auto result=sizing->resume();check(result.completed && result.value==16,"Bootstrap size failed to retain captured backing identity.");
}
}
int main(){try{append_and_growth();captured_queries_and_wrap();failure_prefix_and_lifetime();retained_services_and_adapters();
    std::cout<<"Pointer list checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Pointer list failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
