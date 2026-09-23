#include "dw4/igb_field_member_copy.hpp"
#include "dw4/igb_field_defaults.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbFieldMemberCopyError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.file==b.file && a.field==b.field && a.metadata==b.metadata && a.fields==b.fields && a.types==b.types;}
void validate(const IgbObjectInstance& object){
    if(object.storage && object.storage->faulted())fail("Member copy uses a faulted allocation.");
    if(object.file || object.field || object.metadata || object.fields || object.types)igb_object_validate_instance(object);
    if(object.field && object.field->construction_fault_)fail("Member copy uses a faulted field.");
}
void readable(const IgbObjectInstance& object,std::uint32_t offset){
    if(!object.storage || !address(object) || address(object)+offset==0 || !object.storage->bytes)fail("Member copy requires represented source storage.");
    const auto bytes=object.storage->bytes;const auto at=std::uint64_t(object.offset)+offset;
    if(bytes->released || at>=bytes->bytes.size() || at>=bytes->initialized.size())fail("Member copy reads unavailable source bytes.");
}
void write_byte(const IgbObjectInstance& object,std::uint32_t offset,std::uint8_t value){
    if(!object.storage || !address(object) || address(object)+offset==0 || !object.storage->bytes)fail("Member copy requires represented destination storage.");
    const auto bytes=object.storage->bytes;const auto at=std::uint64_t(object.offset)+offset;
    if(bytes->released || at>=bytes->bytes.size() || at>=bytes->initialized.size())fail("Member copy writes unavailable destination bytes.");
    bytes->bytes[static_cast<std::size_t>(at)]=value;bytes->initialized[static_cast<std::size_t>(at)]=1;
}
template<class T>bool owner_equal(const std::weak_ptr<T>& a,const std::shared_ptr<T>& b){return !a.owner_before(b) && !b.owner_before(a);}
template<class T>bool is_null(const std::weak_ptr<T>& a){const std::weak_ptr<T> empty;return !a.owner_before(empty) && !empty.owner_before(a);}
void merge(std::optional<std::uint32_t>& result,std::uint32_t candidate){
    if(!candidate || (result && *result!=candidate))fail("Member copy has conflicting arithmetic pointer identities.");result=candidate;
}
std::optional<std::uint32_t> string_address(const std::shared_ptr<IgbFieldValuesServices>& values,
    const IgbFieldNameSlot& name,std::optional<std::uint32_t> explicit_address){
    if(name && !*name){if(explicit_address && *explicit_address)fail("Null member string has a nonnull address.");return 0;}
    auto result=explicit_address;
    if(name && *name && values)for(const auto& b:values->strings)if(b.owner==*name)merge(result,b.address);
    if(name && *name && result && !*result)fail("Nonnull member string has a null address.");return result;
}
std::optional<std::uint32_t> metadata_address(const std::shared_ptr<IgbFieldMemberCopyServices>& services,
    const std::optional<std::weak_ptr<IgbRuntimeMetaType>>& owner){
    if(!owner)return {};if(is_null(*owner))return 0;std::optional<std::uint32_t> result;
    if(services && services->objects)for(const auto& b:services->objects->objects)if(b.metadata && owner_equal(*owner,b.metadata))merge(result,address(b));
    const auto lists=services && services->values && services->values->fields?services->values->fields->pointer_lists:nullptr;
    if(lists)for(const auto& b:lists->values)if(b.metadata && owner_equal(*owner,b.metadata))merge(result,b.address);
    return result;
}
std::optional<std::uint32_t> default_address(const std::shared_ptr<IgbFieldValuesServices>& values,const IgbDecodeRuntimeField& field){
    auto result=field.default_bytes_address;
    if(field.default_bytes){const auto& b=*field.default_bytes;
        if(!b.buffer){if(result && *result)fail("Null member buffer has a nonnull address.");return 0;}
        if(values)for(const auto& binding:values->buffers)if(binding.buffer==b.buffer)merge(result,binding.base_address+b.offset);
        if(result && !*result)fail("Nonnull member buffer has a null address.");
    }return result;
}
std::optional<std::uint8_t> member_byte(const IgbObjectInstance& object,std::uint32_t offset,
    const std::shared_ptr<IgbFieldMemberCopyServices>& services){
    readable(object,offset);const auto values=services?services->values:nullptr;
    const auto part=[&](std::optional<std::uint32_t> value,std::uint32_t begin,unsigned width)->std::optional<std::uint8_t>{
        if(!value)return {};return static_cast<std::uint8_t>(*value>>((width-1u-(offset-begin))*8u));};
    if(object.field){const auto& f=*object.field;
        if(offset<4)return part(f.vtable_word,0,4);
        if(offset<8)return part(f.reference_word,4,4);
        if(offset<12)return part(f.offset,8,4);
        if(offset<16)return part(string_address(values,f.name_string,f.name_string_address),12,4);
        if(offset<18)return part(f.type_ordinal,16,2);
        if(offset<20)return part(f.ordinal,18,2);
        if(offset<22)return part(f.size,20,2);
        if(offset>=24 && offset<28)return part(metadata_address(services,f.declaring_type),24,4);
        if(offset>=28 && offset<32){auto pointer=f.global_slot_address;if(f.global_slot && is_null(*f.global_slot)){
                if(pointer && *pointer)fail("Null global cell has a nonnull address.");pointer=0;}
            return part(pointer,28,4);}
        if(offset>=32 && offset<36)return part(default_address(values,f),32,4);
        if(offset==0x24)return f.default_mode;
        if(offset==0x25)return f.flag_25;if(offset==0x26)return f.flag_26;
        if(offset==0x27)return f.flag_27;if(offset==0x28)return f.flag_28;if(offset==0x29)return f.flag_29;
        if(offset==0x2a)return f.flag_2a;if(offset==0x2b)return f.flag_2b;if(offset==0x2c)return f.flag_2c;
        if(offset>=0x30 && offset<0x34)return part(f.word_30,0x30,4);
    }
    return igb_bootstrap_read_byte({object.storage,object.offset},offset);
}
std::optional<std::uint32_t> member_word(const IgbObjectInstance& object,std::uint32_t offset,
    const std::shared_ptr<IgbFieldMemberCopyServices>& services){
    std::uint32_t result{};for(unsigned i=0;i<4;++i){const auto byte=member_byte(object,offset+i,services);if(!byte)return {};result=(result<<8u)|*byte;}return result;
}
std::shared_ptr<IgbInternedString> string_owner(const std::shared_ptr<IgbFieldValuesServices>& values,std::uint32_t pointer){
    std::shared_ptr<IgbInternedString> result;if(values)for(const auto& b:values->strings)if(b.address==pointer){
        if(!b.owner || (result && result!=b.owner))fail("Member string has conflicting address/header bindings.");result=b.owner;
    }return result;
}
void live(const std::shared_ptr<IgbInternedString>& owner){if(!owner || owner->released)fail("Member copy dereferences an unavailable string header.");}
std::optional<IgbObjectInstance> object_owner(const std::shared_ptr<IgbFieldMemberCopyServices>& services,
    std::uint32_t pointer,const IgbObjectInstance& source,const IgbObjectInstance& destination){
    if(!pointer)return IgbObjectInstance{};std::optional<IgbObjectInstance> result;
    const auto consider=[&](const IgbObjectInstance& object){if(address(object)==pointer){validate(object);
        if(result && !same(*result,object))fail("Member object has conflicting arithmetic bindings.");result=object;}};
    consider(source);consider(destination);if(services && services->objects)for(const auto& object:services->objects->objects)consider(object);return result;
}
std::shared_ptr<IgbDependenciesServices> dependencies(const std::shared_ptr<IgbFieldMemberCopyServices>& services){
    return services && services->values && services->values->fields?services->values->fields->dependencies:nullptr;
}
// A byte store updates the logical member without reading stale neighboring
// physical bytes. Unknown optional members become known only after every byte
// has been written by this copy; padding is never inferred.
void synchronize_byte(const IgbObjectInstance& target,std::uint32_t relative,std::uint8_t byte,
    const std::vector<std::uint64_t>& written,const std::shared_ptr<IgbFieldMemberCopyServices>& services){
    if(!target.field){igb_synchronize_field_storage(target,target.offset+relative,1);return;}
    auto& f=*target.field;const auto values=services?services->values:nullptr;
    const auto part=[&](std::optional<std::uint32_t> previous,std::uint32_t begin,unsigned width)->std::optional<std::uint32_t>{
        if(width==1)return byte;
        if(previous){const auto shift=(width-1u-(relative-begin))*8u;return (*previous&~(0xffu<<shift))|(std::uint32_t(byte)<<shift);}
        for(unsigned i=0;i<width;++i){const auto at=std::uint64_t(target.offset)+begin+i;
            if(std::find(written.begin(),written.end(),at)==written.end())return {};}
        std::uint32_t value{};for(unsigned i=0;i<width;++i)value=(value<<8u)|igb_bootstrap_read_byte({target.storage,target.offset},begin+i);return value;
    };
    const auto retain=[&](const auto& owner){if(owner)f.overwritten_constructor_owners_.push_back(owner);};
    if(relative<4){f.vtable_word=part(f.vtable_word,0,4);return;}
    if(relative<8){f.reference_word=*part(f.reference_word,4,4);return;}
    if(relative<12){f.offset=*part(f.offset,8,4);return;}
    if(relative<16){
        const auto old=string_address(values,f.name_string,f.name_string_address);const auto pointer=part(old,12,4);
        if(pointer!=old){if(f.name_string)retain(*f.name_string);if(f.name)f.overwritten_constructor_strings_.push_back(f.name);
            f.name_string.reset();f.name.reset();if(pointer){const auto owner=*pointer?string_owner(values,*pointer):nullptr;
                if(!*pointer || owner)f.name_string=owner;f.name=owner?owner->value:SystemStageString{};}}
        f.name_string_address=pointer;return;
    }
    if(relative<18){const auto value=part(f.type_ordinal,16,2);f.type_ordinal=value?std::optional(static_cast<std::uint16_t>(*value)):std::nullopt;return;}
    if(relative<20){f.ordinal=static_cast<std::uint16_t>(*part(f.ordinal,18,2));return;}
    if(relative<22){f.size=static_cast<std::uint16_t>(*part(f.size,20,2));return;}
    if(relative>=24 && relative<28){
        const auto old=metadata_address(services,f.declaring_type),pointer=part(old,24,4);
        if(pointer!=old){f.declaring_type.reset();if(pointer && !*pointer)f.declaring_type=std::weak_ptr<IgbRuntimeMetaType>{};
            if(pointer && *pointer){std::shared_ptr<IgbRuntimeMetaType> owner;
                const auto consider=[&](std::uint32_t address,const std::shared_ptr<IgbRuntimeMetaType>& value){if(address==*pointer && value){
                    if(owner && owner!=value)fail("Copied metadata pointer has conflicting owners.");owner=value;}};
                if(services && services->objects)for(const auto& object:services->objects->objects)consider(address(object),object.metadata);
                const auto lists=values && values->fields?values->fields->pointer_lists:nullptr;
                if(lists)for(const auto& binding:lists->values)consider(binding.address,binding.metadata);
                if(owner)f.declaring_type=std::weak_ptr(owner);
            }}return;
    }
    if(relative>=28 && relative<32){const auto old=f.global_slot_address;const auto pointer=part(old,28,4);
        if(pointer!=old){f.global_slot.reset();if(pointer && !*pointer)f.global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};}f.global_slot_address=pointer;return;}
    if(relative>=32 && relative<36){const auto old=default_address(values,f),pointer=part(old,32,4);
        if(pointer!=old){if(f.default_bytes)retain(f.default_bytes->buffer);f.default_bytes.reset();
            if(pointer && !*pointer)f.default_bytes=IgbRuntimeFieldDefaultSource{};
            if(pointer && *pointer && values)for(const auto& b:values->buffers){const auto offset=*pointer-b.base_address;
                if(b.buffer && offset<=b.buffer->bytes.size()){
                    if(f.default_bytes && (f.default_bytes->buffer!=b.buffer || f.default_bytes->offset!=offset))fail("Copied buffer pointer has conflicting owners.");
                    f.default_bytes=IgbRuntimeFieldDefaultSource{b.buffer,offset};
                }}
        }f.default_bytes_address=pointer;return;}
    if(relative==0x24)f.default_mode=byte;else if(relative==0x25)f.flag_25=byte;else if(relative==0x26)f.flag_26=byte;
    else if(relative==0x27)f.flag_27=byte;else if(relative==0x28)f.flag_28=byte;else if(relative==0x29)f.flag_29=byte;
    else if(relative==0x2a)f.flag_2a=byte;else if(relative==0x2b)f.flag_2b=byte;else if(relative==0x2c)f.flag_2c=byte;
    else if(relative>=0x30 && relative<0x34)f.word_30=part(f.word_30,0x30,4);
}
}
struct IgbFieldMemberCopyInvocation::Impl {
    enum class P {entry,raw,pointer,retain,assign,temporary,release,release_call,object,object_assign,unknown,done};P phase=P::entry;
    std::shared_ptr<IgbDecodeRuntimeField> field;IgbObjectInstance destination,source,incoming;
    std::shared_ptr<IgbFieldMemberCopyServices> owner;std::uint32_t function{},offset{},count{},step{},pointer{};
    bool backwards{},running{},fault{},pointer_captured{},source_binding{},destination_binding{};
    IgbFieldNameSlot source_name;std::optional<std::uint32_t> source_name_address;
    std::optional<std::weak_ptr<IgbRuntimeMetaType>> source_type;
    std::vector<std::uint64_t> written;
    std::shared_ptr<IgbScratchBuffer> temporary;std::shared_ptr<IgbInternedString> retained,released;
    std::unique_ptr<IgbFieldStringAssignInvocation> string_assign;
    std::unique_ptr<IgbObjectFieldAssignInvocation> object_assign;
    IgbFieldMemberCopyFactory factory;std::shared_ptr<IgbFieldMemberCopyOperation> operation;std::function<MessengerCallResult()> callback;
    IgbDependenciesFactory release_factory;std::shared_ptr<IgbDependenciesOperation> release_operation;std::function<MessengerCallResult()> release_callback;
};
IgbFieldMemberCopyInvocation::IgbFieldMemberCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance destination,
    IgbObjectInstance source,std::uint32_t function):impl_(std::make_unique<Impl>()){
    impl_->field=std::move(field);impl_->destination=std::move(destination);impl_->source=std::move(source);impl_->function=function;
}
IgbFieldMemberCopyInvocation::~IgbFieldMemberCopyInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    for(const auto& object:{s.destination,s.source,s.incoming})if(object.storage)s.owner->unfinished.push_back(object.storage);
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.temporary)s.owner->unfinished.push_back(s.temporary);
    if(s.retained)s.owner->unfinished.push_back(s.retained);if(s.released)s.owner->unfinished.push_back(s.released);
}}
bool IgbFieldMemberCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldMemberCopyInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldMemberCopyInvocation::resume(std::shared_ptr<IgbFieldMemberCopyServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Member copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Member copy was invalidated by a callback.");
        if(s.phase==P::entry || s.phase==P::unknown || s.phase==P::done || (s.phase==P::raw && !s.count))return;
        validate(s.source);validate(s.destination);
        if((s.source_binding && !s.source.field->physical_storage) || (s.destination_binding && !s.destination.field->physical_storage))
            fail("Member copy lost a captured physical field binding.");};
    const auto values=[&]{return services?services->values:nullptr;};
    try{for(;;){check();switch(s.phase){
    case P::entry:
        if(!s.field)fail("Member copy reads a null descriptor.");
        s.source_binding=s.source.field && s.source.field->physical_storage.has_value();
        s.destination_binding=s.destination.field && s.destination.field->physical_storage.has_value();
        if(s.function==0x800642e4 || s.function==0x80064318 || s.function==0x8006434c){
            s.offset=s.field->offset;s.count=s.field->size;s.backwards=address(s.source)+s.offset<address(s.destination)+s.offset;s.phase=P::raw;
        }else if(s.function==0x80071b58 || s.function==0x80071bc4 || s.function==0x8006a81c){s.offset=s.field->offset;s.phase=P::pointer;}
        else{s.factory=services?services->factory:IgbFieldMemberCopyFactory{};s.phase=P::unknown;}
        break;
    case P::raw:{
        while(s.step<s.count){const auto index=s.backwards?s.count-1u-s.step:s.step;
            const std::uint32_t relative=s.offset+index;
            const auto byte=member_byte(s.source,relative,services);if(!byte)return pending("Member copy requires its live canonical source member binding.");
            write_byte(s.destination,relative,*byte);++s.step;
            const auto at=std::uint64_t(s.destination.offset)+relative;s.written.push_back(at);
            if(s.destination.storage)for(const auto& binding:s.destination.storage->bindings)if(binding.field && at>=binding.offset && at-binding.offset<=0xffffffffu){
                IgbObjectInstance alias{s.destination.storage,binding.offset,{},binding.field};
                synchronize_byte(alias,static_cast<std::uint32_t>(at-binding.offset),*byte,s.written,services);
            }
            if(!s.destination.field)igb_synchronize_field_storage(s.destination,s.destination.offset+relative,1);
        }
        // Preserve a raw global-cell identity before synchronization invalidates
        // the destination's previous nonnull binding (including self-copy).
        std::optional<std::weak_ptr<IgbRuntimeFieldSlot>> global;
        if(s.count==4 && s.offset==0x1c && s.source.field)global=s.source.field->global_slot;
        if(global && s.destination.field && s.destination.field->global_slot_address && s.source.field &&
            s.source.field->global_slot_address==s.destination.field->global_slot_address)s.destination.field->global_slot=global;
        s.phase=P::done;break;
    }
    case P::pointer:{
        std::optional<std::uint32_t> pointer;
        if(s.source.field && (s.offset==0x0c || s.offset==0x18)){
            if(!s.pointer_captured){readable(s.source,s.offset);readable(s.source,s.offset+3u);
                if(s.offset==0x0c){s.source_name=s.source.field->name_string;s.source_name_address=s.source.field->name_string_address;
                    if(!s.source_name && !s.source_name_address)return pending("Member copy requires its source name pointer.");}
                else{if(!s.source.field->declaring_type)return pending("Member copy requires its source declaring pointer.");s.source_type=s.source.field->declaring_type;}
                s.pointer_captured=true;
            }
            pointer=s.offset==0x0c?string_address(values(),s.source_name,s.source_name_address):metadata_address(services,s.source_type);
        }else pointer=member_word(s.source,s.offset,services);
        if(!pointer)return pending("Member copy requires its captured source pointer binding.");
        s.pointer=*pointer;s.phase=s.function==0x8006a81c?P::object:P::retain;break;}
    case P::retain:
        if(s.pointer){s.retained=string_owner(values(),s.pointer);if(!s.retained)return pending("String member copy requires its captured source header.");
            live(s.retained);++s.retained->references;}
        s.temporary=std::make_shared<IgbScratchBuffer>();s.temporary->bytes.resize(4);s.temporary->initialized.assign(4,1);
        for(unsigned i=0;i<4;++i)s.temporary->bytes[i]=static_cast<std::uint8_t>(s.pointer>>(24u-i*8u));
        s.string_assign=std::make_unique<IgbFieldStringAssignInvocation>(s.field,s.destination,IgbRuntimeFieldDefaultSource{s.temporary,0});s.phase=P::assign;break;
    case P::assign:{const auto status=s.string_assign->resume(values());check();if(!status.completed)return status;s.string_assign.reset();s.phase=P::temporary;break;}
    case P::temporary:s.pointer=igb_buffer_word(s.temporary,0);s.phase=s.pointer?P::release:P::done;break;
    case P::release:
        s.released=string_owner(values(),s.pointer);if(!s.released)return pending("String member copy requires its reloaded temporary header.");
        live(s.released);--s.released->references;
        if(s.released->references==0){const auto dep=dependencies(services);s.release_factory=dep?dep->factory:IgbDependenciesFactory{};s.phase=P::release_call;}
        else s.phase=P::done;break;
    case P::release_call:{
        if(!s.release_factory){const auto dep=dependencies(services);if(dep)s.release_factory=dep->factory;}
        if(!s.release_factory)return pending("String member copy requires its reached temporary release service.");
        if(!s.release_operation){IgbDependenciesCall call;call.kind=IgbDependenciesServiceKind::release_string;call.string=s.released;call.pool=s.released->container;
            s.release_operation=s.release_factory(call);check();}
        if(!s.release_operation)return pending("String member copy reached an unbound temporary release.");
        if(!s.release_callback)s.release_callback=s.release_operation->resume;
        if(!s.release_callback)return pending("String member copy requires its retained release continuation.");
        const auto status=s.release_callback();check();if(!status.completed)return status;s.phase=P::done;break;
    }
    case P::object:{const auto incoming=object_owner(services,s.pointer,s.source,s.destination);
        if(!incoming)return pending("Object member copy requires its captured incoming object binding.");s.incoming=*incoming;
        s.object_assign=std::make_unique<IgbObjectFieldAssignInvocation>(s.field,s.destination,s.incoming);s.phase=P::object_assign;break;}
    case P::object_assign:{const auto status=s.object_assign->resume(services?services->objects:nullptr);check();if(!status.completed)return status;s.object_assign.reset();s.phase=P::done;break;}
    case P::unknown:
        if(!s.factory && services)s.factory=services->factory;
        if(!s.factory)return pending("Member copy requires its reached function implementation.");
        if(!s.operation){s.operation=s.factory({s.field,s.destination,s.source,s.function});check();}
        if(!s.operation)return pending("Member copy reached an unbound function implementation.");
        if(!s.callback)s.callback=s.operation->resume;
        if(!s.callback)return pending("Member copy requires its retained function continuation.");
        {const auto status=s.callback();check();if(!status.completed)return status;}s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
} // namespace dw4
