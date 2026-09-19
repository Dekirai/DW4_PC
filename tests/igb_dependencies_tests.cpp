#include "dw4/igb_dependencies.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <map>

namespace {
using namespace dw4;
using K=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
void word(std::vector<std::uint8_t>& bytes,std::uint32_t value,bool little=false) {
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<std::uint8_t>(value>>(little?i*8:(3-i)*8)));
}
std::vector<std::uint8_t> table(const std::vector<std::string>& names,bool little=false) {
    std::vector<std::uint8_t> body,bytes;
    for(const auto& name:names)word(body,static_cast<std::uint32_t>(name.size()+1),little);
    for(const auto& name:names) {body.insert(body.end(),name.begin(),name.end());body.push_back(0);}
    word(bytes,static_cast<std::uint32_t>(12+body.size()),little);word(bytes,0x11223344,little);
    word(bytes,static_cast<std::uint32_t>(names.size()),little);bytes.insert(bytes.end(),body.begin(),body.end());return bytes;
}
std::shared_ptr<IgbScratchBuffer> string_buffer(const std::string& value) {
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.assign(value.begin(),value.end());
    buffer->bytes.push_back(0);buffer->initialized.assign(buffer->bytes.size(),1);return buffer;
}
// Explicit prepared intern/loader/list endpoints. The actual caller owns the
// transactions, string decrements, argument captures, reads and publications.
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbStreamState> io=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbLoadingPathsState> paths=std::make_shared<IgbLoadingPathsState>();
    std::shared_ptr<IgbDependenciesState> state=std::make_shared<IgbDependenciesState>();
    std::shared_ptr<IgbDependenciesServices> services=std::make_shared<IgbDependenciesServices>();
    std::shared_ptr<SystemStageLoader> loader=std::make_shared<SystemStageLoader>();
    std::map<std::string,std::shared_ptr<IgbInternedString>> strings;
    std::vector<K> trace;
    std::vector<std::string> names;
    std::vector<std::shared_ptr<SystemStageDirectory>> loaded;
    std::function<void(const IgbDependenciesCall&,IgbDependenciesOperation&)> on_call;
    std::function<void(const IgbDependenciesCall&,std::shared_ptr<IgbDependenciesOperation>&)> on_select;
    explicit Fixture(std::vector<std::uint8_t> bytes=table({"one","two"})) {
        io->file=file;io->flag_d5=1;io->statuses->success=17;io->statuses->failure=91;
        io->int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});
        io->stream=igb_stream_from_bytes(text("folder/main.igb"),std::move(bytes));
        file->pool_118=std::make_shared<ModelSceneData>();igb_bind_native_scratch_pool(io,file->pool_118);
        paths->directory_path=string_buffer("folder");file->loader=loader;
        state->directories=std::make_shared<LoaderDirectoryList>();
        services->globals=std::make_shared<SystemStageServices>();services->globals->loader=loader;
        services->globals->string_pool=std::make_shared<ModelSceneData>();
        loader->primary_prefix=bind("root",1)->value;
        const auto native=make_native_igb_dependency_pool_factory();
        services->factory=[this,native](const IgbDependenciesCall& request) {
            auto operation=native(request);
            const bool pool=static_cast<bool>(operation);
            if(!operation)operation=std::make_shared<IgbDependenciesOperation>();
            const auto base=operation->resume;const auto weak=std::weak_ptr(operation);
            operation->resume=[this,request,base,pool,weak] {
                auto out=weak.lock();trace.push_back(request.kind);
                if(pool) {const auto result=base();if(!result.completed)return result;}
                else switch(request.kind) {
                case K::loader:out->loader_result=loader;break;
                case K::create_string_pool:
                    check(request.capacity==16,"String pool capacity changed.");out->pool_result=std::make_shared<ModelSceneData>();break;
                case K::initialize_string_pool:out->pool_result=request.pool;break;
                case K::intern_name: {
                    const auto value=igb_dependency_name(request.name);
                    if(value) {
                        check(static_cast<bool>(request.pool),"Prepared nonnull intern was called with a null pool.");
                        const auto key=std::string(value->c_str());auto found=strings.find(key);
                        if(found==strings.end() || found->second->released)found=strings.insert_or_assign(key,bind(key,0)).first;
                        ++found->second->references;out->string_result=found->second;
                    }
                    break;
                }
                case K::release_string:
                    check(request.string && request.string->references==0,"String release did not follow a full32 zero decrement.");
                    check(request.pool==request.string->container,"String release lost its captured container.");
                    request.string->released=true;break;
                case K::load_dependency: {
                    const auto value=igb_dependency_name(request.name);check(static_cast<bool>(value),"Prepared dependency name is null.");
                    names.push_back(*value);auto directory=std::make_shared<SystemStageDirectory>();directory->filename=value;
                    directory->reference_word=0xff800001;loaded.push_back(directory);out->directory_result=directory;break;
                }
                case K::reserve_directories:
                    check(request.capacity>=0 && request.capacity<=262144,"Prepared directory reserve exceeds bounds.");
                    request.directories->storage->resize(static_cast<std::size_t>(request.capacity));request.directories->capacity=request.capacity;break;
                default:throw std::runtime_error("Unbound pool identity reached the prepared fixture.");
                }
                if(on_call)on_call(request,*out);
                return MessengerCallResult{true,0,{}};
            };
            if(on_select)on_select(request,operation);return operation;
        };
    }
    std::shared_ptr<IgbInternedString> bind(const std::string& value,std::uint32_t references) {
        auto owner=std::make_shared<IgbInternedString>();owner->value=text(value);owner->references=references;
        owner->container=std::make_shared<ModelSceneData>();igb_bind_interned_string(services->strings,owner);strings[value]=owner;return owner;
    }
    std::unique_ptr<IgbDependenciesInvocation> invocation() {return std::make_unique<IgbDependenciesInvocation>(file,io,paths,state);}
};
void transactions() {
    for(bool little:{false,true}) {
        Fixture f(table({"one","two","one"},little));f.io->swapped=static_cast<std::uint8_t>(little);
        const auto original=f.strings.at("root");std::shared_ptr<IgbScratchBuffer> allocation;
        f.on_call=[&](const auto& request,auto& out) {
            if(request.kind==K::allocate_body)allocation=out.buffer_result;
            if(request.kind==K::load_dependency)check(request.loader==f.loader && *f.loader->primary_prefix=="folder","Dependency load did not see the installed directory.");
        };
        auto call=f.invocation();check(call->resume(f.services).completed && call->value()==17,"Valid dependency table did not complete.");
        check(f.names==std::vector<std::string>({"one","two","one"}),"Dependency names/order changed.");
        check(f.state->directories->count==3 && f.state->directories->storage->at(2)==f.loaded[2],"Dependency list publication lost identity/order.");
        for(const auto& directory:f.loaded)check(directory->reference_word==0xff800002,"Dependency append changed packed retain behavior.");
        check(f.loader->primary_prefix==original->value && original->references==1,"Loader prefix identity/reference count was not restored.");
        check(!f.state->saved_prefix && !f.state->body && allocation->released,"Temporary dependency/string storage was not cleared.");
        check(f.strings.at("folder")->references==0 && f.strings.at("folder")->released,"Installed directory string was not finally released.");
        check(std::count(f.trace.begin(),f.trace.end(),K::intern_name)==4,"Caller did not execute all four intern operations.");
        const auto trace=f.trace;const auto cursor=f.io->stream->position;
        check(call->resume().completed && trace==f.trace && cursor==f.io->stream->position,"Completed dependency caller replayed work.");
    }
    Fixture f;f.services->globals->string_pool.reset();auto call=f.invocation();
    check(call->resume(f.services).completed,"Lazy string pool construction failed.");
    check(std::count(f.trace.begin(),f.trace.end(),K::create_string_pool)==1 && std::count(f.trace.begin(),f.trace.end(),K::initialize_string_pool)==1,"Lazy pool allocation/initialization repeated.");
    for(const auto references:{0u,0x80000001u}) {
        Fixture g;auto stale=g.bind("stale",references);g.state->saved_prefix=stale;auto run=g.invocation();
        check(run->resume(g.services).completed && stale->references==references-1u && !stale->released,"String decrement used object low23 bits or saturated underflow.");
    }
}
void gates_and_short_reads() {
    Fixture gate;gate.io->flag_d5=0;gate.io->stream.reset();gate.file->pool_118.reset();gate.services.reset();
    IgbDependenciesInvocation disabled(gate.file,gate.io,{},gate.state);
    check(disabled.resume().completed && disabled.value()==17,"Disabled dependency flag touched unread path/stream/services.");
    for(std::size_t length=0;length<12;++length) {
        Fixture f{std::vector<std::uint8_t>(length)};f.state->body_bytes=123;auto call=f.invocation();
        check(call->resume(f.services).completed && call->value()==91 && f.state->body_bytes==123 && f.trace.empty(),"Short header published state or reached allocation.");
    }
    auto bytes=table({"one"});bytes.resize(bytes.size()-2);Fixture short_body(bytes);auto call=short_body.invocation();
    check(call->resume(short_body.services).completed && call->value()==91,"Short dependency payload did not return failure.");
    check(short_body.state->body && !short_body.state->body->released && short_body.trace==std::vector<K>{K::allocate_body},"Short payload freed or consumed its published allocation.");
    for(std::uint32_t count:{0u,0xffffffffu,0x80000000u}) {
        std::vector<std::uint8_t> empty;word(empty,13);word(empty,0);word(empty,count);empty.push_back(0);
        Fixture f(empty);f.io->flag_d5=255;auto run=f.invocation();
        check(run->resume(f.services).completed && f.loaded.empty() && !f.state->body,"Signed nonpositive count reached names or skipped final cleanup.");
    }
    Fixture late;late.io->statuses->success.reset();auto finish=late.invocation();
    check(!finish->resume(late.services).completed && !late.state->body && !late.state->saved_prefix,"Missing final status replayed or skipped cleanup.");
    const auto trace=late.trace;late.io->statuses->success=77;
    check(finish->resume(late.services).completed && finish->value()==77 && trace==late.trace,"Late status replayed the completed prefix.");
}
void pending_and_live_inputs() {
    Fixture f;std::shared_ptr<IgbDependenciesOperation> selected;IgbDependenciesCall captured;
    f.on_select=[&](const auto& request,auto& operation) {
        if(request.kind==K::load_dependency && !selected) {
            selected=operation;captured=request;const auto original=operation->resume;
            operation->resume=[original,calls=0]() mutable {if(calls++==0)return MessengerCallResult{false,0,"prepared pause"};return original();};
        }
    };
    auto call=f.invocation();check(!call->resume(f.services).completed,"Dependency call did not pause.");
    const auto first_loader=f.loader;auto other=std::make_shared<SystemStageLoader>();other->primary_prefix=f.bind("other",1)->value;
    f.file->loader=other;f.state->body->bytes[8]='O';
    const auto before=f.trace.size();check(call->resume(f.services).completed,"Retained dependency operation did not complete.");
    check(captured.loader==first_loader && f.names.front()=="One" && f.trace.size()>before,"Dependency continuation recaptured loader or eagerly copied name bytes.");
    check(other->primary_prefix==f.strings.at("root")->value,"Restore did not use the live file loader after the loop.");

    Fixture g;auto globals=g.services->globals;g.services->globals.reset();auto paused=g.invocation();
    check(!paused->resume(g.services).completed,"Absent string global did not pause before prefix capture.");
    const auto replacement=g.bind("changed",1);g.loader->primary_prefix=replacement->value;
    g.services->globals=globals;
    check(paused->resume(g.services).completed && g.loader->primary_prefix==replacement->value,"Prefix was captured before its preceding missing-global read.");

    Fixture h;h.loader->primary_prefix=text("unbound");auto unknown=h.invocation();
    check(!unknown->resume(h.services).completed,"Unrepresented old string reference was silently dropped.");
    const auto unbound=h.loader->primary_prefix;auto owner=std::make_shared<IgbInternedString>();owner->value=unbound;owner->container=std::make_shared<ModelSceneData>();
    igb_bind_interned_string(h.services->strings,owner);
    check(unknown->resume(h.services).completed && owner->references==0 && owner->released,"Late old-string owner binding replayed or skipped its release.");
}
void append_and_failure_prefixes() {
    Fixture f;auto old=f.state->directories;
    f.on_call=[&](const auto& request,auto&) {if(request.kind==K::reserve_directories) {request.directories->count=99;f.state->directories=std::make_shared<LoaderDirectoryList>();}};
    auto call=f.invocation();check(call->resume(f.services).completed,"Live list replacement did not finish.");
    check(old->count==1 && old->storage->at(0)==f.loaded[0] && f.state->directories->count==0,"Append lost captured list/count across reserve.");

    Fixture short_list(table({"one"}));short_list.state->directories->capacity=1;
    auto failing=short_list.invocation();rejects([&]{failing->resume(short_list.services);},"Short physical append backing was accepted.");
    check(short_list.state->directories->count==1 && short_list.loaded.front()->reference_word==0xff800002,"Append fault rolled back retained owner/count prefix.");
    check(failing->faulted() && short_list.state->faulted() && short_list.state->body && *short_list.loader->primary_prefix=="folder","Fault silently cleaned the original prefix.");
    rejects([&]{failing->resume(short_list.services);},"Faulted dependency invocation restarted.");

    Fixture bad_length(table({"one"}));bad_length.on_call=[&](const auto& request,auto&) {if(request.kind==K::load_dependency)bad_length.state->body->initialized[0]=0;};
    auto malformed=bad_length.invocation();rejects([&]{malformed->resume(bad_length.services);},"Unavailable descriptor length was ignored.");
    check(bad_length.state->directories->count==1 && !bad_length.state->body->released,"Descriptor length was read before append or fault incorrectly freed body.");

    Fixture abandoned;abandoned.services->factory={};
    {auto pending_call=abandoned.invocation();check(!pending_call->resume(abandoned.services).completed,"Missing allocator binding fabricated success.");}
    check(abandoned.state->faulted(),"Abandoned dependency invocation did not poison its prefix.");
    auto restarted=abandoned.invocation();rejects([&]{restarted->resume(abandoned.services);},"Abandoned dependency prefix restarted.");
}
}
int main() {
    try {transactions();gates_and_short_reads();pending_and_live_inputs();append_and_failure_prefixes();
        std::cout<<"IGB dependency checks passed: "<<checks<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"IGB dependency checks failed after "<<checks<<": "<<error.what()<<'\n';return 1;}
}
