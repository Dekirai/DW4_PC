#include "dw4/igb_gamecube_thread_adopt.hpp"

#include <array>

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* text){throw IgbGamecubeThreadAdoptError(text);}
MessengerCallResult pending(const char* text){return {false,0,text};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
struct RawPhysical {
    IgbBootstrapPointer pointer;std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t guest{};
    explicit RawPhysical(IgbBootstrapPointer value):pointer(std::move(value)),
        bytes(pointer.storage?pointer.storage->bytes:nullptr),guest(igb_bootstrap_address(pointer)){}
    void validate()const{
        if(!pointer.storage || !guest || !bytes || bytes->released || pointer.storage->faulted() ||
            pointer.storage->bytes!=bytes || igb_bootstrap_address(pointer)!=guest)
            fail("Thread adoption lost its captured physical storage identity.");
    }
    std::uint32_t read(std::uint32_t offset,unsigned width)const{
        validate();const auto at=static_cast<std::uint64_t>(pointer.offset)+offset;
        if(at>bytes->bytes.size() || width>bytes->bytes.size()-at ||
            at>bytes->initialized.size() || width>bytes->initialized.size()-at)
            fail("Thread adoption source reaches an unavailable physical read.");
        const auto data=igb_buffer_bytes(bytes,static_cast<std::size_t>(at),width);
        std::uint32_t value{};for(const auto byte:data)value=(value<<8u)|byte;return value;
    }
};
struct TargetPhysical:RawPhysical {
    IgbObjectInstance instance;bool bound{};
    explicit TargetPhysical(IgbBootstrapPointer value):RawPhysical(std::move(value)){}
    const IgbBootstrapPointer& access(){
        validate();
        if(!bound){instance.storage=pointer.storage;instance.offset=pointer.offset;
            instance.runtime_object=igb_bootstrap_runtime_object(pointer);bound=true;}
        igb_object_validate_instance(instance);
        if(instance.runtime_object->kind!=IgbRuntimeObjectKind::thread)
            fail("Thread adoption requires its canonical reflected thread identity.");
        return pointer;
    }
};
std::optional<IgbBootstrapPointer> resolve(const std::shared_ptr<IgbOsThreadState>& os,std::uint32_t guest){
    if(!guest)return IgbBootstrapPointer{};
    std::optional<IgbBootstrapPointer> result;
    if(os)for(const auto& pointer:os->mappings){
        if(igb_bootstrap_address(pointer)!=guest)continue;
        if(result && (result->storage!=pointer.storage || result->offset!=pointer.offset))
            fail("Current OS thread has conflicting physical address mappings.");
        result=pointer;
    }
    return result;
}
struct CopyGroup{std::uint32_t source;unsigned width,count;};
constexpr std::array<CopyGroup,16> tail{{
    {0x2c8,2,1},{0x2ca,2,1},{0x2cc,4,1},{0x2d0,4,1},{0x2d4,4,1},
    {0x2d8,4,1},{0x2dc,4,1},{0x2e0,4,2},{0x2e8,4,2},{0x2f0,4,1},
    {0x2f4,4,2},{0x2fc,4,2},{0x304,4,1},{0x308,4,1},{0x30c,4,1},{0x310,4,2}
}};
struct PoolCall {
    IgbMemoryFieldConstructCall request;IgbMemoryFieldConstructFactory factory;
    std::shared_ptr<IgbMemoryFieldConstructOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbGamecubeThreadAdoptServices>& services,const Check& check){
        check();if(!factory && services && services->memory)factory=services->memory->factory;
        if(!factory)return pending("Thread adoption requires its captured tag5929C pool service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread adoption reached an unbound tag5929C pool operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Thread adoption requires its retained tag5929C continuation.");
        const auto result=callback();check();return result;
    }
};
struct FreeCall {
    IgbGamecubeThreadAdoptCall request;IgbGamecubeThreadAdoptFactory factory;
    std::shared_ptr<IgbGamecubeThreadAdoptOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbGamecubeThreadAdoptServices>& services,const Check& check){
        check();if(!factory && services)factory=services->factory;
        if(!factory)return pending("Thread adoption requires its captured pool+108 stack release.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Thread adoption reached an unbound pool+108 stack release.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Thread adoption requires its retained stack-release continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
}
struct IgbGamecubeThreadAdoptInvocation::Impl {
    enum class P{first_current,publish_master,second_current,resolve_source,read_first,read_second,
        write_first,write_second,next_group,stack,tag,pool,select_free,free_stack,clear_stack,done};
    P phase=P::first_current;TargetPhysical thread;std::unique_ptr<RawPhysical> source;
    std::shared_ptr<IgbGamecubeThreadAdoptServices> owner;
    std::shared_ptr<ModelSceneObject> pool;PoolCall lookup;FreeCall free;
    std::uint32_t first_address{},source_address{},stack{},first{},second{};
    unsigned group{};bool running{},fault{};
    explicit Impl(IgbBootstrapPointer value):thread(std::move(value)){}
    CopyGroup current_group()const{return group<89?CopyGroup{group*8u,4,2}:tail.at(group-89);}
};
IgbGamecubeThreadAdoptInvocation::IgbGamecubeThreadAdoptInvocation(IgbBootstrapPointer pointer)
    :impl_(std::make_unique<Impl>(std::move(pointer))){}
IgbGamecubeThreadAdoptInvocation::~IgbGamecubeThreadAdoptInvocation(){auto& s=*impl_;
    if((!completed() || faulted()) && s.owner){if(s.thread.pointer.storage)s.owner->retained.push_back(s.thread.pointer.storage);
        if(s.thread.bytes)s.owner->retained.push_back(s.thread.bytes);
        if(s.source && s.source->pointer.storage)s.owner->retained.push_back(s.source->pointer.storage);
        if(s.source && s.source->bytes)s.owner->retained.push_back(s.source->bytes);if(s.pool)s.owner->retained.push_back(s.pool);}}
bool IgbGamecubeThreadAdoptInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbGamecubeThreadAdoptInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbGamecubeThreadAdoptInvocation::resume(std::shared_ptr<IgbGamecubeThreadAdoptServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Thread adoption requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);
    if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Thread adoption was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Thread adoption was invalidated by its enclosing callback.");};
    // External callbacks must preserve the selected receiver. During the copy,
    // destination identity is checked at the reached store after source reads.
    const auto boundary_check=[&]{check();s.thread.access();};
    const auto store=[&](std::uint32_t relative,unsigned width,std::uint32_t value){
        const auto& target=s.thread.access();if(width==2)igb_bootstrap_write_halfword(target,relative,static_cast<std::uint16_t>(value));
        else igb_bootstrap_write_word(target,relative,value);
    };
    try{for(;;){check();switch(s.phase){
    case P::first_current:case P::second_current:
        if(!services || !services->os || !services->os->current_thread_address)
            return pending("Thread adoption requires canonical current OS thread global800000E4.");
        if(s.phase==P::first_current){s.first_address=*services->os->current_thread_address;s.phase=P::publish_master;}
        else{s.source_address=*services->os->current_thread_address;s.phase=P::resolve_source;}break;
    case P::publish_master:store(0x350,4,s.first_address);s.phase=P::second_current;break;
    case P::resolve_source:{
        const auto source=resolve(services?services->os:nullptr,s.source_address);
        if(!source)return pending("Thread adoption requires its captured current-thread source mapping.");
        s.source=std::make_unique<RawPhysical>(*source);s.phase=P::read_first;break;}
    case P::read_first:{const auto group=s.current_group();s.first=s.source->read(group.source,group.width);
        s.phase=group.count==2?P::read_second:P::write_first;break;}
    case P::read_second:{const auto group=s.current_group();s.second=s.source->read(group.source+group.width,group.width);s.phase=P::write_first;break;}
    case P::write_first:{const auto group=s.current_group();store(group.source+0x38,group.width,s.first);
        s.phase=group.count==2?P::write_second:P::next_group;break;}
    case P::write_second:{const auto group=s.current_group();store(group.source+0x38+group.width,group.width,s.second);s.phase=P::next_group;break;}
    case P::next_group:s.phase=++s.group==89+tail.size()?P::stack:P::read_first;break;
    case P::stack:s.stack=igb_bootstrap_read_word(s.thread.access(),0x20);s.phase=s.stack?P::tag:P::clear_stack;break;
    case P::tag:
        s.lookup.request.kind=IgbMemoryFieldConstructServiceKind::tagged_pool;
        s.lookup.request.handle=igb_bootstrap_read_byte(s.thread.access(),4);s.lookup.request.object=s.thread.instance;
        if(services && services->memory)s.lookup.factory=services->memory->factory;s.phase=P::pool;break;
    case P::pool:{const auto result=s.lookup.poll(services,boundary_check);if(!result.completed)return result;
        s.pool=s.lookup.operation->pool_result;s.lookup.clear();boundary_check();s.phase=P::select_free;break;}
    case P::select_free:
        if(!s.pool)fail("Thread adoption dereferences a null tagged pool at+108.");
        s.free.request.object=s.thread.pointer;s.free.request.pool=s.pool;s.free.request.address=s.stack;s.free.request.virtual_slot=0x108;
        if(services)s.free.factory=services->factory;s.phase=P::free_stack;break;
    case P::free_stack:{const auto result=s.free.poll(services,boundary_check);if(!result.completed)return result;
        s.free.clear();boundary_check();s.phase=P::clear_stack;break;}
    case P::clear_stack:store(0x20,4,0);s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbThreadStartupFactory make_native_igb_gamecube_thread_adopt_factory(std::shared_ptr<IgbGamecubeThreadAdoptServices> services,
    IgbThreadStartupFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadStartupCall& call)->std::shared_ptr<IgbThreadStartupOperation>{
        if(call.kind!=IgbThreadStartupServiceKind::object_virtual || call.vtable_word!=0x80470110 || call.virtual_slot!=0xdc)
            return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbGamecubeThreadAdoptInvocation>(call.object);
        auto operation=std::make_shared<IgbThreadStartupOperation>();
        operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};
        return operation;
    };
}
} // namespace dw4
