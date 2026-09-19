#include "dw4/loader_request.hpp"

#include <algorithm>
#include <bit>
#include <tuple>
#include <iostream>
#include <limits>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
std::shared_ptr<ModelSceneObject> object() {return std::make_shared<ModelSceneData>();}
std::shared_ptr<LoaderDirectoryList> list(std::int32_t count=0,std::int32_t capacity=4) {
    auto result=std::make_shared<LoaderDirectoryList>();result->count=count;result->capacity=capacity;
    result->storage=std::make_shared<LoaderDirectoryList::Storage>(static_cast<std::size_t>(capacity<0 ? 0:capacity));return result;
}
// Explicit prepared endpoints for unrecovered factory, allocator, queue and
// concrete decoder internals. These tests qualify caller ordering, not IGB load.
struct Fixture {
    std::shared_ptr<SystemStageLoader> loader=std::make_shared<SystemStageLoader>();
    std::shared_ptr<LoaderRequestServices> services=std::make_shared<LoaderRequestServices>();
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<LoaderDirectoryMutex> mutex=std::make_shared<LoaderDirectoryMutex>();
    std::vector<LoaderRequestServiceKind> trace;
    std::vector<std::string> paths;
    std::vector<std::int32_t> decoder_results{1};
    std::function<void(const LoaderRequestCall&,LoaderRequestOperation&)> on_call;
    std::function<void(const LoaderRequestCall&,std::shared_ptr<LoaderRequestOperation>&)> on_select;
    unsigned locks{},unlocks{};
    Fixture() {
        loader->directories=list();loader->queue_20=object();
        services->directory=std::make_shared<LoaderDirectoryServices>();
        services->directory->filename_field=std::make_shared<LoaderDirectoryNameField>();
        services->directory->mutex_success=17;
        services->globals=std::make_shared<SystemStageServices>();services->globals->string_pool=object();
        mutex->acquire=[this] {++locks;return MessengerCallResult{true,17,{}};};
        mutex->release=[this] {++unlocks;return MessengerCallResult{true,0,{}};};
        services->factory=[this](const LoaderRequestCall& call) {
            auto result=std::make_shared<LoaderRequestOperation>();
            result->resume=[this,call,out=result.get()] {
                trace.push_back(call.kind);
                switch(call.kind) {
                case LoaderRequestServiceKind::create_igb_file:out->file_result=file;break;
                case LoaderRequestServiceKind::create_mutex:out->mutex_result=mutex;break;
                case LoaderRequestServiceKind::initialize_mutex:
                    check(call.file->mutex==call.mutex,"Mutex initialization ran before field publication.");break;
                case LoaderRequestServiceKind::create_string_pool:out->object_result=object();break;
                case LoaderRequestServiceKind::intern_filename:
                    check(call.object && call.filename,"Prepared interning requires nonnull pool and name.");
                    out->string_result=text(call.filename->c_str());break;
                case LoaderRequestServiceKind::resize_directory_list:
                    check(call.list && call.capacity>0,"Prepared allocation received invalid arguments.");
                    call.list->storage->resize(static_cast<std::size_t>(call.capacity));call.list->capacity=call.capacity;break;
                case LoaderRequestServiceKind::enqueue_directory:
                    check(call.object && call.queue_wait && call.file==file,"Queue argument capture changed.");break;
                case LoaderRequestServiceKind::decode_igb:
                    paths.push_back(call.filename ? *call.filename:"<null>");
                    out->load_result=decoder_results[std::min(paths.size()-1,decoder_results.size()-1)];break;
                }
                if(on_call)on_call(call,*out);
                return MessengerCallResult{true,0,{}};
            };
            if(on_select)on_select(call,result);
            return result;
        };
    }
};
void cache_and_missing_boundaries() {
    Fixture f;auto cached=std::make_shared<SystemStageDirectory>();cached->filename=text("cached.igb");cached->claim_count=9;
    f.loader->directories->count=1;f.loader->directories->storage->at(0)=cached;
    f.loader->async_1c=1;f.services->globals.reset();f.services->factory={};
    LoaderDirectoryRequestInvocation call(f.loader,text("cached.igb"));
    check(call.resume(f.services).completed && call.result()==cached && cached->claim_count==10,"Cache hit did not acquire and return same directory.");
    check(call.resume().completed && cached->claim_count==10 && f.trace.empty(),"Cache hit replayed or reached miss-only services.");
    Fixture miss;miss.services->factory={};LoaderDirectoryRequestInvocation request(miss.loader,text("missing.igb"));
    const auto result=request.resume(miss.services);
    check(!result.completed && result.diagnostic.find("8002EBEC")!=std::string::npos,"Missing concrete factory was silently completed.");
    check(miss.loader->directories->count==0 && miss.file->loading==0,"Missing factory published later mutations.");
    rejects([&]{request.result();},"Pending directory request exposed a result.");
    Fixture decode;auto actual=decode.services->factory;
    decode.on_call=[&](const auto& step,auto&) {if(step.kind==LoaderRequestServiceKind::intern_filename)decode.services->factory={};};
    LoaderDirectoryRequestInvocation pending_load(decode.loader,text("new.igb"));
    check(!pending_load.resume(decode.services).completed,"Missing concrete decoder was silently completed.");
    check(decode.file->loading==1 && decode.file->claim_count==1 && decode.loader->directories->count==1 && decode.paths.empty(),
        "Missing decoder lost initialized/published loading prefix.");
    decode.services->factory=actual;check(pending_load.resume(decode.services).completed && pending_load.result()==decode.file,
        "Late decoder binding did not resume caller.");
    check(decode.file->loading==1,"Caller invented concrete decoder completion flags.");
}
void synchronous_fields_and_factory_capture() {
    Fixture f;const auto pool=f.loader->pool_38=object(),a=object(),b=object(),c=object();
    f.file->pool_118=a;f.file->pool_11c=b;f.file->pool_120=c;
    f.loader->format_44=255;f.loader->option_48=0x89abcdef;f.loader->option_08=9;
    f.file->initialize_after_load_115=31;f.file->succeeded_31=99;
    bool ready{};unsigned factory_calls{},resumes{};auto saved=f.services->factory;
    f.on_select=[&](const auto& step,auto& operation) {
        if(step.kind!=LoaderRequestServiceKind::create_igb_file)return;
        ++factory_calls;check(step.object==pool,"Concrete factory captured wrong pool38 argument.");
        auto out=operation.get();operation->resume=[&,out] {++resumes;if(!ready)return MessengerCallResult{false,0,"Prepared concrete factory."};
            out->file_result=f.file;return MessengerCallResult{true,0,{}};};
    };
    LoaderDirectoryRequestInvocation call(f.loader,text("name.igb"));
    check(!call.resume(f.services).completed && factory_calls==1,"Concrete factory did not pause.");
    const auto changed=object();f.loader->pool_38=changed;f.loader->pool_3c.reset();f.loader->pool_40=changed;
    f.loader->format_44=7;f.services->factory={};ready=true;
    check(!call.resume(f.services).completed && factory_calls==1 && resumes==2,"Factory was rebound while pending.");
    check(f.file->format_24==7 && f.file->option_a4==0x89abcdef && f.file->option_41==9,"Post-factory fields did not read live loader state.");
    check(f.file->pool_118==changed && f.file->pool_11c==b && f.file->pool_120==changed,"Non-null pool overrides or null-preserved defaults changed.");
    check(f.file->initialize_after_load_115==31 && f.file->succeeded_31==0 && f.file->loading==1,"Synchronous initialization changed async-only field or loading order.");
    f.services->factory=saved;check(call.resume(f.services).completed && call.result()==f.file,"Synchronous request did not finish after restoring services.");
    check(f.file->filename && *f.file->filename=="name.igb" && f.file->loader.lock()==f.loader && f.file->claim_count==1,"Name, weak parent or first claim publication changed.");
    check(f.paths==std::vector<std::string>{"name.igb"},"Bare synchronous load path changed.");
}
void asynchronous_exact_byte_and_live_branch() {
    for(unsigned mode:{1u,2u,255u}) {
        Fixture f;f.loader->async_1c=static_cast<std::uint8_t>(mode);f.loader->initialize_after_load_34=13;
        f.file->initialize_after_load_115=77;LoaderDirectoryRequestInvocation call(f.loader,text("async.igb"));
        check(call.resume(f.services).completed && call.result()==f.file,"Asynchronous request did not complete queue caller.");
        check(f.file->claim_count==2 && f.loader->directories->count==1 && f.file->loading==1 && f.paths.empty(),"Async caller lost second claim or invented completed decode.");
        check(bool(f.file->mutex)==(mode==1) && f.locks==(mode==1 ? 2u:0u) && f.unlocks==f.locks,"Only exact async1 may prepare/use mutex.");
        check(f.file->initialize_after_load_115==(mode==1 ? 13:77),"Non-one async byte incorrectly copied initialization option.");
        check(f.trace.back()==LoaderRequestServiceKind::enqueue_directory,"Async caller did not reach queue after append/claims.");
    }
    Fixture f;f.loader->async_1c=1;f.file->mutex=f.mutex;
    f.on_call=[&](const auto& step,auto&) {if(step.kind==LoaderRequestServiceKind::intern_filename)f.loader->async_1c=0;};
    LoaderDirectoryRequestInvocation call(f.loader,text("changed.igb"));
    check(call.resume(f.services).completed && f.file->claim_count==1 && f.paths.size()==1,"Late async byte reload did not choose synchronous path.");
    check(f.trace.front()==LoaderRequestServiceKind::create_igb_file && f.trace.at(1)==LoaderRequestServiceKind::intern_filename,
        "Existing mutex repeated factory or initialization.");
    Fixture second;second.on_call=[&](const auto& step,auto&) {if(step.kind==LoaderRequestServiceKind::intern_filename)second.loader->async_1c=2;};
    LoaderDirectoryRequestInvocation changed(second.loader,text("late.igb"));
    check(changed.resume(second.services).completed && second.file->claim_count==2 && !second.file->mutex && second.paths.empty(),
        "Late async transition replayed early mutex preparation or missed second claim.");
}
void retained_queue_and_claim_results() {
    Fixture f;f.loader->async_1c=2;const auto queue=f.loader->queue_20;bool ready{};unsigned factories{},calls{};
    f.on_select=[&](const auto& step,auto& operation) {
        if(step.kind!=LoaderRequestServiceKind::enqueue_directory)return;
        ++factories;check(step.object==queue && step.file==f.file && step.queue_wait,"Queue did not capture actual argument owners.");
        operation->resume=[&] {++calls;return MessengerCallResult{ready,999,"Prepared queue."};};
    };
    LoaderDirectoryRequestInvocation call(f.loader,text("queue.igb"));
    check(!call.resume(f.services).completed && f.file->claim_count==2 && f.loader->directories->count==1,"Pending queue lost preceding publications.");
    f.loader->queue_20=object();f.loader->async_1c=0;f.services->factory={};ready=true;
    check(call.resume(f.services).completed && factories==1 && calls==2 && f.paths.empty(),"Pending queue was redirected/restarted after async mutation.");
    check(call.resume().completed && calls==2 && f.file->claim_count==2,"Completed queue request replayed.");
    Fixture failed;failed.loader->async_1c=1;
    failed.mutex->acquire=[] {return MessengerCallResult{true,88,{}};};
    LoaderDirectoryRequestInvocation ignored(failed.loader,text("claims.igb"));
    check(ignored.resume(failed.services).completed && failed.file->claim_count==0 && failed.unlocks==0 && failed.loader->directories->count==1,
        "Request treated ignored claim-acquire failure as a request failure or incremented anyway.");
}
void append_prefixes_and_growth() {
    for(const auto [count,capacity,expected]:{std::tuple{0,0,4},std::tuple{4,4,8},std::tuple{1024,1024,2048}}) {
        Fixture f;f.loader->directories=list(count,capacity);f.services->directory->filename_field.reset();
        // Null metadata descriptor makes lookup return no match without reading
        // the intentionally empty occupied rows; append must still use the list.
        const auto slots=f.loader->directories;bool ready{};unsigned factories{};
        f.on_select=[&](const auto& step,auto& operation) {
            if(step.kind!=LoaderRequestServiceKind::resize_directory_list)return;
            ++factories;check(step.list==slots && step.capacity==expected,"Capacity growth policy changed.");
            operation->resume=[&,step] {if(!ready)return MessengerCallResult{false,0,"Prepared allocator."};
                step.list->storage->resize(static_cast<std::size_t>(step.capacity));step.list->capacity=step.capacity;return MessengerCallResult{true,0,{}};};
        };
        LoaderDirectoryRequestInvocation call(f.loader,text("grow.igb"));
        check(!call.resume(f.services).completed && slots->count==count && f.file->claim_count==1,"Pending allocator published count too soon.");
        f.loader->directories=list();ready=true;
        check(call.resume(f.services).completed && factories==1 && slots->count==count+1 && slots->storage->at(static_cast<std::size_t>(count))==f.file,
            "Growth recaptured replaced loader list or changed captured append index.");
        check(f.loader->directories->count==0,"Captured append mutated replacement list.");
    }
    for(int kind=0;kind<3;++kind) {
        Fixture f;f.services->directory->filename_field.reset();
        if(kind==0)f.loader->directories->storage.reset();
        if(kind==1)f.loader->directories->storage->clear();
        if(kind==2)f.loader->directories->count=-1;
        LoaderDirectoryRequestInvocation call(f.loader,text("bad.igb"));
        rejects([&]{call.resume(f.services);},"Append accepted an unavailable physical slot.");
        check(call.faulted() && f.file->claim_count==1 && f.file->loading==1 && f.loader->directories->count==(kind==2 ? 0:1),
            "Append failure rolled back count/claim/loading prefix.");
        rejects([&]{call.resume(f.services);},"Faulted append replayed.");
    }
    Fixture null;null.services->directory->filename_field.reset();null.loader->directories.reset();
    LoaderDirectoryRequestInvocation missing(null.loader,text("null.igb"));
    rejects([&]{missing.resume(null.services);},"Null append list was fabricated.");
    check(null.file->claim_count==1 && null.file->loading==1,"Null list failure preceded original first claim.");
}
void path_fallback_and_capture() {
    for(const auto statuses:{std::vector<std::int32_t>{1},std::vector<std::int32_t>{0,1},std::vector<std::int32_t>{-1,0,-9},
        std::vector<std::int32_t>{std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}}) {
        Fixture f;f.loader->primary_prefix=text("first");f.loader->secondary_prefix=text("second");f.file->filename=text("map.igb");
        f.decoder_results=statuses;LoaderFileLoadInvocation call(f.loader,f.file);
        check(call.resume(f.services).completed && call.result()==f.file,"Path wrapper failed based on ignored final decoder result.");
        check(f.paths.size()==statuses.size() && f.paths.front()=="first/map.igb","Signed positive path classification changed.");
        if(statuses.size()>1)check(f.paths.at(1)=="second/map.igb","Secondary path changed.");
        if(statuses.size()>2)check(f.paths.at(2)=="map.igb","Final bare fallback changed.");
    }
    Fixture f;f.loader->primary_prefix=text("old");f.loader->secondary_prefix=text("second");f.file->filename=text("old.igb");
    bool ready{};unsigned factories{},resumes{};auto original=f.services->factory;
    f.on_select=[&](const auto& step,auto& operation) {
        if(++factories!=1)return;
        check(*step.filename=="old/old.igb","Selected path was not formatted before decoder selection.");
        auto out=operation.get();operation->resume=[&,out,path=step.filename] {++resumes;if(!ready)return MessengerCallResult{false,0,"Prepared decoder."};
            f.paths.push_back(*path);out->load_result=0;return MessengerCallResult{true,0,{}};};
    };
    LoaderFileLoadInvocation call(f.loader,f.file);check(!call.resume(f.services).completed,"Decoder did not pause.");
    f.loader->primary_prefix=text("ignored");f.loader->secondary_prefix=text("new");f.file->filename=text("new.igb");
    f.services->factory={};ready=true;
    check(!call.resume(f.services).completed && f.paths==std::vector<std::string>{"old/old.igb"} && resumes==2,
        "Pending decoder changed captured path or missing next binding completed.");
    f.loader->secondary_prefix=text("too-late");f.file->filename=text("too-late.igb");f.services->factory=original;
    check(call.resume(f.services).completed && f.paths==std::vector<std::string>{"old/old.igb","new/new.igb"},
        "Reached secondary path arguments were recaptured while binding absent.");
    Fixture nul;nul.loader->primary_prefix=text(std::string("\0skip",5));nul.loader->secondary_prefix=text(std::string("base\0ignored",12));
    nul.file->filename=text(std::string("name\0ignored",12));LoaderFileLoadInvocation cstr(nul.loader,nul.file);
    check(cstr.resume(nul.services).completed && nul.paths==std::vector<std::string>{"base/name"},"Prefix sprintf/strlen semantics lost embedded NUL behavior.");
    LoaderFileLoadInvocation empty({},{});check(empty.resume().completed && !empty.result(),"Null directory read unused null loader.");
}
void malformed_and_nonreplay() {
    Fixture null_file;null_file.on_call=[](const auto& call,auto& out) {if(call.kind==LoaderRequestServiceKind::create_igb_file)out.file_result.reset();};
    LoaderDirectoryRequestInvocation a(null_file.loader,text("null.igb"));
    rejects([&]{a.resume(null_file.services);},"Null file factory result was accepted.");
    check(null_file.loader->directories->count==0,"Null file result reached append.");
    Fixture mutex;mutex.loader->async_1c=1;mutex.on_call=[](const auto& call,auto& out) {if(call.kind==LoaderRequestServiceKind::create_mutex)out.mutex_result.reset();};
    LoaderDirectoryRequestInvocation b(mutex.loader,text("mutex.igb"));rejects([&]{b.resume(mutex.services);},"Null mutex factory result was accepted.");
    check(!mutex.file->mutex && mutex.file->loading==0,"Null mutex failure occurred after loading publication.");
    Fixture queue;queue.loader->async_1c=2;queue.loader->queue_20.reset();LoaderDirectoryRequestInvocation c(queue.loader,text("queue.igb"));
    rejects([&]{c.resume(queue.services);},"Null async queue was fabricated.");
    check(queue.file->claim_count==2 && queue.loader->directories->count==1,"Null queue failure rolled back second claim or append.");
    Fixture long_path;long_path.loader->primary_prefix=text(std::string(2047,'x'));long_path.file->filename=text("a");
    LoaderFileLoadInvocation d(long_path.loader,long_path.file);rejects([&]{d.resume(long_path.services);},"Oversized sprintf path was accepted.");
    check(long_path.paths.empty(),"Oversized path reached decoder.");
    Fixture null_name;null_name.loader->primary_prefix=text("base");LoaderFileLoadInvocation e(null_name.loader,null_name.file);
    rejects([&]{e.resume(null_name.services);},"Null formatted filename was normalized.");
    Fixture pool;pool.file->pool_118=object();pool.loader->pool_38=object();
    const std::weak_ptr<ModelSceneObject> acquired_pool=pool.loader->pool_38;
    LoaderDirectoryRequestInvocation f(pool.loader,text("pool.igb"));rejects([&]{f.resume(pool.services);},"Unrepresented final pool destructor was silently completed.");
    check(pool.file->loading==0 && pool.file->pool_118!=pool.loader->pool_38,"Final pool destructor failure lost original prefix.");
    pool.loader->pool_38.reset();check(!acquired_pool.expired(),"Failed old-field release lost the incoming acquired pool reference.");
    Fixture replaced_mutex;replaced_mutex.loader->async_1c=1;std::weak_ptr<LoaderDirectoryMutex> overwritten;
    replaced_mutex.on_call=[&](const auto& call,auto&) {
        if(call.kind==LoaderRequestServiceKind::create_mutex) {
            replaced_mutex.file->mutex=std::make_shared<LoaderDirectoryMutex>();overwritten=replaced_mutex.file->mutex;
        }
    };
    LoaderDirectoryRequestInvocation mutex_store(replaced_mutex.loader,text("replacement.igb"));
    check(mutex_store.resume(replaced_mutex.services).completed && !overwritten.expired() && replaced_mutex.file->mutex==replaced_mutex.mutex,
        "Raw factory mutex store released the callback-injected owner.");
    Fixture recursive;LoaderDirectoryRequestInvocation g(recursive.loader,text("recursive.igb"));
    recursive.on_call=[&](const auto&,auto&) {rejects([&]{g.resume(recursive.services);},"Recursive request succeeded.");};
    rejects([&]{g.resume(recursive.services);},"Caught recursive failure allowed outer request to continue.");
    check(g.faulted() && recursive.loader->directories->count==0,"Recursive request did not poison outer prefix.");
    Fixture abandoned;abandoned.services->factory={};
    {LoaderDirectoryRequestInvocation first(abandoned.loader,text("a.igb"));check(!first.resume(abandoned.services).completed,"Missing factory did not pause.");
        LoaderDirectoryRequestInvocation second(abandoned.loader,text("b.igb"));rejects([&]{second.resume(abandoned.services);},"Overlapping request restarted the same loader.");}
    LoaderDirectoryRequestInvocation retry(abandoned.loader,text("a.igb"));rejects([&]{retry.resume(abandoned.services);},"Abandoned request replayed its loader prefix.");
}
}
int main() {
    try {cache_and_missing_boundaries();synchronous_fields_and_factory_capture();asynchronous_exact_byte_and_live_branch();
        retained_queue_and_claim_results();append_prefixes_and_growth();path_fallback_and_capture();malformed_and_nonreplay();
        std::cout<<"Loader request checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"Loader request failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
