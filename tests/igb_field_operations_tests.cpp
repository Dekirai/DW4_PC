#include "dw4/igb_field_operations.hpp"

#include <iostream>

using namespace dw4;
namespace {
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void fails(F run,const char* message){bool caught{};try{run();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto text(const char* value){return std::make_shared<const std::string>(value);}
auto string(const char* value,std::uint32_t count=1){auto s=std::make_shared<IgbInternedString>();s->value=text(value);s->references=count;s->container=std::make_shared<ModelSceneData>();return s;}
auto bytes(unsigned size=4){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,0xa5);b->initialized.assign(size,1);return b;}
auto field(){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=0x80476e6c;f->name_string=std::shared_ptr<IgbInternedString>{};
    f->global_slot=std::shared_ptr<IgbRuntimeFieldSlot>{};f->default_bytes=IgbRuntimeFieldDefaultSource{};f->default_bytes_address=0;return f;}
using K=IgbFieldOperationKind;
struct Fixture {
    std::shared_ptr<IgbDecodeRuntimeField> descriptor=field();
    std::shared_ptr<IgbFieldOperationsServices> services=std::make_shared<IgbFieldOperationsServices>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<IgbFieldOperationCall> calls;std::function<void(const IgbFieldOperationCall&,IgbFieldOperation&,std::uint32_t&)> mutate;
    Fixture(){services->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->dependencies=std::make_shared<IgbDependenciesServices>();services->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->factory=make_native_igb_unsigned_int_field_factory(services,[this](const IgbFieldOperationCall& c){
            calls.push_back(c);auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{std::uint32_t result=4;
                if(c.kind==K::object_pool || c.kind==K::allocation_pool)out->pool_result=pool;
                else if(c.kind==K::allocate_default){out->buffer_result={bytes(c.bytes),0};out->buffer_address=0x40000;}
                if(mutate)mutate(c,*out,result);return MessengerCallResult{true,result,{}};};return op;});
    }
};
void names(){
    Fixture f;auto input=string("initial");auto old=string("old");auto replacement=string("replacement",7);auto displaced=string("displaced",9);
    auto slot=std::make_shared<IgbFieldNameSlot>(input);f.descriptor->name_string=old;unsigned releases{};
    f.services->dependencies->factory=[&](const IgbDependenciesCall& c){check(c.kind==IgbDependenciesServiceKind::release_string && c.string==old && c.pool==old->container,"Name release lost captured header/container.");
        auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++releases;*slot=replacement;f.descriptor->name_string=displaced;old->released=true;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldNameInvocation set(f.descriptor,slot);check(set.resume(f.services).completed,"Field naming failed.");
    check(*f.descriptor->name_string==replacement && f.descriptor->name==replacement->value && input->references==2 && replacement->references==7 && displaced->references==9,"Name cell was not reloaded after release or raw publication added a retain.");
    check(old->references==0 && releases==1,"Old string was not released exactly once.");
    check(set.resume().completed && releases==1,"Completed naming replayed.");
    auto same=field();same->name_string=input;auto alias=std::shared_ptr<IgbFieldNameSlot>(same,&same->name_string);
    IgbFieldNameInvocation self(same,alias);check(self.resume().completed && input->references==2 && *same->name_string==input,"Self-alias name assignment changed references.");
    auto wrapped=string("wrap",0xffffffffu);auto wrapped_slot=std::make_shared<IgbFieldNameSlot>(wrapped);
    IgbFieldNameInvocation invalid(nullptr,wrapped_slot);fails([&]{invalid.resume(f.services);},"Null field naming succeeded.");check(wrapped->references==0,"Incoming retain did not precede null receiver failure.");
    auto unknown=field();unknown->name_string.reset();auto retained=string("held");auto retained_slot=std::make_shared<IgbFieldNameSlot>(retained);
    IgbFieldNameInvocation pending(unknown,retained_slot);check(!pending.resume().completed && retained->references==2,"Missing old pointer did not retain incoming first.");
    unknown->name_string=std::shared_ptr<IgbInternedString>{};check(pending.resume().completed && retained->references==2,"Name resumption repeated initial retain.");
    IgbFieldNameInvocation null_cell(unknown,{});fails([&]{null_cell.resume();},"Null source cell was accepted.");
}
void pending_name_and_lifetime(){
    Fixture f;auto old=string("old");auto incoming=string("in");auto slot=std::make_shared<IgbFieldNameSlot>(incoming);
    f.descriptor->name_string=old;unsigned selects{},polls{};bool ready{};
    f.services->dependencies->factory=[&](const auto&){++selects;auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&]{++polls;
        if(!ready)return MessengerCallResult{false,0,"prepared release"};slot->reset();return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldNameInvocation call(f.descriptor,slot);check(!call.resume(f.services).completed && old->references==0,"Name release did not suspend after decrement.");
    ready=true;check(!call.resume(f.services).completed && selects==1 && polls==2,"Unknown reloaded source replayed release.");
    *slot=std::shared_ptr<IgbInternedString>{};check(call.resume(f.services).completed && !*f.descriptor->name_string && incoming->references==2 && polls==2,"Late null cell was not published raw.");
    std::weak_ptr<IgbInternedString> retained;
    {auto local=string("retained");retained=local;auto source=std::make_shared<IgbFieldNameSlot>(local);auto target=field();target->name_string.reset();
        IgbFieldNameInvocation unfinished(target,source);check(!unfinished.resume(f.services).completed,"Abandoned name did not suspend.");}
    check(!retained.expired() && retained.lock()->references==2,"Abandoning the call lost its original retained name.");
    std::weak_ptr<IgbInternedString> acquired,displaced_old;
    {
        auto source=std::make_shared<IgbFieldNameSlot>(string("acquired"));acquired=**source;
        f.descriptor->name_string=string("released");
        f.services->dependencies->factory=[source](const auto&){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[source]{*source=std::shared_ptr<IgbInternedString>{};return MessengerCallResult{true,0,{}};};return op;};
        IgbFieldNameInvocation replaced(f.descriptor,source);check(replaced.resume(f.services).completed,"Replacement name did not complete.");
    }
    check(!acquired.expired() && acquired.lock()->references==2,"Completed name replacement dropped its initial acquired reference.");
    {
        auto target=field();target->name_string=string("pending old");displaced_old=*target->name_string;
        auto source=std::make_shared<IgbFieldNameSlot>(std::shared_ptr<IgbInternedString>{});
        f.services->dependencies->factory=[target](const auto&){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[target]{target->name_string=std::shared_ptr<IgbInternedString>{};return MessengerCallResult{false,0,"pending release"};};return op;};
        IgbFieldNameInvocation unfinished(target,source);check(!unfinished.resume(f.services).completed,"Old name release did not suspend.");
    }
    f.services->dependencies->factory={};
    check(!displaced_old.expired() && displaced_old.lock()->references==0,"Abandoned release lost its captured old name owner.");
}
void finalization_order(){
    Fixture f;f.descriptor->vtable_word=11;auto type=field();type->ordinal=0xfffe;
    auto first=std::make_shared<IgbRuntimeFieldSlot>();auto second=std::make_shared<IgbRuntimeFieldSlot>();f.descriptor->global_slot=first;
    std::vector<std::uint32_t> trace;
    f.services->factory=[&](const IgbFieldOperationCall& c){check(c.kind==K::field_virtual,"Finalize invented an external endpoint.");trace.push_back(c.virtual_slot);
        auto op=std::make_shared<IgbFieldOperation>();op->resume=[&,c,out=op.get()]{
            if(c.virtual_slot==0x5c){check(c.vtable_word==11,"Wrong captured type vtable.");out->field_result=type;f.descriptor->vtable_word=22;}
            else if(c.virtual_slot==0x64){check(c.vtable_word==22 && f.descriptor->type_ordinal==0xfffe,"Type ordinal or fresh size vtable differs.");f.descriptor->vtable_word=33;return MessengerCallResult{true,0x10007,{}};}
            else{check(c.virtual_slot==0x8c && c.vtable_word==33 && c.argument==0 && f.descriptor->size==7,"Default call lost truncation/zero source/fresh vtable.");f.descriptor->global_slot=second;f.descriptor->size=9;}
            return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldFinalizeInvocation run(f.descriptor);check(run.resume(f.services).completed,"Finalize failed.");
    check(!first->has_value() && *second==f.descriptor && f.descriptor->size==9 && trace==std::vector<std::uint32_t>{0x5c,0x64,0x8c},"Global field publication did not follow completed default callback.");
    Fixture late;late.descriptor->global_slot.reset();IgbFieldFinalizeInvocation wait(late.descriptor);
    check(!wait.resume(late.services).completed && late.calls.size()==2 && late.descriptor->type_ordinal==0xffff && late.descriptor->size==4,"Known native virtual chain did not reach missing global cell.");
    late.descriptor->global_slot=std::shared_ptr<IgbRuntimeFieldSlot>{};check(wait.resume(late.services).completed && late.calls.size()==2,"Late null global replayed default buffer allocation.");
    Fixture prototype;prototype.services->unsigned_int_prototype.reset();IgbFieldFinalizeInvocation missing(prototype.descriptor);
    check(!missing.resume(prototype.services).completed && prototype.calls.empty(),"Unknown prototype was fabricated null.");
    prototype.services->unsigned_int_prototype=type;check(missing.resume(prototype.services).completed && prototype.descriptor->type_ordinal==0xfffe,"Live prototype binding was not used.");
}
void global_cell_lifetime(){
    Fixture live;auto cell=std::make_shared<IgbRuntimeFieldSlot>();live.descriptor->global_slot=cell;
    std::weak_ptr<IgbRuntimeFieldSlot> weak_cell=cell;
    {IgbFieldFinalizeInvocation call(live.descriptor);check(call.resume(live.services).completed && *cell==live.descriptor,"Live descriptor global was not published.");}
    cell.reset();check(weak_cell.expired() && !igb_field_global_slot_is_null(*live.descriptor->global_slot),"Descriptor publication created a strong global-cell cycle or erased nonnull identity.");
    check(live.descriptor->reference_word==1,"Nonowning cell correction changed guest reference count.");
    Fixture expired;{auto short_lived=std::make_shared<IgbRuntimeFieldSlot>();expired.descriptor->global_slot=short_lived;}
    IgbFieldFinalizeInvocation fault(expired.descriptor);fails([&]{fault.resume(expired.services);},"Expired nonnull global cell was silently treated as original null.");
    check(fault.faulted() && expired.descriptor->type_ordinal==0xffff && expired.descriptor->size==4 && expired.calls.size()==2,"Expired cell failed before actual finalization prefix.");
    Fixture null;null.descriptor->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};IgbFieldFinalizeInvocation empty(null.descriptor);
    check(empty.resume(null.services).completed && igb_field_global_slot_is_null(*null.descriptor->global_slot),"Ownerless weak binding did not preserve original null pointer.");
}
void buffer_live_reads(){
    Fixture f;auto old=bytes(2),next=bytes(2),replacement=bytes(8);f.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{old,0};f.descriptor->default_bytes_address=0x10000;
    f.descriptor->reference_word=0x02000001;unsigned pool_queries{};
    f.mutate=[&](const IgbFieldOperationCall& c,IgbFieldOperation&,std::uint32_t& result){
        if(c.kind==K::allocation_size){check(c.buffer.buffer==old && c.address==0x10000,"Size query lost captured backing.");result=0xffffffffu;f.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{next,0};f.descriptor->default_bytes_address=0x20000;f.descriptor->size=8;}
        if(c.kind==K::object_pool){++pool_queries;if(pool_queries==1)check(c.handle==2,"Free did not use live field pool tag.");else{check(c.handle==3,"Allocate did not reload field pool tag.");f.descriptor->size=16;}}
        if(c.kind==K::free_default){check(c.buffer.buffer==next && c.address==0x20000,"Free did not reload changed default pointer.");next->released=true;f.descriptor->size=12;f.descriptor->reference_word=0x03000001;
            f.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{replacement,0};f.descriptor->default_bytes_address=0x30000;}
        if(c.kind==K::allocate_default)check(c.bytes==12 && c.alignment==1,"Allocation did not capture size before pool lookup.");
    };
    IgbFieldBufferInvocation run(f.descriptor);check(run.resume(f.services).completed && f.calls.size()==6 && f.descriptor->size==16 && f.descriptor->default_bytes_address==0x40000,"Signed size comparison/live free-reallocate chain differs.");
    check(f.descriptor->default_bytes->buffer->bytes.size()==12 && next->released && !old->released,"Default allocation/free identity differs.");
    Fixture enough;enough.descriptor->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4),0};enough.descriptor->default_bytes_address=0x12300;
    IgbFieldBufferInvocation fits(enough.descriptor);check(fits.resume(enough.services).completed && enough.calls.size()==2,"Equal default size was unnecessarily reallocated.");
    Fixture null;null.mutate=[](const auto& c,auto& op,auto&){if(c.kind==K::allocate_default){op.buffer_result={};op.buffer_address=0;}};
    IgbFieldFinalizeInvocation zero(null.descriptor);check(zero.resume(null.services).completed && null.descriptor->default_bytes && !null.descriptor->default_bytes->buffer,"Original null allocation result was not preserved.");
    Fixture null_pool;null_pool.mutate=[](const auto& c,auto& op,auto&){if(c.kind==K::object_pool)op.pool_result.reset();};
    IgbFieldBufferInvocation bad(null_pool.descriptor);fails([&]{bad.resume(null_pool.services);},"Null allocation pool was accepted.");
    check(null_pool.descriptor->size==4 && null_pool.calls.size()==1,"Pool failure lost size-store prefix.");
}
void physical_buffer_publication(){
    Fixture f;auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=0x110000;storage->bytes=bytes(0x34);
    storage->bindings.push_back({0,{},{},{},{},f.descriptor});f.descriptor->physical_storage=IgbFieldStorageBinding{storage,0};
    IgbBootstrapPointer physical{storage,0};igb_bootstrap_write_word(physical,0x20,0);
    bool observed{};f.mutate=[&](const IgbFieldOperationCall& c,auto&,auto&){if(c.kind==K::allocate_default){
        check(igb_bootstrap_read_word(physical,0x14)==0x0004a5a5 && igb_bootstrap_read_word(physical,0x20)==0,
            "Pending allocation did not expose the physical size store before pointer publication.");observed=true;}};
    IgbFieldBufferInvocation call(f.descriptor);check(call.resume(f.services).completed && observed &&
        igb_bootstrap_read_word(physical,0x20)==*f.descriptor->default_bytes_address,"Field buffer failed to publish physical default pointer.");
    auto replacement=bytes(4);igb_field_store_default(f.descriptor,{replacement,0},0x220000);
    check(igb_bootstrap_read_word(physical,0x20)==0x220000 && f.descriptor->default_bytes->buffer==replacement,
        "Raw field default helper lost physical/canonical identity.");
    f.mutate={};f.descriptor->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
    IgbFieldFinalizeInvocation finalized(f.descriptor);
    check(finalized.resume(f.services).completed && igb_bootstrap_read_word(physical,0x10)==0xffffa5a5,
        "Field finalization left physical type ordinal stale or overwrote adjacent instance ordinal.");
    storage->bindings[0].field=std::make_shared<IgbDecodeRuntimeField>();
    fails([&]{igb_field_store_size(f.descriptor,8);},"Changed physical field binding was overwritten.");
    check(f.descriptor->size==4,"Rejected physical size store still changed typed size.");
    physical={};storage.reset();fails([&]{igb_field_store_default(f.descriptor,{},0);},"Expired physical field binding was ignored.");
    check(f.descriptor->default_bytes_address==0x220000,"Rejected physical pointer store still changed typed pointer.");
}
void append_and_faults(){
    Fixture f;auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();
    auto& list=*metadata->fields;list.backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);list.backing_bytes=bytes(16);list.backing_address=0x90000;
    auto global=std::make_shared<IgbRuntimeFieldSlot>();f.descriptor->global_slot=global;f.descriptor->reference_word=0xffffffffu;
    f.services->pointer_lists->values.push_back({0x50000,{},f.descriptor});unsigned list_calls{};
    f.services->pointer_lists->factory=[&](const IgbPointerListCall& c){++list_calls;check(global->has_value() && **global==f.descriptor && f.descriptor->reference_word==0,"Raw append happened before global publication/field retain.");
        auto op=std::make_shared<IgbPointerListOperation>();op->resume=[&,c,out=op.get()]{if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=f.pool;else out->size_result=16;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldAppendInvocation call(metadata,f.descriptor);check(call.resume(f.services).completed && list.count==1 && list.backing->at(0)==f.descriptor && list_calls==2,"Actual finalized pointer append failed.");
    Fixture absent;auto published=std::make_shared<IgbRuntimeFieldSlot>();absent.descriptor->global_slot=published;
    IgbFieldAppendInvocation missing({},absent.descriptor);fails([&]{missing.resume(absent.services);},"Null metadata append succeeded.");
    check(*published==absent.descriptor && absent.descriptor->reference_word==1,"Null metadata failed before finalization or after extra field retain.");
    Fixture retained;retained.services->pointer_lists.reset();auto owner=std::make_shared<IgbRuntimeMetaType>();owner->fields=std::make_shared<IgbDecodeFieldRegistry>();
    {IgbFieldAppendInvocation pending(owner,retained.descriptor);check(!pending.resume(retained.services).completed && retained.descriptor->reference_word==2,"Raw append boundary did not retain field first.");}
    check(!retained.services->unfinished.empty(),"Abandoning append lost the retained original field.");
    Fixture recursive;IgbFieldBufferInvocation reentry(recursive.descriptor);recursive.mutate=[&](const auto&,auto&,auto&){try{reentry.resume(recursive.services);}catch(const IgbFieldOperationsError&){}};
    fails([&]{reentry.resume(recursive.services);},"Caught recursive buffer call did not invalidate outer continuation.");
    std::weak_ptr<IgbFieldOperationsServices> weak;IgbFieldOperationsFactory adapter;
    {auto services=std::make_shared<IgbFieldOperationsServices>();weak=services;adapter=make_native_igb_unsigned_int_field_factory(services);services->factory=adapter;}
    check(weak.expired(),"Known field adapter formed an ownership cycle.");
}
}
int main(){try{names();pending_name_and_lifetime();finalization_order();global_cell_lifetime();buffer_live_reads();physical_buffer_publication();append_and_faults();
    std::cout<<"Field operation checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Field operations failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
