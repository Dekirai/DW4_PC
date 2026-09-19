#include "dw4/igb_bootstrap_object.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw IgbBootstrapError(message);}
MessengerCallResult pending(const char* message) {return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::size_t position(const IgbBootstrapPointer& pointer,std::uint32_t relative,std::size_t width) {
    if(!pointer.storage || igb_bootstrap_address(pointer)==0)fail("Bootstrap object has no represented nonnull allocation.");
    const auto bytes=pointer.storage->bytes;
    const auto at=static_cast<std::uint64_t>(pointer.offset)+relative;
    if(!bytes || bytes->released || at>bytes->bytes.size() || width>bytes->bytes.size()-at ||
        at>bytes->initialized.size() || width>bytes->initialized.size()-at)
        fail("Bootstrap object reaches unavailable physical storage.");
    return static_cast<std::size_t>(at);
}
void synchronize_reference(const IgbBootstrapPointer& pointer,std::uint32_t relative,std::size_t width) {
    if(relative<4){const auto at=static_cast<std::size_t>(pointer.offset);const auto& bytes=*pointer.storage->bytes;
        if(at<=bytes.bytes.size() && bytes.bytes.size()-at>=4 && at<=bytes.initialized.size() && bytes.initialized.size()-at>=4 &&
            bytes.initialized[at] && bytes.initialized[at+1] && bytes.initialized[at+2] && bytes.initialized[at+3]){
            const auto word=igb_buffer_word(pointer.storage->bytes,at);
            for(const auto& binding:pointer.storage->bindings)if(binding.offset==pointer.offset && binding.field)binding.field->vtable_word=word;
        }
    }
    if(relative>=8 || static_cast<std::uint64_t>(relative)+width<=4)return;
    const auto& bytes=*pointer.storage->bytes;const auto at=static_cast<std::size_t>(pointer.offset)+4;
    if(at>bytes.bytes.size() || bytes.bytes.size()-at<4 || at>bytes.initialized.size() || bytes.initialized.size()-at<4)return;
    for(unsigned i=0;i<4;++i)if(!bytes.initialized[at+i])return;
    const auto word=igb_buffer_word(pointer.storage->bytes,at);
    for(const auto& binding:pointer.storage->bindings)if(binding.offset==pointer.offset) {
        if(binding.file)binding.file->reference_word=word;
        if(binding.metadata)binding.metadata->reference_word=word;
        if(binding.fields)binding.fields->reference_word=word;
        if(binding.types)binding.types->reference_word=word;
        if(binding.field)binding.field->reference_word=word;
    }
}
template<class T,class Get> std::shared_ptr<T> bound(const IgbBootstrapPointer& pointer,Get get) {
    if(!pointer.storage || igb_bootstrap_address(pointer)==0)fail("Bootstrap object requires an explicit typed allocation binding.");
    std::shared_ptr<T> result;unsigned selected_kind{};
    for(const auto& binding:pointer.storage->bindings)if(binding.offset==pointer.offset) {
        const unsigned kind=(binding.file?1u:0u)|(binding.metadata?2u:0u)|(binding.fields?4u:0u)|(binding.types?8u:0u)|(binding.field?16u:0u);
        if((kind && (kind&(kind-1u))) || (kind && selected_kind && kind!=selected_kind))
            fail("Bootstrap allocation has conflicting object kinds at one offset.");
        if(kind)selected_kind=kind;
        const auto value=get(binding);
        if(value && result && result!=value)fail("Bootstrap allocation has conflicting typed identities.");
        if(value)result=value;
    }
    if(!result)fail("Bootstrap object has no matching native layout binding.");return result;
}
struct Call {
    IgbBootstrapCall request;
    IgbBootstrapFactory factory;
    std::shared_ptr<IgbBootstrapOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};factory={};operation.reset();request={};}
    void select(IgbBootstrapCall value,const std::shared_ptr<IgbBootstrapServices>& services){
        clear();request=std::move(value);factory=services?services->factory:IgbBootstrapFactory{};
    }
    template<class Check> MessengerCallResult poll(const std::shared_ptr<IgbBootstrapServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Bootstrap construction requires its selected allocator or object operation.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Bootstrap construction reached an unbound allocator or object operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Bootstrap construction requires its retained operation continuation.");
        const auto result=callback();check();return result;
    }
};
} // namespace

std::uint32_t igb_bootstrap_address(const IgbBootstrapPointer& pointer) noexcept {
    return (pointer.storage?pointer.storage->original_address:0u)+pointer.offset;
}
std::uint32_t igb_bootstrap_read_word(const IgbBootstrapPointer& pointer,std::uint32_t relative){
    const auto at=position(pointer,relative,4);return igb_buffer_word(pointer.storage->bytes,at);
}
std::uint8_t igb_bootstrap_read_byte(const IgbBootstrapPointer& pointer,std::uint32_t relative){
    const auto at=position(pointer,relative,1);return igb_buffer_bytes(pointer.storage->bytes,at,1).front();
}
void igb_bootstrap_write_word(const IgbBootstrapPointer& pointer,std::uint32_t relative,std::uint32_t value){
    const auto at=position(pointer,relative,4);auto& bytes=*pointer.storage->bytes;
    for(unsigned i=0;i<4;++i){bytes.bytes[at+i]=static_cast<std::uint8_t>(value>>(24u-i*8u));bytes.initialized[at+i]=1;}
    synchronize_reference(pointer,relative,4);
}
void igb_bootstrap_write_halfword(const IgbBootstrapPointer& pointer,std::uint32_t relative,std::uint16_t value){
    const auto at=position(pointer,relative,2);auto& bytes=*pointer.storage->bytes;
    bytes.bytes[at]=static_cast<std::uint8_t>(value>>8u);bytes.bytes[at+1]=static_cast<std::uint8_t>(value);
    bytes.initialized[at]=bytes.initialized[at+1]=1;synchronize_reference(pointer,relative,2);
}
void igb_bootstrap_write_byte(const IgbBootstrapPointer& pointer,std::uint32_t relative,std::uint8_t value){
    const auto at=position(pointer,relative,1);auto& bytes=*pointer.storage->bytes;
    bytes.bytes[at]=value;bytes.initialized[at]=1;synchronize_reference(pointer,relative,1);
}
std::shared_ptr<IgbRuntimeMetaType> igb_bootstrap_metadata(const IgbBootstrapPointer& pointer){
    return bound<IgbRuntimeMetaType>(pointer,[](const IgbObjectBinding& b){return b.metadata;});
}
std::shared_ptr<IgbDecodeFieldRegistry> igb_bootstrap_fields(const IgbBootstrapPointer& pointer){
    return bound<IgbDecodeFieldRegistry>(pointer,[](const IgbObjectBinding& b){return b.fields;});
}
std::shared_ptr<IgbRuntimeTypeList> igb_bootstrap_types(const IgbBootstrapPointer& pointer){
    return bound<IgbRuntimeTypeList>(pointer,[](const IgbObjectBinding& b){return b.types;});
}
std::shared_ptr<IgbDecodeRuntimeField> igb_bootstrap_field(const IgbBootstrapPointer& pointer){
    return bound<IgbDecodeRuntimeField>(pointer,[](const IgbObjectBinding& b){return b.field;});
}

struct IgbRawAllocationInvocation::Impl {
    enum class Phase{entry,after_pool,allocate,after_allocate,call,done};
    std::shared_ptr<ModelSceneObject> pool;
    IgbBootstrapPointer result;
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    std::uint32_t bytes{},prefix{};
    bool pooled{},running{},fault{};
};
IgbRawAllocationInvocation::IgbRawAllocationInvocation(std::uint32_t bytes,std::uint32_t prefix,
    std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){
    impl_->bytes=bytes+prefix;impl_->prefix=prefix;impl_->pool=std::move(pool);
}
IgbRawAllocationInvocation::~IgbRawAllocationInvocation()=default;
bool IgbRawAllocationInvocation::completed()const noexcept{return impl_->phase==Impl::Phase::done;}
bool IgbRawAllocationInvocation::faulted()const noexcept{return impl_->fault;}
IgbBootstrapPointer IgbRawAllocationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete raw allocation has no result.");return impl_->result;
}
MessengerCallResult IgbRawAllocationInvocation::resume(std::shared_ptr<IgbBootstrapServices> services){
    auto& s=*impl_;using P=Impl::Phase;using K=IgbBootstrapServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Raw allocation requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.result),{}};
    Running running(s.running);const auto check=[&]{if(s.fault)fail("Raw allocation continuation was invalidated.");};
    const auto select=[&](K kind,P after){IgbBootstrapCall call;call.kind=kind;call.pool=s.pool;call.bytes=s.bytes;
        s.call.select(std::move(call),services);check();s.phase=P::call;s.after=after;};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        if(s.pool){s.phase=P::allocate;break;}
        if(!services || !services->default_pool_handle)return pending("Raw allocation requires default pool handle805621E0.");
        {const auto handle=*services->default_pool_handle;select(K::pool_for_handle,P::after_pool);s.call.request.handle=handle;}break;
    case P::after_pool:s.pool=std::move(s.call.operation->pool_result);s.call.clear();check();s.phase=P::allocate;break;
    case P::allocate:s.pooled=bool(s.pool);select(s.pooled?K::pool_allocate:K::heap_allocate,P::after_allocate);break;
    case P::after_allocate:
        s.result=std::move(s.call.operation->allocation_result);
        if(s.pooled)s.result.offset+=s.prefix;
        s.call.clear();check();s.phase=P::done;break;
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.phase=s.after;}break;
    case P::done:return {true,igb_bootstrap_address(s.result),{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbBootstrapBaseInvocation::Impl {
    enum class Phase{entry,reset,core,metadata,known_metadata,after_metadata,prefix,after_pool,after_index,call,done};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<void> token=std::make_shared<int>(0);
    Call call;
    Phase phase=Phase::entry,after=Phase::done;
    std::uint32_t returned{};
    bool with_pool_tag{},owns{},running{},fault{};
};
IgbBootstrapBaseInvocation::IgbBootstrapBaseInvocation(IgbBootstrapPointer pointer,bool with_pool_tag):impl_(std::make_unique<Impl>()){
    impl_->object=std::move(pointer);impl_->with_pool_tag=with_pool_tag;
}
IgbBootstrapBaseInvocation::~IgbBootstrapBaseInvocation(){
    if(impl_->owns && !completed())impl_->object.storage->fault_=true;
}
bool IgbBootstrapBaseInvocation::completed()const noexcept{return impl_->phase==Impl::Phase::done;}
bool IgbBootstrapBaseInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbBootstrapBaseInvocation::resume(std::shared_ptr<IgbBootstrapServices> services){
    auto& s=*impl_;using P=Impl::Phase;using K=IgbBootstrapServiceKind;
    if(s.running || s.fault){s.fault=true;if(s.owns)s.object.storage->fault_=true;fail("Base construction requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,igb_bootstrap_address(s.object),{}};
    Running running(s.running);
    const auto check=[&]{if(s.fault || (s.owns && s.object.storage->fault_))fail("Base construction continuation was invalidated.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbBootstrapCall&{clear();IgbBootstrapCall call;call.kind=kind;call.object=s.object;
        s.call.select(std::move(call),services);check();s.phase=P::call;s.after=after;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        if(s.object.storage){
            if(s.object.storage->fault_ || !s.object.storage->active_.expired())fail("Base allocation is faulted or already initializing.");
            s.object.storage->active_=s.token;s.owns=true;
        }
        if(s.with_pool_tag)igb_bootstrap_write_word(s.object,0,0x804717f0u);
        s.phase=P::reset;break;
    case P::reset:{
        igb_bootstrap_write_word(s.object,0,0x804717f0u);
        const auto old=igb_bootstrap_read_word(s.object,4);
        igb_bootstrap_write_word(s.object,4,(old&0xff800000u)|1u);
        const auto flags=igb_bootstrap_read_byte(s.object,5);
        igb_bootstrap_write_byte(s.object,5,static_cast<std::uint8_t>(flags&0x7fu));
        igb_bootstrap_write_byte(s.object,4,0);
        s.phase=s.with_pool_tag?P::core:P::done;break;
    }
    case P::core:
        if(!services || !services->runtime || !services->runtime->singleton)return pending("Base construction requires ArkCore80562100.");
        if(!*services->runtime->singleton)fail("Base construction dereferences a null ArkCore.");
        if((*services->runtime->singleton)->enabled==0){igb_bootstrap_write_byte(s.object,4,0);s.phase=P::done;}
        else s.phase=P::metadata;
        break;
    case P::metadata:{
        const auto vtable=igb_bootstrap_read_word(s.object,0);
        if(vtable==0)fail("Base allocation-address lookup dereferences a null virtual table.");
        if(vtable==0x804717f0u)s.phase=P::known_metadata;
        else select(K::base_metadata,P::after_metadata).vtable_word=vtable;
        break;
    }
    case P::known_metadata:
        if(!services || !services->object_metadata || !services->object_metadata->has_value())return pending("Base construction requires igObject metadata805622A4.");
        s.metadata=**services->object_metadata;s.phase=P::prefix;break;
    case P::after_metadata:s.metadata=std::move(s.call.operation->metadata_result);clear();s.phase=P::prefix;break;
    case P::prefix:
        if(!s.metadata)fail("Base allocation-address lookup dereferences null metadata.");
        if(!s.metadata->allocation_prefix)return pending("Base allocation-address lookup requires live metadata+20.");
        {const auto address=igb_bootstrap_address(s.object)-*s.metadata->allocation_prefix;
            select(K::allocation_pool,P::after_pool).address=address;}break;
    case P::after_pool:
        s.pool=std::move(s.call.operation->pool_result);clear();
        if(s.pool){const auto pool=s.pool;select(K::pool_index,P::after_index).pool=pool;}
        else {igb_bootstrap_write_byte(s.object,4,0);s.phase=P::done;}
        break;
    case P::after_index:clear();igb_bootstrap_write_byte(s.object,4,static_cast<std::uint8_t>(s.returned));s.phase=P::done;break;
    case P::call:{const auto result=s.call.poll(services,check);if(!result.completed)return result;s.returned=result.value;s.phase=s.after;}break;
    case P::done:
        clear();if(s.owns){s.object.storage->active_.reset();s.owns=false;}return {true,igb_bootstrap_address(s.object),{}};
    }}}catch(...){s.fault=true;if(s.owns)s.object.storage->fault_=true;throw;}
}
} // namespace dw4
