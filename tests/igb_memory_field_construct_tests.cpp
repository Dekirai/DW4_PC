#include "dw4/igb_memory_field_construct.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using K=IgbMemoryFieldConstructServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
auto buffer(std::size_t size,std::uint8_t value=0xcc){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
struct Fixture {
    std::shared_ptr<IgbMemoryFieldConstructServices> services=std::make_shared<IgbMemoryFieldConstructServices>();
    std::shared_ptr<IgbDecodeRuntimeField> descriptor=std::make_shared<IgbDecodeRuntimeField>(),element=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeMetaType> metadata=std::make_shared<IgbRuntimeMetaType>(),reference=std::make_shared<IgbRuntimeMetaType>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>(),named=std::make_shared<ModelSceneData>();
    IgbObjectInstance target;IgbPointerListBacking allocation;
    std::vector<IgbMemoryFieldConstructCall> trace;
    std::function<void(const IgbMemoryFieldConstructCall&,IgbMemoryFieldConstructOperation&,MessengerCallResult&)> action;
    unsigned sizes{},alignments{},metadata_calls{},allocations{},selections{};bool ready=true;
    Fixture(){
        target.storage=std::make_shared<IgbObjectStorage>();target.storage->original_address=0x100000;target.storage->bytes=buffer(32);
        target.offset=8;target.fields=std::make_shared<IgbDecodeFieldRegistry>();target.storage->bindings.push_back({8,{},{},target.fields});
        igb_object_set_reference_word(target,0x23000001);igb_bootstrap_write_word({target.storage,target.offset},8,0);igb_bootstrap_write_word({target.storage,target.offset},12,0);
        descriptor->flag_34=1;descriptor->element_field=element;descriptor->word_38=64;descriptor->word_4c=0;descriptor->offset=8;element->vtable_word=0x804762fc;
        metadata->parent=reference;reference->parent=std::shared_ptr<IgbRuntimeMetaType>{};
        services->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(reference);services->status_success=0;services->status_failure=1;
        services->values=std::make_shared<IgbFieldValuesServices>();allocation={buffer(80),0x200000,8,{},std::make_shared<IgbDecodeFieldRegistry::Storage>(16)};
        services->factory=[this](const auto& call){++selections;auto operation=std::make_shared<IgbMemoryFieldConstructOperation>();
            operation->resume=[this,call,out=operation.get()]{trace.push_back(call);MessengerCallResult status{true,0,{}};
                if(!ready)return MessengerCallResult{false,0,"prepared continuation"};
                switch(call.kind){
                case K::field_virtual:
                    if(call.virtual_slot==0x64){++sizes;status.value=4;}
                    else if(call.virtual_slot==0x6c){++alignments;status.value=4;}
                    else if(call.virtual_slot==0x58){++metadata_calls;out->metadata_result=metadata;}
                    else throw std::runtime_error("Unexpected memory-field virtual.");break;
                case K::named_pool:out->pool_result=named;break;
                case K::tagged_pool:out->pool_result=pool;break;
                case K::allocate:++allocations;out->backing_result=allocation;break;
                }
                if(action)action(call,*out,status);return status;
            };return operation;};
    }
    std::uint32_t output(std::uint32_t offset=8)const{return igb_bootstrap_read_word({target.storage,target.offset},offset);}
    void name(const char* value){auto owner=std::make_shared<IgbInternedString>();owner->value=std::make_shared<const std::string>(value);
        igb_bind_field_string(services->values,0x300008,owner);descriptor->word_4c=0x300008;}
};
void gate_and_basic(){
    auto descriptor=std::make_shared<IgbDecodeRuntimeField>();descriptor->flag_34=0;IgbMemoryFieldConstructInvocation skip(descriptor,{});
    check(skip.resume().completed && skip.resume().completed,"False memory construct gate read target or services.");
    Fixture f;const auto before=f.target.storage->bytes->bytes;IgbMemoryFieldConstructInvocation call(f.descriptor,f.target);
    f.action=[&](const auto& c,auto&,auto&){if(c.kind==K::tagged_pool)check(c.handle==0x23,"Tagged pool did not read target high byte.");
        if(c.kind==K::allocate)check(c.bytes==64 && c.alignment==4 && c.virtual_slot==0xdc,"Memory allocation lost64-byte/alignment4 request.");};
    check(call.resume(f.services).completed && f.output()==f.allocation.address,"Positive memory member construction did not publish its allocation.");
    check(f.sizes==2 && f.alignments==1 && f.metadata_calls==1 && f.allocations==1,"Memory member construction skipped repeated original virtual reads.");
    check(f.target.fields->backing==f.allocation.fields && f.target.fields->backing_bytes==f.allocation.bytes &&
        f.target.fields->backing_offset==8 && f.target.fields->backing_address==0x200000 && f.target.fields->count==0,"List backing publication changed typed identity, offset or count.");
    for(std::size_t i=0;i<80;++i)check(f.allocation.bytes->bytes[i]==(i>=8 && i<72?0:0xcc),"Zero fill changed allocation padding.");
    for(std::size_t i=0;i<before.size();++i)if(i<16 || i>=20)check(before[i]==f.target.storage->bytes->bytes[i],"Member publication changed object padding/count/header.");
    check(call.resume(f.services).completed && f.allocations==1,"Completed memory construction replayed allocation.");
}
void separate_reads_and_wrap(){
    Fixture f;auto second=std::make_shared<IgbDecodeRuntimeField>(),third=std::make_shared<IgbDecodeRuntimeField>(),fourth=std::make_shared<IgbDecodeRuntimeField>();
    second->vtable_word=0x2222;third->vtable_word=0x3333;fourth->vtable_word=0x4444;f.descriptor->word_38=9;
    f.action=[&](const auto& c,auto&,auto& status){
        if(c.kind==K::field_virtual && c.virtual_slot==0x64){
            if(f.sizes==1){check(c.field==f.element,"First size lost original element.");status.value=0xabcd0004;f.descriptor->element_field=second;}
            else {check(c.field==second && c.vtable_word==0x2222,"Helper did not reload size element.");status.value=0x12340008;f.descriptor->element_field=third;}
        }else if(c.kind==K::field_virtual && c.virtual_slot==0x6c){check(c.field==third,"Alignment did not reload element after second size.");status.value=0xface0010;}
        else if(c.kind==K::allocate){check(c.bytes==24 && c.alignment==16,"Count rounding, second size or alignment truncation changed.");f.descriptor->element_field=fourth;}
        else if(c.kind==K::field_virtual && c.virtual_slot==0x58)check(c.field==fourth,"Type test did not reload post-allocation element.");
    };
    IgbMemoryFieldConstructInvocation live(f.descriptor,f.target);check(live.resume(f.services).completed,"Mutating element corpus did not complete.");
    for(const auto extent:{0u,0xfffffffdu,0xfffffffeu,0xffffffffu}){
        Fixture wrap;wrap.descriptor->word_38=extent;wrap.allocation={};
        wrap.action=[&](const auto& c,auto&,auto&){if(c.kind==K::allocate)check(c.bytes==((extent+3u)/4u)*4u,"Unsigned ceiling arithmetic did not wrap at32bits.");};
        IgbMemoryFieldConstructInvocation call(wrap.descriptor,wrap.target);check(call.resume(wrap.services).completed && wrap.output()==0,"Wrapped allocation failure changed old member.");
    }
    Fixture product;product.allocation={};product.action=[](const auto& c,auto&,auto& status){if(c.kind==K::field_virtual && c.virtual_slot==0x64)status.value=0xffff;
        if(c.kind==K::allocate)check(c.bytes==0xffff0001u,"Helper size multiplication did not wrap at32bits.");};
    IgbMemoryFieldAllocateInvocation overflow(product.descriptor,product.target,0xffffffffu,product.pool);check(overflow.resume(product.services).completed,"Wrapped helper allocation did not reach null result.");
}
void pool_fallback_and_names(){
    Fixture named;named.name("Objects");named.action=[](const auto& c,auto&,auto&){if(c.kind==K::named_pool)check(*igb_dependency_name(c.name)=="Objects","Named pool lost captured characters.");
        check(c.kind!=K::tagged_pool,"Successful named lookup called tagged fallback.");};
    IgbMemoryFieldConstructInvocation direct(named.descriptor,named.target);check(direct.resume(named.services).completed,"Named memory pool path failed.");
    Fixture absent;absent.name("Absent");absent.named.reset();absent.action=[&](const auto& c,auto&,auto&){if(c.kind==K::named_pool)igb_object_set_reference_word(absent.target,0x67000001);
        if(c.kind==K::tagged_pool)check(c.handle==0x67,"Named miss failed to reload target tag.");};
    IgbMemoryFieldConstructInvocation fallback(absent.descriptor,absent.target);check(fallback.resume(absent.services).completed,"Named miss did not reach tagged pool.");
    Fixture empty;empty.name("");empty.action=[](const auto& c,auto&,auto&){check(c.kind!=K::named_pool,"Empty first character selected named lookup.");};
    IgbMemoryFieldConstructInvocation blank(empty.descriptor,empty.target);check(blank.resume(empty.services).completed,"Empty pool name did not complete.");
}
void null_allocation_and_status_order(){
    Fixture failure;failure.allocation={};failure.services->status_failure.reset();failure.services->reference_metadata.reset();
    igb_bootstrap_write_word({failure.target.storage,failure.target.offset},8,0x7777);
    IgbMemoryFieldConstructInvocation failed(failure.descriptor,failure.target);
    check(!failed.resume(failure.services).completed && failure.output()==0x7777 && failure.metadata_calls==0,"Null allocation touched old member or required a type test.");
    failure.services->status_failure=0x12345678;check(failed.resume(failure.services).completed && failure.allocations==1,"Late failure status replayed allocation or leaked helper status through wrapper.");
    Fixture success;success.services->status_success.reset();IgbMemoryFieldAllocateInvocation helper(success.descriptor,success.target,16,success.pool);
    check(!helper.resume(success.services).completed && success.output()==success.allocation.address && success.allocations==1,"Missing success status paused before original publication.");
    success.descriptor->offset=12;success.services->status_success=0x87654321;
    check(helper.resume(success.services).completed && helper.result()==0x87654321 && success.output(12)==0 && success.allocations==1,"Late success status replayed publication or changed ignored result.");
}
void type_test_zero_and_live_publication(){
    Fixture nonref;nonref.metadata->parent=std::shared_ptr<IgbRuntimeMetaType>{};IgbMemoryFieldAllocateInvocation raw(nonref.descriptor,nonref.target,16,nonref.pool);
    check(raw.resume(nonref.services).completed && nonref.allocation.bytes->bytes[8]==0xcc,"Failed ancestry test incorrectly zero-filled allocation.");
    Fixture null_target;*null_target.services->reference_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    IgbMemoryFieldAllocateInvocation no_match(null_target.descriptor,null_target.target,16,null_target.pool);
    check(no_match.resume(null_target.services).completed && null_target.allocation.bytes->bytes[8]==0xcc,"Null search metadata matched ancestry terminator.");
    Fixture late;late.metadata->parent.reset();IgbMemoryFieldAllocateInvocation pending(late.descriptor,late.target,16,late.pool);
    check(!pending.resume(late.services).completed && late.metadata_calls==1 && late.output()==0,"Unknown parent was inferred null.");
    late.metadata->parent=late.reference;*late.services->reference_metadata=std::make_shared<IgbRuntimeMetaType>();
    check(pending.resume(late.services).completed && late.allocation.bytes->bytes[8]==0 && late.metadata_calls==1,"Late parent binding recaptured already loaded reference global.");
    Fixture moved;moved.action=[&](const auto& c,auto&,auto&){if(c.kind==K::field_virtual && c.virtual_slot==0x58)moved.descriptor->offset=16;};
    IgbMemoryFieldConstructInvocation offset(moved.descriptor,moved.target);check(offset.resume(moved.services).completed && moved.output()==0 && moved.output(16)==moved.allocation.address,"Publication captured descriptor offset before type callback.");
    Fixture cycle;cycle.metadata->parent=cycle.metadata;IgbMemoryFieldAllocateInvocation cyclic(cycle.descriptor,cycle.target,16,cycle.pool);
    rejects([&]{cyclic.resume(cycle.services);},"Cyclic ancestry was accepted.");check(cyclic.faulted() && cycle.output()==0,"Ancestry fault published allocation.");cycle.metadata->parent.reset();
}
void pending_bindings_and_selected_callbacks(){
    Fixture paused;paused.ready=false;IgbMemoryFieldConstructInvocation call(paused.descriptor,paused.target);
    check(!call.resume(paused.services).completed && paused.selections==1,"First selected field callback did not suspend.");
    paused.element->vtable_word=0x5555;paused.descriptor->flag_34=0;paused.ready=true;
    check(call.resume(paused.services).completed && paused.trace[1].vtable_word==0x804762fc && paused.sizes==2,"Suspension replaced a selected size call or reread consumed gate.");
    Fixture physical;const auto allocation=physical.allocation;physical.allocation={};physical.allocation.address=allocation.address;
    IgbMemoryFieldAllocateInvocation bytes(physical.descriptor,physical.target,16,physical.pool);
    check(!bytes.resume(physical.services).completed && physical.allocations==1 && physical.output()==0,"Unknown physical allocation was fabricated.");
    igb_bind_memory_field_allocation(physical.services,allocation);check(bytes.resume(physical.services).completed && physical.allocations==1 && physical.output()==allocation.address,"Late physical binding replayed allocator.");
    Fixture mapping;const auto typed=mapping.allocation.fields;mapping.allocation.fields.reset();IgbMemoryFieldAllocateInvocation pointers(mapping.descriptor,mapping.target,16,mapping.pool);
    check(!pointers.resume(mapping.services).completed && mapping.output()==0 && mapping.allocation.bytes->bytes[8]==0,"Missing pointer mapping prevented prior physical zero fill or manufactured publication.");
    auto known=mapping.allocation;known.fields=typed;igb_bind_memory_field_allocation(mapping.services,known);mapping.descriptor->offset=12;
    check(pointers.resume(mapping.services).completed && mapping.output()==known.address && mapping.output(12)==0,"Late mapping reloaded already captured publication offset.");
}
void fault_prefix_and_owners(){
    Fixture divide;divide.descriptor->word_4c.reset();divide.action=[](const auto& c,auto&,auto& status){if(c.kind==K::field_virtual && c.virtual_slot==0x64)status.value=0x10000;};
    IgbMemoryFieldConstructInvocation zero(divide.descriptor,divide.target);
    check(!zero.resume(divide.services).completed && divide.trace.size()==1,"Zero divisor skipped preceding pool-name pointer read.");
    divide.descriptor->word_4c=0;rejects([&]{zero.resume(divide.services);},"Undefined zero division was fabricated as successful allocation.");check(zero.faulted() && divide.allocations==0,"Zero divisor selected an allocator.");
    Fixture truncated;truncated.allocation.bytes=buffer(18);IgbMemoryFieldAllocateInvocation short_buffer(truncated.descriptor,truncated.target,16,truncated.pool);
    rejects([&]{short_buffer.resume(truncated.services);},"Truncated zero-fill buffer was accepted.");
    check(short_buffer.faulted() && truncated.output()==0 && truncated.allocation.bytes->bytes[8]==0 && truncated.allocation.bytes->bytes[15]==0 &&
        truncated.allocation.bytes->bytes[16]==0xcc && truncated.allocation.bytes->bytes[17]==0xcc,"Faulting word store lost earlier original zero-fill prefix.");
    Fixture recursive;IgbMemoryFieldConstructInvocation recursion(recursive.descriptor,recursive.target);
    recursive.action=[&](const auto&,auto&,auto&){try{recursion.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursion.resume(recursive.services);},"Swallowed recursive memory operation was accepted.");check(recursion.faulted() && recursive.allocations==0,"Recursive fault reached allocation.");
    Fixture overwritten;auto old_bytes=buffer(12);auto old_fields=std::make_shared<IgbDecodeFieldRegistry::Storage>(1);
    std::weak_ptr<IgbScratchBuffer> old_storage=old_bytes;std::weak_ptr<IgbDecodeFieldRegistry::Storage> old_mapping=old_fields;
    overwritten.target.fields->backing=old_fields;overwritten.target.fields->backing_bytes=old_bytes;
    IgbMemoryFieldAllocateInvocation replace(overwritten.descriptor,overwritten.target,16,overwritten.pool);check(replace.resume(overwritten.services).completed,"Backing replacement fixture failed.");
    old_bytes.reset();old_fields.reset();check(!old_storage.expired() && !old_mapping.expired(),"Raw backing publication released displaced host owners.");
}
void native_virtual_adapter(){
    Fixture f;auto registration=std::make_shared<IgbFieldTypeRegistrationServices>();registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
    registration->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>(f.metadata);
    f.services->factory=make_native_igb_memory_field_virtual_factory(registration,f.services->factory);
    IgbMemoryFieldConstructInvocation native(f.descriptor,f.target);
    check(native.resume(f.services).completed && f.sizes==0 && f.alignments==0 && f.metadata_calls==0 && f.allocations==1,"Known object-element leaves/getter fell back to prepared virtual success.");
    check(f.output()==f.allocation.address && f.allocation.bytes->bytes[8]==0,"Native getter failed original ancestry zero fill.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_memory_field_construct_tests");
    gate_and_basic();separate_reads_and_wrap();pool_fallback_and_names();null_allocation_and_status_order();type_test_zero_and_live_publication();
    pending_bindings_and_selected_callbacks();fault_prefix_and_owners();native_virtual_adapter();
    std::cout<<"igb_memory_field_construct_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Memory field construction failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
