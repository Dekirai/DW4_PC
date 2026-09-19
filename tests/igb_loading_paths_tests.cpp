#include "dw4/igb_loading_paths.hpp"
#include "dw4/igb_memory_pools.hpp"
#include "dw4/igb.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace dw4;
using K=IgbLoadingPathsServiceKind;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& fn,const char* reason) {++checks;try {fn();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);}
SystemStageString text(std::string value) {return std::make_shared<const std::string>(std::move(value));}
void word(std::vector<std::uint8_t>& out,std::uint32_t value,bool little=false) {
    for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(8u*(little?i:3-i))));
}
std::vector<std::uint8_t> names(const std::vector<std::string>& entries,bool little=false,std::optional<std::uint32_t> count={}) {
    std::vector<std::uint8_t> data;for(const auto& entry:entries) {data.insert(data.end(),entry.begin(),entry.end());data.push_back(0);}
    // Original strcmp can read an aligned word containing the terminator.
    do {data.push_back(0);}while(data.size()%4!=0);
    std::vector<std::uint8_t> out;word(out,static_cast<std::uint32_t>(data.size()+8),little);
    word(out,count.value_or(static_cast<std::uint32_t>(entries.size())),little);out.insert(out.end(),data.begin(),data.end());return out;
}
struct Fixture {
    std::shared_ptr<LoaderRequestFile> file=std::make_shared<LoaderRequestFile>();
    std::shared_ptr<IgbStreamState> io=std::make_shared<IgbStreamState>();
    std::shared_ptr<IgbLoadingPathsState> state=std::make_shared<IgbLoadingPathsState>();
    std::shared_ptr<IgbLoadingPathsServices> services=std::make_shared<IgbLoadingPathsServices>();
    std::vector<K> trace;
    std::vector<std::shared_ptr<IgbScratchBuffer>> allocated;
    std::function<void(const IgbLoadingPathsCall&,IgbLoadingPathsOperation&)> on_call;
    std::function<void(const IgbLoadingPathsCall&,std::shared_ptr<IgbLoadingPathsOperation>&)> on_select;
    Fixture(std::vector<std::uint8_t> bytes={},std::string filename="dir/ndmw.igb",bool little=false) {
        io->file=file;file->pool_118=std::make_shared<ModelSceneData>();igb_bind_native_scratch_pool(io,file->pool_118);
        io->stream=igb_stream_from_bytes(text(std::move(filename)),std::move(bytes));io->swapped=little;
        io->int_field=std::make_shared<IgbEndianField>(IgbEndianField{4});io->statuses->success=17;io->statuses->failure=91;
        services->default_pool_handle=0xffffffffu;services->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->memory_pools->selector=0;services->memory_pools->fallback=make_original_igb_memory_pool_registry();
        auto native=make_native_igb_path_pool_factory([](const IgbLoadingPathsCall& call) {
            auto operation=std::make_shared<IgbLoadingPathsOperation>();const auto weak=std::weak_ptr(operation);
            operation->resume=[call,weak] {
                if(call.kind==K::create_pool_handle_list)weak.lock()->list_result=std::make_shared<IgbPoolHandleList>();
                else {
                    if(!call.list || call.capacity<0 || call.capacity>262144)throw std::runtime_error("Prepared pool-list allocation exceeds its profile.");
                    call.list->capacity=call.capacity;call.list->backing->resize(static_cast<std::size_t>(call.capacity));
                }
                return MessengerCallResult{true,0,{}};
            };return operation;
        });
        services->factory=[this,native](const IgbLoadingPathsCall& call) {
            auto operation=native(call);auto callback=operation->resume;const auto weak=std::weak_ptr(operation);
            operation->resume=[this,call,callback,weak] {
                const auto result=callback();if(!result.completed)return result;
                trace.push_back(call.kind);const auto output=weak.lock();
                if(output->buffer_result)allocated.push_back(output->buffer_result);
                if(on_call)on_call(call,*output);return result;
            };
            if(on_select)on_select(call,operation);return operation;
        };
    }
    IgbDirectoryPathInvocation path() {return {file,io,state};}
    IgbPoolNamesInvocation pools() {return {file,io,state};}
};
void directory_paths() {
    const std::pair<std::string,std::string> cases[]{{"",""},{"name.igb",""},{"dir/name.igb","dir"},
        {"C:\\games/part\\name.igb","C:\\games/part"},{"/name.igb",""},{"dir/","dir"},{"//a","/"},
        {std::string("a/b\0ignored/path",16),"a"}};
    for(const auto& [name,expected]:cases) {
        Fixture f({},name);auto old=f.state->directory_path=std::make_shared<IgbScratchBuffer>();std::weak_ptr<IgbScratchBuffer> retained=old;
        auto call=f.path();check(call.resume(f.services).completed && call.value()==17,"Directory path did not complete.");
        check(*igb_runtime_read_name({f.state->directory_path,0})==expected && !old->released,"Directory separator truncation or old raw pointer lifetime changed.");
        old.reset();check(!retained.expired() && f.io->stream->position==0,"Directory duplication freed an old raw path or consumed stream bytes.");
        const auto count=f.trace.size();check(call.resume().completed && f.trace.size()==count,"Completed path duplication replayed its allocator.");
    }
    {Fixture f;f.io->native_scratch_pools.clear();const auto pool=f.file->pool_118;auto call=f.path();
        check(!call.resume(f.services).completed && !f.state->directory_path,"Unknown native pool silently duplicated a filename.");
        f.file->pool_118=std::make_shared<ModelSceneData>();f.io->stream->filename=text("changed.igb");igb_bind_native_scratch_pool(f.io,pool);
        check(call.resume(f.services).completed && *igb_runtime_read_name({f.state->directory_path,0})=="dir",
            "Pending duplicate recaptured the live pool or filename.");}
    {Fixture f;f.on_call=[&](const auto&,auto&) {f.io->statuses->success.reset();};auto call=f.path();
        check(!call.resume(f.services).completed && *igb_runtime_read_name({f.state->directory_path,0})=="dir","Missing status did not retain completed path stores.");
        f.io->statuses->success=0x80000000u;check(call.resume(f.services).completed && call.value()==0x80000000u && f.trace.size()==1,"Late path status repeated duplication.");}
    {Fixture f;f.on_call=[](const auto&,auto& output) {output.buffer_result.reset();};auto call=f.path();
        rejects([&]{call.resume(f.services);},"Null duplicate was accepted as an empty path.");check(call.faulted() && !f.state->directory_path,"Null duplicate did not preserve its raw publication.");}
}
void pool_tables() {
    for(bool little:{false,true}) {
        Fixture f(names({"Bootstrap","Default","Unknown",""},little),"data.igb",little);auto call=f.pools();
        check(call.resume(f.services).completed && call.value()==17,"Pool-name table did not complete.");
        const auto list=f.state->pool_handles;check(list->count==46 && list->reference_word==1 && list->backing->at(0)==0 && list->backing->at(1)==1 &&
            list->backing->at(2)==0xffffffffu && list->backing->at(3)==0xffffffffu,"Pool names resolved to wrong numeric handles or lost padding.");
        check(std::all_of(list->backing->begin()+4,list->backing->begin()+46,[](auto value){return value==0xffffffffu;}),"Default padding was not applied to every remaining entry.");
        check(f.allocated.size()==1 && f.allocated[0]->released && f.allocated[0]->bytes.size()==f.io->stream->bytes->size(),
            "Pool-name caller allocated payload-only bytes or omitted final free.");
    }
    for(const auto count:{0u,0xffffffffu,0x80000000u}) {
        Fixture f(names({},false,count));f.services->memory_pools.reset();auto call=f.pools();
        check(call.resume(f.services).completed && f.state->pool_handles->count==46,"Nonpositive signed name count touched the registry or skipped padding.");
    }
    {Fixture f(names(std::vector<std::string>(49,"Bootstrap")));auto call=f.pools();
        check(call.resume(f.services).completed && f.state->pool_handles->count==49,"More than46 actual pool names were truncated.");}
    {Fixture f(names({"Bootstrap"}));const auto list=f.state->pool_handles=std::make_shared<IgbPoolHandleList>();
        list->count=60;list->capacity=64;list->backing->assign(64,123);auto call=f.pools();
        check(call.resume(f.services).completed && f.state->pool_handles==list && list->count==46 && list->backing->at(50)==123 &&
            std::count(f.trace.begin(),f.trace.end(),K::create_pool_handle_list)==0,"Existing list was replaced or unused physical slots were cleared.");}
}
void pending_mutation_and_failure() {
    {Fixture f(names({"Bootstrap"}));f.services->default_pool_handle.reset();auto call=f.pools();
        check(!call.resume(f.services).completed && f.state->pool_handles->count==2 && !f.allocated.front()->released,
            "Missing padding global did not pause after original count publication.");
        f.services->default_pool_handle=37;check(call.resume(f.services).completed && f.state->pool_handles->backing->at(1)==37,
            "Late padding global skipped its captured output slot.");}
    {Fixture f(names({"Bootstrap"}));std::shared_ptr<IgbPoolHandleList> selected;
        const auto replacement=std::make_shared<IgbPoolHandleList>();replacement->count=5;replacement->capacity=64;replacement->backing->resize(64);
        f.on_call=[&](const auto& request,auto&) {if(request.kind==K::reserve_pool_handle_list && !selected) {
            selected=request.list;request.list->count=99;f.state->pool_handles=replacement;f.services->default_pool_handle=32;
        }};
        auto call=f.pools();check(call.resume(f.services).completed && selected->count==1 && selected->backing->at(0)==0 &&
            replacement->count==50 && replacement->backing->at(5)==32,"Growth did not preserve captured index/list or reload the next output owner.");}
    {auto bytes=names({"Default"});bytes.resize(bytes.size()-2);Fixture f(bytes);auto old=f.state->pool_handles=std::make_shared<IgbPoolHandleList>();old->count=9;
        f.on_call=[&](const auto& request,auto&) {if(request.kind==K::free_pool_names)f.io->statuses->failure=73;};auto call=f.pools();
        check(call.resume(f.services).completed && call.value()==73 && old->count==9 && f.allocated.front()->released,
            "Short payload did not free before status or prematurely clear the old list.");}
    {Fixture f(std::vector<std::uint8_t>(7));auto call=f.pools();
        check(call.resume(f.services).completed && call.value()==91 && f.trace.empty() && !f.state->pool_handles,"Short header reached allocation or list writes.");}
    {Fixture f(names({"Default"}));f.on_call=[](const auto& request,auto& output) {if(request.kind==K::allocate_pool_names)output.buffer_result.reset();};auto call=f.pools();
        check(call.resume(f.services).completed && call.value()==91 && f.io->stream->position==8 && f.trace.size()==1,
            "Null allocation reached payload read, list setup or free.");}
    {Fixture f(names({"Default"}));f.on_call=[](const auto& request,auto&) {if(request.kind==K::reserve_pool_handle_list)request.list->backing->clear();};auto call=f.pools();
        rejects([&]{call.resume(f.services);},"Short physical handle backing was accepted.");
        check(f.state->pool_handles->count==1 && !f.allocated.front()->released && f.state->pools_faulted(),"Failed handle store rolled back the prefix or freed early.");}
    {Fixture f(names({"Default"}));f.services->memory_pools.reset();{auto call=f.pools();check(!call.resume(f.services).completed,"Missing memory manager binding invented a handle.");}
        auto retry=f.pools();rejects([&]{retry.resume(f.services);},"Abandoned pool-name work replayed its stream read.");}
    {Fixture f(names({"Default"}));auto call=f.pools();f.on_call=[&](const auto&,auto&) {try {call.resume(f.services);}catch(const std::runtime_error&) {}};
        rejects([&]{call.resume(f.services);},"Swallowed recursive entry failed to poison the outer pool table.");}
}
void original_pool_table(const std::filesystem::path& supplied) {
    auto input=supplied;if(std::filesystem::is_directory(input)) {
        if(std::filesystem::is_directory(input/"files"))input/="files";input/="ndmw.igb";
    }
    std::ifstream file(input,std::ios::binary);check(bool(file),"Original pool-name input could not be opened.");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto metadata=read_igb_metadata(std::span<const std::uint8_t>(bytes));
    const auto little=metadata.header.byte_order==IgbByteOrder::little;
    const auto read=[&](std::size_t offset) {
        check(offset+4<=bytes.size(),"Original pool-name block length is truncated.");std::uint32_t value{};
        for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(bytes[offset+i])<<(8u*(little?i:3-i));return value;
    };
    const auto offset=metadata.metadata_bytes+read(metadata.metadata_bytes);const auto size=read(offset);
    check(read(offset+4)==46 && offset+size<=bytes.size(),"Original pool-name input is outside the recovered46-name profile.");
    Fixture f(std::move(bytes),"ndmw.igb",little);f.io->stream->position=static_cast<std::uint32_t>(offset);
    auto call=f.pools();check(call.resume(f.services).completed && call.value()==17,"Original pool-name table did not complete.");
    check(f.state->pool_handles->count==46 && f.io->stream->position==offset+size,"Original pool table consumed a wrong range.");
    for(std::uint32_t i=0;i<46;++i)check(f.state->pool_handles->backing->at(i)==i,"Original pool name disagrees with the independently recovered constructor name table.");
    check(f.allocated.front()->released,"Original pool-name allocation was retained after successful completion.");
    std::cout<<"Original pool-name table: 46 numeric handles, offset "<<offset<<", next cursor "<<f.io->stream->position<<".\n";
}
}
int main(int argc,char** argv) {
    if(argc>2) {std::cerr<<"Usage: dw4_igb_loading_paths_tests [EXTRACTED_ROOT]\n";return 2;}
    try {directory_paths();pool_tables();pending_mutation_and_failure();
        if(argc==2)original_pool_table(std::filesystem::path(argv[1]));std::cout<<"IGB loading path checks: "<<checks<<"\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"IGB loading paths failed after "<<checks<<" checks: "<<error.what()<<"\n";return 1;}
}
