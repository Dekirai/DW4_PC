#include "dw4/igb_metadata_probe.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMetadataProbeError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
}
struct IgbMetadataProbeInvocation::Impl {
    enum class P {construct,core,offset,word,pool,name,resolve,release,done};
    IgbBootstrapPointer local;
    std::shared_ptr<IgbMetadataProbeServices> owner;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::unique_ptr<IgbMetadataConstructorInvocation> constructor;
    std::shared_ptr<IgbInternedString> string;
    IgbDependenciesCall request;
    IgbDependenciesFactory factory;
    std::shared_ptr<IgbDependenciesOperation> operation;
    std::function<MessengerCallResult()> callback;
    std::uint32_t offset{},value{},address{},slot{};
    P phase=P::construct,after=P::done;
    bool running{},fault{};
};
IgbMetadataProbeInvocation::IgbMetadataProbeInvocation(IgbBootstrapPointer local):impl_(std::make_unique<Impl>()){impl_->local=std::move(local);}
IgbMetadataProbeInvocation::~IgbMetadataProbeInvocation()=default;
bool IgbMetadataProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMetadataProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbMetadataProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete metadata probe has no value.");return impl_->value;
}
MessengerCallResult IgbMetadataProbeInvocation::resume(std::shared_ptr<IgbMetadataProbeServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Metadata probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Metadata probe continuation was invalidated.");};
    const auto fallback=[&]{return services && services->metadata && services->metadata->dependencies?services->metadata->dependencies->factory:IgbDependenciesFactory{};};
    try{for(;;){check();switch(s.phase){
    case P::construct:
        if(!s.constructor)s.constructor=std::make_unique<IgbMetadataConstructorInvocation>(s.local,IgbMetadataConstructor::reset);
        {const auto status=s.constructor->resume(services?services->metadata:nullptr);check();if(!status.completed)return status;}
        s.metadata=s.constructor->result();s.constructor.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->runtime || !services->runtime->singleton)return pending("Metadata probe requires live ArkCore80562100 after construction.");
        s.core=*services->runtime->singleton;s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Metadata probe dereferences null ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Metadata probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>0x60 || (s.offset&3u)!=0)fail("Metadata probe exceeds its aligned 0x64-byte local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);
        igb_bootstrap_write_word(s.local,0,0x804761d8);s.phase=P::pool;break;
    case P::pool:case P::name:
        s.slot=s.phase==P::pool?0x60u:0x1cu;s.after=s.phase==P::pool?P::name:P::done;
        s.address=igb_bootstrap_read_word(s.local,s.slot);s.phase=s.address?P::resolve:s.after;break;
    case P::resolve:{
        // Resolve the captured address, never a replacement live pointer.
        const auto& address=s.slot==0x60?s.metadata->default_pool_string_address:s.metadata->name_string_address;
        const auto& header=s.slot==0x60?s.metadata->default_pool_string:s.metadata->name_string;
        if(address && *address==s.address && header)s.string=*header;
        else if(services){const auto found=services->strings.find(s.address);
            if(found!=services->strings.end())s.string=found->second;
            else return pending("Metadata probe requires a canonical header for its captured string address.");}
        else return pending("Metadata probe requires a canonical header for its captured string address.");
        if(!s.string || s.string->released)fail("Metadata probe reads a null or released captured string header.");
        if(s.owner)s.owner->retained.push_back(s.string);
        --s.string->references;
        if(s.string->references!=0){s.string.reset();s.phase=s.after;break;}
        s.request={};s.request.kind=IgbDependenciesServiceKind::release_string;s.request.string=s.string;s.request.pool=s.string->container;
        s.factory=fallback();s.phase=P::release;break;
    }
    case P::release:
        if(!s.factory)s.factory=fallback();
        if(!s.factory)return pending("Metadata probe requires reached string release8007381C.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Metadata probe reached an unbound string release8007381C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Metadata probe requires its retained release continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        s.callback={};s.operation.reset();s.factory={};s.request={};s.string.reset();check();s.phase=s.after;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_metadata_probe_factory(
    std::shared_ptr<IgbMetadataProbeServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbMetadataProbeServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::layout_probe || call.function!=0x80029ffc)
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto selected=weak.lock();if(!selected)fail("Metadata probe services expired before selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        operation->resume=[selected,invocation=std::shared_ptr<IgbMetadataProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->local_storage)return pending("Metadata probe requires its represented stack-local storage.");
                auto local=selected->local_storage();if(!igb_bootstrap_address(local))fail("Metadata probe has null stack-local storage.");
                invocation=std::make_shared<IgbMetadataProbeInvocation>(std::move(local));}
            return invocation->resume(selected);
        };return operation;
    };
}
} // namespace dw4
