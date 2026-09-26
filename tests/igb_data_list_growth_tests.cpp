#include "dw4/igb_data_list_growth.hpp"

#include <algorithm>
#include <array>
#include <iostream>

// Explicit metadata, tagged-pool, allocator and release endpoints surround
// actual4155C/41660/62BB8/031E8. No host vector represents the runtime list.
namespace {
using namespace dw4;
using MK=IgbMemoryFieldConstructServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F action,const char* message){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto bytes(std::size_t size,std::uint8_t value=0xcd,std::uint8_t known=0){auto result=std::make_shared<IgbScratchBuffer>();
    result->bytes.assign(size,value);result->initialized.assign(size,known);return result;}
IgbBootstrapPointer pointer(const IgbObjectInstance& object){return {object.storage,object.offset};}
IgbObjectInstance list_object(){IgbObjectInstance value;value.offset=0x10;value.storage=std::make_shared<IgbObjectStorage>();
    value.storage->original_address=0x100000;value.storage->bytes=bytes(0x24);value.runtime_object=std::make_shared<IgbRuntimeObject>(IgbRuntimeObjectKind::thread_list);
    value.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{value.storage,value.offset};IgbObjectBinding binding;
    binding.offset=value.offset;binding.runtime_object=value.runtime_object;value.storage->bindings.push_back(binding);
    igb_bootstrap_write_word(pointer(value),0,0x80476ce0);igb_bootstrap_write_byte(pointer(value),4,0x35);
    igb_bootstrap_write_word(pointer(value),8,0);igb_bootstrap_write_word(pointer(value),0x0c,0);igb_bootstrap_write_word(pointer(value),0x10,0);return value;}
void word(const IgbObjectInstance& object,unsigned offset,std::uint32_t value){igb_bootstrap_write_word(pointer(object),offset,value);}
std::uint32_t word(const IgbObjectInstance& object,unsigned offset){return igb_bootstrap_read_word(pointer(object),offset);}
IgbPointerListBacking allocation(std::uint32_t address,unsigned size,std::uint8_t fill=0xcd,std::uint8_t known=0,unsigned offset=0x10){
    IgbPointerListBacking value;value.address=address;value.offset=offset;value.bytes=bytes(size+offset,fill,known);return value;}
struct Fixture {
    std::shared_ptr<IgbDataListGrowthServices> services=std::make_shared<IgbDataListGrowthServices>();
    IgbObjectInstance list=list_object(),global;
    std::shared_ptr<IgbDecodeRuntimeField> descriptor=std::make_shared<IgbDecodeRuntimeField>(),element=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>(),reference=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbPointerListBacking output=allocation(0x400000,16384,0xd7);
    std::vector<IgbObjectInitCall> object_calls;std::vector<IgbMemoryFieldConstructCall> memory_calls;std::vector<IgbDataListGrowthCall> free_calls;
    std::vector<std::shared_ptr<IgbObjectInitOperation>> object_operations;
    std::vector<std::shared_ptr<IgbMemoryFieldConstructOperation>> memory_operations;
    std::vector<std::shared_ptr<IgbDataListGrowthOperation>> free_operations;
    std::function<std::optional<MessengerCallResult>(const IgbObjectInitCall&)> object_before;
    std::function<void(const IgbMemoryFieldConstructCall&,IgbMemoryFieldConstructOperation&,MessengerCallResult&)> memory_action;
    std::function<MessengerCallResult(const IgbDataListGrowthCall&,const std::function<void()>&)> free_action;
    unsigned object_polls{},memory_polls{},free_polls{},sizes{},alignments{},allocations{},tags{};
    Fixture(){
        services->data_defaults=std::make_shared<IgbDataListDefaultsServices>();services->objects=std::make_shared<IgbObjectInitServices>();
        services->memory=std::make_shared<IgbMemoryFieldConstructServices>();services->memory->values=std::make_shared<IgbFieldValuesServices>();
        services->memory->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(reference);services->memory->status_success=0x11223344;
        services->memory->status_failure=0xaabbccdd;reference->parent=std::shared_ptr<IgbRuntimeMetaType>{};
        global.offset=0x10;global.storage=std::make_shared<IgbObjectStorage>();global.storage->original_address=0x200000;global.storage->bytes=bytes(0x60);
        global.field=std::make_shared<IgbDecodeRuntimeField>();global.field->physical_storage=IgbFieldStorageBinding{global.storage,global.offset};
        IgbObjectBinding binding;binding.offset=global.offset;binding.field=global.field;global.storage->bindings.push_back(binding);
        igb_bootstrap_write_word(pointer(global),0,0x80476088);igb_bootstrap_write_halfword(pointer(global),0x12,4);
        global.field->ordinal=2; // Deliberately stale; adjacent physical halfwords remain unknown.
        *services->data_defaults->globals->at(2)->slot=global.field;
        descriptor->offset=0x10;descriptor->element_field=element;element->vtable_word=0x804762fc;
        metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();metadata->fields->count=0;
        metadata->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(5);metadata->fields->backing->at(4)=descriptor;
        services->objects->factory=[this](const IgbObjectInitCall& call){object_calls.push_back(call);auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[this,call,out=operation.get()]{++object_polls;if(object_before){const auto status=object_before(call);if(status)return *status;}
                check(call.kind==IgbObjectInitServiceKind::object_virtual && call.virtual_slot==0x58,"Growth selected an unrelated metadata operation.");
                out->metadata_result=metadata;return MessengerCallResult{true,0,{}};};object_operations.push_back(operation);return operation;};
        services->memory->factory=[this](const IgbMemoryFieldConstructCall& call){memory_calls.push_back(call);auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
            operation->resume=[this,call,out=operation.get()]{++memory_polls;MessengerCallResult status{true,0,{}};
                if(call.kind==MK::tagged_pool){++tags;out->pool_result=pool;}
                else if(call.kind==MK::field_virtual){if(call.virtual_slot==0x64){++sizes;status.value=4;}
                    else if(call.virtual_slot==0x6c){++alignments;status.value=4;}else if(call.virtual_slot==0x58)out->metadata_result=reference;
                    else throw std::runtime_error("Unexpected prepared element virtual.");}
                else if(call.kind==MK::allocate){++allocations;out->backing_result=output;}
                else throw std::runtime_error("Unexpected prepared memory service.");
                if(memory_action)memory_action(call,*out,status);
                if(status.completed && call.kind==MK::allocate && out->backing_result.address && out->backing_result.bytes)
                    igb_bind_memory_field_allocation(services->memory,out->backing_result);
                return status;};memory_operations.push_back(operation);return operation;};
        services->factory=[this](const IgbDataListGrowthCall& call){free_calls.push_back(call);auto operation=std::make_shared<IgbDataListGrowthOperation>();
            operation->resume_checked=[this,call](const std::function<void()>& guard){++free_polls;
                return free_action?free_action(call,guard):MessengerCallResult{true,0,{}};};
            operation->resume=[out=operation.get()]{return out->resume_checked({});};free_operations.push_back(operation);return operation;};
    }
    void old(IgbPointerListBacking value,std::uint32_t capacity){igb_bind_memory_field_allocation(services->memory,value);word(list,0x10,value.address);word(list,0x0c,capacity);}
};

void fixed_original_growth_arithmetic(){
    struct Case{std::uint32_t old_capacity,count,capacity;};
    for(const auto c:std::array<Case,9>{{{0,0,4},{2,1,4},{5,6,10},{513,1025,1026},{1023,1025,2046},
        {1024,1025,2048},{1500,1600,2524},{0xffffffff,5,8},{16,0xffffffff,16}}}){
        Fixture f;word(f.list,0x0c,c.old_capacity);word(f.list,8,77);IgbDataListGrowthInvocation run(f.list,c.count,4);
        check(run.resume(f.services).completed && run.completed() && !run.faulted() && word(f.list,0x0c)==c.capacity && word(f.list,8)==c.count &&
            word(f.list,0x10)==f.output.address && f.sizes==2 && f.alignments==1 && f.allocations==1 && f.tags==1 && f.free_calls.empty(),
            "Growth changed signed thresholds/non-power-of-two arithmetic or missed actual nested allocation.");
        const auto request=std::find_if(f.memory_calls.begin(),f.memory_calls.end(),[](const auto& call){return call.kind==MK::allocate;});
        check(request!=f.memory_calls.end() && request->bytes==c.capacity*4 && request->alignment==4 && request->virtual_slot==0xdc,
            "Computed original capacity did not reach actual62BB8 byte allocation.");
        bool matches=true;for(unsigned at=0;at<f.output.bytes->bytes.size();++at){const bool zero=at>=f.output.offset && at<f.output.offset+c.capacity*4;
            matches=matches && f.output.bytes->bytes[at]==(zero?0:0xd7) && f.output.bytes->initialized[at]==unsigned(zero);}
        check(matches,"Growth zero fill changed padding or an unallocated tail.");
        word(f.list,8,99);check(run.resume().completed && word(f.list,8)==99 && f.allocations==1,"Completed growth replayed allocation/count publication.");
    }
    Fixture cycle;IgbDataListGrowthInvocation impossible(cycle.list,0x7fffffff,4);
    check(!impossible.resume(cycle.services).completed && !impossible.faulted() && !impossible.resume().completed &&
        cycle.object_calls.empty() && cycle.memory_calls.empty() && word(cycle.list,8)==0 && word(cycle.list,0x0c)==0,
        "Unsupported repeating original growth arithmetic entered allocation or fabricated count/capacity completion.");
}

void physical_ordinal_and_capture_before_metadata(){
    Fixture f;bool ready{};const auto captured=f.descriptor;auto old=allocation(0x300000,16,0x61,1);f.old(old,4);
    f.object_before=[&](const IgbObjectInitCall&)->std::optional<MessengerCallResult>{if(!ready)return MessengerCallResult{false,0,"Prepared metadata suspension."};return {};};
    IgbDataListCapacityInvocation run(f.list,4,4);check(!run.resume(f.services).completed && f.object_calls.size()==1 && f.memory_calls.empty(),
        "Growth failed before its captured physical ordinal or called size before metadata returned.");
    igb_bootstrap_write_halfword(pointer(f.global),0x12,2);*f.services->data_defaults->globals->at(2)->slot=std::make_shared<IgbDecodeRuntimeField>();
    word(f.list,0,0x12345678);word(f.list,0x10,0);ready=true;
    check(run.resume().completed && f.object_calls.size()==1 && f.object_calls[0].vtable_word==0x80476ce0 &&
        f.free_calls.size()==1 && f.free_calls[0].address==old.address && word(f.list,0x0c)==4,
        "Metadata suspension recaptured vtable/global ordinal/old backing or skipped the captured old-buffer release.");
    check(f.memory_calls[1].field==f.element && f.descriptor==captured && !f.global.storage->bytes->initialized[f.global.offset+0x10] &&
        !f.global.storage->bytes->initialized[f.global.offset+0x14],"Ordinal read widened into unknown neighboring halfwords.");
    Fixture negative;igb_bootstrap_write_halfword(pointer(negative.global),0x12,0xffff);IgbDataListCapacityInvocation invalid(negative.list,4,4);
    rejects([&]{invalid.resume(negative.services);},"Negative signed ordinal invented a represented registry-prefix entry.");
    check(invalid.faulted() && negative.object_polls==1 && negative.memory_calls.empty(),"Negative ordinal failed in the wrong original call order.");
    Fixture unknown;unknown.global.storage->bytes->initialized[unknown.global.offset+0x12]=0;IgbDataListCapacityInvocation missing(unknown.list,4,4);
    rejects([&]{missing.resume(unknown.services);},"Unknown physical ordinal used its stale canonical value.");
    check(unknown.object_calls.empty() && unknown.memory_calls.empty(),"Unknown ordinal invoked metadata before its original halfword read.");
}

void outer_size_and_fresh_inner_elements(){
    Fixture f;auto second=std::make_shared<IgbDecodeRuntimeField>(),third=std::make_shared<IgbDecodeRuntimeField>(),fourth=std::make_shared<IgbDecodeRuntimeField>();
    second->vtable_word=0x11111111;third->vtable_word=0x22222222;fourth->vtable_word=0x33333333;
    f.memory_action=[&](const auto& call,auto&,auto& status){
        if(call.kind==MK::tagged_pool)f.descriptor->element_field=second;
        else if(call.kind==MK::field_virtual && call.virtual_slot==0x64){if(f.sizes==1){check(call.field==f.element,"Outer size lost element captured before pool lookup.");status.value=0xabcd0004;}
            else{check(call.field==second && call.vtable_word==0x11111111,"62BB8 did not freshly select its own size element.");status.value=0xffff0008;f.descriptor->element_field=third;}}
        else if(call.kind==MK::field_virtual && call.virtual_slot==0x6c){check(call.field==third,"62BB8 reused outer/size element for alignment.");status.value=0xbeef0010;}
        else if(call.kind==MK::allocate){check(call.bytes==32 && call.alignment==16,"Outer division or inner size/alignment truncation changed.");f.descriptor->element_field=fourth;}
        else if(call.kind==MK::field_virtual && call.virtual_slot==0x58)check(call.field==fourth,"Allocation ancestry did not select fresh post-allocation element.");};
    IgbDataListCapacityInvocation run(f.list,4,4);check(run.resume(f.services).completed && f.sizes==2 && word(f.list,0x0c)==4,
        "Capacity wrapper did not compose separate outer and inner element operations.");
    Fixture divide;divide.memory_action=[&](const auto& call,auto&,auto& status){if(call.kind==MK::field_virtual && call.virtual_slot==0x64)status.value=0x10000;};
    IgbDataListCapacityInvocation zero(divide.list,4,4);rejects([&]{zero.resume(divide.services);},"Zero low16 element size fabricated a quotient.");
    check(zero.faulted() && divide.tags==1 && divide.sizes==1 && divide.allocations==0 && word(divide.list,0x0c)==0,
        "Division failure occurred after allocation or before the original pool/size calls.");
    Fixture wrap;wrap.output={};IgbDataListCapacityInvocation product(wrap.list,0x40000001,4);
    check(product.resume(wrap.services).completed && word(wrap.list,0x0c)==0x40000001 && word(wrap.list,0x10)==0,
        "Wrapped byte product or ignored null-allocation status changed capacity behavior.");
    check(std::find_if(wrap.memory_calls.begin(),wrap.memory_calls.end(),[](const auto& call){return call.kind==MK::allocate && call.bytes==4;})!=wrap.memory_calls.end(),
        "Capacity multiplication did not wrap before unsigned element-size division.");
}

void status_after_publication_and_count_last(){
    Fixture f;word(f.list,8,3);f.services->memory->status_success.reset();IgbDataListGrowthInvocation run(f.list,5,4);
    check(!run.resume(f.services).completed && word(f.list,0x10)==f.output.address && word(f.list,0x0c)==0 && word(f.list,8)==3 && f.allocations==1,
        "Unknown nested allocation status moved pointer publication or allowed capacity/count stores early.");
    f.services->memory->status_success=0xfedcba98;check(run.resume().completed && word(f.list,0x0c)==8 && word(f.list,8)==5 && f.allocations==1,
        "Late allocator status replayed allocation or blocked the original ignored-status continuation.");
    Fixture failure;failure.output={};failure.services->memory->status_failure.reset();IgbDataListGrowthInvocation failed(failure.list,1,4);
    check(!failed.resume(failure.services).completed && word(failure.list,0x10)==0 && word(failure.list,0x0c)==0,
        "Unknown null-allocation failure status was treated as completed.");
    failure.services->memory->status_failure=7;check(failed.resume().completed && word(failure.list,0x0c)==4 && word(failure.list,8)==1,
        "Original ignored allocation failure incorrectly prevented capacity/count publication.");
}

void fresh_capacity_copy_destination_and_release_tag(){
    Fixture f;auto old=allocation(0x300000,32,0,1);for(unsigned i=0;i<32;++i)old.bytes->bytes[old.offset+i]=static_cast<std::uint8_t>(0x40+i);
    f.old(old,8);word(f.list,8,2);auto second_pool=std::make_shared<ModelSceneData>();bool freed{};
    f.memory_action=[&](const auto& call,auto& out,auto&){if(call.kind==MK::allocate){word(f.list,0x0c,3);igb_bootstrap_write_byte(pointer(f.list),4,0x77);}
        if(call.kind==MK::tagged_pool){if(f.tags==1)check(call.handle==0x35,"Allocation lookup did not capture original byte tag.");
            else{check(call.handle==0x77,"Old-buffer release did not reload the live byte tag.");out.pool_result=second_pool;}}};
    f.free_action=[&](const IgbDataListGrowthCall& call,const std::function<void()>&){check(call.address==old.address && call.pool==second_pool &&
        call.virtual_slot==0x108 && word(f.list,0x0c)==3 && word(f.list,8)==2,"Release lost captured old backing or occurred after final capacity/count stores.");
        for(unsigned i=0;i<16;++i)check(f.output.bytes->bytes[f.output.offset+i]==(i<12?old.bytes->bytes[old.offset+i]:0),
            "Copy did not use the fresh signed minimum capacity and actual published destination.");return MessengerCallResult{freed,0,"Prepared free suspension."};};
    IgbDataListCapacityInvocation run(f.list,4,4);check(!run.resume(f.services).completed && f.free_calls.size()==1 && word(f.list,0x0c)==3,
        "Pending old-buffer release published requested capacity early.");
    f.free_operations[0]->resume_checked=[](const std::function<void()>&)->MessengerCallResult{throw std::runtime_error("Replacement free must not run.");};
    f.services->factory={};freed=true;check(run.resume().completed && word(f.list,0x0c)==4 && word(f.list,8)==2 && f.free_polls==2 && f.tags==2,
        "Resumed release lost selected continuation or replayed copy/tag lookup.");
}

void zero_capacity_and_failed_allocation_prefix(){
    Fixture zero;zero.services->data_defaults.reset();zero.services->objects.reset();zero.list.storage->bytes->initialized[zero.list.offset+4]=0;
    word(zero.list,8,9);word(zero.list,0x0c,8);IgbDataListCapacityInvocation clear(zero.list,0,4);
    check(clear.resume(zero.services).completed && word(zero.list,0x10)==0 && word(zero.list,0x0c)==0 && word(zero.list,8)==9 &&
        zero.object_calls.empty() && zero.memory_calls.empty() && zero.free_calls.empty(),"Zero/null capacity branch read metadata/tag or changed list count.");
    Fixture old;word(old.list,0x10,0x87654321);word(old.list,0x0c,8);bool ready{};
    old.free_action=[&](const auto& call,const std::function<void()>&){check(call.address==0x87654321 && word(old.list,0x10)==0x87654321 && word(old.list,0x0c)==8,
        "Zero-capacity branch cleared backing/capacity before captured free returned.");return MessengerCallResult{ready,0,"Prepared free suspension."};};
    IgbDataListCapacityInvocation pending(old.list,0,4);check(!pending.resume(old.services).completed && old.object_calls.empty() && old.tags==1,
        "Zero-capacity release required descriptor metadata or skipped the live tag lookup.");ready=true;
    check(pending.resume().completed && word(old.list,0x10)==0 && word(old.list,0x0c)==0,"Zero-capacity release did not clear backing then capacity.");
    Fixture failure;auto buffer=allocation(0x300000,16,0x5a,1);failure.old(buffer,4);failure.output={};IgbDataListCapacityInvocation ignored(failure.list,4,4);
    check(ignored.resume(failure.services).completed && word(failure.list,0x10)==buffer.address && word(failure.list,0x0c)==4 &&
        failure.free_calls.size()==1 && failure.free_calls[0].address==buffer.address && failure.sizes==2,
        "Null allocation incorrectly skipped original self-copy/free or replaced the existing backing pointer.");
}

void caught_allocator_reentry_stops_zero_and_publication(){
    for(bool growth:{false,true}){Fixture f;const auto before=f.output.bytes->bytes,mask=f.output.bytes->initialized;unsigned caught{};
        IgbDataListCapacityInvocation capacity(f.list,4,4);IgbDataListGrowthInvocation outer(f.list,1,4);
        f.memory_action=[&](const auto& call,auto&,auto&){if(call.kind==MK::allocate){try{if(growth)outer.resume(f.services);else capacity.resume(f.services);}
            catch(const std::runtime_error&){++caught;}}};
        rejects([&]{if(growth)outer.resume(f.services);else capacity.resume(f.services);},"Caught parent recursion let actual62BB8 zero/publish afterward.");
        check(caught==1 && (growth?outer.faulted():capacity.faulted()) && f.output.bytes->bytes==before && f.output.bytes->initialized==mask &&
            word(f.list,0x10)==0 && word(f.list,0x0c)==0 && word(f.list,8)==0 && f.free_calls.empty(),
            "Faulted parent allowed nested allocation buffer/publication/capacity/count effects.");
        const auto calls=f.allocations;f.memory_action={};rejects([&]{if(growth)outer.resume(f.services);else capacity.resume(f.services);},
            "Faulted growth/capacity revived after removing its allocator callback.");check(f.allocations==calls,"Faulted parent repeated allocation.");
    }
}

void raw_copy_direction_overlap_and_zero(){
    for(const auto offsets:std::array<std::pair<unsigned,unsigned>,4>{{{4,0},{0,4},{0,0},{2,0}}}){
        auto services=std::make_shared<IgbMemoryFieldConstructServices>();auto storage=allocation(0x300000,24,0,1);
        for(unsigned i=0;i<24;++i)storage.bytes->bytes[storage.offset+i]=static_cast<std::uint8_t>(i+1);igb_bind_memory_field_allocation(services,storage);
        auto expected=storage.bytes->bytes;std::array<std::uint8_t,12> original{};
        for(unsigned i=0;i<12;++i)original[i]=expected[storage.offset+offsets.second+i];
        for(unsigned i=0;i<12;++i)expected[storage.offset+offsets.first+i]=original[i];
        IgbAllocationCopyInvocation run(storage.address+offsets.first,storage.address+offsets.second,12);
        const auto status=run.resume(services);check(status.completed && status.value==storage.address+offsets.first &&
            run.resume().value==status.value && storage.bytes->bytes==expected,"Raw original copy lost overlap/equality direction or destination return semantics.");
    }
    IgbAllocationCopyInvocation zero(0xdeadbeef,0xffffffff,0);const auto zero_status=zero.resume();
    check(zero_status.completed && zero_status.value==0xdeadbeef && zero.resume().value==0xdeadbeef && !zero.faulted(),
        "Zero raw copy resolved/dereferenced an unused address or lost its destination return.");
    for(bool backwards:{false,true}){auto services=std::make_shared<IgbMemoryFieldConstructServices>();
        auto source=allocation(backwards?0x100000:0x300000,4,0x31,1),target=allocation(0x200000,4);
        source.bytes->initialized[source.offset+(backwards?2:1)]=0;igb_bind_memory_field_allocation(services,source);igb_bind_memory_field_allocation(services,target);
        IgbAllocationCopyInvocation run(target.address,source.address,4);rejects([&]{run.resume(services);},"Raw copy accepted an unknown reached source byte.");
        const unsigned reached=backwards?3:0;for(unsigned i=0;i<4;++i)check(target.bytes->bytes[target.offset+i]==(i==reached?0x31:0xcd) &&
            target.bytes->initialized[target.offset+i]==unsigned(i==reached),"Raw copy ignored unsigned direction or prevalidated later source bytes.");
        source.bytes->initialized.assign(source.bytes->initialized.size(),1);rejects([&]{run.resume(services);},"Faulted raw copy replayed after source repair.");
    }
    auto services=std::make_shared<IgbMemoryFieldConstructServices>();auto span=allocation(0x300010,8,0,1,0x10);
    span.bytes->bytes[0x0c]=0x12;span.bytes->bytes[0x0d]=0x34;span.bytes->bytes[0x0e]=0x56;span.bytes->bytes[0x0f]=0x78;
    igb_bind_memory_field_allocation(services,span);IgbAllocationCopyInvocation prefix(span.address,span.address-4,4);
    check(prefix.resume(services).completed && igb_buffer_word(span.bytes,span.offset)==0x12345678,
        "Raw copy lost canonical allocation physical-prefix addressing.");
}

void raw_copy_pending_and_fault_prefix(){
    auto services=std::make_shared<IgbMemoryFieldConstructServices>();auto source=allocation(0x300000,4,0x62,1),target=allocation(0x200000,4);
    igb_bind_memory_field_allocation(services,target);IgbAllocationCopyInvocation run(target.address,source.address,4);
    check(!run.resume(services).completed && target.bytes->initialized[target.offset]==0,"Absent raw source mapping invented payload or wrote target.");
    igb_bind_memory_field_allocation(services,source);check(run.resume(services).completed && igb_buffer_word(target.bytes,target.offset)==0x62626262,
        "Late source mapping failed to resume the captured copy operands.");
    auto bounded=std::make_shared<IgbMemoryFieldConstructServices>();auto output=allocation(0x200000,2);
    igb_bind_memory_field_allocation(bounded,output);igb_bind_memory_field_allocation(bounded,source);
    IgbAllocationCopyInvocation partial(output.address,source.address,4);rejects([&]{partial.resume(bounded);},"Raw copy wrote outside its represented target allocation.");
    check(partial.faulted() && output.bytes->bytes[output.offset]==0x62 && output.bytes->bytes[output.offset+1]==0x62,
        "Raw target bound fault discarded the preceding byte-write prefix.");
}

void raw_copy_captured_owners_and_read_before_destination(){
    auto services=std::make_shared<IgbMemoryFieldConstructServices>();auto source=allocation(0x300000,4,0x63,1),target=allocation(0x200000,4);
    igb_bind_memory_field_allocation(services,source);IgbAllocationCopyInvocation waiting(target.address,source.address,4);
    check(!waiting.resume(services).completed,"Missing raw destination mapping was fabricated.");source.bytes->bytes[source.offset]=0xee;
    igb_bind_memory_field_allocation(services,target);check(waiting.resume(services).completed && igb_buffer_word(target.bytes,target.offset)==0x63636363,
        "Late destination mapping reread the already captured first source byte.");
    auto selected=std::make_shared<IgbMemoryFieldConstructServices>();auto old_source=allocation(0x300000,4,0x71,1),old_target=allocation(0x200000,4);
    igb_bind_memory_field_allocation(selected,old_source);igb_bind_memory_field_allocation(selected,old_target);
    auto new_source=allocation(0x300000,4,0x88,1),new_target=allocation(0x200000,4,0x99);bool replaced{};
    IgbAllocationCopyInvocation copy(old_target.address,old_source.address,4);const auto guard=[&]{
        if(!replaced && old_target.bytes->initialized[old_target.offset]){replaced=true;selected->allocations={new_source,new_target};}};
    check(copy.resume(selected,guard).completed && replaced && igb_buffer_word(old_target.bytes,old_target.offset)==0x71717171 &&
        std::all_of(new_target.bytes->bytes.begin(),new_target.bytes->bytes.end(),[](auto value){return value==0x99;}),
        "Canonical lookup replacement redirected already captured source/destination byte owners.");
    auto bad=std::make_shared<IgbMemoryFieldConstructServices>();auto unknown=allocation(0x300000,4,0x45,1);unknown.bytes->initialized[unknown.offset]=0;
    igb_bind_memory_field_allocation(bad,unknown);IgbAllocationCopyInvocation source_first(0,unknown.address,4);
    rejects([&]{source_first.resume(bad);},"Raw copy skipped the reached unknown source before its invalid destination.");
    check(source_first.faulted(),"Reached raw source fault became pending destination resolution.");
    auto released=std::make_shared<IgbMemoryFieldConstructServices>();auto from=allocation(0x300000,4,0x67,1),to=allocation(0x200000,4);
    igb_bind_memory_field_allocation(released,from);igb_bind_memory_field_allocation(released,to);bool invalidated{};
    IgbAllocationCopyInvocation partial(to.address,from.address,4);const auto invalidate=[&]{if(!invalidated && to.bytes->initialized[to.offset]){
        invalidated=true;from.bytes->released=true;}};
    rejects([&]{partial.resume(released,invalidate);},"Raw copy used a released captured source owner.");
    check(partial.faulted() && invalidated && to.bytes->bytes[to.offset]==0x67 && to.bytes->initialized[to.offset+1]==0,
        "Released-owner fault changed its completed first-byte prefix.");
}

void copy_and_free_failure_preserve_capacity_prefix(){
    Fixture f;auto old=allocation(0x300000,16,0x63,1);f.old(old,3);word(f.list,8,8);old.bytes->initialized[old.offset+10]=0;
    IgbDataListGrowthInvocation run(f.list,4,4);rejects([&]{run.resume(f.services);},"List growth accepted an unknown reached old-buffer source byte.");
    check(run.faulted() && word(f.list,0x10)==f.output.address && word(f.list,0x0c)==3 && word(f.list,8)==8 && f.free_calls.empty() &&
        f.output.bytes->bytes[f.output.offset+11]==0x63 && f.output.bytes->bytes[f.output.offset+10]==0,
        "Copy failure lost actual allocation/backward-byte prefix or published capacity/count/freed old storage.");
    old.bytes->initialized[old.offset+10]=1;rejects([&]{run.resume();},"Failed growth copy resumed after source repair.");
    Fixture freed;auto original=allocation(0x300000,16,0x41,1);freed.old(original,3);word(freed.list,8,7);unsigned caught{};
    IgbDataListGrowthInvocation outer(freed.list,4,4);
    freed.free_action=[&](const auto&,const std::function<void()>&){try{outer.resume(freed.services);}catch(const std::runtime_error&){++caught;}
        return MessengerCallResult{true,0,{}};};
    rejects([&]{outer.resume(freed.services);},"Caught growth reentry during free published final capacity/count.");
    check(outer.faulted() && caught==1 && word(freed.list,0x10)==freed.output.address && word(freed.list,0x0c)==3 && word(freed.list,8)==7 &&
        freed.free_polls==1 && freed.output.bytes->bytes[freed.output.offset+11]==0x41,
        "Free callback fault erased completed copy or continued capacity/count stores.");
}

void actual_manager_guard_reaches_nested_allocator(){
    Fixture f;const auto make=[](IgbRuntimeObjectKind kind,std::uint32_t address,std::uint32_t table,unsigned size){
        IgbObjectInstance object;object.offset=0x10;object.storage=std::make_shared<IgbObjectStorage>();object.storage->original_address=address;
        object.storage->bytes=bytes(size+object.offset);object.runtime_object=std::make_shared<IgbRuntimeObject>(kind);
        object.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{object.storage,object.offset};IgbObjectBinding binding;
        binding.offset=object.offset;binding.runtime_object=object.runtime_object;object.storage->bindings.push_back(binding);
        word(object,0,table);word(object,4,2);return object;};
    auto manager=make(IgbRuntimeObjectKind::thread_manager,0x500000,0x804700a8,0x10);
    auto thread=make(IgbRuntimeObjectKind::thread,0x600000,0x80470110,0x388);
    auto semaphore=make(IgbRuntimeObjectKind::semaphore,0x700000,0x80470314,0x28);
    word(manager,8,igb_bootstrap_address(pointer(f.list)));word(manager,0x0c,igb_bootstrap_address(pointer(semaphore)));
    igb_bootstrap_write_byte(pointer(semaphore),0x14,1);word(semaphore,0x18,1);word(semaphore,0x10,1);
    auto services=std::make_shared<IgbThreadManagerInsertServices>();services->memory=f.services->memory;
    services->runtime=std::make_shared<IgbThreadRuntimeInitServices>();services->runtime->objects=std::make_shared<IgbObjectFieldConstructServices>();
    services->runtime->objects->objects={manager,thread,semaphore,f.list};
    auto interrupts=std::make_shared<IgbExecutionInterruptState>();interrupts->enabled=true;services->interrupts=make_native_igb_interrupt_factory(interrupts);
    services->runtime->factory=make_native_igb_semaphore_runtime_access_factory(services);services->factory=make_native_igb_data_list_growth_factory(f.services);
    IgbThreadManagerInsertInvocation run(pointer(manager),pointer(thread));const auto before=f.output.bytes->bytes,mask=f.output.bytes->initialized;unsigned caught{};
    f.memory_action=[&](const auto& call,auto&,auto&){if(call.kind==MK::allocate){try{run.resume(services);}catch(const std::runtime_error&){++caught;}}};
    rejects([&]{run.resume(services);},"Caught manager reentry escaped growth/capacity guards into actual62BB8 publication.");
    check(caught==1 && run.faulted() && word(thread,4)==3 && word(semaphore,0x18)==0 && interrupts->enabled==true &&
        word(f.list,8)==0 && word(f.list,0x0c)==0 && word(f.list,0x10)==0 && f.output.bytes->bytes==before && f.output.bytes->initialized==mask,
        "Nested allocator changed buffer/list/count after actual manager invalidation or lost its retain/wait prefix.");
    rejects([&]{run.resume(services);},"Faulted actual manager resumed after allocator reentry.");check(f.allocations==1,"Faulted actual manager repeated list allocation.");
}
}
int main(){try{
    fixed_original_growth_arithmetic();physical_ordinal_and_capture_before_metadata();outer_size_and_fresh_inner_elements();status_after_publication_and_count_last();
    fresh_capacity_copy_destination_and_release_tag();zero_capacity_and_failed_allocation_prefix();caught_allocator_reentry_stops_zero_and_publication();
    raw_copy_direction_overlap_and_zero();raw_copy_pending_and_fault_prefix();raw_copy_captured_owners_and_read_before_destination();
    copy_and_free_failure_preserve_capacity_prefix();actual_manager_guard_reaches_nested_allocator();
    std::cout<<"igb_data_list_growth_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Data-list growth tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
