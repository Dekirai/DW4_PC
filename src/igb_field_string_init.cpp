#include "dw4/igb_field_string_init.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldStringInitError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
std::shared_ptr<IgbInternedString> string_owner(const std::shared_ptr<IgbFieldValuesServices>& services,std::uint32_t address){
    if(!address)return {};std::shared_ptr<IgbInternedString> result;
    if(services)for(const auto& binding:services->strings)if(binding.address==address){
        if(!binding.owner || (result && result!=binding.owner))fail("String initialization has conflicting pointer/header bindings.");result=binding.owner;
    }return result;
}
std::optional<std::uint32_t> string_address(const std::shared_ptr<IgbFieldValuesServices>& services,const std::shared_ptr<IgbInternedString>& owner){
    if(!owner)return 0;std::optional<std::uint32_t> result;
    if(services)for(const auto& binding:services->strings)if(binding.owner==owner){
        if(!binding.address || (result && *result!=binding.address))fail("String initialization has conflicting header/pointer bindings.");result=binding.address;
    }return result;
}
void require_live(const std::shared_ptr<IgbInternedString>& owner){if(!owner || owner->released)fail("String initialization dereferences a null or released string header.");}
std::shared_ptr<IgbDependenciesServices> dependencies(const std::shared_ptr<IgbFieldValuesServices>& services){return services && services->fields?services->fields->dependencies:nullptr;}
struct Call {
    IgbDependenciesCall request;IgbDependenciesFactory factory;std::shared_ptr<IgbDependenciesOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbFieldValuesServices>& services,Check check){
        if(!factory){if(const auto dep=dependencies(services))factory=dep->factory;}
        if(!factory)return pending("String initialization requires its reached dependency factory.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("String initialization reached an unbound dependency operation.");
        if(!callback)callback=operation->resume;if(!callback)return pending("String initialization requires its retained dependency continuation.");
        const auto status=callback();check();return status;
    }
};
void select_release(Call& call,const std::shared_ptr<IgbInternedString>& owner,const std::shared_ptr<IgbFieldValuesServices>& services){
    call.request.kind=IgbDependenciesServiceKind::release_string;call.request.string=owner;call.request.pool=owner->container;
    if(const auto dep=dependencies(services))call.factory=dep->factory;
}
void synchronize(const IgbObjectInstance& target,std::uint32_t offset,std::uint32_t address,
    const std::shared_ptr<IgbInternedString>& owner){
    if(target.metadata && (offset==0x1c || offset==0x60)){
        auto& m=*target.metadata;
        auto& header=offset==0x1c?m.name_string:m.default_pool_string;
        auto& pointer=offset==0x1c?m.name_string_address:m.default_pool_string_address;
        const auto old_value=offset==0x1c?m.name:m.default_pool_name.value_or(SystemStageString{});
        if(header && *header && *header!=owner)m.overwritten_constructor_owners_.push_back(*header);
        if(old_value && (!owner || old_value!=owner->value))m.overwritten_constructor_strings_.push_back(old_value);
        pointer=address;if(address && !owner)header.reset();else header=owner;
        if(offset==0x1c)m.name=owner?owner->value:SystemStageString{};
        else if(address && !owner)m.default_pool_name.reset();else m.default_pool_name=owner?owner->value:SystemStageString{};
    }
    if(!target.field)return;auto& field=*target.field;
    if(offset==0x0c){
        if(field.name_string && *field.name_string && *field.name_string!=owner)field.overwritten_constructor_owners_.push_back(*field.name_string);
        if(field.name && (!owner || field.name!=owner->value))field.overwritten_constructor_strings_.push_back(field.name);
        field.name_string_address=address;
        // A raw copied pointer need not have a known header until a later read.
        // Keep its numeric identity without manufacturing a null typed owner.
        if(address && !owner)field.name_string.reset();else field.name_string=owner;
        field.name=owner?owner->value:SystemStageString{};
    }else if(offset==0x40 && field.vtable_word==0x804762fc)field.word_40=address;
}
}
struct IgbFieldStringAssignInvocation::Impl {
    enum class P {offset,incoming,retain,old,release_old,release,publish,done};P phase=P::offset;
    std::shared_ptr<IgbDecodeRuntimeField> descriptor;IgbObjectInstance target;IgbRuntimeFieldDefaultSource source;
    std::shared_ptr<IgbFieldValuesServices> owner;std::shared_ptr<IgbInternedString> incoming,old;
    std::uint32_t offset{},incoming_address{},old_address{};Call call;bool running{},fault{};
};
IgbFieldStringAssignInvocation::IgbFieldStringAssignInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,
    IgbObjectInstance target,IgbRuntimeFieldDefaultSource source):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);impl_->source=std::move(source);
}
IgbFieldStringAssignInvocation::~IgbFieldStringAssignInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.descriptor)s.owner->unfinished.push_back(s.descriptor);if(s.target.storage)s.owner->unfinished.push_back(s.target.storage);
    if(s.source.buffer)s.owner->unfinished.push_back(s.source.buffer);if(s.incoming)s.owner->unfinished.push_back(s.incoming);if(s.old)s.owner->unfinished.push_back(s.old);
}}
bool IgbFieldStringAssignInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldStringAssignInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldStringAssignInvocation::resume(std::shared_ptr<IgbFieldValuesServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("String assignment requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("String assignment continuation was invalidated.");};
    try{for(;;){check();switch(s.phase){
    case P::offset:if(!s.descriptor)fail("String assignment reads a null descriptor offset.");s.offset=s.descriptor->offset;s.phase=P::incoming;break;
    case P::incoming:s.incoming_address=igb_buffer_word(s.source.buffer,s.source.offset);s.phase=s.incoming_address?P::retain:P::old;break;
    case P::retain:
        s.incoming=string_owner(services,s.incoming_address);if(!s.incoming)return pending("String assignment requires its captured incoming string header.");
        require_live(s.incoming);++s.incoming->references;s.phase=P::old;break;
    case P::old:s.old_address=igb_bootstrap_read_word({s.target.storage,s.target.offset},s.offset);s.phase=s.old_address?P::release_old:P::publish;break;
    case P::release_old:
        s.old=string_owner(services,s.old_address);if(!s.old && s.target.field && s.offset==0x0c && s.target.field->name_string_address==s.old_address && s.target.field->name_string)
            s.old=*s.target.field->name_string;
        if(!s.old && s.target.metadata){const auto& m=*s.target.metadata;
            if(s.offset==0x1c && m.name_string_address==s.old_address && m.name_string)s.old=*m.name_string;
            if(s.offset==0x60 && m.default_pool_string_address==s.old_address && m.default_pool_string)s.old=*m.default_pool_string;
        }
        if(!s.old)return pending("String assignment requires the captured destination string header.");require_live(s.old);--s.old->references;
        if(s.old->references==0){select_release(s.call,s.old,services);s.phase=P::release;}else s.phase=P::publish;break;
    case P::release:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.call.clear();check();s.phase=P::publish;break;}
    case P::publish:{
        const auto address=igb_buffer_word(s.source.buffer,s.source.offset);const auto owner=string_owner(services,address);
        igb_bootstrap_write_word({s.target.storage,s.target.offset},s.offset,address);synchronize(s.target,s.offset,address,owner);s.phase=P::done;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbFieldStringInitInvocation::Impl {
    enum class P {source,global,allocate,after_allocate,after_initialize,publish_pool,intern,name,after_intern,temporary,assign,temporary_read,release_temporary,release,call,done};
    P phase=P::source,after=P::done;std::shared_ptr<IgbDecodeRuntimeField> descriptor;IgbObjectInstance target;
    std::shared_ptr<IgbFieldValuesServices> owner;std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbInternedString> source,incoming,temporary_owner;std::uint32_t source_address{},temporary_address{};
    std::shared_ptr<IgbScratchBuffer> temporary;std::unique_ptr<IgbFieldStringAssignInvocation> assignment;
    Call call;bool running{},fault{};
};
IgbFieldStringInitInvocation::IgbFieldStringInitInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,IgbObjectInstance target):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);
}
IgbFieldStringInitInvocation::~IgbFieldStringInitInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.descriptor)s.owner->unfinished.push_back(s.descriptor);if(s.target.storage)s.owner->unfinished.push_back(s.target.storage);
    if(s.source)s.owner->unfinished.push_back(s.source);if(s.incoming)s.owner->unfinished.push_back(s.incoming);
    if(s.temporary)s.owner->unfinished.push_back(s.temporary);if(s.temporary_owner)s.owner->unfinished.push_back(s.temporary_owner);if(s.pool)s.owner->unfinished.push_back(s.pool);
}}
bool IgbFieldStringInitInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldStringInitInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldStringInitInvocation::resume(std::shared_ptr<IgbFieldValuesServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbDependenciesServiceKind;
    if(s.running || s.fault){s.fault=true;fail("String initialization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("String initialization continuation was invalidated.");};
    const auto globals=[&]{const auto dep=dependencies(services);return dep?dep->globals:std::shared_ptr<SystemStageServices>{};};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbDependenciesCall&{clear();s.call.request.kind=kind;if(const auto dep=dependencies(services))s.call.factory=dep->factory;
        s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::source:
        if(!s.descriptor)fail("String initialization reads a null descriptor default pointer.");
        if(!s.descriptor->default_bytes)return pending("String initialization requires represented descriptor default pointer+20.");
        s.source_address=igb_buffer_word(s.descriptor->default_bytes->buffer,s.descriptor->default_bytes->offset);
        s.phase=s.source_address?P::global:P::temporary;break;
    case P::global:{const auto global=globals();if(!global)return pending("String initialization requires canonical string-pool80562140.");
        s.phase=global->string_pool?P::intern:P::allocate;break;}
    case P::allocate:select(K::create_string_pool,P::after_allocate).bytes=16;break;
    case P::after_allocate:s.pool=s.call.operation->pool_result;clear();if(s.pool)select(K::initialize_string_pool,P::after_initialize).pool=s.pool;else s.phase=P::publish_pool;break;
    case P::after_initialize:s.pool=s.call.operation->pool_result;clear();s.phase=P::publish_pool;break;
    case P::publish_pool:{const auto global=globals();if(!global)return pending("String initialization requires canonical pool publication storage.");
        if(global->string_pool && global->string_pool!=s.pool && s.owner)s.owner->unfinished.push_back(global->string_pool);
        global->string_pool=s.pool;s.pool.reset();s.phase=P::intern;break;}
    case P::intern:{const auto global=globals();if(!global)return pending("String initialization requires its reloaded string-pool global.");
        s.pool=global->string_pool;s.phase=P::name;break;}
    case P::name:{
        s.source=string_owner(services,s.source_address);if(!s.source)return pending("String initialization requires the captured default string's character binding.");
        require_live(s.source);if(!s.source->native_name && !s.source->value)return pending("String initialization requires nonnull default character storage.");
        const auto name=igb_interned_string_name(s.source);auto& request=select(K::intern_name,P::after_intern);request.pool=s.pool;request.name=name;break;}
    case P::after_intern:s.incoming=s.call.operation->string_result;clear();s.phase=P::temporary;break;
    case P::temporary:{const auto address=string_address(services,s.incoming);if(!address)return pending("String initialization requires its returned string's arithmetic address.");
        s.temporary=std::make_shared<IgbScratchBuffer>();s.temporary->bytes.resize(4);s.temporary->initialized.assign(4,1);
        for(unsigned i=0;i<4;++i)s.temporary->bytes[i]=static_cast<std::uint8_t>(*address>>(24-i*8));
        s.assignment=std::make_unique<IgbFieldStringAssignInvocation>(s.descriptor,s.target,IgbRuntimeFieldDefaultSource{s.temporary,0});s.phase=P::assign;break;}
    case P::assign:{const auto status=s.assignment->resume(services);check();if(!status.completed)return status;s.assignment.reset();check();s.phase=P::temporary_read;break;}
    case P::temporary_read:s.temporary_address=igb_buffer_word(s.temporary,0);s.phase=s.temporary_address?P::release_temporary:P::done;break;
    case P::release_temporary:
        s.temporary_owner=string_owner(services,s.temporary_address);if(!s.temporary_owner)return pending("String initialization requires its reloaded temporary header.");
        require_live(s.temporary_owner);--s.temporary_owner->references;if(s.temporary_owner->references==0){select_release(s.call,s.temporary_owner,services);s.phase=P::release;}else s.phase=P::done;break;
    case P::release:{const auto status=s.call.poll(services,check);if(!status.completed)return status;clear();s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
