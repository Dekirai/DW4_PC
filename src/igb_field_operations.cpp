#include "dw4/igb_field_operations.hpp"
#include "dw4/igb_field_reference.hpp"

#include <bit>

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* message){throw IgbFieldOperationsError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& f):flag(f){flag=true;}~Running(){flag=false;}};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,Check check){
        check();
        if(!factory)factory=fallback;if(!factory)return pending("Field operation requires its selected native service.");
        if(!operation){operation=factory(request);check();}if(!operation)return pending("Field operation reached an unbound native service.");
        if(!callback && !checked_callback){
            if constexpr(requires{operation->resume_checked;})checked_callback=operation->resume_checked;
            if(!checked_callback)callback=operation->resume;
        }
        if(!callback && !checked_callback)return pending("Field operation requires its retained service continuation.");
        const auto status=checked_callback?checked_callback(check):callback();check();return status;
    }
};
using FieldCall=Call<IgbFieldOperationCall,IgbFieldOperationsFactory,IgbFieldOperation>;
using NameCall=Call<IgbDependenciesCall,IgbDependenciesFactory,IgbDependenciesOperation>;
using K=IgbFieldOperationKind;
void require_field(const std::shared_ptr<IgbDecodeRuntimeField>& field){if(!field)fail("Field operation dereferences a null descriptor.");}
std::optional<std::uint32_t> vtable(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    require_field(field);if(field->vtable_word && !*field->vtable_word)fail("Field operation dereferences a null vtable.");return field->vtable_word;
}
void retain_name(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(field->name_string && *field->name_string)field->overwritten_constructor_owners_.push_back(*field->name_string);
    if(field->name)field->overwritten_constructor_strings_.push_back(field->name);
}
} // namespace

void igb_field_store_size(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint16_t value){
    require_field(field);
    if(field->physical_storage){const auto& binding=*field->physical_storage;auto storage=binding.storage.lock();
        if(!storage)fail("Field size store reached expired physical storage.");IgbBootstrapPointer pointer{storage,binding.offset};
        if(igb_bootstrap_field(pointer)!=field)fail("Field size store reached a changed physical identity.");
        igb_bootstrap_write_halfword(pointer,0x14,value);}
    field->size=value;
}
void igb_field_store_default(const std::shared_ptr<IgbDecodeRuntimeField>& field,
    const IgbRuntimeFieldDefaultSource& source,std::uint32_t address){
    require_field(field);
    if((address==0)!=(!source.buffer))fail("Field default pointer and storage binding disagree.");
    if(field->physical_storage){const auto& binding=*field->physical_storage;auto storage=binding.storage.lock();
        if(!storage)fail("Field default store reached expired physical storage.");IgbBootstrapPointer pointer{storage,binding.offset};
        if(igb_bootstrap_field(pointer)!=field)fail("Field default store reached a changed physical identity.");
        igb_bootstrap_write_word(pointer,0x20,address);}
    if(field->default_bytes && field->default_bytes->buffer)field->overwritten_constructor_owners_.push_back(field->default_bytes->buffer);
    field->default_bytes=source;field->default_bytes_address=address;
}

struct IgbFieldNameInvocation::Impl {
    enum class P{source,old,release,publish,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;std::shared_ptr<IgbFieldNameSlot> source;
    std::shared_ptr<IgbInternedString> incoming,old;std::shared_ptr<IgbFieldOperationsServices> owner;
    NameCall call;P phase=P::source;bool running{},fault{};
};
IgbFieldNameInvocation::IgbFieldNameInvocation(std::shared_ptr<IgbDecodeRuntimeField> field,std::shared_ptr<IgbFieldNameSlot> source):impl_(std::make_unique<Impl>()){
    impl_->field=std::move(field);impl_->source=std::move(source);
}
IgbFieldNameInvocation::~IgbFieldNameInvocation(){auto& s=*impl_;if(!completed()){
    const auto keep=[&](const std::shared_ptr<IgbInternedString>& value){if(!value)return;
        if(s.owner)s.owner->unfinished.push_back(value);else if(s.field)s.field->overwritten_constructor_owners_.push_back(value);};
    keep(s.incoming);keep(s.old);}}
bool IgbFieldNameInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldNameInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldNameInvocation::resume(std::shared_ptr<IgbFieldOperationsServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field naming requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field name continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field name was invalidated by its enclosing callback.");};
    try{for(;;){check();switch(s.phase){
    case P::source:
        if(!s.source)fail("Field naming reads a null source pointer cell.");if(!s.source->has_value())return pending("Field naming requires the captured source pointer cell.");
        s.incoming=**s.source;if(s.incoming){if(s.incoming->released)fail("Field naming retains a released string header.");++s.incoming->references;}
        s.phase=P::old;break;
    case P::old:
        require_field(s.field);if(!s.field->name_string)return pending("Field naming requires the live old name pointer.");
        s.old=*s.field->name_string;
        if(s.old){if(s.old->released)fail("Field naming reads a released old string header.");--s.old->references;}
        s.phase=s.old && s.old->references==0?P::release:P::publish;break;
    case P::release:
        s.call.request.kind=IgbDependenciesServiceKind::release_string;s.call.request.string=s.old;s.call.request.pool=s.old->container;s.phase=P::call;break;
    case P::call:{const auto dep=services?services->dependencies:nullptr;
        const auto status=s.call.poll(dep?dep->factory:IgbDependenciesFactory{},check);if(!status.completed)return status;
        s.call.clear();check();s.phase=P::publish;break;}
    case P::publish:
        if(!s.source->has_value())return pending("Field naming requires its reloaded source pointer after release.");
        if(s.incoming && s.incoming!=**s.source)s.field->overwritten_constructor_owners_.push_back(s.incoming);
        if(s.old)s.field->overwritten_constructor_owners_.push_back(s.old);
        retain_name(s.field);s.field->name_string=**s.source;
        s.field->name=**s.source?(**s.source)->value:SystemStageString{};
        if(**s.source)s.field->name_string_address.reset();else s.field->name_string_address=0;
        s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldBufferInvocation::Impl {
    enum class P{size,after_size,buffer,address,size_pool,size_call,compare,free_capture,free_address,free_pool,free_call,
        allocate_capture,allocate_pool,allocate_call,publish,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;std::shared_ptr<IgbFieldOperationsServices> owner;
    std::shared_ptr<ModelSceneObject> pool;IgbRuntimeFieldDefaultSource buffer,incoming;
    std::optional<std::uint32_t> address;std::uint32_t bytes{},returned{};
    FieldCall call;P phase=P::size,after=P::done;bool running{},fault{};
};
IgbFieldBufferInvocation::IgbFieldBufferInvocation(std::shared_ptr<IgbDecodeRuntimeField> field):impl_(std::make_unique<Impl>()){impl_->field=std::move(field);}
IgbFieldBufferInvocation::~IgbFieldBufferInvocation(){auto& s=*impl_;if(!completed() && s.owner){
    if(s.incoming.buffer)s.owner->unfinished.push_back(s.incoming.buffer);
    if(s.call.request.kind==K::allocate_default && s.call.operation && s.call.operation->buffer_result.buffer)
        s.owner->unfinished.push_back(s.call.operation->buffer_result.buffer);
}}
bool IgbFieldBufferInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldBufferInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldBufferInvocation::resume(std::shared_ptr<IgbFieldOperationsServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field buffer requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field buffer continuation was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field buffer was invalidated by its enclosing callback.");};
    const auto clear=[&]{s.call.clear();check();};
    const auto select=[&](K kind,P after)->IgbFieldOperationCall&{clear();s.call.request.kind=kind;s.call.request.field=s.field;s.phase=P::call;s.after=after;return s.call.request;};
    const auto capture=[&]{if(!s.field->default_bytes)return false;s.buffer=*s.field->default_bytes;s.address=s.field->default_bytes_address;return true;};
    const auto address=[&]{if(!s.address && s.field->default_bytes && s.field->default_bytes->buffer==s.buffer.buffer && s.field->default_bytes->offset==s.buffer.offset)
            s.address=s.field->default_bytes_address;return s.address.has_value();};
    try{for(;;){check();switch(s.phase){
    case P::size:{const auto table=vtable(s.field);if(!table)return pending("Field buffer requires its live size virtual binding.");
        auto& call=select(K::field_virtual,P::after_size);call.vtable_word=*table;call.virtual_slot=0x64;break;}
    case P::after_size:clear();igb_field_store_size(s.field,static_cast<std::uint16_t>(s.returned));s.phase=P::buffer;break;
    case P::buffer:
        if(!capture())return pending("Field buffer requires live default pointer+20.");s.phase=s.buffer.buffer?P::address:P::allocate_capture;break;
    case P::address:
        if(!address())return pending("Field buffer requires the captured allocation's arithmetic identity.");
        {auto& call=select(K::allocation_pool,P::size_pool);call.buffer=s.buffer;call.address=*s.address;}break;
    case P::size_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::size_call;break;
    case P::size_call:
        if(!s.pool)fail("Field size query dereferences a null allocation pool.");
        {auto& call=select(K::allocation_size,P::compare);call.pool=s.pool;call.buffer=s.buffer;call.address=*s.address;}break;
    case P::compare:clear();s.phase=static_cast<std::int32_t>(s.field->size)<=std::bit_cast<std::int32_t>(s.returned)?P::done:P::free_capture;break;
    case P::free_capture:
        if(!capture())return pending("Field buffer requires the freshly reloaded pointer for free.");s.phase=s.buffer.buffer?P::free_address:P::allocate_capture;break;
    case P::free_address:
        if(!address())return pending("Field free requires the captured allocation's arithmetic identity.");
        select(K::object_pool,P::free_pool).handle=s.field->reference_word>>24u;break;
    case P::free_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::free_call;break;
    case P::free_call:
        if(!s.pool)fail("Field free dereferences a null object pool.");
        {auto& call=select(K::free_default,P::allocate_capture);call.pool=s.pool;call.buffer=s.buffer;call.address=*s.address;}break;
    case P::allocate_capture:
        clear();s.bytes=s.field->size;select(K::object_pool,P::allocate_pool).handle=s.field->reference_word>>24u;break;
    case P::allocate_pool:s.pool=std::move(s.call.operation->pool_result);clear();s.phase=P::allocate_call;break;
    case P::allocate_call:
        if(!s.pool)fail("Field allocation dereferences a null object pool.");
        {auto& call=select(K::allocate_default,P::publish);call.pool=s.pool;call.bytes=s.bytes;call.alignment=1;}break;
    case P::publish:{s.incoming=s.call.operation->buffer_result;const auto pointer=s.call.operation->buffer_address;clear();
        if((pointer==0)!=(!s.incoming.buffer))fail("Field default allocation requires a consistent pointer and storage binding.");
        igb_field_store_default(s.field,s.incoming,pointer);s.incoming={};s.phase=P::done;break;}
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbFieldOperationsFactory{},check);if(!status.completed)return status;
        s.returned=status.value;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldFinalizeInvocation::Impl {
    enum class P{type,after_type,size,after_size,defaults,global,physical_publish,call,done};
    std::shared_ptr<IgbDecodeRuntimeField> field;FieldCall call;P phase=P::type,after=P::done;
    std::shared_ptr<IgbFieldOperationsServices> owner;
    std::optional<IgbBootstrapPointer> physical;std::shared_ptr<IgbScratchBuffer> physical_bytes;
    std::uint32_t allocation_address{},global_address{},returned{};bool running{},fault{};
    void validate()const{
        require_field(field);
        if(!physical){
            if(field->physical_storage)fail("Field finalization acquired a physical binding after canonical capture.");
            return;
        }
        if(!physical->storage || !field->physical_storage || field->construction_fault_ || physical->storage->faulted() ||
            field->physical_storage->storage.lock()!=physical->storage || field->physical_storage->offset!=physical->offset ||
            physical->storage->original_address!=allocation_address || physical->storage->bytes!=physical_bytes ||
            !physical_bytes || physical_bytes->released)
            fail("Field finalization lost its captured physical allocation identity.");
        IgbObjectInstance object;object.storage=physical->storage;object.offset=physical->offset;object.field=field;
        igb_object_validate_instance(object);
    }
    std::optional<std::uint32_t> table(){
        validate();
        // Each original load is fresh; a read never refreshes the typed cache.
        const auto value=physical?std::optional<std::uint32_t>{igb_bootstrap_read_word(*physical,0)}:field->vtable_word;
        if(value && !*value)fail("Field finalization dereferences a null vtable.");return value;
    }
};
IgbFieldFinalizeInvocation::IgbFieldFinalizeInvocation(std::shared_ptr<IgbDecodeRuntimeField> field):impl_(std::make_unique<Impl>()){
    auto& s=*impl_;s.field=std::move(field);
    // Capture ownership without accessing the original receiver before63F28.
    if(s.field && s.field->physical_storage){const auto& binding=*s.field->physical_storage;
        s.physical=IgbBootstrapPointer{binding.storage.lock(),binding.offset};
        if(s.physical->storage){s.physical_bytes=s.physical->storage->bytes;s.allocation_address=s.physical->storage->original_address;}
    }
}
IgbFieldFinalizeInvocation::~IgbFieldFinalizeInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);
    if(s.physical && s.physical->storage)s.owner->unfinished.push_back(s.physical->storage);
    if(s.physical_bytes)s.owner->unfinished.push_back(s.physical_bytes);
}}
bool IgbFieldFinalizeInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbFieldFinalizeInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldFinalizeInvocation::resume(std::shared_ptr<IgbFieldOperationsServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;if(s.running || s.fault){s.fault=true;fail("Field finalization requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check_local=[&]{if(s.fault)fail("Field finalization continuation was invalidated.");};
    const auto check=[&]{check_local();if(enclosing_check)enclosing_check();check_local();};
    const auto clear=[&]{s.call.clear();check();};
    const auto virtual_call=[&](std::uint32_t slot,P after){const auto table=s.table();if(!table)return false;
        clear();s.call.request.kind=K::field_virtual;s.call.request.field=s.field;s.call.request.vtable_word=*table;s.call.request.virtual_slot=slot;
        s.phase=P::call;s.after=after;return true;};
    try{for(;;){check();switch(s.phase){
    case P::type:if(!virtual_call(0x5c,P::after_type))return pending("Field finalization requires its live type virtual.");break;
    case P::after_type:{const auto type=s.call.operation->field_result;clear();const auto ordinal=type?type->ordinal:std::uint16_t{0xffff};s.validate();
        if(s.field->physical_storage){const auto& binding=*s.field->physical_storage;auto storage=binding.storage.lock();
            if(!storage)fail("Field finalization reached expired physical storage.");const IgbBootstrapPointer pointer{storage,binding.offset};
            if(igb_bootstrap_field(pointer)!=s.field)fail("Field finalization reached a changed physical identity.");
            igb_bootstrap_write_halfword(pointer,0x10,ordinal);}
        s.field->type_ordinal=ordinal;s.phase=P::size;break;}
    case P::size:if(!virtual_call(0x64,P::after_size))return pending("Field finalization requires its freshly reloaded size virtual.");break;
    case P::after_size:clear();s.validate();igb_field_store_size(s.field,static_cast<std::uint16_t>(s.returned));s.phase=P::defaults;break;
    case P::defaults:if(!virtual_call(0x8c,P::global))return pending("Field finalization requires its freshly reloaded default virtual.");break;
    case P::global:
        clear();s.validate();
        if(s.physical){s.global_address=igb_bootstrap_read_word(*s.physical,0x1c);
            s.phase=s.global_address?P::physical_publish:P::done;break;}
        if(!s.field->global_slot)return pending("Field finalization requires its live descriptor-global pointer+1C.");
        if(const auto cell=s.field->global_slot->lock()){if(cell->has_value() && **cell && **cell!=s.field)s.field->overwritten_constructor_owners_.push_back(**cell);*cell=s.field;}
        else if(!igb_field_global_slot_is_null(*s.field->global_slot))fail("Field finalization dereferences an expired nonnull descriptor-global cell.");
        s.phase=P::done;break;
    case P::physical_publish:
        //63F84 loaded the destination already; late host bindings must not
        // reload the receiver or publish through a stale canonical cell.
        if(!s.field->global_slot_address || *s.field->global_slot_address!=s.global_address)
            return pending("Field finalization requires the captured physical global address mapping.");
        if(!s.field->global_slot || igb_field_global_slot_is_null(*s.field->global_slot))
            return pending("Field finalization requires the captured physical global cell owner.");
        if(const auto cell=s.field->global_slot->lock()){
            if(cell->has_value() && **cell && **cell!=s.field)s.field->overwritten_constructor_owners_.push_back(**cell);*cell=s.field;
        }else fail("Field finalization dereferences an expired nonnull descriptor-global cell.");
        s.phase=P::done;break;
    case P::call:{const auto status=s.call.poll(services?services->factory:IgbFieldOperationsFactory{},check);if(!status.completed)return status;
        s.returned=status.value;s.phase=s.after;break;}
    case P::done:clear();return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbFieldAppendInvocation::Impl {
    std::shared_ptr<IgbRuntimeMetaType> metadata;std::shared_ptr<IgbDecodeRuntimeField> field;std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbFieldReferenceAccess> reference;
    std::shared_ptr<IgbFieldOperationsServices> owner;std::unique_ptr<IgbFieldFinalizeInvocation> finalize;
    std::shared_ptr<IgbMetadataOperation> append;std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    unsigned phase{};bool running{},fault{};
};
IgbFieldAppendInvocation::IgbFieldAppendInvocation(std::shared_ptr<IgbRuntimeMetaType> metadata,std::shared_ptr<IgbDecodeRuntimeField> field):impl_(std::make_unique<Impl>()){
    impl_->metadata=std::move(metadata);impl_->field=std::move(field);impl_->finalize=std::make_unique<IgbFieldFinalizeInvocation>(impl_->field);
    impl_->reference=std::make_shared<IgbFieldReferenceAccess>(impl_->field);
}
IgbFieldAppendInvocation::~IgbFieldAppendInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.field)s.owner->unfinished.push_back(s.field);if(s.reference)s.owner->unfinished.push_back(s.reference);
}}
bool IgbFieldAppendInvocation::completed()const noexcept{return impl_->phase==3;}
bool IgbFieldAppendInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbFieldAppendInvocation::resume(std::shared_ptr<IgbFieldOperationsServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;if(s.running || s.fault){s.fault=true;fail("Field append requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running guard(s.running);if(!s.owner && services)s.owner=services;
    const auto check=[&]{if(s.fault)fail("Field append continuation was invalidated.");
        if(enclosing_check)enclosing_check();
        if(s.fault)fail("Field append was invalidated by its enclosing callback.");};
    try{for(;;){check();switch(s.phase){
    case 0:{const auto status=s.finalize->resume(services,check);check();if(!status.completed)return status;s.finalize.reset();check();s.phase=1;break;}
    case 1:
        if(!s.metadata)fail("Finalized field append dereferences null metadata.");s.fields=s.metadata->fields;
        if(s.field)s.reference->write(s.reference->read()+1u);s.phase=2;break;
    case 2:
        if(!s.append){if(!services || !services->pointer_lists)return pending("Finalized field append requires native pointer-list services.");
            IgbMetadataOperationCall request;request.kind=IgbMetadataOperationKind::append_field;request.fields=s.fields;request.field=s.field;
            s.append=make_native_igb_pointer_list_metadata_factory(services->pointer_lists)(request);check();}
        if(!s.callback && !s.checked_callback){s.checked_callback=s.append->resume_checked;if(!s.checked_callback)s.callback=s.append->resume;}
        if(!s.callback && !s.checked_callback)return pending("Finalized field append requires its retained raw append continuation.");
        {const auto status=s.checked_callback?s.checked_callback(check):s.callback();check();if(!status.completed)return status;}
        s.callback={};s.checked_callback={};s.append.reset();check();s.phase=3;break;
    default:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

IgbFieldOperationsFactory make_native_igb_unsigned_int_field_factory(std::shared_ptr<IgbFieldOperationsServices> services,IgbFieldOperationsFactory remaining){
    std::weak_ptr<IgbFieldOperationsServices> weak=services;
    return [weak,remaining=std::move(remaining)](const IgbFieldOperationCall& call){
        if(call.kind!=K::field_virtual || call.vtable_word!=0x80476e6c ||
           (call.virtual_slot!=0x5c && call.virtual_slot!=0x64 && !(call.virtual_slot==0x8c && !call.source && call.argument==0)))
            return remaining?remaining(call):std::shared_ptr<IgbFieldOperation>{};
        const auto selected=weak.lock();if(!selected)fail("Unsigned-int field services expired before selection.");auto operation=std::make_shared<IgbFieldOperation>();
        if(call.virtual_slot==0x64)operation->resume=[]{return MessengerCallResult{true,4,{}};};
        else if(call.virtual_slot==0x5c)operation->resume=[selected,out=operation.get()]{
            if(!selected->unsigned_int_prototype)return pending("Unsigned-int field getter requires canonical805614C0.");out->field_result=*selected->unsigned_int_prototype;return MessengerCallResult{true,0,{}};};
        else{auto invocation=std::make_shared<IgbFieldBufferInvocation>(call.field);operation->resume=[invocation,selected]{return invocation->resume(selected);};
            operation->resume_checked=[invocation,selected](const std::function<void()>& check){return invocation->resume(selected,check);};}
        return operation;
    };
}
} // namespace dw4
