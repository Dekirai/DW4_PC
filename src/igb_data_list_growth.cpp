#include "dw4/igb_data_list_growth.hpp"

#include <algorithm>
#include <bit>

namespace dw4 {
namespace {
[[noreturn]]void fail(const char* message){throw IgbDataListGrowthError(message);}
MessengerCallResult pending(const char* message){return {false,0,message};}
struct Running{bool& flag;explicit Running(bool& value):flag(value){flag=true;}~Running(){flag=false;}};
struct ObjectAccess {
    IgbObjectInstance object;std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t address{};
    explicit ObjectAccess(IgbObjectInstance value):object(std::move(value)),bytes(object.storage?object.storage->bytes:nullptr),
        address(igb_bootstrap_address({object.storage,object.offset})){}
    IgbBootstrapPointer pointer()const{
        if(!object.storage || !bytes || bytes->released || object.storage->bytes!=bytes || object.storage->faulted() ||
            !address || igb_bootstrap_address({object.storage,object.offset})!=address)
            fail("Data-list growth lost its captured physical object allocation.");
        igb_object_validate_instance(object);return {object.storage,object.offset};
    }
};
struct FieldAccess {
    std::shared_ptr<IgbDecodeRuntimeField> field;std::optional<IgbBootstrapPointer> captured;
    std::shared_ptr<IgbScratchBuffer> bytes;std::uint32_t address{};
    explicit FieldAccess(std::shared_ptr<IgbDecodeRuntimeField> value):field(std::move(value)){
        if(field && field->physical_storage){captured=IgbBootstrapPointer{field->physical_storage->storage.lock(),field->physical_storage->offset};
            if(captured->storage){bytes=captured->storage->bytes;address=igb_bootstrap_address(*captured);}}
    }
    std::optional<IgbBootstrapPointer> pointer(){
        if(!field || field->construction_fault_)fail("Data-list growth dereferences a null or faulted descriptor.");
        if(!field->physical_storage){if(captured)fail("Data-list growth lost its captured descriptor binding.");return {};}
        IgbBootstrapPointer current{field->physical_storage->storage.lock(),field->physical_storage->offset};
        if(!current.storage || !current.storage->bytes || current.storage->bytes->released || current.storage->faulted())
            fail("Data-list growth requires live physical descriptor bytes.");
        if(captured && (current.storage!=captured->storage || current.offset!=captured->offset || current.storage->bytes!=bytes ||
            igb_bootstrap_address(current)!=address))fail("Data-list growth changed its captured descriptor allocation.");
        IgbObjectInstance instance;instance.storage=current.storage;instance.offset=current.offset;instance.field=field;
        igb_object_validate_instance(instance);
        if(!captured){captured=current;bytes=current.storage->bytes;address=igb_bootstrap_address(current);}return current;
    }
    std::int32_t ordinal(){
        auto value=field?field->ordinal:std::uint16_t{};
        if(const auto p=pointer())value=static_cast<std::uint16_t>((std::uint16_t(igb_bootstrap_read_byte(*p,0x12))<<8u)|igb_bootstrap_read_byte(*p,0x13));
        return std::bit_cast<std::int16_t>(value);
    }
    std::optional<std::uint32_t> table(){if(const auto p=pointer())return igb_bootstrap_read_word(*p,0);return field->vtable_word;}
};
template<class Request,class Factory,class Operation>struct Call {
    Request request;Factory factory;std::shared_ptr<Operation> operation;
    std::function<MessengerCallResult()> callback;
    std::function<MessengerCallResult(const std::function<void()>&)> checked_callback;
    void clear(){callback={};checked_callback={};operation.reset();factory={};request={};}
    template<class Check>MessengerCallResult poll(const Factory& fallback,const Check& check){
        check();if(!factory)factory=fallback;
        if(!factory)return pending("Data-list growth requires its selected original lower service.");
        if(!operation){operation=factory(request);check();}
        if(!operation)return pending("Data-list growth reached an unbound original lower operation.");
        if(!callback && !checked_callback){checked_callback=operation->resume_checked;if(!checked_callback)callback=operation->resume;}
        if(!callback && !checked_callback)return pending("Data-list growth requires its retained lower continuation.");
        const auto result=checked_callback?checked_callback(check):callback();check();return result;
    }
};
using ObjectCall=Call<IgbObjectInitCall,IgbObjectInitFactory,IgbObjectInitOperation>;
using MemoryCall=Call<IgbMemoryFieldConstructCall,IgbMemoryFieldConstructFactory,IgbMemoryFieldConstructOperation>;
using FreeCall=Call<IgbDataListGrowthCall,IgbDataListGrowthFactory,IgbDataListGrowthOperation>;
std::optional<std::shared_ptr<IgbDecodeRuntimeField>> global_data(const std::shared_ptr<IgbDataListGrowthServices>& services){
    const auto defaults=services?services->data_defaults:nullptr;
    if(!defaults || !defaults->globals || defaults->globals->size()<=2 || !(*defaults->globals)[2])return {};
    const auto& binding=*(*defaults->globals)[2];
    if(binding.address!=0x80561d48u)fail("Data-list growth requires the canonical80561D48 descriptor alias.");
    if(!binding.slot || !*binding.slot)return {};return **binding.slot;
}
std::optional<std::uint32_t> field_address(const std::shared_ptr<IgbDecodeRuntimeField>& field){
    if(!field)return 0;if(!field->physical_storage)return {};
    const auto& binding=*field->physical_storage;const auto storage=binding.storage.lock();
    if(!storage)fail("Data-list element has expired physical storage.");return igb_bootstrap_address({storage,binding.offset});
}
std::optional<std::shared_ptr<IgbDecodeRuntimeField>> resolve_field(std::uint32_t address,
    const std::shared_ptr<IgbDecodeRuntimeField>& candidate,const std::shared_ptr<IgbDataListGrowthServices>& services){
    if(!address)return std::shared_ptr<IgbDecodeRuntimeField>{};
    std::optional<std::shared_ptr<IgbDecodeRuntimeField>> result;
    if(candidate){const auto value=field_address(candidate);if(value && *value==address)result=candidate;}
    const auto visit=[&](const std::shared_ptr<IgbPointerListServices>& pointers){if(pointers)for(const auto& value:pointers->values){
        if(value.address!=address)continue;if(!value.field || value.metadata || (result && *result!=value.field))
            fail("Data-list element pointer has conflicting descriptor mappings.");result=value.field;
    }};
    if(services && services->memory && services->memory->values && services->memory->values->fields)
        visit(services->memory->values->fields->pointer_lists);
    const auto lists=services && services->data_defaults?services->data_defaults->lists:nullptr;
    if(lists && lists->schema && lists->schema->fields)visit(lists->schema->fields->pointer_lists);
    return result;
}
}

struct IgbDataListCapacityInvocation::Impl {
    enum class P{begin,zero_backing,table,global,select_metadata,old_backing,ordinal,metadata,field,element,
        allocate_tag,allocate_pool,size_element,size_table,size,divide,allocate,old_test,copy_count,copy_destination,copy,
        free_test,free_tag,free_pool,select_free,free,clear_backing,capacity,done};P phase=P::begin;
    ObjectAccess target;std::shared_ptr<IgbDataListGrowthServices> owner;
    std::unique_ptr<FieldAccess> global_field,data_field,element_field;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> element_candidate;
    std::optional<std::uint32_t> element_address;
    std::shared_ptr<ModelSceneObject> pool;
    ObjectCall object_call;MemoryCall memory_call;FreeCall free_call;
    std::unique_ptr<IgbMemoryFieldAllocateInvocation> allocation;
    std::unique_ptr<IgbAllocationCopyInvocation> copying;
    std::uint32_t capacity{},element_bytes{},table{},old_backing{},element_size{},count{},copy_bytes{},destination{};
    std::int32_t ordinal{};bool zero{},running{},fault{};
    Impl(IgbObjectInstance object,std::uint32_t cap,std::uint32_t bytes):target(std::move(object)),capacity(cap),element_bytes(bytes){}
};
IgbDataListCapacityInvocation::IgbDataListCapacityInvocation(IgbObjectInstance object,std::uint32_t capacity,std::uint32_t bytes)
    :impl_(std::make_unique<Impl>(std::move(object),capacity,bytes)){}
IgbDataListCapacityInvocation::~IgbDataListCapacityInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.target.object.storage)s.owner->retained.push_back(s.target.object.storage);if(s.target.bytes)s.owner->retained.push_back(s.target.bytes);
    if(s.pool)s.owner->retained.push_back(s.pool);if(s.metadata)s.owner->retained.push_back(s.metadata);
    const auto keep=[&](const std::unique_ptr<FieldAccess>& f){if(f){if(f->field)s.owner->retained.push_back(f->field);if(f->bytes)s.owner->retained.push_back(f->bytes);}};
    keep(s.global_field);keep(s.data_field);keep(s.element_field);if(s.element_candidate)s.owner->retained.push_back(s.element_candidate);
}}
bool IgbDataListCapacityInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListCapacityInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbDataListCapacityInvocation::resume(std::shared_ptr<IgbDataListGrowthServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;using K=IgbMemoryFieldConstructServiceKind;
    if(s.running || s.fault){s.fault=true;fail("Data-list capacity requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Data-list capacity was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Data-list capacity was invalidated by its enclosing callback.");s.target.pointer();};
    const auto callback_check=[&]{check();if(s.data_field)s.data_field->pointer();if(s.element_field)s.element_field->pointer();};
    const auto memory=services?services->memory:nullptr;
    const auto tag=[&]{s.memory_call.clear();callback_check();s.memory_call.request.kind=K::tagged_pool;
        s.memory_call.request.object=s.target.object;s.memory_call.request.handle=igb_bootstrap_read_byte(s.target.pointer(),4);
        if(memory)s.memory_call.factory=memory->factory;};
    try{for(;;){check();switch(s.phase){
    case P::begin:s.zero=s.capacity==0;s.phase=s.zero?P::zero_backing:P::table;break;
    case P::zero_backing:s.old_backing=igb_bootstrap_read_word(s.target.pointer(),0x10);s.phase=P::free_test;break;
    case P::table:s.table=igb_bootstrap_read_word(s.target.pointer(),0);s.phase=P::global;break;
    case P::global:{const auto field=global_data(services);if(!field)return pending("Data-list capacity requires canonical descriptor80561D48.");
        s.global_field=std::make_unique<FieldAccess>(*field);s.phase=P::select_metadata;break;}
    case P::select_metadata:
        if(!s.table)fail("Data-list capacity dereferences a null metadata vtable.");
        s.object_call.request.kind=IgbObjectInitServiceKind::object_virtual;s.object_call.request.object=s.target.object;
        s.object_call.request.vtable_word=s.table;s.object_call.request.virtual_slot=0x58;
        if(services && services->objects)s.object_call.factory=services->objects->factory;s.phase=P::old_backing;break;
    case P::old_backing:s.old_backing=igb_bootstrap_read_word(s.target.pointer(),0x10);s.phase=P::ordinal;break;
    case P::ordinal:s.ordinal=s.global_field->ordinal();s.global_field.reset();s.phase=P::metadata;break;
    case P::metadata:{const auto result=s.object_call.poll(services && services->objects?services->objects->factory:IgbObjectInitFactory{},callback_check);
        if(!result.completed)return result;s.metadata=s.object_call.operation->metadata_result;s.object_call.clear();callback_check();s.phase=P::field;break;}
    case P::field:s.data_field=std::make_unique<FieldAccess>(igb_runtime_field_at(s.metadata,s.ordinal));s.phase=P::element;break;
    case P::element:{
        const auto physical=s.data_field->pointer();const auto& field=s.data_field->field;
        if(physical)s.element_address=igb_bootstrap_read_word(*physical,0x3c);
        else if(!field->element_field)return pending("Data-list capacity requires its captured element pointer+3C.");
        if(field->element_field)s.element_candidate=field->element_field->lock();
        s.phase=P::allocate_tag;break;}
    case P::allocate_tag:tag();s.phase=P::allocate_pool;break;
    case P::allocate_pool:{const auto result=s.memory_call.poll(memory?memory->factory:IgbMemoryFieldConstructFactory{},callback_check);
        if(!result.completed)return result;s.pool=s.memory_call.operation->pool_result;s.memory_call.clear();callback_check();s.phase=P::size_element;break;}
    case P::size_element:
        if(s.element_address){const auto field=resolve_field(*s.element_address,s.element_candidate,services);
            if(!field)return pending("Data-list capacity requires its captured element descriptor mapping.");
            s.element_field=std::make_unique<FieldAccess>(*field);
        }else s.element_field=std::make_unique<FieldAccess>(s.element_candidate);
        s.phase=P::size_table;break;
    case P::size_table:{const auto table=s.element_field->table();if(!table)return pending("Data-list capacity requires its captured element size virtual.");
        if(!*table)fail("Data-list capacity dereferences a null element size vtable.");
        s.memory_call.request.kind=K::field_virtual;s.memory_call.request.field=s.element_field->field;
        s.memory_call.request.vtable_word=*table;s.memory_call.request.virtual_slot=0x64;
        if(memory)s.memory_call.factory=memory->factory;s.phase=P::size;break;}
    case P::size:{const auto result=s.memory_call.poll(memory?memory->factory:IgbMemoryFieldConstructFactory{},callback_check);
        if(!result.completed)return result;s.element_size=static_cast<std::uint16_t>(result.value);
        s.memory_call.clear();callback_check();s.element_field.reset();s.element_candidate.reset();s.phase=P::divide;break;}
    case P::divide:
        if(!s.element_size)fail("Data-list capacity reaches undefined PPC unsigned division by zero.");
        s.count=(s.capacity*s.element_bytes)/s.element_size;
        s.allocation=std::make_unique<IgbMemoryFieldAllocateInvocation>(s.data_field->field,s.target.object,s.count,s.pool);
        s.phase=P::allocate;break;
    case P::allocate:{const auto result=s.allocation->resume(memory,callback_check);if(!result.completed)return result;
        callback_check();s.allocation.reset();s.data_field.reset();s.phase=P::old_test;break;}
    case P::old_test:s.phase=s.old_backing?P::copy_count:P::capacity;break;
    case P::copy_count:{const auto current=igb_bootstrap_read_word(s.target.pointer(),0x0c);
        const auto smaller=std::bit_cast<std::int32_t>(current)<std::bit_cast<std::int32_t>(s.capacity)?current:s.capacity;
        s.copy_bytes=smaller*s.element_bytes;s.phase=P::copy_destination;break;}
    case P::copy_destination:s.destination=igb_bootstrap_read_word(s.target.pointer(),0x10);
        s.copying=std::make_unique<IgbAllocationCopyInvocation>(s.destination,s.old_backing,s.copy_bytes);s.phase=P::copy;break;
    case P::copy:{const auto result=s.copying->resume(memory,callback_check);if(!result.completed)return result;
        callback_check();s.copying.reset();s.phase=P::free_test;break;}
    case P::free_test:s.phase=s.old_backing?P::free_tag:(s.zero?P::clear_backing:P::capacity);break;
    case P::free_tag:tag();s.phase=P::free_pool;break;
    case P::free_pool:{const auto result=s.memory_call.poll(memory?memory->factory:IgbMemoryFieldConstructFactory{},callback_check);
        if(!result.completed)return result;s.pool=s.memory_call.operation->pool_result;s.memory_call.clear();callback_check();s.phase=P::select_free;break;}
    case P::select_free:
        if(!s.pool)fail("Data-list capacity dereferences a null tagged pool at+108.");
        s.free_call.request.object=s.target.object;s.free_call.request.pool=s.pool;s.free_call.request.address=s.old_backing;
        s.free_call.request.virtual_slot=0x108;if(services)s.free_call.factory=services->factory;s.phase=P::free;break;
    case P::free:{const auto result=s.free_call.poll(services?services->factory:IgbDataListGrowthFactory{},callback_check);
        if(!result.completed)return result;s.free_call.clear();callback_check();s.phase=s.zero?P::clear_backing:P::capacity;break;}
    case P::clear_backing:igb_bootstrap_write_word(s.target.pointer(),0x10,0);s.phase=P::capacity;break;
    case P::capacity:igb_bootstrap_write_word(s.target.pointer(),0x0c,s.capacity);s.phase=P::done;break;
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}

struct IgbDataListGrowthInvocation::Impl {
    enum class P{capacity,grow,resize,count,blocked,done};P phase=P::capacity;
    ObjectAccess target;std::shared_ptr<IgbDataListGrowthServices> owner;
    std::unique_ptr<IgbDataListCapacityInvocation> resize;
    std::vector<std::uint32_t> visited;
    std::uint32_t count{},element_bytes{},capacity{};bool running{},fault{};
    Impl(IgbObjectInstance object,std::uint32_t n,std::uint32_t bytes):target(std::move(object)),count(n),element_bytes(bytes){}
};
IgbDataListGrowthInvocation::IgbDataListGrowthInvocation(IgbObjectInstance object,std::uint32_t count,std::uint32_t bytes)
    :impl_(std::make_unique<Impl>(std::move(object),count,bytes)){}
IgbDataListGrowthInvocation::~IgbDataListGrowthInvocation(){auto& s=*impl_;if((!completed() || faulted()) && s.owner){
    if(s.target.object.storage)s.owner->retained.push_back(s.target.object.storage);if(s.target.bytes)s.owner->retained.push_back(s.target.bytes);
}}
bool IgbDataListGrowthInvocation::completed()const noexcept{return impl_->phase==Impl::P::done;}
bool IgbDataListGrowthInvocation::faulted()const noexcept{return impl_->fault;}
MessengerCallResult IgbDataListGrowthInvocation::resume(std::shared_ptr<IgbDataListGrowthServices> services,
    const std::function<void()>& enclosing_check){
    auto& s=*impl_;using P=Impl::P;
    if(s.running || s.fault){s.fault=true;fail("Data-list growth requires an unfaulted nonrecursive invocation.");}
    if(completed())return {true,0,{}};Running running(s.running);if(!s.owner && services)s.owner=services;if(!services)services=s.owner;
    const auto check=[&]{if(s.fault)fail("Data-list growth was invalidated.");if(enclosing_check)enclosing_check();
        if(s.fault)fail("Data-list growth was invalidated by its enclosing callback.");s.target.pointer();};
    try{for(;;){check();switch(s.phase){
    case P::capacity:{const auto current=igb_bootstrap_read_word(s.target.pointer(),0x0c);
        s.capacity=std::bit_cast<std::int32_t>(current)>4?current:4u;s.phase=P::grow;break;}
    case P::grow:
        if(std::bit_cast<std::int32_t>(s.capacity)>=std::bit_cast<std::int32_t>(s.count)){
            s.resize=std::make_unique<IgbDataListCapacityInvocation>(s.target.object,s.capacity,s.element_bytes);s.phase=P::resize;break;}
        if(std::find(s.visited.begin(),s.visited.end(),s.capacity)!=s.visited.end()){s.phase=P::blocked;break;}
        s.visited.push_back(s.capacity);
        if(std::bit_cast<std::int32_t>(s.capacity)<1024)s.capacity<<=1u;
        else {
            // Collapse only the original repeated +1024 arithmetic. Stop at
            // its first signed wrap, then resume the exact doubling branch.
            const auto needed=(std::uint64_t(s.count)-s.capacity+1023u)/1024u;
            const auto until_wrap=(std::uint64_t(0x7fffffffu)-s.capacity)/1024u+1u;
            s.capacity+=static_cast<std::uint32_t>(std::min(needed,until_wrap)*1024u);
        }break;
    case P::resize:{const auto result=s.resize->resume(services,check);if(!result.completed)return result;
        check();s.resize.reset();s.phase=P::count;break;}
    case P::count:igb_bootstrap_write_word(s.target.pointer(),8,s.count);s.phase=P::done;break;
    case P::blocked:return pending("Original data-list growth repeats its wrapped capacity state without reaching allocation.");
    case P::done:return {true,0,{}};
    }}}catch(...){s.fault=true;throw;}
}
IgbThreadManagerInsertFactory make_native_igb_data_list_growth_factory(std::shared_ptr<IgbDataListGrowthServices> services,
    IgbThreadManagerInsertFactory remaining){
    return [weak=std::weak_ptr(services),remaining=std::move(remaining)](const IgbThreadManagerInsertCall& call)->std::shared_ptr<IgbThreadManagerInsertOperation>{
        if(call.kind!=IgbThreadManagerInsertServiceKind::grow_list)return remaining?remaining(call):nullptr;
        const auto selected=weak.lock();auto invocation=std::make_shared<IgbDataListGrowthInvocation>(call.object,call.count,call.element_bytes);
        auto operation=std::make_shared<IgbThreadManagerInsertOperation>();operation->resume=[selected,invocation]{return invocation->resume(selected);};
        operation->resume_checked=[selected,invocation](const std::function<void()>& check){return invocation->resume(selected,check);};return operation;
    };
}
} // namespace dw4
