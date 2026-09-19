#include "dw4/igb_memory_pools.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    ++checks;try {operation();}catch(const std::runtime_error&) {return;}throw std::runtime_error(reason);
}
SystemStageString text(std::string name) {return std::make_shared<const std::string>(std::move(name));}
IgbRuntimeNameSource source(std::string name,std::uint32_t offset=0,bool padding=true) {
    auto buffer=std::make_shared<IgbScratchBuffer>();buffer->bytes.resize(offset);
    buffer->bytes.insert(buffer->bytes.end(),name.begin(),name.end());
    buffer->bytes.resize(buffer->bytes.size()+(padding?4:1));
    buffer->initialized.assign(buffer->bytes.size(),std::uint8_t{1});return {buffer,offset};
}
struct Fixture {
    std::shared_ptr<IgbMemoryPoolRegistry> registry=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbMemoryPoolsServices> services=std::make_shared<IgbMemoryPoolsServices>();
    Fixture() {services->selector=0;services->fallback=registry;}
};
void replace_name(const std::shared_ptr<IgbMemoryPoolRegistry>& registry,std::size_t slot,std::string_view value) {
    auto& bytes=registry->inline_bytes->bytes;const auto offset=0x18+slot*0x90;
    std::fill_n(bytes.begin()+offset,128,std::uint8_t{});std::copy(value.begin(),value.end(),bytes.begin()+offset);
}
void original_names() {
    Fixture f;
    const std::array<std::pair<const char*,std::uint32_t>,10> fixtures{{
        {"Bootstrap",0},{"Default",1},{"Current",2},{"System",4},{"MetaData",5},
        {"List",8},{"Texture",12},{"AI",19},{"User0",30},{"User15",45}}};
    for(const auto& [name,expected]:fixtures)for(std::uint32_t alignment=0;alignment<4;++alignment) {
        IgbMemoryPoolLookupInvocation call(source(name,alignment));
        check(call.resume(f.services).completed && call.value()==expected,"Original pool-name handle or alignment changed.");
        f.services->selector.reset();check(call.resume().completed && call.value()==expected,"Completed lookup reselected mutable globals.");
        f.services->selector=0;
    }
    for(const auto name:{"DEFAULT","Missing","","User16"}) {
        IgbMemoryPoolLookupInvocation call(text(name));
        check(call.resume(f.services).completed && call.value()==0xffffffffu,"Missing/empty/case-distinct pool name acquired a handle.");
    }
    IgbMemoryPoolLookupInvocation embedded(text(std::string("Default\0suffix",14)));
    check(embedded.resume(f.services).completed && embedded.value()==1,"Embedded terminator was compared as a host string suffix.");
    replace_name(f.registry,0,"User15");f.registry->inline_bytes->bytes.resize(0x18+128);
    f.registry->inline_bytes->initialized.resize(0x18+128);
    IgbMemoryPoolLookupInvocation first(text("User15"));
    check(first.resume(f.services).completed && first.value()==0,"First duplicate match touched a later unavailable physical entry.");
}
void selection_and_capture() {
    for(const auto selector:{std::numeric_limits<std::int32_t>::min(),-1,4,5,std::numeric_limits<std::int32_t>::max()}) {
        Fixture f;f.services->selector=selector;IgbMemoryPoolLookupInvocation call(SystemStageString{});
        rejects([&]{call.resume(f.services);},"Invalid selector fell back or skipped the null manager dereference.");
        check(call.faulted(),"Reached null-manager fault did not poison lookup.");
        f.services->selector=0;rejects([&]{call.resume(f.services);},"Faulted lookup restarted on another manager.");
    }
    for(std::int32_t selector=1;selector<4;++selector) {
        Fixture f;f.services->selector=selector;f.services->fallback.reset();
        IgbMemoryPoolLookupInvocation call(text("Current"));
        check(!call.resume(f.services).completed,"Absent selected table entry fabricated a manager.");
        f.services->selector=0;f.services->active[static_cast<std::size_t>(selector-1)]=f.registry;
        check(call.resume(f.services).completed && call.value()==2,"Pending table read recaptured the selector or required fallback.");
    }
    {Fixture f;f.services->selector.reset();IgbMemoryPoolLookupInvocation call(text("Default"));
        check(!call.resume().completed && !call.resume(f.services).completed,"Missing selector global was treated as zero.");
        rejects([&]{call.value();},"Incomplete lookup exposed a handle.");
        f.services->selector=0;f.services->fallback.reset();check(!call.resume(f.services).completed,"Missing fallback global became represented null.");
        f.services->fallback=std::shared_ptr<IgbMemoryPoolRegistry>{};
        rejects([&]{call.resume(f.services);},"Represented null fallback was treated as pending initialization.");}
    {Fixture f;f.registry->original_lookup_bound=false;auto name=source("Default");IgbMemoryPoolLookupInvocation call(name);
        check(!call.resume(f.services).completed,"Unbound virtual lookup fabricated success.");
        auto replacement=make_original_igb_memory_pool_registry();replace_name(replacement,0,"Default");
        f.services->fallback=replacement;f.services->selector=7;f.registry->original_lookup_bound=true;
        std::copy_n("Current",7,name.buffer->bytes.begin());
        check(call.resume(f.services).completed && call.value()==2,"Virtual-boundary resume lost captured owner or read the name eagerly.");}
}
void physical_read_boundaries() {
    {Fixture f;f.registry->inline_bytes.reset();
        IgbMemoryPoolLookupInvocation null_name(SystemStageString{}),empty(source("",0,false));
        check(null_name.resume(f.services).completed && null_name.value()==0xffffffffu,"Null request read the inline name array.");
        check(empty.resume(f.services).completed && empty.value()==0xffffffffu,"Empty request read beyond its first byte.");}
    {Fixture f;auto name=source("Vertex",0,false);IgbMemoryPoolLookupInvocation call(name);
        rejects([&]{call.resume(f.services);},"Aligned strcmp fabricated unread bytes after a terminating NUL.");
        check(call.faulted(),"Aligned physical read failure did not poison lookup.");}
    {Fixture f;auto name=source("Vertex",1,false);IgbMemoryPoolLookupInvocation call(name);
        check(call.resume(f.services).completed && call.value()==10,"Unequal-alignment byte path required unused padding.");}
    {Fixture f;auto name=source("Vertex");name.buffer->initialized[7]=0;IgbMemoryPoolLookupInvocation call(name);
        rejects([&]{call.resume(f.services);},"Aligned strcmp accepted an uninitialized trailing word byte.");}
    {Fixture f;auto name=source("Z",0,false);IgbMemoryPoolLookupInvocation call(name);
        check(call.resume(f.services).completed && call.value()==0xffffffffu,"Early first-byte mismatch eagerly read unused requested bytes.");}
    {Fixture f;auto name=source("Default");name.buffer->released=true;IgbMemoryPoolLookupInvocation call(name);
        rejects([&]{call.resume(f.services);},"Released requested-name alias was accepted.");}
    {Fixture f;f.registry->inline_bytes->initialized[0x18]=0;IgbMemoryPoolLookupInvocation call(text("Default"));
        rejects([&]{call.resume(f.services);},"Unknown physical inline name bytes were skipped.");}
    {Fixture f;f.registry->inline_bytes->bytes.resize(0x18);IgbMemoryPoolLookupInvocation call(text("Default"));
        rejects([&]{call.resume(f.services);},"Missing first physical name slot was fabricated.");}
}
}
int main(int argc,char**) {
    try {
        if(argc!=1)throw std::runtime_error("Usage: igb_memory_pools_tests");
        original_names();selection_and_capture();physical_read_boundaries();
        std::cout<<"igb_memory_pools_tests: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
