#include "dw4/igb_object_field_construct.hpp"
#include "dw4/igb_field_defaults.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbObjectFieldConstructError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){
    return a.storage==b.storage && a.offset==b.offset && a.file==b.file && a.field==b.field &&
        a.metadata==b.metadata && a.fields==b.fields && a.types==b.types && a.runtime_object==b.runtime_object;
}
void validate(const IgbObjectInstance& object){
    const unsigned count=unsigned(bool(object.file))+unsigned(bool(object.field))+unsigned(bool(object.metadata))+
        unsigned(bool(object.fields))+unsigned(bool(object.types))+unsigned(bool(object.runtime_object));
    if(!address(object)){
        if(object.storage || count)fail("Null member pointer has a nonnull native identity.");return;
    }
    if(!object.storage || object.storage->faulted())fail("Member pointer requires unfaulted physical storage.");
    igb_object_validate_instance(object);
}
std::optional<IgbObjectInstance> resolve(const std::shared_ptr<IgbObjectFieldConstructServices>& services,
    std::uint32_t pointer,const IgbObjectInstance& target,const IgbObjectInstance& incoming){
    if(!pointer)return IgbObjectInstance{};std::optional<IgbObjectInstance> result;
    const auto consider=[&](const IgbObjectInstance& object){if(address(object)==pointer){
        validate(object);if(result && !same(*result,object))fail("Member pointer has conflicting native allocation identities.");result=object;
    }};
    consider(target);consider(incoming);if(services)for(const auto& object:services->objects)consider(object);return result;
}
struct Call {
    IgbObjectFieldConstructCall request;IgbObjectFieldConstructFactory factory;
    std::shared_ptr<IgbObjectFieldConstructOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbObjectFieldConstructServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Object member construction requires its reached pool or destructor service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Object member construction reached an unbound pool or destructor operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Object member construction requires its retained service continuation.");
        const auto status=callback();check();return status;
    }
};
void select_destroy(Call& call,const IgbObjectInstance& object,const std::shared_ptr<IgbObjectFieldConstructServices>& services){
    call.request.kind=IgbObjectFieldConstructServiceKind::destroy_object;call.request.object=object;
    if(services)call.factory=services->factory;
}
void preserve(const std::shared_ptr<IgbObjectFieldConstructServices>& services,const IgbObjectInstance& object){
    if(services && object.storage)services->unfinished.push_back(object.storage);
}
// Scalars/nulls use the common physical synchronizer; the object store also
// carries a known typed identity for the pointer members represented below.
void publish(const IgbObjectInstance& target,std::uint32_t offset,const IgbObjectInstance& incoming){
    const auto pointer=address(incoming);
    if(target.metadata){
        if(offset==0x28 && pointer && !incoming.fields)fail("Metadata fields member requires a field-list identity.");
        if(offset==0x34 && pointer && !incoming.types)fail("Metadata children member requires a type-list identity.");
        if(offset==0x38 && pointer && !incoming.metadata)fail("Metadata parent member requires a metadata identity.");
    }
    igb_bootstrap_write_word({target.storage,target.offset},offset,pointer);
    igb_synchronize_field_storage(target,target.offset+offset,4);
    if(target.metadata){auto& metadata=*target.metadata;
        if(offset==0x28){if(metadata.fields && metadata.fields!=incoming.fields)metadata.overwritten_constructor_owners_.push_back(metadata.fields);metadata.fields=incoming.fields;}
        if(offset==0x34){if(metadata.children && *metadata.children && *metadata.children!=incoming.types)metadata.overwritten_constructor_owners_.push_back(*metadata.children);metadata.children=incoming.types;metadata.word_34=pointer;}
        if(offset==0x38){if(metadata.parent && *metadata.parent && *metadata.parent!=incoming.metadata)metadata.overwritten_constructor_owners_.push_back(*metadata.parent);metadata.parent=incoming.metadata;}
        if(offset==0x44)metadata.word_44=pointer;
    }
    if(target.field){
        if(offset==0x18 && (!pointer || incoming.metadata))target.field->declaring_type=std::weak_ptr(incoming.metadata);
        if(offset==0x38 && target.field->vtable_word==0x804762fc && (!pointer || incoming.metadata))target.field->object_type=std::weak_ptr(incoming.metadata);
    }
}
std::shared_ptr<IgbInternedString> name_owner(const std::shared_ptr<IgbObjectFieldConstructServices>& services,std::uint32_t pointer){
    std::shared_ptr<IgbInternedString> result;
    if(services && services->values)for(const auto& binding:services->values->strings)if(binding.address==pointer){
        if(!binding.owner || (result && result!=binding.owner))fail("Member pool name has conflicting arithmetic identities.");result=binding.owner;
    }return result;
}
}
void igb_bind_object_field_instance(const std::shared_ptr<IgbObjectFieldConstructServices>& services,const IgbObjectInstance& object){
    if(!services)fail("Member binding requires its native services.");validate(object);if(!address(object))return;
    for(const auto& old:services->objects)if(address(old)==address(object)){
        if(!same(old,object))fail("Member binding conflicts with an existing arithmetic identity.");return;
    }
    services->objects.push_back(object);
}
struct IgbObjectFieldAssignInvocation::Impl {
    enum class P {flag,old,ownership,retain,release_old,destroy,publish,done};P phase=P::flag;
    std::shared_ptr<IgbDecodeRuntimeField> descriptor;IgbObjectInstance target,incoming,old;
    std::shared_ptr<IgbObjectFieldConstructServices> owner;Call call;
    std::uint32_t old_address{};bool shared{},running{},fault{};
};
IgbObjectFieldAssignInvocation::IgbObjectFieldAssignInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,
    IgbObjectInstance target,IgbObjectInstance incoming):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);impl_->incoming=std::move(incoming);
}
IgbObjectFieldAssignInvocation::~IgbObjectFieldAssignInvocation(){auto& s=*impl_;if(!completed() || faulted()){
    preserve(s.owner,s.target);preserve(s.owner,s.incoming);preserve(s.owner,s.old);
}}
bool IgbObjectFieldAssignInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbObjectFieldAssignInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbObjectFieldAssignInvocation::resume(std::shared_ptr<IgbObjectFieldConstructServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Object member assignment requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Object member assignment was invalidated by a callback.");validate(s.target);validate(s.incoming);};
    try{for(;;){check();switch(s.phase){
    case P::flag:
        if(!s.descriptor)fail("Object member assignment dereferences a null descriptor.");
        if(!s.descriptor->flag_3c)return pending("Object member assignment requires flag+3C.");
        s.shared=*s.descriptor->flag_3c!=0;s.phase=P::old;break;
    case P::old:s.old_address=igb_bootstrap_read_word({s.target.storage,s.target.offset},s.descriptor->offset);s.phase=P::ownership;break;
    case P::ownership:
        if(!s.shared){if(!s.descriptor->flag_35)return pending("Object member assignment requires live flag+35.");s.shared=*s.descriptor->flag_35!=0;}
        s.phase=s.shared?P::retain:P::publish;break;
    case P::retain:
        if(address(s.incoming))igb_object_set_reference_word(s.incoming,igb_object_reference_word(s.incoming)+1u);
        s.phase=P::release_old;break;
    case P::release_old:
        if(s.old_address){const auto old=resolve(services,s.old_address,s.target,s.incoming);
            if(!old)return pending("Object member assignment requires its captured old object's physical binding.");s.old=*old;
            igb_object_set_reference_word(s.old,igb_object_reference_word(s.old)-1u);
            if((igb_object_reference_word(s.old)&0x7fffffu)==0){select_destroy(s.call,s.old,services);s.phase=P::destroy;break;}
        }
        s.phase=P::publish;break;
    case P::destroy:{const auto status=s.call.poll(services,check);if(!status.completed)return status;
        s.call.clear();check();s.phase=P::publish;break;}
    case P::publish:
        publish(s.target,s.descriptor->offset,s.incoming);
        if(services && address(s.incoming))igb_bind_object_field_instance(services,s.incoming);
        s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbObjectFieldConstructInvocation::Impl {
    enum class P {gate,name_pointer,name,after_named,tagged,after_tagged,metadata,factory,assign,flag3c,flag35,release,destroy,call,done};
    P phase=P::gate,after=P::done;
    std::shared_ptr<IgbDecodeRuntimeField> descriptor;IgbObjectInstance target,incoming;
    std::shared_ptr<IgbObjectFieldConstructServices> owner;std::shared_ptr<IgbInternedString> name;
    std::shared_ptr<ModelSceneObject> pool;std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::unique_ptr<IgbReflectedFactoryInvocation> factory;std::unique_ptr<IgbObjectFieldAssignInvocation> assignment;
    Call call;std::uint32_t name_pointer{};bool running{},fault{};
};
IgbObjectFieldConstructInvocation::IgbObjectFieldConstructInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,IgbObjectInstance target):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);
}
IgbObjectFieldConstructInvocation::~IgbObjectFieldConstructInvocation(){auto& s=*impl_;if(!completed() || faulted()){
    preserve(s.owner,s.target);preserve(s.owner,s.incoming);if(s.owner){if(s.name)s.owner->unfinished.push_back(s.name);if(s.pool)s.owner->unfinished.push_back(s.pool);}
}}
bool IgbObjectFieldConstructInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbObjectFieldConstructInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbObjectFieldConstructInvocation::resume(std::shared_ptr<IgbObjectFieldConstructServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbObjectFieldConstructServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Object member construction requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Object member construction was invalidated by a callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbObjectFieldConstructCall&{clear();s.call.request.kind=kind;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!s.descriptor)fail("Object member construction dereferences a null descriptor.");
        if(!s.descriptor->flag_34)return pending("Object member construction requires live flag+34.");
        s.phase=*s.descriptor->flag_34?P::name_pointer:P::done;break;
    case P::name_pointer:
        if(!s.descriptor->word_40)return pending("Object member construction requires pool-name pointer+40.");
        s.name_pointer=*s.descriptor->word_40;s.phase=s.name_pointer?P::name:P::tagged;break;
    case P::name:{
        s.name=name_owner(services,s.name_pointer);if(!s.name)return pending("Object member construction requires its captured pool-name binding.");
        if(s.name->released)fail("Object member construction reads a released pool-name string.");
        bool nonempty{};
        if(s.name->native_name)nonempty=igb_buffer_bytes(s.name->native_name->buffer,s.name->native_name->offset,1).front()!=0;
        else {if(!s.name->value)return pending("Object member construction requires pool-name character storage.");nonempty=!s.name->value->empty() && s.name->value->front()!=0;}
        if(nonempty)select(K::named_pool,P::after_named).name=igb_interned_string_name(s.name);else s.phase=P::tagged;break;}
    case P::after_named:s.pool=s.call.operation->pool_result;clear();s.phase=s.pool?P::metadata:P::tagged;break;
    case P::tagged:{validate(s.target);const auto tag=igb_bootstrap_read_byte({s.target.storage,s.target.offset},4);
        auto& call=select(K::tagged_pool,P::after_tagged);call.object=s.target;call.handle=tag;break;}
    case P::after_tagged:s.pool=s.call.operation->pool_result;clear();s.phase=P::metadata;break;
    case P::metadata:
        if(!s.descriptor->object_type)return pending("Object member construction requires live descriptor metadata+38.");
        s.metadata=s.descriptor->object_type->lock();
        if(!s.metadata && !igb_field_declaring_type_is_null(*s.descriptor->object_type))fail("Object member construction metadata lifetime expired.");
        s.factory=std::make_unique<IgbReflectedFactoryInvocation>(s.metadata,s.pool);s.phase=P::factory;break;
    case P::factory:{const auto status=s.factory->resume(services?services->reflected:nullptr);check();if(!status.completed)return status;
        s.incoming=s.factory->result_instance();s.factory.reset();check();
        if(services && address(s.incoming))igb_bind_object_field_instance(services,s.incoming);
        s.assignment=std::make_unique<IgbObjectFieldAssignInvocation>(s.descriptor,s.target,s.incoming);s.phase=P::assign;break;}
    case P::assign:{const auto status=s.assignment->resume(services);check();if(!status.completed)return status;
        s.assignment.reset();check();s.phase=P::flag3c;break;}
    case P::flag3c:
        if(!s.descriptor->flag_3c)return pending("Object member temporary release requires live flag+3C.");
        s.phase=*s.descriptor->flag_3c?P::release:P::flag35;break;
    case P::flag35:
        if(!s.descriptor->flag_35)return pending("Object member temporary release requires live flag+35.");
        s.phase=*s.descriptor->flag_35?P::release:P::done;break;
    case P::release:
        if(address(s.incoming)){validate(s.incoming);igb_object_set_reference_word(s.incoming,igb_object_reference_word(s.incoming)-1u);
            if((igb_object_reference_word(s.incoming)&0x7fffffu)==0){select_destroy(s.call,s.incoming,services);s.phase=P::destroy;break;}}
        s.phase=P::done;break;
    case P::destroy:{const auto status=s.call.poll(services,check);if(!status.completed)return status;clear();s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
