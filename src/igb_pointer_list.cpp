#include "dw4/igb_pointer_list.hpp"
#include "dw4/igb_field_defaults.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message){throw IgbPointerListError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& f):flag(f){flag=true;}~Running(){flag=false;}};
using K=IgbPointerListServiceKind;
struct Call {
    IgbPointerListCall request;IgbPointerListFactory factory;
    std::shared_ptr<IgbPointerListOperation> operation;std::function<MessengerCallResult()> callback;
    void clear(){callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const std::shared_ptr<IgbPointerListServices>& services,Check check){
        if(!factory && services)factory=services->factory;
        if(!factory)return pending("Pointer list requires its selected native allocator service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Pointer list reached an unbound native allocator service.");
        if(!callback)callback=operation->resume;
        if(!callback)return pending("Pointer list requires its retained allocator continuation.");
        const auto result=callback();check();return result;
    }
};
IgbPointerListBacking backing(const std::shared_ptr<IgbRuntimeTypeList>& list){
    return {list->backing_bytes,list->backing_address,list->backing_offset,list->backing,{}};
}
IgbPointerListBacking backing(const std::shared_ptr<IgbDecodeFieldRegistry>& list){
    return {list->backing_bytes,list->backing_address,list->backing_offset,{},list->backing};
}
// Exact per-store bounds preserve earlier memset stores on a later failure.
std::size_t physical(const IgbPointerListBacking& b,std::uint32_t relative,std::size_t width){
    const auto target=b.address+relative;
    const auto base=b.address-b.offset;
    const auto at=static_cast<std::uint32_t>(target-base);
    if(!target || !b.bytes || b.bytes->released || at>b.bytes->bytes.size() || width>b.bytes->bytes.size()-at ||
       at>b.bytes->initialized.size() || width>b.bytes->initialized.size()-at)
        fail("Pointer list store reaches unavailable physical backing.");
    return at;
}
void write(const IgbPointerListBacking& b,std::uint32_t relative,std::uint32_t value,unsigned width){
    const auto at=physical(b,relative,width);
    for(unsigned i=0;i<width;++i){b.bytes->bytes[at+i]=static_cast<std::uint8_t>(value>>((width-1u-i)*8u));b.bytes->initialized[at+i]=1;}
}
struct Size {
    IgbPointerListBacking backing;Call call;std::shared_ptr<ModelSceneObject> pool;
    unsigned phase{};std::uint32_t result{};bool running{},fault{};
    template<class Check>MessengerCallResult resume(const std::shared_ptr<IgbPointerListServices>& services,Check check){
        if(running || fault){fault=true;fail("Pointer-list size requires an unfaulted nonrecursive invocation.");}
        Running guard(running);
        const auto validate=[&]{check();if(fault)fail("Pointer-list size continuation was invalidated.");};
        try{for(;;){validate();switch(phase){
        case 0:call.request.kind=K::allocation_pool;call.request.backing=backing;phase=1;break;
        case 1:{const auto status=call.poll(services,validate);if(!status.completed)return status;
            pool=std::move(call.operation->pool_result);call.clear();validate();phase=2;break;}
        case 2:if(!pool)fail("Pointer-list size dereferences a null allocation pool.");
            call.request.kind=K::allocation_size;call.request.backing=backing;call.request.pool=pool;phase=3;break;
        case 3:{const auto status=call.poll(services,validate);if(!status.completed)return status;
            result=call.operation->size_result;call.clear();validate();phase=4;break;}
        default:return {true,result,{}};
        }}}catch(...){fault=true;throw;}
    }
};
} // namespace

struct IgbPointerListInvocation::Impl {
    enum class P{entry,append_capacity,reserve_capacity,old_capacity,grow,object_pool,reallocate,publish,zero,store,write_value,size,call,done};
    std::shared_ptr<IgbRuntimeTypeList> types;std::shared_ptr<IgbDecodeFieldRegistry> fields;
    IgbPointerListValue value;IgbPointerListBacking selected,incoming;
    std::shared_ptr<IgbPointerListServices> owner;std::shared_ptr<ModelSceneObject> pool;
    std::unique_ptr<Size> size;Call call;P phase=P::entry,after=P::done;
    IgbBootstrapPointer physical_object;
    std::int32_t index{};std::uint32_t capacity{},old{},grown{},zero_at{},zero_left{},zero_total{},store_at{};
    bool field_list{},append{},running{},fault{},physical_captured{};
};
IgbPointerListInvocation::IgbPointerListInvocation(std::shared_ptr<IgbRuntimeTypeList> list,IgbPointerListValue value):impl_(std::make_unique<Impl>()){
    impl_->types=std::move(list);impl_->value=std::move(value);impl_->append=true;
}
IgbPointerListInvocation::IgbPointerListInvocation(std::shared_ptr<IgbDecodeFieldRegistry> list,IgbPointerListValue value):impl_(std::make_unique<Impl>()){
    impl_->fields=std::move(list);impl_->value=std::move(value);impl_->field_list=true;impl_->append=true;
}
IgbPointerListInvocation::IgbPointerListInvocation(std::shared_ptr<IgbRuntimeTypeList> list,std::int32_t index):impl_(std::make_unique<Impl>()){
    impl_->types=std::move(list);impl_->index=index;
}
IgbPointerListInvocation::IgbPointerListInvocation(std::shared_ptr<IgbDecodeFieldRegistry> list,std::int32_t index):impl_(std::make_unique<Impl>()){
    impl_->fields=std::move(list);impl_->index=index;impl_->field_list=true;
}
IgbPointerListInvocation::~IgbPointerListInvocation(){
    auto& s=*impl_;if((!completed() || faulted()) && s.owner){
        if(s.incoming.bytes)s.owner->unfinished.push_back(s.incoming.bytes);
        if(s.incoming.types)s.owner->unfinished.push_back(s.incoming.types);
        if(s.incoming.fields)s.owner->unfinished.push_back(s.incoming.fields);
    }
}
bool IgbPointerListInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbPointerListInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbPointerListInvocation::resume(std::shared_ptr<IgbPointerListServices> services){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Pointer list requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Pointer-list continuation was invalidated.");
        if(s.physical_captured){
            const auto& binding=s.field_list?s.fields->physical_storage:s.types->physical_storage;
            if(binding && !s.physical_object.storage){
                s.physical_object={binding->storage.lock(),binding->offset};
                if(!s.physical_object.storage)fail("Pointer-list physical allocation binding expired.");
            }
            if(bool(binding)!=bool(s.physical_object.storage) || (binding &&
                (binding->storage.lock()!=s.physical_object.storage || binding->offset!=s.physical_object.offset)))
                fail("Pointer-list operation lost its captured physical allocation binding.");
            if(s.physical_object.storage){
                if(s.physical_object.storage->faulted())fail("Pointer-list physical allocation is faulted.");
                if(s.field_list?igb_bootstrap_fields(s.physical_object)!=s.fields:igb_bootstrap_types(s.physical_object)!=s.types)
                    fail("Pointer-list physical allocation changed its typed owner.");
            }
        }};
    const auto live=[&]{if(s.field_list){if(!s.fields)fail("Pointer list dereferences a null field list.");return backing(s.fields);}
        if(!s.types)fail("Pointer list dereferences a null type list.");return backing(s.types);};
    const auto count=[&]()->std::int32_t&{return s.field_list?s.fields->count:s.types->count;};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbPointerListCall&{clear();s.call.request.kind=kind;s.call.request.types=s.types;s.call.request.fields=s.fields;
        s.after=after;s.phase=P::call;return s.call.request;};
    const auto capacity=[&](IgbPointerListBacking b,P after){
        if(!b.address){if(b.types || b.fields || b.bytes)fail("Pointer list requires the represented arithmetic backing address.");s.capacity=0;s.phase=after;}
        else{s.size=std::make_unique<Size>();s.size->backing=std::move(b);s.after=after;s.phase=P::size;}
    };
    const auto retain=[&](const auto& value){if(value){if(s.field_list)s.fields->retained_backings_.push_back(value);else s.types->retained_backings_.push_back(value);}};
    const auto overlaps_header=[&](const IgbPointerListBacking& b,std::uint32_t offset){
        if(!s.physical_object.storage || b.bytes!=s.physical_object.storage->bytes)return false;
        const auto slots=s.field_list?(b.fields?b.fields->size():0):(b.types?b.types->size():0);
        const auto at=std::uint64_t(s.physical_object.offset)+offset;
        return at<std::uint64_t(b.offset)+slots*4u && at+4u>b.offset;
    };
    const auto header=[&](std::uint32_t offset,std::uint32_t value){
        if(!s.physical_object.storage)return;
        igb_bootstrap_write_word(s.physical_object,offset,value);
        const auto b=live();if(overlaps_header(b,offset)){
            // A header store over an aliased pointer array can leave a nonnull
            // address without a known owner. Preserve the bytes and discard
            // the stale mapping rather than presenting that address as null.
            retain(b.fields);retain(b.types);
            if(s.field_list)s.fields->backing.reset();else s.types->backing.reset();
        }
    };
    const auto synchronize_alias=[&](const IgbPointerListBacking& b,std::uint32_t relative,unsigned width){
        if(!s.physical_object.storage || b.bytes!=s.physical_object.storage->bytes)return;
        IgbObjectInstance object;object.storage=s.physical_object.storage;object.offset=s.physical_object.offset;
        object.fields=s.fields;object.types=s.types;
        // A represented backing can alias its own header. In particular,
        //53C24 must observe an element store over +0C before incrementing it.
        const auto at=(b.address+relative)-(b.address-b.offset);
        igb_synchronize_field_storage(object,at,width);
    };
    try{for(;;){check();switch(s.phase){
    case P::entry:{const auto b=live();const auto& binding=s.field_list?s.fields->physical_storage:s.types->physical_storage;
        if(binding){s.physical_object={binding->storage.lock(),binding->offset};
            if(!s.physical_object.storage)fail("Pointer-list physical allocation binding expired.");}
        s.physical_captured=true;check();
        if(s.append){s.index=count();capacity(b,P::append_capacity);}else capacity(b,P::reserve_capacity);break;}
    case P::append_capacity:
        if(s.index<std::bit_cast<std::int32_t>(s.capacity))s.phase=P::store;
        else capacity(live(),P::reserve_capacity);break;
    case P::reserve_capacity:
        if(s.index<std::bit_cast<std::int32_t>(s.capacity))s.phase=s.append?P::store:P::done;
        else capacity(live(),P::old_capacity);break;
    case P::old_capacity:
        s.old=s.capacity;s.grown=s.old?s.old:4u;s.phase=P::grow;break;
    case P::grow:
        while(std::bit_cast<std::int32_t>(s.grown)<=s.index){
            const auto next=s.grown<512u?s.grown*2u:s.grown+512u;
            if(std::bit_cast<std::int32_t>(next)<=std::bit_cast<std::int32_t>(s.grown))fail("Pointer-list growth reaches a nonterminating wrapped original capacity.");
            s.grown=next;
        }
        s.selected=live(); //682A4 captures backing and bytes before pool lookup.
        {auto& call=select(K::object_pool,P::object_pool);call.handle=(s.field_list?s.fields->reference_word:s.types->reference_word)>>24u;}break;
    case P::object_pool:
        s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::reallocate;break;
    case P::reallocate:
        if(!s.pool)fail("Pointer-list reallocation dereferences a null object pool.");
        {auto& call=select(K::reallocate,P::publish);call.pool=s.pool;call.backing=s.selected;call.bytes=s.grown<<2u;}break;
    case P::publish:{
        s.incoming=std::move(s.call.operation->backing_result);clear();
        // Actual538D8 publishes +08 before the tail clear at538F4. A failed
        // physical header store must leave the canonical backing unchanged.
        header(8,s.incoming.address);
        // The returned mapping was prepared before538D8 as well. It may
        // overlap +08 even when the previous backing did not; installing it
        // unchanged would reintroduce an owner invalidated by that store.
        if(overlaps_header(s.incoming,8)){
            retain(s.incoming.types);retain(s.incoming.fields);
            s.incoming.types.reset();s.incoming.fields.reset();
        }
        const auto previous=live();retain(previous.bytes);retain(previous.types);retain(previous.fields);
        if(s.field_list){s.fields->backing_address=s.incoming.address;s.fields->backing_offset=s.incoming.offset;
            s.fields->backing_bytes=s.incoming.bytes;s.fields->backing=s.incoming.fields;}
        else{s.types->backing_address=s.incoming.address;s.types->backing_offset=s.incoming.offset;
            s.types->backing_bytes=s.incoming.bytes;s.types->backing=s.incoming.types;}
        s.zero_at=s.old<<2u;s.zero_left=(s.grown-s.old)<<2u;s.zero_total=s.zero_left;s.phase=P::zero;break;
    }
    case P::zero:{
        const auto b=live();
        //03130 uses byte stores for lengths below32, otherwise alignment bytes,
        //full words, then a byte tail. Preserve partial malformed-store prefixes.
        while(s.zero_left){
            const auto width=s.zero_total>=32u && ((b.address+s.zero_at)&3u)==0 && s.zero_left>=4u?4u:1u;
            const auto before=s.zero_at;write(b,s.zero_at,0,width);synchronize_alias(b,s.zero_at,width);s.zero_at+=width;s.zero_left-=width;
            for(auto at=before/4u;at<s.zero_at/4u;++at){
                if(b.types && at<b.types->size()){retain((*b.types)[at]);(*b.types)[at].reset();}
                if(b.fields && at<b.fields->size()){retain((*b.fields)[at]);(*b.fields)[at].reset();}}
        }
        s.incoming={};s.selected={};s.phase=s.append?P::store:P::done;break;
    }
    case P::store:
        s.store_at=std::bit_cast<std::uint32_t>(count())<<2u;s.selected=live();s.phase=P::write_value;break;
    case P::write_value:{
        const auto raw_index=s.store_at;auto& b=s.selected;
        const auto slot=raw_index/4u;
        // The original stores the pointer before incrementing the live count.
        // Negative signed counts may shift to a valid low physical slot.
        physical(b,raw_index,4);
        // A late typed binding may fill the captured physical allocation; it
        // must not redirect the reached store to a replacement live backing.
        const auto current=live();
        if(current.address==b.address && current.offset==b.offset && current.bytes==b.bytes){
            if(!b.fields)b.fields=current.fields;if(!b.types)b.types=current.types;
        }
        if(s.field_list){if(s.value.metadata || (s.value.address==0)!=(!s.value.field))fail("Pointer-list field value requires a consistent explicit identity.");
            if(!b.fields || slot>=b.fields->size())return pending("Pointer-list store requires its explicit physical field-owner mapping.");
            write(b,raw_index,s.value.address,4);synchronize_alias(b,raw_index,4);retain((*b.fields)[slot]);(*b.fields)[slot]=s.value.field;
        }else{if(s.value.field || (s.value.address==0)!=(!s.value.metadata))fail("Pointer-list type value requires a consistent explicit identity.");
            if(!b.types || slot>=b.types->size())return pending("Pointer-list store requires its explicit physical metadata-owner mapping.");
            write(b,raw_index,s.value.address,4);synchronize_alias(b,raw_index,4);retain((*b.types)[slot]);(*b.types)[slot]=s.value.metadata;
        }
        const auto next=std::bit_cast<std::uint32_t>(count())+1u;
        // Actual53C20 has already stored the pointer. Preserve that prefix if
        // the following +0C store at53C2C cannot reach physical header bytes.
        header(12,next);count()=std::bit_cast<std::int32_t>(next);s.phase=P::done;break;
    }
    case P::size:{const auto status=s.size->resume(services,check);if(!status.completed)return status;
        s.capacity=s.size->result>>2u;s.size.reset();check();s.phase=s.after;break;}
    case P::call:{const auto status=s.call.poll(services,check);if(!status.completed)return status;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbMetadataOperationsFactory make_native_igb_pointer_list_metadata_factory(std::shared_ptr<IgbPointerListServices> services,IgbMetadataOperationsFactory remaining){
    std::weak_ptr<IgbPointerListServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbMetadataOperationCall& call){
        const bool fields=call.kind==IgbMetadataOperationKind::append_field;
        if(!fields && call.kind!=IgbMetadataOperationKind::append_child)return remaining?remaining(call):std::shared_ptr<IgbMetadataOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Pointer-list services expired before selection.");
        auto operation=std::make_shared<IgbMetadataOperation>();auto invocation=std::make_shared<std::shared_ptr<IgbPointerListInvocation>>();
        operation->resume=[selected,call,fields,invocation]{
            if(!*invocation){IgbPointerListValue value;bool found=fields?!call.field:!call.metadata;
                for(const auto& candidate:selected->values)if((fields && call.field && candidate.field==call.field) || (!fields && call.metadata && candidate.metadata==call.metadata)){
                    if(found && (value.address!=candidate.address || value.metadata!=candidate.metadata || value.field!=candidate.field))fail("Pointer-list value has conflicting arithmetic identities.");
                    value=candidate;found=true;
                }
                if(!found)return pending("Pointer-list append requires the captured value's original arithmetic identity.");
                *invocation=fields?std::make_shared<IgbPointerListInvocation>(call.fields,value):std::make_shared<IgbPointerListInvocation>(call.children,value);
            }
            return (*invocation)->resume(selected);
        };return operation;
    };
}
IgbArkCoreFactory make_native_igb_pointer_list_bootstrap_factory(std::shared_ptr<IgbPointerListServices> services,IgbArkCoreFactory remaining){
    std::weak_ptr<IgbPointerListServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbArkCoreCall& call){
        if(call.kind!=IgbArkCoreServiceKind::reserve_types && call.kind!=IgbArkCoreServiceKind::backing_size)
            return remaining?remaining(call):std::shared_ptr<IgbArkCoreOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Bootstrap pointer-list services expired before selection.");
        auto operation=std::make_shared<IgbArkCoreOperation>();
        if(call.kind==IgbArkCoreServiceKind::reserve_types){auto invocation=std::make_shared<IgbPointerListInvocation>(call.types,std::bit_cast<std::int32_t>(call.argument));
            operation->resume=[selected,invocation]{return invocation->resume(selected);};
        }else{
            auto invocation=std::make_shared<Size>();invocation->backing={call.type_backing_bytes,call.type_backing_address,call.type_backing_offset,call.type_backing,{}};
            operation->resume=[selected,invocation]{return invocation->resume(selected,[]{});};}
        return operation;
    };
}
} // namespace dw4
