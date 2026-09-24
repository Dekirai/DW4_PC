#include "dw4/igb_field_probe.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldProbeError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
bool scalar(IgbMetaFieldKind kind){return kind==IgbMetaFieldKind::signed_int || kind==IgbMetaFieldKind::boolean ||
    kind==IgbMetaFieldKind::signed_byte || kind==IgbMetaFieldKind::enumeration || kind==IgbMetaFieldKind::unsigned_byte;}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){
    return a.storage==b.storage && a.offset==b.offset && a.file==b.file && a.field==b.field &&
        a.metadata==b.metadata && a.fields==b.fields && a.types==b.types;
}
std::optional<IgbObjectInstance> object_binding(const std::shared_ptr<IgbFieldProbeServices>& services,
    std::uint32_t address,const IgbObjectInstance& local){
    std::optional<IgbObjectInstance> result;
    const auto consider=[&](const IgbObjectInstance& object){
        if(igb_bootstrap_address({object.storage,object.offset})!=address)return;
        if(!object.storage || object.storage->faulted())fail("Field probe requires an unfaulted captured object allocation.");
        igb_object_validate_instance(object);
        if(result && !same(*result,object))fail("Field probe object pointer has conflicting native identities.");
        result=object;
    };
    consider(local);
    if(services && services->object_cleanup)for(const auto& object:services->object_cleanup->objects)consider(object);
    return result;
}
}
struct IgbFieldProbeInvocation::Impl {
    enum class P {construct,core,offset,word,cache,object,object_resolve,object_destroy,base,name,resolve,release,done};
    IgbBootstrapPointer local;
    IgbMetaFieldKind kind{};
    std::shared_ptr<IgbFieldProbeServices> owner;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::unique_ptr<IgbMetaFieldConstructorInvocation> constructor;
    std::shared_ptr<IgbInternedString> string;
    IgbDependenciesCall request;IgbDependenciesFactory factory;
    std::shared_ptr<IgbDependenciesOperation> operation;
    std::function<MessengerCallResult()> callback;
    IgbObjectInstance object;
    IgbObjectFieldConstructCall object_request;
    IgbObjectFieldConstructFactory object_factory;
    std::shared_ptr<IgbObjectFieldConstructOperation> object_operation;
    std::function<MessengerCallResult()> object_callback;
    std::uint32_t offset{},value{},address{},slot{};
    P phase=P::construct,after=P::done;bool running{},fault{};
};
IgbFieldProbeInvocation::IgbFieldProbeInvocation(IgbBootstrapPointer local,IgbMetaFieldKind kind):impl_(std::make_unique<Impl>()){
    if(kind!=IgbMetaFieldKind::object && !scalar(kind) && kind!=IgbMetaFieldKind::memory_reference &&
        kind!=IgbMetaFieldKind::string && kind!=IgbMetaFieldKind::pointer)
        fail("Unsupported field layout probe profile.");
    impl_->local=std::move(local);impl_->kind=kind;
}
IgbFieldProbeInvocation::~IgbFieldProbeInvocation()=default;
bool IgbFieldProbeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldProbeInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbFieldProbeInvocation::value()const{
    if(!completed() || faulted())fail("Incomplete field layout probe has no value.");return impl_->value;
}
MessengerCallResult IgbFieldProbeInvocation::resume(std::shared_ptr<IgbFieldProbeServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Field layout probe requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.value,{}};Running running(s.running);
    if(!s.owner && services){s.owner=services;if(s.local.storage)s.owner->retained.push_back(s.local.storage);}
    const auto check=[&]{if(s.fault)fail("Field layout probe continuation was invalidated.");};
    const auto fallback=[&]{return services && services->fields && services->fields->dependencies?services->fields->dependencies->factory:IgbDependenciesFactory{};};
    const auto object_fallback=[&]{return services && services->object_cleanup?services->object_cleanup->factory:IgbObjectFieldConstructFactory{};};
    try{for(;;){check();switch(s.phase){
    case P::construct:
        if(!s.constructor)s.constructor=std::make_unique<IgbMetaFieldConstructorInvocation>(s.local,
            s.kind==IgbMetaFieldKind::unsigned_byte?IgbMetaFieldConstructor::reset:IgbMetaFieldConstructor::specialized_reset,s.kind);
        {const auto status=s.constructor->resume(services?services->fields:nullptr);check();if(!status.completed)return status;}
        s.field=s.constructor->result();s.constructor.reset();check();s.phase=P::core;break;
    case P::core:
        if(!services || !services->runtime || !services->runtime->singleton)return pending("Field probe requires live ArkCore80562100 after reset.");
        s.core=*services->runtime->singleton;
        // Actual23180 captures ArkCore before publishing the derived table,
        // then reads the captured owner's +394. No specialized reset is called.
        if(s.kind==IgbMetaFieldKind::unsigned_byte)igb_bootstrap_write_word(s.local,0,0x80470a44);
        s.phase=P::offset;break;
    case P::offset:
        if(!s.core)fail("Field probe dereferences null ArkCore at+394.");
        if(!s.core->vtable_offset)return pending("Field probe requires captured ArkCore offset+394.");
        s.offset=*s.core->vtable_offset;s.phase=P::word;break;
    case P::word:
        if(s.offset>igb_meta_field_profile(s.kind).bytes-4 || (s.offset&3u)!=0)fail("Field probe exceeds its aligned local-object profile.");
        s.value=igb_bootstrap_read_word(s.local,s.offset);
        igb_bootstrap_write_word(s.local,0,igb_meta_field_profile(s.kind).vtable);
        s.phase=scalar(s.kind) || s.kind==IgbMetaFieldKind::string || s.kind==IgbMetaFieldKind::pointer?P::base:P::cache;break;
    case P::base:
        if(!scalar(s.kind))igb_bootstrap_write_word(s.local,0,0x80471384);
        igb_bootstrap_write_word(s.local,0,0x80471914);s.phase=P::name;break;
    case P::cache:case P::name:
        s.slot=s.phase==P::cache?(s.kind==IgbMetaFieldKind::memory_reference?0x4cu:0x40u):0x0cu;
        s.after=s.phase==P::cache?(s.kind==IgbMetaFieldKind::memory_reference?P::object:P::base):P::done;
        if((s.kind==IgbMetaFieldKind::memory_reference || s.kind==IgbMetaFieldKind::boolean ||
            s.kind==IgbMetaFieldKind::string || s.kind==IgbMetaFieldKind::signed_byte ||
            s.kind==IgbMetaFieldKind::enumeration || s.kind==IgbMetaFieldKind::pointer || s.kind==IgbMetaFieldKind::unsigned_byte) &&
            igb_bootstrap_address(s.local)+s.slot==0){s.phase=s.after;break;}
        s.address=igb_bootstrap_read_word(s.local,s.slot);s.phase=s.address?P::resolve:s.after;break;
    case P::object:
        // The original reads this member after the preceding string callback.
        if(igb_bootstrap_address(s.local)+0x48u==0){s.phase=P::base;break;}
        s.address=igb_bootstrap_read_word(s.local,0x48);s.phase=s.address?P::object_resolve:P::base;break;
    case P::object_resolve:{
        const auto object=object_binding(services,s.address,{s.local.storage,s.local.offset,{},s.field});
        if(!object)return pending("Field probe requires the physical binding of its captured +48 object.");
        s.object=*object;if(s.owner)s.owner->retained.push_back(s.object.storage);
        igb_object_set_reference_word(s.object,igb_object_reference_word(s.object)-1u);
        if((igb_object_reference_word(s.object)&0x7fffffu)!=0){s.phase=P::base;break;}
        s.object_request.kind=IgbObjectFieldConstructServiceKind::destroy_object;s.object_request.object=s.object;
        s.object_factory=object_fallback();s.phase=P::object_destroy;break;
    }
    case P::object_destroy:
        if(!s.object_factory)s.object_factory=object_fallback();
        if(!s.object_factory)return pending("Field probe requires object destruction80066E1C.");
        if(!s.object_operation){s.object_operation=s.object_factory(s.object_request);check();}
        if(!s.object_operation)return pending("Field probe reached unbound object destruction80066E1C.");
        if(!s.object_callback)s.object_callback=s.object_operation->resume;
        if(!s.object_callback)return pending("Field probe requires its retained object-destruction continuation.");
        {const auto status=s.object_callback();check();if(!status.completed)return status;}
        s.object_callback={};s.object_operation.reset();s.object_factory={};s.object_request={};check();s.phase=P::base;break;
    case P::resolve:
        // The selected physical pointer survives missing bindings and callbacks.
        if(s.slot==0x0c && s.field->name_string_address && *s.field->name_string_address==s.address && s.field->name_string)
            s.string=*s.field->name_string;
        else if(services){const auto found=services->strings.find(s.address);
            if(found==services->strings.end())return pending("Field probe requires a canonical header for its captured string pointer.");
            s.string=found->second;
        }else return pending("Field probe requires a canonical header for its captured string pointer.");
        if(!s.string || s.string->released)fail("Field probe reads a null or released captured string header.");
        if(s.owner)s.owner->retained.push_back(s.string);
        --s.string->references;
        if(s.string->references!=0){s.string.reset();s.phase=s.after;break;}
        s.request={};s.request.kind=IgbDependenciesServiceKind::release_string;s.request.string=s.string;s.request.pool=s.string->container;
        s.factory=fallback();s.phase=P::release;break;
    case P::release:
        if(!s.factory)s.factory=fallback();
        if(!s.factory)return pending("Field probe requires string release8007381C.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Field probe reached an unbound string release8007381C.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Field probe requires its retained release continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}
        s.callback={};s.operation.reset();s.factory={};s.request={};s.string.reset();check();s.phase=s.after;break;
    case P::done:return {true,s.value,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbTypeRegistrationFactory make_native_igb_field_probe_factory(
    std::shared_ptr<IgbFieldProbeServices> services,IgbTypeRegistrationFactory remaining){
    std::weak_ptr<IgbFieldProbeServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbTypeRegistrationCall& call){
        if(call.kind!=IgbTypeRegistrationServiceKind::layout_probe ||
            (call.function!=0x80028934 && call.function!=0x8002e308 && call.function!=0x8002ad90 && call.function!=0x80038778 &&
                call.function!=0x80025348 && call.function!=0x80037c78 && call.function!=0x800314d4 && call.function!=0x80027648 &&
                call.function!=0x80023180))
            return remaining?remaining(call):std::shared_ptr<IgbTypeRegistrationOperation>{};
        auto selected=weak.lock();if(!selected)fail("Field probe services expired before selection.");
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();
        const auto kind=call.function==0x8002e308?IgbMetaFieldKind::signed_int:
            call.function==0x8002ad90?IgbMetaFieldKind::memory_reference:
            call.function==0x80038778?IgbMetaFieldKind::boolean:
            call.function==0x80025348?IgbMetaFieldKind::string:
            call.function==0x80037c78?IgbMetaFieldKind::signed_byte:
            call.function==0x800314d4?IgbMetaFieldKind::enumeration:
            call.function==0x80023180?IgbMetaFieldKind::unsigned_byte:
            call.function==0x80027648?IgbMetaFieldKind::pointer:IgbMetaFieldKind::object;
        operation->resume=[selected,kind,invocation=std::shared_ptr<IgbFieldProbeInvocation>{}]()mutable{
            if(!invocation){if(!selected->profile_local_storage && (kind!=IgbMetaFieldKind::object || !selected->local_storage))
                    return pending("Field probe requires represented stack-local storage.");
                auto local=selected->profile_local_storage?selected->profile_local_storage(kind):selected->local_storage();
                if(!igb_bootstrap_address(local))fail("Field probe has null stack-local storage.");
                invocation=std::make_shared<IgbFieldProbeInvocation>(std::move(local),kind);}
            return invocation->resume(selected);
        };return operation;
    };
}
} // namespace dw4
