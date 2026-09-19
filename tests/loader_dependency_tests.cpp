#include "dw4/loader_dependency.hpp"

#include <algorithm>
#include <iostream>
#include <tuple>

namespace {
using namespace dw4;
using K=LoaderDependencyServiceKind;
unsigned checks{};
void check(bool value,const char* why) {++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why) {++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<ModelSceneObject> object(std::uint32_t refs=1) {auto p=std::make_shared<ModelSceneData>();p->reference_word=refs;return p;}
std::shared_ptr<LoaderDirectoryList> list(std::int32_t count=0,std::int32_t capacity=4) {
    auto p=std::make_shared<LoaderDirectoryList>();p->count=count;p->capacity=capacity;
    p->storage->resize(static_cast<std::size_t>(std::max(capacity,0)));return p;
}
std::shared_ptr<IgbInternedString> string_owner(SystemStageString name,std::uint32_t refs=1) {
    auto p=std::make_shared<IgbInternedString>();p->value=std::move(name);p->references=refs;p->container=object();return p;
}
// Prepared endpoint bodies, not actual factory/bootstrap/decoder implementations.
struct Fixture {
    std::shared_ptr<SystemStageLoader> loader=std::make_shared<SystemStageLoader>();
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<LoaderDependencyServices> services=std::make_shared<LoaderDependencyServices>();
    std::vector<K> trace;
    std::vector<std::string> paths;
    std::vector<std::shared_ptr<ModelSceneObject>> fallbacks;
    unsigned fallback_index{},decodes{};
    std::vector<std::int32_t> decode_values{1};
    std::function<void(const LoaderDependencyCall&,LoaderDependencyOperation&)> after;
    std::function<void(const LoaderDependencyCall&,std::shared_ptr<LoaderDependencyOperation>&)> select;
    std::function<void(const LoaderRequestCall&)> decode;
    Fixture() {
        loader->directories=list();loader->pool_38=object();loader->pool_3c=object();loader->pool_40=object();
        services->directory=std::make_shared<LoaderDirectoryServices>();
        services->directory->filename_field=std::make_shared<LoaderDirectoryNameField>();services->directory->mutex_success=7;
        services->globals=std::make_shared<SystemStageServices>();services->globals->string_pool=object();
        services->request=std::make_shared<LoaderRequestServices>();
        services->request->factory=[this](const LoaderRequestCall& call) {
            check(call.kind==LoaderRequestServiceKind::decode_igb,"Dependency caller selected an async/mutex endpoint.");
            auto out=std::make_shared<LoaderRequestOperation>();
            out->resume=[this,call,p=out.get()] {
                paths.push_back(call.filename?*call.filename:"<null>");
                p->load_result=decode_values[std::min<std::size_t>(decodes++,decode_values.size()-1)];
                if(decode)decode(call);return MessengerCallResult{true,0,{}};
            };return out;
        };
        services->factory=[this](const LoaderDependencyCall& call) {
            auto out=std::make_shared<LoaderDependencyOperation>();
            out->resume=[this,call,p=out.get()] {
                trace.push_back(call.kind);
                switch(call.kind) {
                case K::object_pool:
                    p->object_result=fallback_index<fallbacks.size()?fallbacks[fallback_index]:nullptr;++fallback_index;break;
                case K::create_igb_file:p->file_result=file;break;
                case K::destroy_object:break;
                case K::create_string_pool:p->object_result=object();break;
                case K::initialize_string_pool:p->object_result=call.object;break;
                case K::intern_name: {
                    const auto name=igb_dependency_name(call.name);
                    if(name) {
                        for(const auto& candidate:services->strings->owners)
                            if(candidate && !candidate->released && candidate->value && *candidate->value==name->c_str()) {p->string_result=candidate;break;}
                        if(p->string_result)++p->string_result->references;
                        else p->string_result=string_owner(text(name->c_str()));
                    }break;
                }
                case K::release_string:check(call.string && call.string->references==0,"String release ran before full32 zero.");break;
                case K::reserve_directories:
                    call.list->storage=std::make_shared<LoaderDirectoryList::Storage>(static_cast<std::size_t>(call.capacity));
                    call.list->capacity=call.capacity;break;
                }
                if(after)after(call,*p);return MessengerCallResult{true,0,{}};
            };
            if(select)select(call,out);return out;
        };
    }
};
void cache_references() {
    for(const auto refs:{0u,1u,0x7fffffu,0x800000u,0xffffffffu,0xff000000u}) {
        Fixture f;auto row=std::make_shared<SystemStageDirectory>();row->filename=text("cache");row->reference_word=refs;
        row->claim_count=4;f.loader->directories->count=1;f.loader->directories->storage->at(0)=row;
        f.services->globals.reset();f.services->request.reset();
        LoaderDependencyInvocation call(f.loader,text("cache"));
        check(call.resume(f.services).completed && call.result()==row,"Cache hit did not return canonical directory.");
        check(row->reference_word==refs && row->claim_count==5,"Lookup temporary/count semantics changed.");
        check(f.trace==((refs&0x7fffffu)==0?std::vector<K>{K::destroy_object}:std::vector<K>{}),"Lookup final-reference condition is not low23.");
        check(call.resume().completed && row->claim_count==5,"Completed cache request replayed.");
    }
    Fixture f;auto cached=std::make_shared<SystemStageDirectory>();cached->filename=text("root");
    bool ready{};unsigned selected{},resumes{};
    f.services->directory->root_directory=[&](const auto& owner) {
        check(owner==f.loader,"Root getter captured wrong loader.");++selected;
        auto op=std::make_shared<LoaderDirectoryRootOperation>();op->result=cached;
        op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared root getter."};};return op;
    };
    LoaderDependencyInvocation call(f.loader,text("igImageDirectory"));
    check(!call.resume(f.services).completed && cached->claim_count==0,"Root getter pause published a claim.");
    f.services->directory->root_directory={};ready=true;
    check(call.resume(f.services).completed && selected==1 && resumes==2 && call.result()==cached,"Root getter binding was not retained.");
}
void pool_fallbacks_and_fields() {
    for(unsigned mode:{0u,1u,2u,255u}) {
        Fixture f;f.loader->async_1c=static_cast<std::uint8_t>(mode);f.loader->initialize_after_load_34=23;
        f.loader->format_44=7;f.loader->option_08=19;f.loader->option_48=0x87654321;
        f.file->initialize_after_load_115=91;f.file->succeeded_31=11;
        LoaderDependencyInvocation call(f.loader,text("dep"));
        check(call.resume(f.services).completed,"Synchronous dependency request did not finish.");
        check(f.paths==std::vector<std::string>{"dep"} && f.file->claim_count==1 && !f.file->mutex,"Async flag changed synchronous/mutex semantics.");
        check(f.file->initialize_after_load_115==(mode==1?23:91),"Async exact1 option copy changed.");
        check(f.file->loading==1 && f.file->succeeded_31==0 && f.file->reference_word==1,"Loading publication or creator temporary changed.");
        check(f.file->format_24==7 && f.file->option_41==19 && f.file->option_a4==0x87654321,"Live loader option copies changed.");
        check(f.loader->pool_38->reference_word==2 && f.loader->pool_3c->reference_word==2 && f.loader->pool_40->reference_word==2,"Pool field references were not retained.");
    }
    Fixture f;f.loader->pool_38.reset();f.loader->pool_3c.reset();f.loader->pool_40.reset();
    const auto create=object(),first=object(),third=object();f.fallbacks={create,first,nullptr,third};
    const auto old1=f.file->pool_118=object(0x800001),old2=f.file->pool_11c=object(2),old3=f.file->pool_120=object(0);
    f.loader->reference_word=0xab000001;unsigned observed{};
    f.after=[&](const auto& c,auto&) {
        if(c.kind==K::object_pool) {check(c.pool_handle==0xabu+observed,"Pool fallback did not capture fresh original high byte.");++observed;f.loader->reference_word+=0x01000000;}
        if(c.kind==K::create_igb_file)check(c.object==create,"Factory fallback pool was reused incorrectly.");
    };
    LoaderDependencyInvocation call(f.loader,text("dep"));check(call.resume(f.services).completed,"Independent fallback requests failed.");
    check(observed==4 && f.file->pool_118==first && !f.file->pool_11c && f.file->pool_120==third,"Null fallback did not clear old pool or fallback was cached.");
    check(old1->reference_word==0x800000 && old2->reference_word==1 && old3->reference_word==0xffffffff,"Pool decrements did not preserve full packed words.");
    check(std::count(f.trace.begin(),f.trace.end(),K::destroy_object)==1,"Pool destructor low23 selection changed.");
    check(first->reference_word==2 && third->reference_word==2 && create->reference_word==1,"Factory argument was retained as a file pool.");
    Fixture alias;const auto shared=object(1);alias.loader->pool_38=shared;alias.file->pool_118=shared;
    LoaderDependencyInvocation same(alias.loader,text("alias"));check(same.resume(alias.services).completed && shared->reference_word==1,"Pool self assignment changed references.");
    check(std::find(alias.trace.begin(),alias.trace.end(),K::destroy_object)==alias.trace.end(),"Self assignment released before retaining.");
}
void pending_prefixes() {
    Fixture f;const auto old=f.file->pool_118=object();const auto incoming=f.loader->pool_38;
    bool ready{};unsigned factories{},resumes{};std::weak_ptr<ModelSceneObject> overwritten;
    f.select=[&](const auto& c,auto& op) {if(c.kind==K::destroy_object && c.object==old) {
        ++factories;op->resume=[&]{++resumes;return MessengerCallResult{ready,0,"Prepared final pool destructor."};};
    }};
    LoaderDependencyInvocation call(f.loader,text("dep"));
    check(!call.resume(f.services).completed && incoming->reference_word==2 && old->reference_word==0,"Pending pool destruction lost prefix.");
    {auto injected=object();overwritten=injected;f.file->pool_118=injected;}
    const auto factory=f.services->factory;f.services->factory={};f.loader->pool_38=object();ready=true;
    check(!call.resume(f.services).completed && factories==1 && resumes==2,"Pool destructor binding was redirected or restarted.");
    check(f.file->pool_118==incoming && !overwritten.expired() && incoming->reference_word==2,"Pending assignment lost captured incoming or released injected raw store.");
    f.services->factory=factory;check(call.resume(f.services).completed,"Caller did not continue after restored intern endpoint.");
    Fixture missing;missing.services->factory={};LoaderDependencyInvocation absent(missing.loader,text("dep"));
    check(!absent.resume(missing.services).completed && missing.file->loading==0,"Missing actual factory was treated as success.");
    rejects([&]{absent.result();},"Pending request exposed return value.");
    Fixture failure;failure.select=[&](const auto& c,auto& op) {if(c.kind==K::intern_name)op->resume=[]()->MessengerCallResult {throw std::runtime_error("Prepared intern failure");};};
    LoaderDependencyInvocation fault(failure.loader,text("dep"));rejects([&]{fault.resume(failure.services);},"Endpoint failure did not escape.");
    check(fault.faulted() && failure.file->loading==1 && failure.loader->directories->count==0,"Fault lost reached initialization prefix.");
    rejects([&]{fault.resume(failure.services);},"Faulted prefix replayed.");
}
void string_transactions() {
    for(const auto refs:{0u,1u,0x800001u,0xffffffffu}) {
        Fixture f;const auto old=string_owner(text("old"),refs);igb_bind_interned_string(f.services->strings,old);f.file->filename=old->value;
        LoaderDependencyInvocation call(f.loader,text("new"));check(call.resume(f.services).completed,"Filename replacement did not complete.");
        check(old->references==refs-1 && old->released==(refs==1),"Filename references incorrectly use object low23 semantics.");
        check(*f.file->filename=="new" && f.file->loader.lock()==f.loader,"Filename and parent publication changed.");
    }
    Fixture same;auto owner=string_owner(text("dep"));igb_bind_interned_string(same.services->strings,owner);same.file->filename=owner->value;
    LoaderDependencyInvocation alias(same.loader,text("dep"));check(alias.resume(same.services).completed && owner->references==1 && !owner->released,"Same interned filename was released before interning.");
    Fixture missing;const auto previous=text("old");missing.file->filename=previous;
    LoaderDependencyInvocation waiting(missing.loader,text("dep"));
    check(!waiting.resume(missing.services).completed && missing.file->filename==previous && missing.file->claim_count==0,"Unknown old filename refcount was fabricated.");
    auto bound=string_owner(previous,2);igb_bind_interned_string(missing.services->strings,bound);
    missing.file->filename=text("injected");check(waiting.resume(missing.services).completed && bound->references==1,"Late old-owner binding reread filename or repeated interning.");
    check(std::count(missing.trace.begin(),missing.trace.end(),K::intern_name)==1,"Pending old owner repeated interning.");
    Fixture create;create.services->globals->string_pool.reset();
    const auto replacement=object();create.after=[&](const auto& c,auto& out) {if(c.kind==K::initialize_string_pool)out.object_result=replacement;};
    LoaderDependencyInvocation created(create.loader,text("dep"));check(created.resume(create.services).completed && create.services->globals->string_pool==replacement,"String initializer returned owner was ignored.");
    check(create.trace.at(1)==K::create_string_pool && create.trace.at(2)==K::initialize_string_pool && create.trace.at(3)==K::intern_name,"String-pool lazy factory order changed.");
}
void append_load_and_final_temporary() {
    Fixture f;f.loader->directories=list(0,0);const auto selected=f.loader->directories;
    f.after=[&](const auto& c,auto&) {if(c.kind==K::reserve_directories) {check(c.capacity==4 && c.file->reference_word==2,"Reserve ran before incoming retain.");c.list->count=99;f.loader->directories=list();}};
    f.loader->primary_prefix=text("first");f.loader->secondary_prefix=text("second");f.decode_values={0,-1,0};
    LoaderDependencyInvocation call(f.loader,text("dep"));check(call.resume(f.services).completed,"Three-path synchronous load failed.");
    check(selected->count==1 && selected->storage->at(0)==f.file && f.loader->directories->count==0,"Append used replaced list or reread count after reserve.");
    check(f.paths==std::vector<std::string>{"first/dep","second/dep","dep"} && call.result()==f.file,"Signed retry/final-result ignore changed.");
    Fixture bad;bad.loader->directories=list(-1,4);bad.services->directory->filename_field.reset();
    LoaderDependencyInvocation malformed(bad.loader,text("dep"));rejects([&]{malformed.resume(bad.services);},"Negative physical append index accepted.");
    check(bad.loader->directories->count==0 && bad.file->reference_word==2 && bad.file->claim_count==1,"Malformed append lost published prefix.");
    Fixture finish;bool ready{};unsigned calls{};finish.decode=[&](const auto&){finish.file->reference_word=0x800001;};
    finish.select=[&](const auto& c,auto& op){if(c.kind==K::destroy_object && c.directory)op->resume=[&]{++calls;return MessengerCallResult{ready,0,"Prepared final file destructor."};};};
    LoaderDependencyInvocation final(finish.loader,text("dep"));
    check(!final.resume(finish.services).completed && finish.file->reference_word==0x800000 && finish.decodes==1,"Final creator temporary release ran before or repeated load.");
    ready=true;check(final.resume(finish.services).completed && final.result()==finish.file && calls==2 && finish.decodes==1,"Final destructor lost captured wrapper return.");
}
void live_name_and_nested_calls() {
    Fixture f;auto source=std::make_shared<IgbScratchBuffer>();source->bytes={'x','x','x',0};source->initialized={1,0,0,0};
    f.services->directory->filename_field.reset();f.services->factory={};
    LoaderDependencyInvocation unread(f.loader,IgbRuntimeNameSource{source,0});
    check(!unread.resume(f.services).completed,"Initial mismatch/disabled descriptor consumed unread filename tail.");
    Fixture live;auto names=std::make_shared<IgbScratchBuffer>();names->bytes={'o','l','d',0};names->initialized={1,1,1,1};
    auto row=std::make_shared<SystemStageDirectory>();row->filename=text("new");live.loader->directories->count=1;live.loader->directories->storage->at(0)=row;
    LoaderDependencyInvocation deferred(live.loader,IgbRuntimeNameSource{names,0});
    check(!deferred.resume().completed,"Absent descriptor-global storage did not pause.");names->bytes={'n','e','w',0};
    check(deferred.resume(live.services).completed && deferred.result()==row && live.trace.empty(),"Lazy filename was frozen before descriptor pause.");
    Fixture nested;auto cached=std::make_shared<SystemStageDirectory>();cached->filename=text("child");
    nested.loader->directories->count=1;nested.loader->directories->storage->at(0)=cached;
    nested.decode=[&](const auto&){LoaderDependencyInvocation child(nested.loader,text("child"));check(child.resume(nested.services).completed && child.result()==cached,"Nested dependency request blocked on loader-global guard.");};
    LoaderDependencyInvocation parent(nested.loader,text("parent"));check(parent.resume(nested.services).completed && cached->claim_count==1,"Synchronous nested loader request failed.");
    Fixture recursive;LoaderDependencyInvocation call(recursive.loader,text("dep"));
    recursive.after=[&](const auto& c,auto&){if(c.kind==K::create_igb_file)try{call.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{call.resume(recursive.services);},"Swallowed recursive call did not invalidate outer invocation.");check(call.faulted(),"Recursive fault was not retained.");
}
void adapters() {
    Fixture f;unsigned interned{};
    IgbDependenciesFactory strings=[&](const IgbDependenciesCall& c)->std::shared_ptr<IgbDependenciesOperation> {
        if(c.kind!=IgbDependenciesServiceKind::intern_name)return {};
        auto op=std::make_shared<IgbDependenciesOperation>();op->string_result=string_owner(igb_dependency_name(c.name));
        op->resume=[&]{++interned;return MessengerCallResult{true,0,{}};};return op;
    };
    f.services->factory=make_loader_dependency_string_factory(strings,f.services->factory);
    auto factory=make_native_loader_dependency_factory(f.services);
    IgbDependenciesCall request;request.kind=IgbDependenciesServiceKind::load_dependency;request.loader=f.loader;request.name=text("adapter");
    const auto op=factory(request);check(op && op->resume().completed && op->directory_result==f.file && interned==1,"Native dependency/string adapters did not compose canonical owners.");
    IgbDependenciesCall unrelated;unrelated.kind=IgbDependenciesServiceKind::loader;check(!factory(unrelated),"Dependency adapter completed unrelated operation.");
    auto transient=std::make_shared<LoaderDependencyServices>();const auto orphan=make_native_loader_dependency_factory(transient);
    const std::weak_ptr<LoaderDependencyServices> observed=transient;transient.reset();
    check(observed.expired(),"Dependency adapter strongly retained its service graph.");
    rejects([&]{orphan(request);},"Expired canonical services were replaced with invented defaults.");
}
void abandoned_final_destructor() {
    Fixture f;const std::weak_ptr<LoaderRequestFile> observed=f.file;
    f.decode=[&](const auto& c) {
        c.file->reference_word=1;
        f.loader->directories->storage->at(0).reset();f.loader->directories->count=0;
        f.file.reset();
    };
    f.select=[](const auto& c,auto& operation) {
        if(c.kind==K::destroy_object && c.directory)
            operation->resume=[] {return MessengerCallResult{false,0,"Prepared final destructor suspension."};};
    };
    {
        LoaderDependencyInvocation call(f.loader,text("abandon"));
        check(!call.resume(f.services).completed && !call.faulted(),"Final destructor did not retain a pending continuation.");
        check(!observed.expired() && observed.lock()->reference_word==0,"Pending destructor changed original final reference prefix.");
    }
    check(!observed.expired() && observed.lock()->reference_word==0,"Abandoning final destructor implicitly destroyed its selected owner.");
    Fixture strings;auto old=string_owner(text("old"));const std::weak_ptr<IgbInternedString> old_owner=old;
    strings.file->filename=old->value;igb_bind_interned_string(strings.services->strings,old);old.reset();
    strings.select=[&](const auto& c,auto& operation) {
        if(c.kind==K::release_string)operation->resume=[&] {
            strings.services->strings->owners.clear();
            return MessengerCallResult{false,0,"Prepared final string release."};
        };
    };
    {LoaderDependencyInvocation call(strings.loader,text("new"));check(!call.resume(strings.services).completed,"Final string release did not pause.");}
    check(!old_owner.expired() && old_owner.lock()->references==0 && !old_owner.lock()->released,
        "Abandoning string release discarded selected header/container metadata.");
}
}
int main() {
    try {cache_references();pool_fallbacks_and_fields();pending_prefixes();string_transactions();append_load_and_final_temporary();live_name_and_nested_calls();adapters();abandoned_final_destructor();
        std::cout<<"Loader dependency: "<<checks<<" checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Loader dependency after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}
}
