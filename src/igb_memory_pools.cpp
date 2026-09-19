#include "dw4/igb_memory_pools.hpp"
#include "dw4/igb_stream.hpp"

#include <algorithm>
#include <string_view>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbMemoryPoolsError(message);}
constexpr std::array<std::string_view,46> names{
    "Bootstrap","Default","Current","NonTracked","System","MetaData","String","Fast","List","Temporary",
    "Vertex","RenderList","Texture","Application","World","Actor","Level","Frame","Physics","AI",
    "DriverData","Clut","DMA","Audio","Video","Handles","Image","ImageObject","Attribute","Node",
    "User0","User1","User2","User3","User4","User5","User6","User7","User8","User9","User10","User11",
    "User12","User13","User14","User15"
};
std::uint8_t byte(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t offset) {
    return igb_buffer_bytes(buffer,offset,1).front();
}
// Equality path of original strcmp8009D5D8. Inline names and native scratch
// allocations have four-byte aligned bases. Keep the original left-then-right
// word reads, including bytes beyond an embedded terminator when aligned.
bool equal_name(const std::shared_ptr<IgbScratchBuffer>& left,std::uint32_t left_offset,
    const IgbRuntimeNameSource& right) {
    auto right_offset=right.offset;
    auto a=byte(left,left_offset),b=byte(right.buffer,right_offset);
    if(a!=b)return false;
    std::uint32_t consumed{};
    if((right_offset&3u)==0) {
        for(;;) {
            if(consumed>=4096)fail("Memory-pool comparison exceeds its represented name range.");
            const auto first=igb_buffer_word(left,left_offset);
            const auto second=igb_buffer_word(right.buffer,right_offset);
            if(((first-0x01010101u)&0x80808080u)!=0) {
                a=byte(left,left_offset);b=byte(right.buffer,right_offset);
                if(a!=b)return false;
                break;
            }
            if(first!=second)return false;
            left_offset+=4;right_offset+=4;consumed+=4;
        }
    }
    for(;;) {
        if(a==0)return true;
        if(++consumed>=4096)fail("Memory-pool comparison exceeds its represented name range.");
        a=byte(left,++left_offset);b=byte(right.buffer,++right_offset);
        if(a!=b)return false;
    }
}
}
std::shared_ptr<IgbMemoryPoolRegistry> make_original_igb_memory_pool_registry() {
    auto result=std::make_shared<IgbMemoryPoolRegistry>();
    result->inline_bytes=std::make_shared<IgbScratchBuffer>();
    //8005AF08 zeroes +14..19F3;8005D02C copies127 bytes and writes each
    // terminator at +97+i*90. Unused object-header bytes stay unrepresented.
    result->inline_bytes->bytes.resize(0x19f4);
    result->inline_bytes->initialized.resize(0x19f4);
    std::fill(result->inline_bytes->initialized.begin()+0x14,result->inline_bytes->initialized.end(),std::uint8_t{1});
    for(std::size_t index=0;index<names.size();++index)
        std::copy(names[index].begin(),names[index].end(),result->inline_bytes->bytes.begin()+0x18+index*0x90);
    result->original_lookup_bound=true;return result;
}
struct IgbMemoryPoolLookupInvocation::Impl {
    IgbRuntimeNameSource name;
    std::optional<std::int32_t> selector;
    std::shared_ptr<IgbMemoryPoolRegistry> registry;
    bool captured{},done{},fault{};
    std::uint32_t value{};
};
IgbMemoryPoolLookupInvocation::IgbMemoryPoolLookupInvocation(IgbRuntimeNameSource source)
    :impl_(std::make_unique<Impl>()) {impl_->name=std::move(source);}
IgbMemoryPoolLookupInvocation::IgbMemoryPoolLookupInvocation(SystemStageString name)
    :impl_(std::make_unique<Impl>()) {
    if(name) {
        auto buffer=std::make_shared<IgbScratchBuffer>();
        buffer->bytes.assign(name->begin(),name->end());buffer->bytes.resize(buffer->bytes.size()+4);
        buffer->initialized.assign(buffer->bytes.size(),std::uint8_t{1});impl_->name.buffer=std::move(buffer);
    }
}
IgbMemoryPoolLookupInvocation::~IgbMemoryPoolLookupInvocation()=default;
bool IgbMemoryPoolLookupInvocation::completed() const noexcept {return impl_->done;}
bool IgbMemoryPoolLookupInvocation::faulted() const noexcept {return impl_->fault;}
std::uint32_t IgbMemoryPoolLookupInvocation::value() const {
    if(!completed())fail("Incomplete memory-pool lookup has no returned handle.");return impl_->value;
}
MessengerCallResult IgbMemoryPoolLookupInvocation::resume(std::shared_ptr<IgbMemoryPoolsServices> services) {
    auto& s=*impl_;
    if(s.fault)fail("Memory-pool lookup requires an unfaulted invocation.");
    if(s.done)return {true,s.value,{}};
    try {
        if(!s.selector) {
            if(!services || !services->selector)return {false,0,"Memory-pool lookup requires selector global805621DC."};
            s.selector=*services->selector;
        }
        if(!s.captured) {
            if(*s.selector==0) {
                if(!services || !services->fallback)return {false,0,"Memory-pool lookup requires fallback global805621C8."};
                s.registry=*services->fallback;
            }else if(*s.selector>0 && *s.selector<4) {
                const auto index=static_cast<std::size_t>(*s.selector-1);
                if(!services || !services->active[index])return {false,0,"Memory-pool lookup requires its selected804F2294 entry."};
                s.registry=*services->active[index];
            }
            s.captured=true;
        }
        if(!s.registry)fail("Memory-pool lookup dereferences a null selected manager.");
        if(!s.registry->original_lookup_bound)return {false,0,"Memory-pool manager virtual+74 remains unrepresented."};
        s.value=0xffffffffu;
        if(s.name.buffer || s.name.offset!=0) {
            if(byte(s.name.buffer,s.name.offset)!=0)for(std::uint32_t index=0;index<46;++index)
                if(equal_name(s.registry->inline_bytes,0x18u+index*0x90u,s.name)) {s.value=index;break;}
        }
        s.done=true;return {true,s.value,{}};
    }catch(...) {s.fault=true;throw;}
}
} // namespace dw4
