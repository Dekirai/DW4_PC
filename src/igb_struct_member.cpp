#include "dw4/igb_struct_member.hpp"
#include "dw4/igb_field_defaults.hpp"

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbStructMemberError(message);}
[[noreturn]] void copy_fail(const char* message){throw IgbFieldDefaultCopyError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& flag):value(flag){value=true;}~Running(){value=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
struct FieldAccess {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::optional<IgbBootstrapPointer> captured;
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t allocation_address{};
    explicit FieldAccess(std::shared_ptr<IgbDecodeRuntimeField> value):field(std::move(value)){
        if(field && field->physical_storage){const auto& binding=*field->physical_storage;
            captured=IgbBootstrapPointer{binding.storage.lock(),binding.offset};
            if(captured->storage){bytes=captured->storage->bytes;allocation_address=captured->storage->original_address;}
        }
    }
    std::optional<IgbBootstrapPointer> physical(){
        if(!field)fail("Struct member operation dereferences a null descriptor.");
        if(field->construction_fault_)fail("Struct member operation reached a faulted descriptor.");
        if(!field->physical_storage){if(captured)fail("Struct member operation lost its captured physical descriptor.");return {};}
        const auto& binding=*field->physical_storage;IgbBootstrapPointer current{binding.storage.lock(),binding.offset};
        if(!current.storage || current.storage->faulted() || !current.storage->bytes || current.storage->bytes->released)
            fail("Struct member operation requires live unfaulted physical descriptor storage.");
        if(captured && (captured->storage!=current.storage || captured->offset!=current.offset || bytes!=current.storage->bytes ||
            allocation_address!=current.storage->original_address))
            fail("Struct member operation changed its captured physical descriptor allocation.");
        IgbObjectInstance object;object.storage=current.storage;object.offset=current.offset;object.field=field;
        igb_object_validate_instance(object);
        if(!captured){captured=current;bytes=current.storage->bytes;allocation_address=current.storage->original_address;}
        return current;
    }
    std::uint32_t offset(){if(const auto p=physical())field->offset=igb_bootstrap_read_word(*p,8);return field->offset;}
    std::uint16_t size(){if(const auto p=physical()){
            const auto high=igb_bootstrap_read_byte(*p,0x14),low=igb_bootstrap_read_byte(*p,0x15);
            field->size=static_cast<std::uint16_t>((std::uint16_t(high)<<8u)|low);
        }return field->size;
    }
    std::optional<std::uint32_t> function(bool destructor){
        auto& result=destructor?field_word_destructor():field_word_constructor();
        if(const auto p=physical())result=igb_bootstrap_read_word(*p,destructor?0x3c:0x38);
        return result;
    }
    std::optional<std::uint32_t> vtable(){if(const auto p=physical())field->vtable_word=igb_bootstrap_read_word(*p,0);return field->vtable_word;}
private:
    std::optional<std::uint32_t>& field_word_constructor(){if(!field)fail("Struct member operation reads a null constructor descriptor.");return field->struct_constructor;}
    std::optional<std::uint32_t>& field_word_destructor(){if(!field)fail("Struct member operation reads a null destructor descriptor.");return field->struct_destructor;}
};
void validate_target(const IgbObjectInstance& object){
    if(!object.storage || object.storage->faulted() || !object.storage->bytes || object.storage->bytes->released)
        copy_fail("Field default copy requires live unfaulted destination storage.");
    if(object.file || object.field || object.metadata || object.fields || object.types || object.runtime_object)
        igb_object_validate_instance(object);
    if(object.field && object.field->construction_fault_)copy_fail("Field default copy reached a faulted destination descriptor.");
}
std::optional<std::uint32_t> source_address(const std::shared_ptr<IgbFieldValuesServices>& values,
    const IgbRuntimeFieldDefaultSource& source){
    if(!source.buffer)return 0;
    std::optional<std::uint32_t> result;
    if(values)for(const auto& binding:values->buffers)if(binding.buffer==source.buffer){
        const auto candidate=binding.base_address+source.offset;
        if(!candidate || (result && *result!=candidate))copy_fail("Field default source has conflicting arithmetic identities.");
        result=candidate;
    }return result;
}
bool resolve_source(IgbRuntimeFieldDefaultSource& source,bool& bound,std::uint32_t pointer,
    const std::shared_ptr<IgbFieldValuesServices>& values){
    if(!pointer)copy_fail("Field default copy reads through a null source pointer.");
    bool found=bound;
    if(values)for(const auto& binding:values->buffers){
        const auto offset=pointer-binding.base_address;
        if(!binding.buffer || std::uint64_t(offset)>=binding.buffer->bytes.size())continue;
        if(found && (source.buffer!=binding.buffer || source.offset!=offset))
            copy_fail("Field default source pointer has conflicting native allocation bindings.");
        source={binding.buffer,offset};found=true;
    }
    bound=found;return found;
}
bool supported(std::uint32_t slot){return slot==0x84 || slot==0x74 || slot==0x7c || slot==0x80 || slot==0xf0 || slot==0x70 || slot==0x78;}
} // namespace

struct IgbFieldDefaultCopyInvocation::Impl {
    enum class P{offset,size,source_pointer,resolve,read,write,done};P phase=P::offset;
    FieldAccess descriptor;IgbObjectInstance target;
    std::shared_ptr<IgbScratchBuffer> target_bytes;
    std::shared_ptr<IgbFieldValuesServices> owner;
    IgbRuntimeFieldDefaultSource source;
    std::optional<std::uint32_t> pointer;
    std::uint32_t receiver_address{},member_offset{},destination_address{},count{},step{},index{};
    unsigned target_physical_kinds{};
    std::uint8_t byte{};bool bound{},backwards{},running{},fault{};
    Impl(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance object):descriptor(std::move(field)),target(std::move(object)),
        target_bytes(target.storage?target.storage->bytes:nullptr),receiver_address(address(target)),
        target_physical_kinds((target.field && target.field->physical_storage?1u:0u)|
            (target.fields && target.fields->physical_storage?2u:0u)|
            (target.types && target.types->physical_storage?4u:0u)){}
};
IgbFieldDefaultCopyInvocation::IgbFieldDefaultCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance object)
    :impl_(std::make_unique<Impl>(std::move(field),std::move(object))){}
IgbFieldDefaultCopyInvocation::~IgbFieldDefaultCopyInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.descriptor.field)s.owner->unfinished.push_back(s.descriptor.field);
    if(s.descriptor.captured && s.descriptor.captured->storage)s.owner->unfinished.push_back(s.descriptor.captured->storage);
    if(s.target.storage)s.owner->unfinished.push_back(s.target.storage);
    if(s.source.buffer)s.owner->unfinished.push_back(s.source.buffer);
}}
bool IgbFieldDefaultCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldDefaultCopyInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldDefaultCopyInvocation::resume(std::shared_ptr<IgbFieldValuesServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;copy_fail("Field default copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)copy_fail("Field default copy was invalidated by a callback.");if(enclosing_check)enclosing_check();
        if(s.phase!=P::offset)s.descriptor.physical();};
    try{for(;;){check();switch(s.phase){
    case P::offset:s.member_offset=s.descriptor.offset();s.destination_address=s.receiver_address+s.member_offset;s.phase=P::size;break;
    case P::size:s.count=s.descriptor.size();s.phase=P::source_pointer;break;
    case P::source_pointer:{
        const auto physical=s.descriptor.physical();const auto& field=*s.descriptor.field;
        if(!s.pointer){
            if(physical)s.pointer=igb_bootstrap_read_word(*physical,0x20);
            else{s.pointer=field.default_bytes_address;
                if(!s.pointer && field.default_bytes)s.pointer=source_address(services,*field.default_bytes);}
            if(!s.pointer)return pending("Field default copy requires the captured source pointer+20.");
            // A physical pointer can differ from an old canonical source view.
            // Resolve that new pointer without borrowing the stale buffer.
            if(field.default_bytes && ((!field.default_bytes_address && !physical) || field.default_bytes_address==s.pointer)){
                s.source=*field.default_bytes;s.bound=bool(s.source.buffer);
                if(!s.source.buffer && *s.pointer)copy_fail("Nonnull default pointer has an explicitly null source binding.");
            }
        }
        s.backwards=*s.pointer<s.destination_address;
        s.phase=s.count?P::resolve:P::done;break;
    }
    case P::resolve:
        if(!resolve_source(s.source,s.bound,*s.pointer,services))return pending("Field default copy requires its captured nonnull source allocation.");
        s.phase=P::read;break;
    case P::read:{
        if(s.step==s.count){s.phase=P::done;break;}
        s.index=s.backwards?s.count-1u-s.step:s.step;
        if(*s.pointer+s.index==0)copy_fail("Field default copy reads a null arithmetic byte address.");
        const auto at=std::uint64_t(s.source.offset)+s.index;
        if(!s.source.buffer || s.source.buffer->released || at>=s.source.buffer->bytes.size() ||
            at>=s.source.buffer->initialized.size() || !s.source.buffer->initialized[static_cast<std::size_t>(at)])
            copy_fail("Field default copy reads unavailable source bytes.");
        s.byte=s.source.buffer->bytes[static_cast<std::size_t>(at)];s.phase=P::write;break;
    }
    case P::write:{
        if(s.destination_address+s.index==0)copy_fail("Field default copy writes a null arithmetic byte address.");
        validate_target(s.target);
        if(s.target_bytes!=s.target.storage->bytes || address(s.target)!=s.receiver_address ||
            ((s.target_physical_kinds&1u) && !s.target.field->physical_storage) ||
            ((s.target_physical_kinds&2u) && !s.target.fields->physical_storage) ||
            ((s.target_physical_kinds&4u) && !s.target.types->physical_storage))
            copy_fail("Field default copy changed its captured destination allocation.");
        const auto at=std::uint64_t(s.target.offset)+s.member_offset+s.index;
        const auto bytes=s.target.storage->bytes;
        if(at>=bytes->bytes.size() || at>=bytes->initialized.size())copy_fail("Field default copy writes unavailable destination bytes.");
        bytes->bytes[static_cast<std::size_t>(at)]=s.byte;bytes->initialized[static_cast<std::size_t>(at)]=1;
        igb_synchronize_field_storage(s.target,static_cast<std::uint32_t>(at),1);
        ++s.step;s.phase=P::read;break;
    }
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbStructMemberInvocation::Impl {
    enum class P{entry,function,offset,dispatch,reset_test,destructor_offset,destructor_function,
        constructor_offset,constructor_function,copy,virtual_table,virtual_dispatch,child,call,done};
    P phase=P::entry,after=P::done;FieldAccess descriptor;IgbObjectInstance object;
    std::uint32_t slot{},callee{},member_offset{},table{},returned{},receiver_address{};
    std::shared_ptr<IgbStructMemberServices> owner;
    std::unique_ptr<IgbFieldDefaultCopyInvocation> copy;
    std::unique_ptr<IgbStructMemberInvocation> child;
    IgbStructMemberCall request;IgbStructMemberFactory factory;
    std::shared_ptr<IgbStructMemberOperation> operation;std::function<MessengerCallResult()> callback;
    std::shared_ptr<IgbScratchBuffer> target_bytes;
    unsigned target_physical_kinds{};bool target_captured{};
    bool running{},fault{};
    Impl(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance target,std::uint32_t value)
        :descriptor(std::move(field)),object(std::move(target)),slot(value),receiver_address(address(object)){}
};
IgbStructMemberInvocation::IgbStructMemberInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance object,std::uint32_t slot)
    :impl_(std::make_unique<Impl>(std::move(field),std::move(object),slot)){if(!supported(slot))fail("Unsupported struct member virtual.");}
IgbStructMemberInvocation::~IgbStructMemberInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.descriptor.field)s.owner->retained.push_back(s.descriptor.field);
    if(s.descriptor.captured && s.descriptor.captured->storage)s.owner->retained.push_back(s.descriptor.captured->storage);
    if(s.object.storage)s.owner->retained.push_back(s.object.storage);
    if(s.operation)s.owner->retained.push_back(s.operation);
}}
bool IgbStructMemberInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbStructMemberInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbStructMemberInvocation::resume(std::shared_ptr<IgbStructMemberServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbStructMemberServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Struct member requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.returned,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Struct member was invalidated by a callback.");if(enclosing_check)enclosing_check();
        if(s.phase!=P::entry && s.slot!=0x84 && s.slot!=0x74)s.descriptor.physical();
        if(s.target_captured && s.object.storage){
            validate_target(s.object);
            if(s.target_bytes!=s.object.storage->bytes || address(s.object)!=s.receiver_address ||
                ((s.target_physical_kinds&1u) && !s.object.field->physical_storage) ||
                ((s.target_physical_kinds&2u) && !s.object.fields->physical_storage) ||
                ((s.target_physical_kinds&4u) && !s.object.types->physical_storage))
                fail("Struct guest function changed its captured receiver allocation.");
        }
    };
    const auto clear=[&]{s.callback={};s.operation.reset();s.factory={};s.request={};check();};
    const auto guest=[&](P after){
        if(!s.callee)fail("Struct member invokes a null selected guest function.");
        clear();s.request.kind=K::guest_function;s.request.field=s.descriptor.field;s.request.object=s.object;
        s.request.function=s.callee;s.request.address=s.receiver_address+s.member_offset;
        if(!s.target_captured){s.target_captured=true;if(s.object.storage)s.target_bytes=s.object.storage->bytes;
            s.target_physical_kinds=(s.object.field && s.object.field->physical_storage?1u:0u)|
                (s.object.fields && s.object.fields->physical_storage?2u:0u)|
                (s.object.types && s.object.types->physical_storage?4u:0u);}
        check();
        if(services)s.factory=services->factory;s.after=after;s.phase=P::call;
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:
        s.phase=s.slot==0x84 || s.slot==0x74?P::done:s.slot==0x70?P::reset_test:s.slot==0x78?P::virtual_table:P::function;break;
    case P::function:{const auto function=s.descriptor.function(s.slot==0x80);
        if(!function)return pending("Struct member requires its live constructor/destructor word.");s.callee=*function;
        if(s.slot==0xf0){s.returned=s.callee?1u:0u;s.phase=P::done;}
        else s.phase=s.callee?P::offset:P::done;break;
    }
    case P::offset:s.member_offset=s.descriptor.offset();s.phase=P::dispatch;break;
    case P::dispatch:guest(P::done);break;
    case P::reset_test:{const auto function=s.descriptor.function(false);
        if(!function)return pending("Struct defaults require the live constructor word+38.");
        if(*function)s.phase=P::destructor_offset;
        else{s.copy=std::make_unique<IgbFieldDefaultCopyInvocation>(s.descriptor.field,s.object);s.phase=P::copy;}break;
    }
    case P::destructor_offset:s.member_offset=s.descriptor.offset();s.phase=P::destructor_function;break;
    case P::destructor_function:{const auto function=s.descriptor.function(true);
        if(!function)return pending("Struct defaults require the selected destructor word+3C.");s.callee=*function;guest(P::constructor_offset);break;
    }
    case P::constructor_offset:clear();s.member_offset=s.descriptor.offset();s.phase=P::constructor_function;break;
    case P::constructor_function:{const auto function=s.descriptor.function(false);
        if(!function)return pending("Struct defaults require the freshly reloaded constructor word+38.");s.callee=*function;guest(P::done);break;
    }
    case P::copy:{const auto result=s.copy->resume(services?services->values:nullptr,check);check();if(!result.completed)return result;
        s.copy.reset();check();s.phase=P::done;break;
    }
    case P::virtual_table:{const auto table=s.descriptor.vtable();if(!table)return pending("Struct reset requires its fresh descriptor vtable.");
        if(!*table)fail("Struct reset dereferences a null descriptor vtable at+70.");s.table=*table;s.phase=P::virtual_dispatch;break;
    }
    case P::virtual_dispatch:
        if(s.table==0x80470e00){s.child=std::make_unique<IgbStructMemberInvocation>(s.descriptor.field,s.object,0x70);s.phase=P::child;}
        else{clear();s.request.kind=K::field_virtual;s.request.field=s.descriptor.field;s.request.object=s.object;
            s.request.vtable_word=s.table;s.request.virtual_slot=0x70;if(services)s.factory=services->factory;s.after=P::done;s.phase=P::call;}
        break;
    case P::child:{const auto result=s.child->resume(services,check);check();if(!result.completed)return result;
        s.child.reset();check();s.phase=P::done;break;
    }
    case P::call:
        if(!s.factory && services)s.factory=services->factory;
        if(!s.factory)return pending("Struct member requires its selected guest-function or fresh field-virtual service.");
        if(!s.operation){s.operation=s.factory(s.request);check();}
        if(!s.operation)return pending("Struct member reached an unbound selected guest function or field virtual.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Struct member requires its retained selected callback.");
        {const auto result=s.callback();check();if(!result.completed)return result;s.phase=s.after;}break;
    case P::done:clear();return {true,s.returned,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbObjectInitFactory make_native_igb_struct_member_factory(std::shared_ptr<IgbStructMemberServices> services,IgbObjectInitFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbObjectInitCall& call)->std::shared_ptr<IgbObjectInitOperation>{
        if(call.kind!=IgbObjectInitServiceKind::field_virtual || !supported(call.virtual_slot))
            return remaining?remaining(call):nullptr;
        //667F4's existing field-call interface supplies the descriptor rather
        // than a captured table. Honor an explicit selected table if present;
        // otherwise perform the original live descriptor-table load here.
        std::optional<std::uint32_t> table;
        if(call.vtable_word)table=call.vtable_word;
        else if(call.field){FieldAccess descriptor(call.field);table=descriptor.vtable();}
        if(table && !*table)fail("Struct member dispatch dereferences a null descriptor vtable.");
        if(!table || *table!=0x80470e00)
            return remaining?remaining(call):nullptr;
        auto invocation=std::make_shared<IgbStructMemberInvocation>(call.field,call.object,call.virtual_slot);
        auto operation=std::make_shared<IgbObjectInitOperation>();const auto selected=weak.lock();
        operation->resume=[invocation,selected]{return invocation->resume(selected);};return operation;
    };
}
} // namespace dw4
