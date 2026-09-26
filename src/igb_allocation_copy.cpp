#include "dw4/igb_allocation_copy.hpp"

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* message){throw IgbAllocationCopyError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
struct Binding {
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t base{};
    std::size_t at(std::uint32_t guest,bool reading)const{
        const auto offset=guest-base;
        if(!guest || !bytes || bytes->released || offset>=bytes->bytes.size() || offset>=bytes->initialized.size() ||
            (reading && !bytes->initialized[offset]))
            fail(reading?"Allocation copy reads unavailable source bytes.":"Allocation copy writes unavailable destination bytes.");
        return offset;
    }
};
std::optional<Binding> resolve(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,std::uint32_t address){
    if(!address)fail("Allocation copy reaches a null allocation pointer.");
    std::optional<Binding> result;
    if(services)for(const auto& mapping:services->allocations){
        if(!mapping.bytes)continue;
        const auto base=mapping.address-mapping.offset,offset=address-base;
        if(offset>=mapping.bytes->bytes.size() && address!=mapping.address)continue;
        if(result && (result->bytes!=mapping.bytes || result->base!=base))
            fail("Allocation copy pointer has conflicting physical mappings.");
        result=Binding{mapping.bytes,base};
    }
    return result;
}
}
struct IgbAllocationCopyInvocation::Impl {
    enum class P{begin,source,read,destination,write,done};P phase=P::begin;
    std::uint32_t destination{},source{},count{},step{},index{};
    std::uint8_t byte{};bool backwards{},running{},fault{};
    std::optional<Binding> source_binding,destination_binding;
    std::shared_ptr<IgbMemoryFieldConstructServices> owner;
};
IgbAllocationCopyInvocation::IgbAllocationCopyInvocation(std::uint32_t destination,std::uint32_t source,std::uint32_t count)
    :impl_(std::make_unique<Impl>()){
    impl_->destination=destination;impl_->source=source;impl_->count=count;impl_->backwards=source<destination;
}
IgbAllocationCopyInvocation::~IgbAllocationCopyInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.source_binding && s.source_binding->bytes)s.owner->retained.push_back(s.source_binding->bytes);
    if(s.destination_binding && s.destination_binding->bytes)s.owner->retained.push_back(s.destination_binding->bytes);
}}
bool IgbAllocationCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbAllocationCopyInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbAllocationCopyInvocation::resume(std::shared_ptr<IgbMemoryFieldConstructServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Allocation copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.destination,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Allocation copy was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Allocation copy was invalidated by its enclosing callback.");};
    try{for(;;){check();switch(s.phase){
    case P::begin:s.phase=s.count?P::source:P::done;break;
    case P::source:
        s.source_binding=resolve(services,s.source);
        if(!s.source_binding)return pending("Allocation copy requires its captured source allocation binding.");
        s.phase=P::read;break;
    case P::read:
        if(s.step==s.count){s.phase=P::done;break;}
        s.index=s.backwards?s.count-1u-s.step:s.step;
        s.byte=s.source_binding->bytes->bytes[s.source_binding->at(s.source+s.index,true)];
        s.phase=s.destination_binding?P::write:P::destination;break;
    case P::destination:
        s.destination_binding=resolve(services,s.destination);
        if(!s.destination_binding)return pending("Allocation copy requires its captured destination allocation binding.");
        s.phase=P::write;break;
    case P::write:{
        const auto at=s.destination_binding->at(s.destination+s.index,false);
        s.destination_binding->bytes->bytes[at]=s.byte;s.destination_binding->bytes->initialized[at]=1;
        ++s.step;s.phase=P::read;break;}
    case P::done:return {true,s.destination,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
