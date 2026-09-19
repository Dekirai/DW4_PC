#include "dw4/igb_type_registration.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbTypeRegistrationError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
}
struct IgbTypeRegistrationInvocation::Impl {
    enum class Phase {pool,after_pool,cache,label,create,after_create,metadata,flags,parent,after_parent,
        parent_getter,after_parent_getter,after_attach,name,after_name,probe,after_probe,defaults,after_defaults,
        publish,after_publish,release,filter_core,filter_fields,filter_pool,filter_names,after_filter_find,
        filter_reload,filter_configure,layout,after_layout,post_layout,callbacks,callback_pool,after_callback_pool,
        after_callback_create,extras,after_append,trailing,finish,clear_busy,call,done};
    IgbTypeRegistrationArguments arguments;
    std::shared_ptr<IgbRuntimeMetaType> metadata,parent;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::shared_ptr<ModelSceneObject> pool,filter_names;
    std::shared_ptr<IgbRegistrationCallbackList> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> callback_services;
    SystemStageString label;
    IgbTypeRegistrationCall request;
    IgbTypeRegistrationFactory factory;
    std::shared_ptr<IgbTypeRegistrationOperation> operation;
    std::function<MessengerCallResult()> callback;
    Phase phase=Phase::pool,after=Phase::done;
    std::size_t extra_index{};
    std::uint32_t returned{};
    bool first{},running{},fault{};
};
IgbTypeRegistrationInvocation::IgbTypeRegistrationInvocation(IgbTypeRegistrationArguments arguments)
    :impl_(std::make_unique<Impl>()){impl_->arguments=std::move(arguments);}
IgbTypeRegistrationInvocation::~IgbTypeRegistrationInvocation(){
    if((!completed() || faulted()) && impl_->callbacks && impl_->callback_services)
        impl_->callback_services->unfinished_callbacks_.push_back(impl_->callbacks);
}
bool IgbTypeRegistrationInvocation::completed()const noexcept{return impl_->phase==Impl::Phase::done;}
bool IgbTypeRegistrationInvocation::faulted()const noexcept{return impl_->fault;}
std::shared_ptr<IgbRegistrationCallbackList> IgbTypeRegistrationInvocation::result()const{
    if(!completed() || faulted())fail("Incomplete type registration has no callback-list result.");return impl_->callbacks;
}
MessengerCallResult IgbTypeRegistrationInvocation::resume(std::shared_ptr<IgbTypeRegistrationServices> services){
    auto& s=*impl_;using P=Impl::Phase;using K=IgbTypeRegistrationServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Type registration requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};
    struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}} running(s.running);
    const auto check=[&]{if(s.fault)fail("Type registration continuation was invalidated.");};
    const auto clear=[&]{s.callback={};s.factory={};s.operation.reset();s.request={};check();};
    const auto select=[&](K kind,P after)->IgbTypeRegistrationCall&{
        clear();s.request.kind=kind;s.request.metadata=s.metadata;s.factory=services?services->factory:IgbTypeRegistrationFactory{};
        s.after=after;s.phase=P::call;return s.request;
    };
    const auto function=[&](K kind,std::uint32_t address,P after){
        if(address==0)fail("Type registration reaches a null required function pointer.");
        select(kind,after).function=address;
    };
    const auto pool=[&](P after)->bool{
        if(!services || !services->metadata_pool_handle)return false;
        const auto handle=*services->metadata_pool_handle;select(K::pool_for_handle,after).handle=handle;return true;
    };
    const auto core=[&]()->bool{
        if(!services || !services->runtime || !services->runtime->singleton)return false;
        s.core=*services->runtime->singleton;return true;
    };
    try{for(;;){check();switch(s.phase){
    case P::pool:
        if(!pool(P::after_pool))return pending("Type registration requires metadata-pool handle805621F4 before its cache read.");break;
    case P::after_pool:s.pool=std::move(s.operation->object_result);clear();s.phase=P::cache;break;
    case P::cache:
        if(!s.arguments.metadata_slot)fail("Type registration dereferences a null metadata cache cell.");
        if(!*s.arguments.metadata_slot)return pending("Type registration requires represented metadata cache storage.");
        s.phase=**s.arguments.metadata_slot?P::metadata:P::label;break;
    case P::label:{
        std::string label="igMetaObject: ";
        if(s.arguments.name){const auto end=s.arguments.name->find('\0');const auto count=end==std::string::npos?s.arguments.name->size():end;
            // A bounded host representation, not a model of corrupted original
            // stack storage after an overlong registration name.
            if(count>240)fail("Registration name exceeds the bounded stack-label profile.");label.append(*s.arguments.name,0,count);}
        s.label=std::make_shared<const std::string>(std::move(label));s.phase=P::create;break;
    }
    case P::create:{auto& request=select(K::create_metadata,P::after_create);request.pool=s.pool;request.allocation_label=s.label;break;}
    case P::after_create:{
        auto created=std::move(s.operation->metadata_result);clear();*s.arguments.metadata_slot=std::move(created);s.phase=P::metadata;break;
    }
    case P::metadata:
        if(!*s.arguments.metadata_slot)return pending("Type registration requires the reloaded metadata cache value.");
        s.metadata=**s.arguments.metadata_slot;
        if(!s.metadata)fail("Type registration dereferences null constructed metadata at +24.");s.phase=P::flags;break;
    case P::flags:
        if((s.metadata->class_flags&8u)!=0){s.phase=P::done;break;}
        s.metadata->class_flags|=8u;
        if((s.metadata->class_flags&4u)!=0)s.phase=P::clear_busy;
        else if((s.metadata->class_flags&2u)!=0)s.phase=P::layout;
        else{s.first=true;s.phase=(s.metadata->class_flags&1u)!=0?P::name:P::parent;}break;
    case P::parent:function(K::register_parent,s.arguments.parent_registration,P::after_parent);s.callback_services=services;break;
    case P::after_parent:
        s.callbacks=std::move(s.operation->callbacks_result);if(!s.callback_services)s.callback_services=services;
        clear();s.phase=P::parent_getter;break;
    case P::parent_getter:function(K::get_parent,s.arguments.parent_getter,P::after_parent_getter);break;
    case P::after_parent_getter:
        s.parent=std::move(s.operation->metadata_result);clear();select(K::attach_parent,P::after_attach).parent=s.parent;break;
    case P::after_attach:clear();s.metadata->class_flags|=1u;s.phase=P::name;break;
    case P::name:select(K::set_name,P::after_name).name=s.arguments.name;break;
    case P::after_name:
        clear();s.metadata->native_size=s.arguments.native_size;s.metadata->field_callback=s.arguments.field_callback;
        s.metadata->flag_1a=static_cast<std::uint8_t>(s.arguments.flag);s.phase=s.metadata->flag_1a==0?P::probe:P::defaults;break;
    case P::probe:function(K::layout_probe,s.arguments.layout_probe,P::after_probe);break;
    case P::after_probe:clear();s.metadata->type_word=s.returned;s.phase=P::defaults;break;
    case P::defaults:
        if(s.arguments.defaults_callback)function(K::defaults,s.arguments.defaults_callback,P::after_defaults);
        else s.phase=P::after_defaults;break;
    case P::after_defaults:clear();s.metadata->class_flags|=2u;s.phase=P::publish;break;
    case P::publish:select(K::publish_type,P::after_publish);break;
    case P::after_publish:clear();s.phase=P::release;break;
    case P::release:
        --s.metadata->reference_word;
        if((s.metadata->reference_word&0x7fffffu)==0)select(K::destroy_metadata,P::filter_core);
        else s.phase=P::filter_core;break;
    case P::filter_core:
        clear();if(!core())return pending("Type registration requires the fresh ArkCore filter owner.");s.phase=P::filter_fields;break;
    case P::filter_fields:
        if(!s.core)fail("Type registration dereferences null ArkCore filter state at +64.");
        if(!s.core->registration_filter_enabled)return pending("Type registration requires ArkCore filter word+64.");
        s.phase=*s.core->registration_filter_enabled==0?P::layout:P::filter_pool;break;
    case P::filter_pool:
        if(!s.core->registration_filter_pool)return pending("Type registration requires captured ArkCore filter pool+60.");
        s.phase=*s.core->registration_filter_pool?P::filter_names:P::layout;break;
    case P::filter_names:
        if(!s.core->registration_filter_names)return pending("Type registration requires captured ArkCore filter names+68.");
        s.filter_names=*s.core->registration_filter_names;
        if(s.filter_names){auto& request=select(K::find_filter_name,P::after_filter_find);request.object=s.filter_names;request.name=s.arguments.name;}
        else s.phase=P::layout;break;
    case P::after_filter_find:clear();s.phase=s.returned==0xffffffffu?P::layout:P::filter_reload;break;
    case P::filter_reload:
        if(!core())return pending("Type registration requires the reloaded ArkCore after filter lookup.");
        s.phase=P::filter_configure;break;
    case P::filter_configure:
        if(!s.core)fail("Type registration dereferences null reloaded ArkCore at +60.");
        if(!s.core->registration_filter_pool)return pending("Type registration requires the fresh filter pool+60.");
        {const auto selected=*s.core->registration_filter_pool;select(K::configure_filter,P::layout).pool=selected;}break;
    case P::layout:clear();select(K::compute_layout,P::after_layout);break;
    case P::after_layout:clear();s.phase=P::post_layout;break;
    case P::post_layout:
        if(!s.metadata->post_layout_callback)return pending("Type registration requires the live metadata callback+4C.");
        if(*s.metadata->post_layout_callback)function(K::post_layout,*s.metadata->post_layout_callback,P::callbacks);
        else s.phase=P::callbacks;break;
    case P::callbacks:
        clear();s.phase=s.callbacks?P::extras:P::callback_pool;break;
    case P::callback_pool:
        if(!pool(P::after_callback_pool))return pending("Registration callback-list allocation requires the fresh metadata-pool handle.");break;
    case P::after_callback_pool:
        s.pool=std::move(s.operation->object_result);clear();s.callback_services=services;
        {auto& request=select(K::create_callbacks,P::after_callback_create);request.pool=s.pool;request.bytes=12;}break;
    case P::after_callback_create:
        s.callbacks=std::move(s.operation->callbacks_result);clear();
        if(s.callbacks)igb_initialize_registration_callbacks(s.callbacks);s.phase=P::extras;break;
    case P::extras:
        if(!s.arguments.extra_callbacks){s.phase=P::trailing;break;}
        if(s.extra_index>=s.arguments.extra_callbacks->size() || s.extra_index>=65536)fail("Registration callback table reaches unavailable physical storage or exceeds the native bound.");
        if(const auto address=(*s.arguments.extra_callbacks)[s.extra_index];address){
            auto& request=select(K::append_callback,P::after_append);request.callbacks=s.callbacks;request.function=address;
        }else s.phase=P::trailing;break;
    case P::after_append:clear();++s.extra_index;s.phase=P::extras;break;
    case P::trailing:
        if(s.first && s.arguments.trailing_callback){auto& request=select(K::append_callback,P::finish);request.callbacks=s.callbacks;request.function=s.arguments.trailing_callback;}
        else s.phase=P::finish;break;
    case P::finish:clear();s.metadata->class_flags|=4u;s.phase=P::clear_busy;break;
    case P::clear_busy:s.metadata->class_flags&=~8u;s.phase=P::done;break;
    case P::call:
        if(!s.factory && services)s.factory=services->factory;
        if(!s.factory)return pending("Type registration requires its reached native engine operation.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Type registration reached an unbound parent, field, allocator or registry operation.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Type registration requires its retained operation continuation.");
        {const auto result=s.callback();check();if(!result.completed)return result;s.returned=result.value;s.phase=s.after;}break;
    case P::done:clear();return {true,0,{}};
    }}}
    catch(...){s.fault=true;throw;}
}
} // namespace dw4
