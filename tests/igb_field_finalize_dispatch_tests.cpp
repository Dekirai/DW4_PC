#include "dw4/igb_field_operations.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void fails(F run,const char* message){bool failed{};try{run();}catch(const std::exception&){failed=true;}check(failed,message);}
std::shared_ptr<IgbScratchBuffer> bytes(std::size_t size){
    auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,0xa5);result->initialized.assign(size,1);return result;
}
// Deliberately leave the canonical cache untouched, as independent raw guest
// writes can disagree with a previously captured native descriptor cache.
void raw_word(const std::shared_ptr<IgbScratchBuffer>& target,std::uint32_t offset,std::uint32_t value){
    for(unsigned i=0;i<4;++i){target->bytes.at(offset+i)=static_cast<std::uint8_t>(value>>(24u-8u*i));target->initialized.at(offset+i)=1;}
}
struct Fixture {
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbObjectStorage> storage=std::make_shared<IgbObjectStorage>();
    std::shared_ptr<IgbFieldOperationsServices> services=std::make_shared<IgbFieldOperationsServices>();
    std::vector<IgbFieldOperationCall> calls;
    std::function<MessengerCallResult(const IgbFieldOperationCall&,IgbFieldOperation&)> action;
    std::shared_ptr<IgbFieldOperation> selected;
    unsigned polls{};
    bool checked{};
    Fixture(){
        field->vtable_word=0xdeadbeefu;field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
        storage->original_address=0x120000;storage->bytes=bytes(0x34);
        storage->bindings.push_back({0,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{storage,0};
        raw_word(storage->bytes,0,0x11111111);
        raw_word(storage->bytes,0x1c,0);
        services->factory=[this](const IgbFieldOperationCall& call){
            check(call.kind==IgbFieldOperationKind::field_virtual,"Finalization selected an unrelated service.");
            calls.push_back(call);auto operation=std::make_shared<IgbFieldOperation>();selected=operation;
            auto resume=[this,call,out=operation.get()]{++polls;
                return action?action(call,*out):MessengerCallResult{true,4,{}};};
            if(checked)operation->resume_checked=[resume](const std::function<void()>& guard){if(guard)guard();return resume();};
            else operation->resume=resume;
            return operation;
        };
    }
};
void physical_dispatch_and_fresh_reload(){
    Fixture f;auto type=std::make_shared<IgbDecodeRuntimeField>();type->ordinal=0x8001;
    f.action=[&](const auto& call,auto& out){
        if(call.virtual_slot==0x5c){
            check(call.vtable_word==0x11111111,"Type callback used the stale canonical VT.");out.field_result=type;
            raw_word(f.storage->bytes,0,0x22222222);f.field->vtable_word=0;
        }else if(call.virtual_slot==0x64){
            check(call.vtable_word==0x22222222 && igb_bootstrap_read_word({f.storage,0},0x10)==0x8001a5a5,
                "Size callback did not follow physical ordinal publication and a fresh VT load.");
            raw_word(f.storage->bytes,0,0x33333333);f.field->vtable_word.reset();return MessengerCallResult{true,0x10007,{}};
        }else{
            check(call.virtual_slot==0x8c && call.vtable_word==0x33333333 && call.argument==0 &&
                igb_bootstrap_read_word({f.storage,0},0x14)==0x0007a5a5,
                "Default callback lost fresh physical VT, truncation or the zero argument.");
        }
        return MessengerCallResult{true,0,{}};
    };
    IgbFieldFinalizeInvocation call(f.field);check(call.resume(f.services).completed,"Physical dispatch did not complete.");
    check(!f.field->vtable_word && f.calls.size()==3 && f.polls==3,"A raw VT read refreshed the typed cache or replayed a callback.");
    check(call.resume().completed && f.polls==3,"Completed finalization replayed.");
}
void first_read_failures(){
    for(unsigned mode=0;mode<4;++mode){
        Fixture f;const auto original=f.storage->bytes;
        if(mode==0){original->bytes.clear();original->initialized.clear();}
        if(mode==1)raw_word(original,0,0);
        if(mode==2)original->initialized[2]=0;
        if(mode==3){original->bytes.resize(3);original->initialized.resize(3);}
        IgbFieldFinalizeInvocation call(f.field);
        const auto before=original->bytes,known=original->initialized;
        fails([&]{call.resume(f.services);},"Invalid physical VT reached a virtual callback.");
        check(call.faulted() && f.calls.empty() && f.polls==0 && original->bytes==before && original->initialized==known,
            "First physical read fault changed data or selected a callback.");
        original->bytes.assign(0x34,0xa5);original->initialized.assign(0x34,1);raw_word(original,0,0x11111111);
        fails([&]{call.resume(f.services);},"Restoring a failed VT resurrected the invocation.");
        check(f.calls.empty(),"Sticky VT fault selected a service after restoration.");
    }
}
void later_read_fault_prefix(){
    for(unsigned selected=0;selected<2;++selected){
        Fixture f;f.action=[&](const auto& call,auto&){
            if(call.virtual_slot==(selected?0x64u:0x5cu))f.storage->bytes->initialized[0]=0;
            return MessengerCallResult{true,0x1234,{}};
        };
        IgbFieldFinalizeInvocation call(f.field);fails([&]{call.resume(f.services);},"Later unknown VT was ignored.");
        check(f.calls.size()==selected+1 && f.polls==selected+1 &&
            igb_bootstrap_read_word({f.storage,0},0x10)==0xffffa5a5,
            "Fresh VT failure lost the already completed type-ordinal prefix.");
        check(igb_bootstrap_read_word({f.storage,0},0x14)==(selected?0x1234a5a5u:0xa5a5a5a5u),
            "Fresh VT failure moved the size store across its original dispatch boundary.");
    }
}
void retained_pending_callbacks(){
    for(unsigned mode=0;mode<2;++mode){
        Fixture f;f.checked=mode!=0;bool ready{};unsigned type_polls{};
        f.action=[&](const auto& request,auto&){
            if(request.virtual_slot==0x5c){++type_polls;check(request.vtable_word==0x11111111,"Pending call reloaded its selected VT.");
                if(!ready)return MessengerCallResult{false,0,"Prepared type callback"};}
            else check(request.vtable_word==0x22222222,"Subsequent dispatch did not freshly reload the physical VT.");
            return MessengerCallResult{true,4,{}};
        };
        IgbFieldFinalizeInvocation call(f.field);check(!call.resume(f.services).completed,"Prepared callback did not suspend.");
        raw_word(f.storage->bytes,0,0x22222222);f.field->vtable_word=0;
        f.selected->resume=[]()->MessengerCallResult{throw std::runtime_error("Replacement callback was selected.");};
        f.selected->resume_checked=[](const auto&)->MessengerCallResult{throw std::runtime_error("Replacement checked callback was selected.");};
        ready=true;check(call.resume(f.services).completed,"Retained callback did not resume.");
        check(type_polls==2 && f.calls.size()==3 && f.polls==4,"Pending finalization reselected or replayed a completed operation.");
    }
}
void identity_replacement_faults(){
    for(const auto selected_slot:{0x5cu,0x64u,0x8cu})for(unsigned mode=0;mode<4;++mode){
        Fixture f;bool ready{};f.action=[&](const auto& request,auto&){
            return MessengerCallResult{request.virtual_slot!=selected_slot || ready,4,"Prepared pending callback"};};
        IgbFieldFinalizeInvocation call(f.field);check(!call.resume(f.services).completed,"Identity fixture did not suspend.");
        const auto initial_polls=f.polls;
        const auto original=f.storage->bytes;const auto before=original->bytes,known=original->initialized;
        auto replacement=bytes(0x34);const auto bindings=f.storage->bindings;
        if(mode==0)f.storage->bytes=replacement;
        if(mode==1)f.storage->original_address+=0x1000;
        if(mode==2)f.field->physical_storage->offset=4;
        if(mode==3)f.storage->bindings.clear();
        check(!call.resume(f.services).completed && !call.faulted() && f.polls==initial_polls+1,
            "Pending selected callback eagerly revalidated its receiver before completing.");
        ready=true;fails([&]{call.resume(f.services);},"Changed captured physical identity reached a receiver store.");
        check(call.faulted() && f.polls==initial_polls+2 && original->bytes==before && original->initialized==known &&
            replacement->bytes==std::vector<std::uint8_t>(0x34,0xa5),"Identity failure lost the exact pending prefix.");
        f.storage->bytes=original;f.storage->original_address=0x120000;f.field->physical_storage->offset=0;f.storage->bindings=bindings;
        fails([&]{call.resume(f.services);},"Restoring physical identity cleared a latched fault.");
    }
}
void callback_fault_and_owner_retention(){
    Fixture f;IgbFieldFinalizeInvocation call(f.field);
    f.action=[&](const auto&,auto&){try{call.resume(f.services);}catch(const std::exception&){}return MessengerCallResult{true,4,{}};};
    fails([&]{call.resume(f.services);},"Caught recursive finalization continued to its first store.");
    check(f.polls==1 && igb_bootstrap_read_word({f.storage,0},0x10)==0xa5a5a5a5,"Caught recursion changed the physical store prefix.");

    std::weak_ptr<IgbScratchBuffer> old_bytes;std::weak_ptr<IgbFieldOperationsServices> weak_services;
    auto owner=std::make_shared<IgbFieldOperationsServices>();weak_services=owner;
    {
        Fixture local;local.services=owner;owner->factory=[](const auto&){auto operation=std::make_shared<IgbFieldOperation>();
            operation->resume=[] {return MessengerCallResult{false,0,"Prepared lifetime callback"};};return operation;};
        old_bytes=local.storage->bytes;
        {IgbFieldFinalizeInvocation abandoned(local.field);check(!abandoned.resume(owner).completed,"Lifetime fixture did not suspend.");
            local.storage->bytes=bytes(0x34);}
    }
    check(!old_bytes.expired() && !owner->unfinished.empty(),"Abandoning finalization discarded its captured physical byte owner.");
    owner.reset();check(weak_services.expired() && old_bytes.expired(),"Finalizer retention formed an ownership cycle.");
}
void canonical_only_profile(){
    Fixture f;f.field->physical_storage.reset();f.storage.reset();f.field->vtable_word.reset();
    IgbFieldFinalizeInvocation pending(f.field);check(!pending.resume(f.services).completed && f.calls.empty(),
        "Canonical-only unknown VT was fabricated or faulted.");
    f.field->vtable_word=0x45454545;check(pending.resume(f.services).completed && f.calls.size()==3,
        "Canonical-only late VT stopped using its established profile.");
    for(const auto& call:f.calls)check(call.vtable_word==0x45454545,"Canonical-only dispatch selected a different VT.");
    Fixture null;null.field->physical_storage.reset();null.field->vtable_word=0;
    IgbFieldFinalizeInvocation invalid(null.field);fails([&]{invalid.resume(null.services);},"Canonical-only null VT succeeded.");
    check(null.calls.empty(),"Canonical null VT selected a callback.");
}
void physical_global_pointer(){
    Fixture zero;auto stale=std::make_shared<IgbRuntimeFieldSlot>();zero.field->global_slot=stale;zero.field->global_slot_address=0x901000;
    IgbFieldFinalizeInvocation skip(zero.field);check(skip.resume(zero.services).completed && !stale->has_value(),
        "Physical null global pointer published through a stale nonnull canonical cell.");
    Fixture expired;{auto stale_owner=std::make_shared<IgbRuntimeFieldSlot>();expired.field->global_slot=stale_owner;}
    expired.field->global_slot_address=0x902000;
    IgbFieldFinalizeInvocation ignore(expired.field);check(ignore.resume(expired.services).completed,
        "Physical null global pointer dereferenced an expired stale owner.");

    for(unsigned mode=0;mode<2;++mode){
        Fixture f;if(mode==0){f.storage->bytes->bytes.resize(31);f.storage->bytes->initialized.resize(31);}
        else f.storage->bytes->initialized[0x1f]=0;
        IgbFieldFinalizeInvocation call(f.field);fails([&]{call.resume(f.services);},"Unavailable physical global pointer was treated as canonical null.");
        check(call.faulted() && f.calls.size()==3 && f.polls==3 &&
            igb_bootstrap_read_word({f.storage,0},0x10)==0xffffa5a5 && igb_bootstrap_read_word({f.storage,0},0x14)==0x0004a5a5,
            "Global pointer read fault lost the completed type/size/default prefix.");
    }

    Fixture late;constexpr std::uint32_t destination=0x903000;
    raw_word(late.storage->bytes,0x1c,destination);auto wrong=std::make_shared<IgbRuntimeFieldSlot>();
    auto right=std::make_shared<IgbRuntimeFieldSlot>();late.field->global_slot=wrong;late.field->global_slot_address=destination+4;
    IgbFieldFinalizeInvocation pending(late.field);check(!pending.resume(late.services).completed && !wrong->has_value() && late.polls==3,
        "Mismatched physical global address published into the stale cell.");
    // The original loaded this pointer once. The host mapping can arrive after
    // all receiver bytes are unavailable without restarting original accesses.
    raw_word(late.storage->bytes,0x1c,0);late.storage->bytes->initialized[0x1c]=0;
    late.field->global_slot_address.reset();
    check(!pending.resume(late.services).completed && late.polls==3 && !wrong->has_value(),
        "Missing late numeric mapping repeated the raw pointer read or callbacks.");
    late.field->global_slot_address=destination;late.field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
    check(!pending.resume(late.services).completed && late.polls==3,"Known-null owner satisfied a captured nonnull physical destination.");
    late.field->global_slot=right;check(pending.resume(late.services).completed && *right==late.field && !wrong->has_value() && late.polls==3,
        "Captured physical global destination was not published once its exact mapping arrived.");
    check(pending.resume().completed && late.polls==3,"Completed physical global publication replayed callbacks.");

    Fixture dead;raw_word(dead.storage->bytes,0x1c,destination);dead.field->global_slot_address=destination;
    {auto cell=std::make_shared<IgbRuntimeFieldSlot>();dead.field->global_slot=cell;}
    IgbFieldFinalizeInvocation fault(dead.field);fails([&]{fault.resume(dead.services);},"Matching expired physical global owner was silently accepted.");
    check(fault.faulted() && dead.polls==3,"Expired destination fault preceded completed finalization callbacks.");
}
}
int main(){try{physical_dispatch_and_fresh_reload();first_read_failures();later_read_fault_prefix();retained_pending_callbacks();
    identity_replacement_faults();callback_fault_and_owner_retention();canonical_only_profile();physical_global_pointer();
    std::cout<<"Field finalizer physical dispatch checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Field finalizer physical dispatch failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
