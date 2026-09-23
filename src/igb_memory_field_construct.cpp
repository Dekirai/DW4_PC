#include "dw4/igb_memory_field_construct.hpp"
#include "dw4/igb_field_defaults.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbMemoryFieldConstructError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running {bool& value;explicit Running(bool& v):value(v){value=true;}~Running(){value=false;}};
struct Call {
    IgbMemoryFieldConstructCall request;IgbMemoryFieldConstructFactory factory;
    std::shared_ptr<IgbMemoryFieldConstructOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Memory member construction requires its reached field or allocator service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Memory member construction reached an unbound field or allocator operation.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Memory member construction requires its retained continuation.");
        const auto status=callback();check();return status;
    }
};
std::shared_ptr<IgbDecodeRuntimeField> element(const std::shared_ptr<IgbDecodeRuntimeField>& descriptor){
    if(!descriptor)fail("Memory member construction dereferences a null descriptor.");
    const auto value=descriptor->element_field->lock();
    if(!value)fail("Memory member construction dereferences a null or expired element descriptor.");return value;
}
bool select_virtual(Call& call,const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t slot,
    const std::shared_ptr<IgbMemoryFieldConstructServices>& services){
    if(!field->vtable_word)return false;
    if(!*field->vtable_word)fail("Memory member construction dereferences a null element vtable.");
    call.request.kind=IgbMemoryFieldConstructServiceKind::field_virtual;call.request.field=field;
    call.request.vtable_word=*field->vtable_word;call.request.virtual_slot=slot;if(services)call.factory=services->factory;return true;
}
std::shared_ptr<IgbInternedString> string_owner(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,std::uint32_t address){
    std::shared_ptr<IgbInternedString> result;
    if(services && services->values)for(const auto& b:services->values->strings)if(b.address==address){
        if(!b.owner || (result && result!=b.owner))fail("Memory member pool name has conflicting arithmetic identities.");result=b.owner;
    }return result;
}
void retain(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,const IgbPointerListBacking& b){
    if(!services)return;if(b.bytes)services->retained.push_back(b.bytes);if(b.fields)services->retained.push_back(b.fields);if(b.types)services->retained.push_back(b.types);
}
void merge(IgbPointerListBacking& destination,const IgbPointerListBacking& incoming){
    if(destination.address!=incoming.address)fail("Memory allocation binding changed its arithmetic identity.");
    if(destination.bytes && incoming.bytes && (destination.bytes!=incoming.bytes || destination.offset!=incoming.offset))fail("Memory allocation has conflicting physical storage.");
    if(destination.fields && incoming.fields && destination.fields!=incoming.fields)fail("Memory allocation has conflicting field-pointer mappings.");
    if(destination.types && incoming.types && destination.types!=incoming.types)fail("Memory allocation has conflicting metadata-pointer mappings.");
    if(!destination.bytes && incoming.bytes){destination.bytes=incoming.bytes;destination.offset=incoming.offset;}
    if(!destination.fields)destination.fields=incoming.fields;if(!destination.types)destination.types=incoming.types;
    if(destination.fields && destination.types)fail("Memory allocation cannot represent two pointer-list kinds.");
}
void refresh(IgbPointerListBacking& backing,const std::shared_ptr<IgbMemoryFieldConstructServices>& services){
    if(services)for(const auto& b:services->allocations)if(b.address==backing.address)merge(backing,b);
}
std::size_t physical(const IgbPointerListBacking& b,std::uint32_t relative,unsigned width){
    const auto target=b.address+relative,base=b.address-b.offset,at=target-base;
    if(!target || !b.bytes || b.bytes->released || at>b.bytes->bytes.size() || width>b.bytes->bytes.size()-at ||
        at>b.bytes->initialized.size() || width>b.bytes->initialized.size()-at)fail("Memory zero fill reaches unavailable physical storage.");
    return at;
}
void synchronize_zero(const IgbPointerListBacking& b,std::uint32_t relative,unsigned width,
    const std::shared_ptr<IgbMemoryFieldConstructServices>& services){
    if(!b.fields && !b.types)return;
    const auto count=b.fields?b.fields->size():b.types->size();
    for(std::uint64_t i=relative/4;i<=(std::uint64_t(relative)+width-1)/4 && i<count;++i){
        const auto at=std::uint64_t(b.offset)+i*4;
        if(at+4>b.bytes->bytes.size() || at+4>b.bytes->initialized.size())continue;
        bool zero=true;for(unsigned byte=0;byte<4;++byte)zero=zero && b.bytes->initialized[static_cast<std::size_t>(at)+byte] && b.bytes->bytes[static_cast<std::size_t>(at)+byte]==0;
        if(!zero)continue;
        if(b.fields){auto& old=(*b.fields)[static_cast<std::size_t>(i)];if(old && services)services->retained.push_back(old);old.reset();}
        else {auto& old=(*b.types)[static_cast<std::size_t>(i)];if(old && services)services->retained.push_back(old);old.reset();}
    }
}
}
void igb_bind_memory_field_allocation(const std::shared_ptr<IgbMemoryFieldConstructServices>& services,IgbPointerListBacking backing){
    if(!services || !backing.address)fail("Memory allocation binding requires services and a nonnull arithmetic address.");
    if(backing.fields && backing.types)fail("Memory allocation binding has conflicting pointer-list kinds.");
    for(auto& old:services->allocations)if(old.address==backing.address){merge(old,backing);return;}
    services->allocations.push_back(std::move(backing));
}
struct IgbMemoryFieldAllocateInvocation::Impl {
    enum class P {size_element,size_table,after_size,alignment_element,alignment_table,after_alignment,allocate,
        after_allocate,test_element,test_global,test_table,after_test,ancestry,zero,destination,mapping,publish,status,call,done};
    P phase=P::size_element,after=P::done;
    std::shared_ptr<IgbDecodeRuntimeField> descriptor,element;IgbObjectInstance target;
    std::shared_ptr<ModelSceneObject> pool;std::shared_ptr<IgbRuntimeMetaType> reference,metadata;
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> visited;
    std::shared_ptr<IgbMemoryFieldConstructServices> owner;IgbPointerListBacking backing;Call call;
    std::uint32_t count{},bytes{},alignment{},returned{},zero_at{},offset{},result{};
    bool allocated{},running{},fault{},zeroed{};
};
IgbMemoryFieldAllocateInvocation::IgbMemoryFieldAllocateInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,
    IgbObjectInstance target,std::uint32_t count,std::shared_ptr<ModelSceneObject> pool):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);impl_->count=count;impl_->pool=std::move(pool);
}
IgbMemoryFieldAllocateInvocation::~IgbMemoryFieldAllocateInvocation(){auto& s=*impl_;if(!completed() || faulted()){
    retain(s.owner,s.backing);if(s.owner){if(s.target.storage)s.owner->retained.push_back(s.target.storage);if(s.pool)s.owner->retained.push_back(s.pool);}
}}
bool IgbMemoryFieldAllocateInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMemoryFieldAllocateInvocation::faulted()const noexcept{return impl_->fault;}
std::uint32_t IgbMemoryFieldAllocateInvocation::result()const{if(!completed() || faulted())fail("Incomplete memory allocation helper has no status result.");return impl_->result;}
MessengerCallResult IgbMemoryFieldAllocateInvocation::resume(std::shared_ptr<IgbMemoryFieldConstructServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbMemoryFieldConstructServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Memory allocation helper requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,s.result,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Memory allocation helper was invalidated by a callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto capture=[&](P next)->bool{if(!s.descriptor)fail("Memory allocation helper reads a null descriptor.");
        if(!s.descriptor->element_field)return false;s.element=element(s.descriptor);s.phase=next;return true;};
    const auto invoke=[&](std::uint32_t slot,P after)->bool{if(!select_virtual(s.call,s.element,slot,services))return false;s.after=after;s.phase=P::call;return true;};
    const auto retain_old=[&](const auto& old){if(old){if(s.target.fields)s.target.fields->retained_backings_.push_back(old);else if(s.target.types)s.target.types->retained_backings_.push_back(old);}};
    try{for(;;){check();switch(s.phase){
    case P::size_element:if(!capture(P::size_table))return pending("Memory allocation requires its first live element pointer+3C.");break;
    case P::size_table:if(!invoke(0x64,P::after_size))return pending("Memory allocation requires its captured size element vtable.");break;
    case P::after_size:s.bytes=s.count*static_cast<std::uint16_t>(s.returned);clear();s.phase=P::alignment_element;break;
    case P::alignment_element:if(!capture(P::alignment_table))return pending("Memory allocation requires its reloaded alignment element+3C.");break;
    case P::alignment_table:if(!invoke(0x6c,P::after_alignment))return pending("Memory allocation requires its captured alignment element vtable.");break;
    case P::after_alignment:s.alignment=static_cast<std::uint16_t>(s.returned);clear();s.phase=P::allocate;break;
    case P::allocate:
        if(!s.pool)fail("Memory allocation dereferences a null pool at virtual+DC.");
        s.call.request.kind=K::allocate;s.call.request.pool=s.pool;s.call.request.bytes=s.bytes;s.call.request.alignment=s.alignment;
        s.call.request.virtual_slot=0xdc;if(services)s.call.factory=services->factory;s.after=P::after_allocate;s.phase=P::call;break;
    case P::after_allocate:
        s.backing=s.call.operation->backing_result;clear();
        if(!s.backing.address && (s.backing.bytes || s.backing.fields || s.backing.types))fail("Null memory allocation has a nonnull physical mapping.");
        s.allocated=s.backing.address!=0;s.phase=s.allocated?P::test_element:P::status;break;
    case P::test_element:if(!capture(P::test_global))return pending("Memory allocation requires its live post-allocation element+3C.");break;
    case P::test_global:
        if(!services || !services->reference_metadata || !*services->reference_metadata)return pending("Memory type test requires canonical reference metadata80561684.");
        s.reference=**services->reference_metadata;s.phase=P::test_table;break;
    case P::test_table:if(!invoke(0x58,P::after_test))return pending("Memory type test requires its captured element metadata virtual.");break;
    case P::after_test:s.metadata=s.call.operation->metadata_result;clear();s.phase=P::ancestry;break;
    case P::ancestry:
        if(!s.metadata){s.phase=P::destination;break;}
        if(s.metadata==s.reference){s.phase=P::zero;break;}
        if(!s.metadata->parent)return pending("Memory type test requires its live metadata parent+38.");
        if(std::find(s.visited.begin(),s.visited.end(),s.metadata)!=s.visited.end())fail("Memory type test reached a cyclic metadata parent chain.");
        s.visited.push_back(s.metadata);s.metadata=*s.metadata->parent;break;
    case P::zero:
        refresh(s.backing,services);if(!s.backing.bytes && s.bytes)return pending("Memory zero fill requires its allocated physical buffer binding.");
        while(s.zero_at<s.bytes){const auto remaining=s.bytes-s.zero_at;const unsigned width=s.bytes>=32 && ((s.backing.address+s.zero_at)&3u)==0 && remaining>=4?4u:1u;
            const auto at=physical(s.backing,s.zero_at,width);for(unsigned i=0;i<width;++i){s.backing.bytes->bytes[at+i]=0;s.backing.bytes->initialized[at+i]=1;}
            synchronize_zero(s.backing,s.zero_at,width,services);s.zero_at+=width;
        }
        s.zeroed=true;s.phase=P::destination;break;
    case P::destination:s.offset=s.descriptor->offset;s.phase=P::mapping;break;
    case P::mapping:
        // Arithmetic pointer publication does not invent its allocation's host
        // representation. A late mapping preserves all earlier zero-fill effects.
        refresh(s.backing,services);
        if((s.target.fields || s.target.types) && s.offset==8){
            if(!s.backing.bytes)return pending("List member publication requires its allocated physical buffer binding.");
            if(s.target.fields && !s.backing.fields)return pending("List member publication requires explicit field-pointer backing mapping.");
            if(s.target.types && !s.backing.types)return pending("List member publication requires explicit metadata-pointer backing mapping.");
            if((s.target.fields && s.backing.types) || (s.target.types && s.backing.fields))fail("Memory backing pointer-list kind disagrees with target storage.");
            if(s.zeroed){
                const auto slots=s.bytes/4u;const auto represented=s.target.fields?s.backing.fields->size():s.backing.types->size();
                if(represented<slots)return pending("Zero-filled list backing requires all reached pointer slots to be represented.");
                // A typed mapping may have arrived after the physical zero fill.
                for(std::uint32_t i=0;i<slots;++i)synchronize_zero(s.backing,i*4u,4,services);
            }
        }
        s.phase=P::publish;break;
    case P::publish:
        igb_object_validate_instance(s.target);
        igb_bootstrap_write_word({s.target.storage,s.target.offset},s.offset,s.backing.address);
        if(s.offset==8 && s.target.fields){auto& list=*s.target.fields;
            if(list.backing!=s.backing.fields)retain_old(list.backing);if(list.backing_bytes!=s.backing.bytes)retain_old(list.backing_bytes);
            list.backing=s.backing.fields;list.backing_bytes=s.backing.bytes;list.backing_address=s.backing.address;list.backing_offset=s.backing.offset;
        }else if(s.offset==8 && s.target.types){auto& list=*s.target.types;
            if(list.backing!=s.backing.types)retain_old(list.backing);if(list.backing_bytes!=s.backing.bytes)retain_old(list.backing_bytes);
            list.backing=s.backing.types;list.backing_bytes=s.backing.bytes;list.backing_address=s.backing.address;list.backing_offset=s.backing.offset;
        }else {igb_synchronize_field_storage(s.target,s.target.offset+s.offset,4);retain(services,s.backing);}
        s.phase=P::status;break;
    case P::status:{const auto status=services?(s.allocated?services->status_success:services->status_failure):std::optional<std::uint32_t>{};
        if(!status)return pending(s.allocated?"Memory allocation requires success status global805622D0 after publication.":"Memory allocation requires failure status global805622D4.");
        s.result=*status;s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.returned=status.value;s.phase=s.after;break;}
    case P::done:return {true,s.result,{}};
    }}}catch(...){s.fault=true;throw;}
}
struct IgbMemoryFieldConstructInvocation::Impl {
    enum class P {gate,element,table,after_size,extent,name_pointer,count,name,after_named,tagged,after_tagged,allocate,call,done};
    P phase=P::gate,after=P::done;std::shared_ptr<IgbDecodeRuntimeField> descriptor,element;
    IgbObjectInstance target;std::shared_ptr<IgbMemoryFieldConstructServices> owner;
    std::shared_ptr<IgbInternedString> name;std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<IgbMemoryFieldAllocateInvocation> allocation;Call call;
    std::uint32_t size{},extent{},count{},name_pointer{},returned{};bool running{},fault{};
};
IgbMemoryFieldConstructInvocation::IgbMemoryFieldConstructInvocation(std::shared_ptr<IgbDecodeRuntimeField> descriptor,IgbObjectInstance target):impl_(std::make_unique<Impl>()){
    impl_->descriptor=std::move(descriptor);impl_->target=std::move(target);
}
IgbMemoryFieldConstructInvocation::~IgbMemoryFieldConstructInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.target.storage)s.owner->retained.push_back(s.target.storage);if(s.pool)s.owner->retained.push_back(s.pool);if(s.name)s.owner->retained.push_back(s.name);
}}
bool IgbMemoryFieldConstructInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbMemoryFieldConstructInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbMemoryFieldConstructInvocation::resume(std::shared_ptr<IgbMemoryFieldConstructServices> services){
    auto& s=*impl_;using P=Impl::P;using K=IgbMemoryFieldConstructServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Memory member construction requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Memory member construction was invalidated by a callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbMemoryFieldConstructCall&{clear();s.call.request.kind=kind;
        if(services)s.call.factory=services->factory;s.after=after;s.phase=P::call;return s.call.request;};
    try{for(;;){check();switch(s.phase){
    case P::gate:
        if(!s.descriptor)fail("Memory member construction reads a null descriptor.");
        if(!s.descriptor->flag_34)return pending("Memory member construction requires live flag+34.");
        s.phase=*s.descriptor->flag_34?P::element:P::done;break;
    case P::element:
        if(!s.descriptor->element_field)return pending("Memory member construction requires element pointer+3C.");
        s.element=element(s.descriptor);s.phase=P::table;break;
    case P::table:
        if(!select_virtual(s.call,s.element,0x64,services))return pending("Memory member construction requires its captured element size virtual.");
        s.after=P::after_size;s.phase=P::call;break;
    case P::after_size:s.size=static_cast<std::uint16_t>(s.returned);clear();s.phase=P::extent;break;
    case P::extent:
        if(!s.descriptor->word_38)return pending("Memory member construction requires requested byte count+38.");
        s.extent=*s.descriptor->word_38;s.phase=P::name_pointer;break;
    case P::name_pointer:
        if(!s.descriptor->word_4c)return pending("Memory member construction requires pool-name pointer+4C.");
        s.name_pointer=*s.descriptor->word_4c;s.phase=P::count;break;
    case P::count:
        if(!s.size)fail("Memory member construction reaches undefined original unsigned division by zero.");
        s.count=(s.extent+s.size-1u)/s.size;s.phase=s.name_pointer?P::name:P::tagged;break;
    case P::name:{s.name=string_owner(services,s.name_pointer);if(!s.name)return pending("Memory member construction requires its captured pool-name binding.");
        if(s.name->released)fail("Memory member construction reads released pool-name characters.");bool nonempty{};
        if(s.name->native_name)nonempty=igb_buffer_bytes(s.name->native_name->buffer,s.name->native_name->offset,1).front()!=0;
        else {if(!s.name->value)return pending("Memory member construction requires pool-name character storage.");nonempty=!s.name->value->empty() && s.name->value->front()!=0;}
        if(nonempty)select(K::named_pool,P::after_named).name=igb_interned_string_name(s.name);else s.phase=P::tagged;break;}
    case P::after_named:s.pool=s.call.operation->pool_result;clear();s.phase=s.pool?P::allocate:P::tagged;break;
    case P::tagged:{const auto handle=igb_bootstrap_read_byte({s.target.storage,s.target.offset},4);
        auto& call=select(K::tagged_pool,P::after_tagged);call.object=s.target;call.handle=handle;break;}
    case P::after_tagged:s.pool=s.call.operation->pool_result;clear();s.phase=P::allocate;break;
    case P::allocate:{if(!s.allocation)s.allocation=std::make_unique<IgbMemoryFieldAllocateInvocation>(s.descriptor,s.target,s.count,s.pool);
        const auto status=s.allocation->resume(services);check();if(!status.completed)return status;s.allocation.reset();check();s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.returned=status.value;s.phase=s.after;break;}
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbMemoryFieldConstructFactory make_native_igb_memory_field_virtual_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration,IgbMemoryFieldConstructFactory remaining){
    return [weak=std::weak_ptr(registration),remaining=std::move(remaining)](const IgbMemoryFieldConstructCall& call)->std::shared_ptr<IgbMemoryFieldConstructOperation>{
        if(call.kind!=IgbMemoryFieldConstructServiceKind::field_virtual || call.vtable_word!=0x804762fc ||
            (call.virtual_slot!=0x64 && call.virtual_slot!=0x6c && call.virtual_slot!=0x58))return remaining?remaining(call):nullptr;
        auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
        if(call.virtual_slot!=0x58)operation->resume=[] {return MessengerCallResult{true,4,{}};};
        else {const auto selected=weak.lock();if(!selected)fail("Memory element metadata services expired before selection.");
            operation->resume=[selected,out=operation.get()]{const auto cell=igb_field_type_metadata_slot(selected,IgbFieldTypeKind::object);
                if(!cell || !*cell)return pending("Memory element metadata getter requires canonical805616E8.");
                out->metadata_result=**cell;return MessengerCallResult{true,0,{}};};}
        return operation;
    };
}
} // namespace dw4
