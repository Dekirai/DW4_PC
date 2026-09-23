#include "dw4/igb_memory_member_copy.hpp"
#include "dw4/igb_field_defaults.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMemoryMemberCopyError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
using K=IgbMemoryMemberCopyServiceKind;
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
bool same(const IgbObjectInstance& a,const IgbObjectInstance& b){return a.storage==b.storage && a.offset==b.offset &&
    a.file==b.file && a.field==b.field && a.metadata==b.metadata && a.fields==b.fields && a.types==b.types;}
void validate(const IgbObjectInstance& object){
    if(!object.storage || object.storage->faulted())fail("Memory member access requires an unfaulted allocation.");
    if(object.field || object.file || object.metadata || object.fields || object.types)igb_object_validate_instance(object);
    if(object.field && object.field->construction_fault_)fail("Memory member access uses a faulted field.");
}
std::shared_ptr<IgbFieldValuesServices> values(const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    return services && services->buffers?services->buffers->values:nullptr;
}
struct Buffer {IgbRuntimeFieldDefaultSource bytes;std::uint32_t address{};};
void resolve(Buffer& buffer,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    if(!buffer.address){if(buffer.bytes.buffer)fail("Null memory pointer has nonnull storage.");return;}
    const auto bindings=values(services);if(!bindings)return;
    for(const auto& b:bindings->buffers)if(b.buffer){const auto offset=buffer.address-b.base_address;
        if(std::uint64_t(offset)>=b.buffer->bytes.size())continue;
        if(buffer.bytes.buffer && (buffer.bytes.buffer!=b.buffer || buffer.bytes.offset!=offset))fail("Memory pointer has conflicting allocation bindings.");
        buffer.bytes={b.buffer,offset};
    }
}
void remember(const Buffer& buffer,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    if(!services || !buffer.bytes.buffer)return;services->unfinished.push_back(buffer.bytes.buffer);
    const auto bindings=values(services);if(!bindings)return;const auto base=buffer.address-buffer.bytes.offset;
    for(const auto& b:bindings->buffers)if(b.buffer==buffer.bytes.buffer){if(b.base_address!=base)fail("Copied allocation changed its arithmetic base.");return;}
    bindings->buffers.push_back({buffer.bytes.buffer,base});
}
struct MemberRead {
    IgbObjectInstance object;std::uint32_t offset{};bool captured{},known_bytes{};
    Buffer result;std::optional<std::uint32_t> pointer;
    MemberRead(IgbObjectInstance value,std::uint32_t member):object(std::move(value)),offset(member){}
    bool poll(const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
        if(!captured){validate(object);if(!address(object) || address(object)+offset==0)fail("Memory member read dereferences a null address.");
            const auto storage=object.storage->bytes;const auto at=std::uint64_t(object.offset)+offset;
            if(!storage || storage->released || at>storage->bytes.size() || 4>storage->bytes.size()-at ||
                at>storage->initialized.size() || 4>storage->initialized.size()-at)
                fail("Memory member read reaches unavailable physical object storage.");
            if(object.field && offset==0x20){
                if(!object.field->default_bytes && !object.field->default_bytes_address)return false;
                if(object.field->default_bytes){result.bytes=*object.field->default_bytes;known_bytes=true;}
                pointer=object.field->default_bytes_address;
                if(known_bytes && !result.bytes.buffer){if(pointer && *pointer)fail("Null canonical memory member has a nonnull address.");pointer=0;}
            }else pointer=igb_bootstrap_read_word({object.storage,object.offset},offset);
            captured=true;
        }
        if(!pointer && known_bytes && result.bytes.buffer){const auto bindings=values(services);
            if(bindings)for(const auto& b:bindings->buffers)if(b.buffer==result.bytes.buffer){const auto candidate=b.base_address+result.bytes.offset;
                if(!candidate || (pointer && *pointer!=candidate))fail("Canonical memory member has conflicting arithmetic addresses.");pointer=candidate;}}
        if(!pointer)return false;result.address=*pointer;
        if(!result.address && result.bytes.buffer)fail("Nonnull canonical memory member has a null address.");
        return true;
    }
};
std::size_t position(Buffer& buffer,std::uint32_t relative,unsigned width,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    resolve(buffer,services);const auto at=std::uint64_t(buffer.bytes.offset)+relative;
    if(!buffer.address || buffer.address+relative==0 || !buffer.bytes.buffer || buffer.bytes.buffer->released ||
        at>buffer.bytes.buffer->bytes.size() || width>buffer.bytes.buffer->bytes.size()-at ||
        at>buffer.bytes.buffer->initialized.size() || width>buffer.bytes.buffer->initialized.size()-at)
        fail("Memory element access reaches unavailable buffer storage.");
    return static_cast<std::size_t>(at);
}
bool mapped(Buffer& buffer,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){resolve(buffer,services);return !buffer.address || bool(buffer.bytes.buffer);}
std::uint32_t read(Buffer& buffer,std::uint32_t relative,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    const auto at=position(buffer,relative,4,services);return igb_buffer_word(buffer.bytes.buffer,at);
}
void write(Buffer& buffer,std::uint32_t relative,std::uint32_t value,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    const auto at=position(buffer,relative,4,services);for(unsigned i=0;i<4;++i){buffer.bytes.buffer->bytes[at+i]=static_cast<std::uint8_t>(value>>(24u-i*8u));buffer.bytes.buffer->initialized[at+i]=1;}
}
void publish(const IgbObjectInstance& target,std::uint32_t offset,const Buffer& buffer,
    const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    validate(target);if(!address(target) || address(target)+offset==0)fail("Memory member publication dereferences a null address.");
    igb_bootstrap_write_word({target.storage,target.offset},offset,buffer.address);
    const auto begin=std::uint64_t(target.offset)+offset;
    auto copy_services=std::make_shared<IgbFieldMemberCopyServices>();copy_services->values=values(services);
    if(services)copy_services->objects=services->objects;
    std::vector<std::uint64_t> written;
    // A word store can overlap another typed view's canonical member. Merge
    // each changed byte without importing stale physical neighbor bytes.
    for(unsigned i=0;i<4;++i){const auto at=begin+i;written.push_back(at);
        for(const auto& binding:target.storage->bindings)if(binding.field && at>=binding.offset && at-binding.offset<=0xffffffffu){
            IgbObjectInstance alias{target.storage,binding.offset,{},binding.field};
            igb_synchronize_field_copy_byte(alias,static_cast<std::uint32_t>(at-binding.offset),
                static_cast<std::uint8_t>(buffer.address>>(24u-i*8u)),written,copy_services);
        }
    }
    for(const auto& binding:target.storage->bindings){
        IgbObjectInstance alias{target.storage,binding.offset,binding.file,binding.field,binding.metadata,binding.fields,binding.types};
        if(!binding.field)igb_synchronize_field_storage(alias,static_cast<std::uint32_t>(begin),4);
        else {const auto derived=std::max(begin,std::uint64_t(binding.offset)+0x34);
            if(derived<begin+4)igb_synchronize_field_storage(alias,static_cast<std::uint32_t>(derived),static_cast<std::uint32_t>(begin+4-derived));}
        if(binding.field && begin==std::uint64_t(binding.offset)+0x20 && (!buffer.address || buffer.bytes.buffer)){
            auto& f=*binding.field;
            if(f.default_bytes && f.default_bytes->buffer && f.default_bytes->buffer!=buffer.bytes.buffer)
                f.overwritten_constructor_owners_.push_back(f.default_bytes->buffer);
            f.default_bytes=buffer.bytes;f.default_bytes_address=buffer.address;
        }
    }
    if(!target.field && !target.file && !target.metadata && !target.fields && !target.types)
        igb_synchronize_field_storage(target,static_cast<std::uint32_t>(begin),4);
}
template<class T>bool is_null(const std::weak_ptr<T>& owner){const std::weak_ptr<T> empty;return !owner.owner_before(empty) && !empty.owner_before(owner);}
bool element(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::shared_ptr<IgbDecodeRuntimeField>& value){
    if(!field)fail("Memory copy dereferences a null descriptor.");if(!field->element_field)return false;
    if(is_null(*field->element_field)){value.reset();return true;}
    value=field->element_field->lock();if(!value)fail("Memory copy dereferences an expired element descriptor.");return true;
}
std::optional<std::uint32_t> field_table(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)fail("Memory ancestry dereferences a null element descriptor.");
    if(field->physical_storage){const auto& b=*field->physical_storage;const auto storage=b.storage.lock();
        if(!storage || storage->faulted() || igb_bootstrap_field({storage,b.offset})!=field)fail("Memory ancestry lost the element's physical binding.");
        field->vtable_word=igb_bootstrap_read_word({storage,b.offset},0);}
    if(field->vtable_word && !*field->vtable_word)fail("Memory ancestry dereferences a null element vtable.");return field->vtable_word;
}
std::optional<IgbObjectInstance> object_owner(std::uint32_t pointer,const std::shared_ptr<IgbMemoryMemberCopyServices>& services){
    if(!pointer)return IgbObjectInstance{};std::optional<IgbObjectInstance> result;
    if(services && services->objects)for(const auto& object:services->objects->objects)if(address(object)==pointer){
        validate(object);if(result && !same(*result,object))fail("Memory element pointer has conflicting object identities.");result=object;}
    return result;
}
struct Call {
    IgbMemoryMemberCopyCall request;IgbMemoryMemberCopyFactory factory;std::shared_ptr<IgbMemoryMemberCopyOperation> operation;
    std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbMemoryMemberCopyServices>& services,Check check){
        if(!factory && services)factory=services->factory;if(!factory)return pending("Memory member copy requires its reached object operation.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("Memory member copy reached an unbound object operation.");
        if(!callback)callback=operation->resume;if(!callback)return pending("Memory member copy requires its retained object continuation.");
        const auto status=callback();check();return status;
    }
};
struct TypeTest {
    enum class P {global,table,call,parents,done};P phase=P::global;
    std::shared_ptr<IgbDecodeRuntimeField> field;IgbFieldTypeKind kind;
    std::shared_ptr<IgbRuntimeMetaType> target,current;std::vector<std::shared_ptr<IgbRuntimeMetaType>> visited;Call call;bool result{};
    TypeTest(std::shared_ptr<IgbDecodeRuntimeField> value,IgbFieldTypeKind type):field(std::move(value)),kind(type){}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbMemoryMemberCopyServices>& services,Check check){
        for(;;){check();switch(phase){
        case P::global:{const auto cell=igb_field_type_metadata_slot(services?services->registration:nullptr,kind);
            if(!cell || !*cell)return pending("Memory ancestry requires its represented canonical metadata cell.");target=**cell;phase=P::table;break;}
        case P::table:{const auto table=field_table(field);if(!table)return pending("Memory ancestry requires its captured element vtable.");
            call.request.kind=K::element_metadata;call.request.field=field;call.request.vtable_word=*table;call.request.virtual_slot=0x58;
            if(services)call.factory=services->factory;phase=P::call;break;}
        case P::call:{const auto status=call.poll(services,check);if(!status.completed)return status;current=call.operation->metadata_result;call.clear();check();phase=P::parents;break;}
        case P::parents:
            if(!current){phase=P::done;break;}if(current==target){result=true;phase=P::done;break;}
            if(!current->parent)return pending("Memory ancestry requires its live metadata parent.");
            if(visited.size()>=262144 || std::find(visited.begin(),visited.end(),current)!=visited.end())fail("Memory ancestry reached a cyclic or unbounded parent chain.");
            visited.push_back(current);current=*current->parent;break;
        case P::done:return {true,result?1u:0u,{}};
        }}
    }
};
void preserve(const std::shared_ptr<IgbMemoryMemberCopyServices>& owner,const IgbObjectInstance& object){if(owner && object.storage)owner->unfinished.push_back(object.storage);}
}
struct IgbMemoryMemberReleaseInvocation::Impl {
    enum class P {gate,element,test,member,size,visit,object,decrement,destroy,next,done};P phase=P::gate;
    std::shared_ptr<IgbDecodeRuntimeField> field,element;IgbObjectInstance destination,object;
    std::shared_ptr<IgbMemoryMemberCopyServices> owner;std::unique_ptr<MemberRead> member;Buffer backing;
    std::unique_ptr<TypeTest> test;std::unique_ptr<IgbBufferCopyInvocation> size;Call call;
    std::uint32_t count{},index{},pointer{};bool running{},fault{};
};
IgbMemoryMemberReleaseInvocation::IgbMemoryMemberReleaseInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance destination)
    :impl_(std::make_unique<Impl>()){impl_->field=std::move(field);impl_->destination=std::move(destination);}
IgbMemoryMemberReleaseInvocation::~IgbMemoryMemberReleaseInvocation(){auto& s=*impl_;if(!completed() || faulted()){
    preserve(s.owner,s.destination);preserve(s.owner,s.object);if(s.owner && s.backing.bytes.buffer)s.owner->unfinished.push_back(s.backing.bytes.buffer);}}
bool IgbMemoryMemberReleaseInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMemoryMemberReleaseInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMemoryMemberReleaseInvocation::resume(std::shared_ptr<IgbMemoryMemberCopyServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Memory release requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Memory release was invalidated by a callback.");};
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!s.field)fail("Memory release dereferences a null descriptor.");if(!s.field->flag_42)return pending("Memory release requires live ownership flag+42.");
        s.phase=*s.field->flag_42?P::element:P::done;break;
    case P::element:if(!element(s.field,s.element))return pending("Memory release requires its live element pointer.");
        if(!s.element){s.phase=P::done;break;}s.test=std::make_unique<TypeTest>(s.element,IgbFieldTypeKind::object);s.phase=P::test;break;
    case P::test:{const auto status=s.test->poll(services,check);if(!status.completed)return status;
        const auto matches=s.test->result;s.test.reset();s.phase=matches?P::member:P::done;break;}
    case P::member:
        if(!s.member)s.member=std::make_unique<MemberRead>(s.destination,s.field->offset);
        if(!s.member->poll(services))return pending("Memory release requires its captured destination pointer binding.");
        s.backing=s.member->result;s.member.reset();if(!s.backing.address){s.phase=P::done;break;}
        s.size=std::make_unique<IgbBufferCopyInvocation>(IgbBufferFunction::size,s.backing.bytes,s.backing.address);s.phase=P::size;break;
    case P::size:{const auto status=s.size->resume(services?services->buffers:nullptr);check();if(!status.completed)return status;
        s.count=status.value>>2u;s.size.reset();s.phase=P::visit;break;}
    case P::visit:
        if(s.index>=s.count){s.phase=P::done;break;}if(s.index>=262144)fail("Memory release exceeds the represented element visit bound.");
        if(!mapped(s.backing,services))return pending("Memory release requires its captured physical backing.");
        s.pointer=read(s.backing,s.index*4u,services);s.phase=s.pointer?P::object:P::next;break;
    case P::object:{const auto object=object_owner(s.pointer,services);if(!object)return pending("Memory release requires its captured element object binding.");
        s.object=*object;s.phase=P::decrement;break;}
    case P::decrement:
        igb_object_set_reference_word(s.object,igb_object_reference_word(s.object)-1u);
        if((igb_object_reference_word(s.object)&0x7fffffu)==0){s.call.request.kind=K::destroy_object;s.call.request.object=s.object;s.call.request.function=0x80066e1c;
            if(services)s.call.factory=services->factory;s.phase=P::destroy;}else s.phase=P::next;break;
    case P::destroy:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.call.clear();check();s.phase=P::next;break;}
    case P::next:++s.index;s.object={};s.phase=P::visit;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbMemoryMemberCopyInvocation::Impl {
    enum class P {release,free_gate,old,free,source,copy,publish,element,test_object,second_element,test_memory,
        pair_source,pair_destination,size,visit,resolve_object,clone_object,after_object,inner_copy,store,next,done};P phase=P::release;
    std::shared_ptr<IgbDecodeRuntimeField> field,element;IgbObjectInstance destination,source,object,cloned;
    std::shared_ptr<IgbMemoryMemberCopyServices> owner;std::unique_ptr<IgbMemoryMemberReleaseInvocation> release;
    std::unique_ptr<MemberRead> member;std::unique_ptr<TypeTest> test;std::unique_ptr<IgbBufferCopyInvocation> buffer_call;
    Buffer old,original,result,loop_source,loop_destination,inner;Call call;
    std::uint32_t offset{},count{},index{},pointer{},returned{};bool object_elements{},running{},fault{};
};
IgbMemoryMemberCopyInvocation::IgbMemoryMemberCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,IgbObjectInstance destination,IgbObjectInstance source)
    :impl_(std::make_unique<Impl>()){impl_->field=std::move(field);impl_->destination=std::move(destination);impl_->source=std::move(source);}
IgbMemoryMemberCopyInvocation::~IgbMemoryMemberCopyInvocation(){auto& s=*impl_;if(!completed() || faulted()){
    preserve(s.owner,s.source);preserve(s.owner,s.destination);preserve(s.owner,s.object);preserve(s.owner,s.cloned);
    if(s.owner)for(const auto& b:{s.old,s.original,s.result,s.loop_source,s.loop_destination,s.inner})if(b.bytes.buffer)s.owner->unfinished.push_back(b.bytes.buffer);
}}
bool IgbMemoryMemberCopyInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMemoryMemberCopyInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMemoryMemberCopyInvocation::resume(std::shared_ptr<IgbMemoryMemberCopyServices> services){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Memory member copy requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Memory member copy was invalidated by a callback.");};
    const auto buffer=[&]{return services?services->buffers:nullptr;};
    try{for(;;){check();switch(s.phase){
    case P::release:
        if(!s.release)s.release=std::make_unique<IgbMemoryMemberReleaseInvocation>(s.field,s.destination);
        {const auto status=s.release->resume(services);check();if(!status.completed)return status;}s.release.reset();s.phase=P::free_gate;break;
    case P::free_gate:
        if(!s.field->flag_40)return pending("Memory member copy requires its live free flag+40.");s.phase=*s.field->flag_40?P::old:P::source;break;
    case P::old:
        if(!s.member)s.member=std::make_unique<MemberRead>(s.destination,s.field->offset);
        if(!s.member->poll(services))return pending("Memory member copy requires its captured old pointer binding.");s.old=s.member->result;s.member.reset();
        if(!s.old.address){s.phase=P::source;break;}s.buffer_call=std::make_unique<IgbBufferCopyInvocation>(IgbBufferFunction::free,s.old.bytes,s.old.address);s.phase=P::free;break;
    case P::free:{const auto status=s.buffer_call->resume(buffer());check();if(!status.completed)return status;s.buffer_call.reset();s.phase=P::source;break;}
    case P::source:
        if(!s.member)s.member=std::make_unique<MemberRead>(s.source,s.field->offset);
        if(!s.member->poll(services))return pending("Memory member copy requires its captured source pointer binding.");s.original=s.member->result;s.member.reset();
        if(!s.original.address){s.result={};s.phase=P::publish;break;}
        s.buffer_call=std::make_unique<IgbBufferCopyInvocation>(IgbBufferFunction::copy,s.original.bytes,s.original.address);s.phase=P::copy;break;
    case P::copy:{const auto status=s.buffer_call->resume(buffer());check();if(!status.completed)return status;
        s.result={s.buffer_call->buffer_result(),s.buffer_call->buffer_address()};s.buffer_call.reset();s.phase=P::publish;break;}
    case P::publish:
        publish(s.destination,s.field->offset,s.result,services);remember(s.result,services);s.phase=P::element;break;
    case P::element:
        if(!element(s.field,s.element))return pending("Memory member copy requires its reloaded post-copy element.");
        if(!s.element){s.phase=P::done;break;}s.test=std::make_unique<TypeTest>(s.element,IgbFieldTypeKind::object);s.phase=P::test_object;break;
    case P::test_object:{const auto status=s.test->poll(services,check);if(!status.completed)return status;
        s.object_elements=s.test->result;s.test.reset();s.phase=s.object_elements?P::pair_source:P::second_element;break;}
    case P::second_element:
        if(!element(s.field,s.element))return pending("Memory member copy requires its reloaded nested-memory element.");
        s.test=std::make_unique<TypeTest>(s.element,IgbFieldTypeKind::memory_reference);s.phase=P::test_memory;break;
    case P::test_memory:{const auto status=s.test->poll(services,check);if(!status.completed)return status;
        const auto matches=s.test->result;s.test.reset();s.phase=matches?P::pair_source:P::done;break;}
    case P::pair_source:
        if(!s.member){s.offset=s.field->offset;s.member=std::make_unique<MemberRead>(s.source,s.offset);}
        if(!s.member->poll(services))return pending("Memory element copy requires its captured source backing pointer.");s.loop_source=s.member->result;s.member.reset();
        s.member=std::make_unique<MemberRead>(s.destination,s.offset);s.phase=P::pair_destination;break;
    case P::pair_destination:
        if(!s.member->poll(services))return pending("Memory element copy requires its captured destination backing pointer.");s.loop_destination=s.member->result;s.member.reset();
        if(s.object_elements && (!s.loop_source.address || !s.loop_destination.address)){s.phase=P::done;break;}
        s.buffer_call=std::make_unique<IgbBufferCopyInvocation>(IgbBufferFunction::size,s.loop_source.bytes,s.loop_source.address);s.phase=P::size;break;
    case P::size:{const auto status=s.buffer_call->resume(buffer());check();if(!status.completed)return status;s.count=status.value>>2u;s.buffer_call.reset();s.phase=P::visit;break;}
    case P::visit:
        if(s.index>=s.count){s.phase=P::done;break;}if(s.index>=262144)fail("Memory copy exceeds the represented element visit bound.");
        if(!mapped(s.loop_source,services))return pending("Memory element copy requires its captured source buffer storage.");
        s.pointer=read(s.loop_source,s.index*4u,services);
        if(!s.pointer){s.returned=0;s.phase=P::store;break;}
        if(s.object_elements)s.phase=P::resolve_object;
        else{s.inner={{},s.pointer};s.buffer_call=std::make_unique<IgbBufferCopyInvocation>(IgbBufferFunction::copy,s.inner.bytes,s.pointer);s.phase=P::inner_copy;}break;
    case P::resolve_object:{const auto object=object_owner(s.pointer,services);if(!object)return pending("Memory element clone requires its captured object binding.");
        s.object=*object;const auto table=igb_bootstrap_read_word({s.object.storage,s.object.offset},0);if(!table)fail("Memory element clone dereferences a null vtable.");
        s.call.request.kind=K::clone_object;s.call.request.object=s.object;s.call.request.vtable_word=table;s.call.request.virtual_slot=0x54;s.call.request.argument=1;
        if(services)s.call.factory=services->factory;s.phase=P::clone_object;break;}
    case P::clone_object:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.cloned=s.call.operation->object_result;s.call.clear();check();s.phase=P::after_object;break;}
    case P::after_object:
        s.returned=address(s.cloned);if(s.returned && services && services->objects)igb_bind_object_field_instance(services->objects,s.cloned);
        preserve(services,s.cloned);s.phase=P::store;break;
    case P::inner_copy:{const auto status=s.buffer_call->resume(buffer());check();if(!status.completed)return status;
        s.inner={s.buffer_call->buffer_result(),s.buffer_call->buffer_address()};s.returned=s.inner.address;s.buffer_call.reset();remember(s.inner,services);s.phase=P::store;break;}
    case P::store:
        if(!mapped(s.loop_destination,services))return pending("Memory element copy requires its captured destination buffer storage.");
        write(s.loop_destination,s.index*4u,s.returned,services);s.phase=P::next;break;
    case P::next:++s.index;s.object={};s.cloned={};s.inner={};s.phase=P::visit;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbFieldMemberCopyFactory make_native_igb_memory_member_copy_factory(std::shared_ptr<IgbMemoryMemberCopyServices> services,IgbFieldMemberCopyFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbFieldMemberCopyCall& call){
        if(call.function!=0x80062824)return remaining?remaining(call):std::shared_ptr<IgbFieldMemberCopyOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Memory member services expired before selection.");
        auto invocation=std::make_shared<IgbMemoryMemberCopyInvocation>(call.field,call.destination,call.source);auto op=std::make_shared<IgbFieldMemberCopyOperation>();
        op->resume=[invocation,selected]{return invocation->resume(selected);};return op;
    };
}
IgbMemoryMemberCopyFactory make_native_igb_memory_member_virtual_factory(std::shared_ptr<IgbFieldTypeRegistrationServices> registration,IgbMemoryMemberCopyFactory remaining){
    return [weak=std::weak_ptr(registration),remaining=std::move(remaining)](const IgbMemoryMemberCopyCall& call){
        if(call.kind!=K::element_metadata || call.virtual_slot!=0x58 || (call.vtable_word!=0x804762fc && call.vtable_word!=0x80476088))
            return remaining?remaining(call):std::shared_ptr<IgbMemoryMemberCopyOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Memory ancestry metadata services expired before selection.");
        const auto kind=call.vtable_word==0x804762fc?IgbFieldTypeKind::object:IgbFieldTypeKind::memory_reference;
        auto op=std::make_shared<IgbMemoryMemberCopyOperation>();op->resume=[selected,kind,out=op.get()]{const auto cell=igb_field_type_metadata_slot(selected,kind);
            if(!cell || !*cell)return pending("Memory element getter requires its represented class cell.");out->metadata_result=**cell;return MessengerCallResult{true,0,{}};};return op;
    };
}
} // namespace dw4
