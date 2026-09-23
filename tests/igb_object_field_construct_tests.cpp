#include "dw4/igb_object_field_construct.hpp"

#include <iostream>

namespace {
using namespace dw4;using K=IgbObjectFieldConstructServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
std::uint32_t address(const IgbObjectInstance& object){return igb_bootstrap_address({object.storage,object.offset});}
IgbObjectInstance instance(std::uint32_t address,bool metadata=false){
    IgbObjectInstance object;object.storage=std::make_shared<IgbObjectStorage>();object.storage->original_address=address;
    object.storage->bytes=std::make_shared<IgbScratchBuffer>();object.storage->bytes->bytes.assign(0x74,0xcc);object.storage->bytes->initialized.assign(0x74,1);
    object.offset=8;
    if(metadata){object.metadata=std::make_shared<IgbRuntimeMetaType>();object.storage->bindings.push_back({8,{},object.metadata});}
    else {object.fields=std::make_shared<IgbDecodeFieldRegistry>();object.storage->bindings.push_back({8,{},{},object.fields});}
    igb_object_set_reference_word(object,1);return object;
}
std::shared_ptr<IgbRuntimeMetaType> schema(){
    auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->factory_redirect=0;metadata->allocation_prefix=8;
    metadata->native_size=0x10;metadata->default_pool_name=SystemStageString{};metadata->type_word=0x80474a5c;
    metadata->instance_count=0;metadata->instances=std::shared_ptr<IgbObjectInstanceList>{};metadata->fields=std::make_shared<IgbDecodeFieldRegistry>();
    metadata->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();return metadata;
}
struct Fixture {
    std::shared_ptr<IgbObjectFieldConstructServices> services=std::make_shared<IgbObjectFieldConstructServices>();
    std::shared_ptr<IgbDecodeRuntimeField> descriptor=std::make_shared<IgbDecodeRuntimeField>();
    IgbObjectInstance target=instance(0x100000,true),incoming=instance(0x200000),old=instance(0x300000);
    std::shared_ptr<IgbRuntimeMetaType> meta=schema(),base=schema();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>(),named=std::make_shared<ModelSceneData>();
    std::vector<K> trace;unsigned allocations{},destroyed{},pool_calls{},selected{};bool ready=true,allocation_ready=true;
    std::function<void(const IgbObjectFieldConstructCall&,IgbObjectFieldConstructOperation&)> action;
    std::function<void(const IgbFileFactoryCall&,IgbFileFactoryOperation&)> allocation_action;
    Fixture(){
        descriptor->offset=0x28;descriptor->flag_34=1;descriptor->flag_35=1;descriptor->flag_3c=0;descriptor->word_40=0;descriptor->object_type=meta;
        igb_object_set_reference_word(target,0x87000001);igb_bootstrap_write_word({target.storage,target.offset},0x28,0);target.metadata->fields_address=0;
        services->values=std::make_shared<IgbFieldValuesServices>();services->reflected=std::make_shared<IgbFileFactoryServices>();
        services->factory=[this](const IgbObjectFieldConstructCall& call){++selected;auto operation=std::make_shared<IgbObjectFieldConstructOperation>();
            operation->resume=[this,call,out=operation.get()]{trace.push_back(call.kind);
                if(call.kind==K::destroy_object){++destroyed;if(!ready)return MessengerCallResult{false,0,"prepared destructor"};}
                else {++pool_calls;if(!ready)return MessengerCallResult{false,0,"prepared pool"};out->pool_result=call.kind==K::named_pool?named:pool;}
                if(action)action(call,*out);return MessengerCallResult{true,0,{}};
            };return operation;};
        services->reflected->factory=[this](const IgbFileFactoryCall& call){auto operation=std::make_shared<IgbFileFactoryOperation>();
            operation->resume=[this,call,out=operation.get()]{
                check(call.kind==IgbFileFactoryServiceKind::allocate,"Member construction selected an unexpected reflected endpoint.");
                ++allocations;if(!allocation_ready)return MessengerCallResult{false,0,"prepared allocation"};
                check(call.pool==pool || call.pool==named,"Reflected factory lost the member's selected pool.");
                check(call.bytes==0x18,"Reflected factory lost metadata size/prefix arithmetic.");out->allocation_result=incoming.storage;
                if(allocation_action)allocation_action(call,*out);return MessengerCallResult{true,0,{}};
            };return operation;};
        auto initializer=std::make_shared<IgbObjectInitServices>();services->reflected->initializer=initializer;
        initializer->runtime=std::make_shared<IgbRuntimeTypesServices>();auto core=std::make_shared<IgbRuntimeTypeRegistry>();
        core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;initializer->runtime->singleton=core;initializer->object_metadata=base;
        initializer->factory=[this](const IgbObjectInitCall& call){auto operation=std::make_shared<IgbObjectInitOperation>();
            operation->resume=[this,call,out=operation.get()]{
                if(call.kind==IgbObjectInitServiceKind::object_virtual && call.virtual_slot==0x58)out->metadata_result=meta;
                return MessengerCallResult{true,0,{}};
            };return operation;};
    }
    std::uint32_t output(std::uint32_t offset=0x28)const{return igb_bootstrap_read_word({target.storage,target.offset},offset);}
    void old_member(){igb_bind_object_field_instance(services,old);igb_bootstrap_write_word({target.storage,target.offset},0x28,address(old));target.metadata->fields=old.fields;target.metadata->fields_address=address(old);}
    void name(const char* value){auto owner=std::make_shared<IgbInternedString>();owner->value=std::make_shared<const std::string>(value);
        igb_bind_field_string(services->values,0x400008,owner);descriptor->word_40=0x400008;}
};
void gates_and_nulls(){
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->flag_34=0;IgbObjectFieldConstructInvocation skipped(field,{});
    check(skipped.resume().completed && skipped.resume().completed,"False construction flag read target storage or services.");
    Fixture unknown;unknown.descriptor->flag_34.reset();IgbObjectFieldConstructInvocation gate(unknown.descriptor,unknown.target);
    check(!gate.resume(unknown.services).completed && unknown.trace.empty(),"Unknown construct flag was inferred false.");
    unknown.descriptor->flag_34=0;check(gate.resume().completed,"Late false construct flag failed.");
    Fixture abstract;abstract.meta->flag_1a=1;abstract.old_member();IgbObjectFieldConstructInvocation call(abstract.descriptor,abstract.target);
    check(call.resume(abstract.services).completed && abstract.output()==0 && abstract.allocations==0,"Abstract result did not run null member assignment.");
    check(abstract.old.fields->reference_word==0 && abstract.destroyed==1 && !abstract.target.metadata->fields && abstract.target.metadata->fields_address==0,
        "Null reflected result lost old release or canonical null publication.");
    Fixture null;null.descriptor->object_type=std::weak_ptr<IgbRuntimeMetaType>{};IgbObjectFieldConstructInvocation bad(null.descriptor,null.target);
    rejects([&]{bad.resume(null.services);},"Known null metadata was silently treated as absent.");check(bad.faulted() && null.pool_calls==1 && null.allocations==0,"Null metadata fault changed the reached pool prefix.");
}
void reflected_child_and_pool_order(){
    Fixture f;const auto before=f.target.storage->bytes->bytes;IgbObjectFieldConstructInvocation call(f.descriptor,f.target);
    f.action=[&](const auto& c,auto&){check(c.kind==K::tagged_pool && c.handle==0x87 && c.object.metadata==f.target.metadata,"Tagged pool did not use the target's physical high byte.");};
    check(call.resume(f.services).completed && f.output()==address(f.incoming),"Actual reflected child did not publish.");
    check(f.target.metadata->fields==f.incoming.fields && f.target.metadata->fields_address==address(f.incoming),"Canonical field-list identity did not follow physical publication.");
    check(f.incoming.fields->reference_word==1 && igb_object_reference_word(f.incoming)==1 && f.meta->instance_count==1,"Child retain/temporary release or actual initializer count changed.");
    check(f.allocations==1 && f.pool_calls==1 && f.services->objects.size()==1,"Construction replayed a service or failed to retain completed allocation.");
    for(std::size_t i=0;i<before.size();++i)if(i<f.target.offset+0x28 || i>=f.target.offset+0x2c)check(before[i]==f.target.storage->bytes->bytes[i],"Member construction changed unrelated bytes or padding.");
    check(call.resume(f.services).completed && f.allocations==1,"Completed member construction replayed allocation.");
    Fixture named;named.name("Objects");IgbObjectFieldConstructInvocation named_call(named.descriptor,named.target);
    named.action=[&](const auto& c,auto&){check(c.kind==K::named_pool && *igb_dependency_name(c.name)=="Objects","Named pool lost its captured character identity.");};
    check(named_call.resume(named.services).completed && named.trace==std::vector<K>{K::named_pool},"Named pool unnecessarily called tagged fallback.");
    Fixture missing;missing.name("Absent");missing.named.reset();IgbObjectFieldConstructInvocation fallback(missing.descriptor,missing.target);
    missing.action=[&](const auto& c,auto&){if(c.kind==K::named_pool)igb_object_set_reference_word(missing.target,0x12000001);
        else check(c.handle==0x12,"Named miss did not reload target pool tag after lookup.");};
    check(fallback.resume(missing.services).completed && missing.trace==std::vector<K>({K::named_pool,K::tagged_pool}),"Named miss/tagged fallback order changed.");
    Fixture empty;empty.name("");IgbObjectFieldConstructInvocation empty_call(empty.descriptor,empty.target);
    check(empty_call.resume(empty.services).completed && empty.trace==std::vector<K>{K::tagged_pool},"Empty first character did not select tagged pool.");
    Fixture byte_only;for(unsigned i=5;i<8;++i)byte_only.target.storage->bytes->initialized[byte_only.target.offset+i]=0;
    IgbObjectFieldConstructInvocation byte_call(byte_only.descriptor,byte_only.target);
    check(byte_call.resume(byte_only.services).completed && byte_only.trace==std::vector<K>{K::tagged_pool},"Target-pool selection read unneeded low reference bytes.");
}
void suspension_and_live_metadata(){
    Fixture f;f.ready=false;f.descriptor->object_type.reset();IgbObjectFieldConstructInvocation call(f.descriptor,f.target);
    check(!call.resume(f.services).completed && f.allocations==0 && f.selected==1,"Pending pool did not suspend before metadata.");
    f.ready=true;f.services->factory={};f.descriptor->word_40=99;
    check(!call.resume(f.services).completed && f.selected==1 && f.pool_calls==2,"Selected pool continuation was replaced or restarted.");
    f.descriptor->object_type=f.meta;f.allocation_ready=false;
    check(!call.resume(f.services).completed && f.allocations==1,"Late metadata did not enter actual reflected allocation.");
    f.descriptor->flag_34=0;f.descriptor->object_type=std::weak_ptr<IgbRuntimeMetaType>{};f.allocation_ready=true;
    check(call.resume(f.services).completed && f.allocations==2 && f.output()==address(f.incoming),"Pending child allocation re-read the already consumed gate or metadata.");
    Fixture later;later.name("Late");later.ready=false;IgbObjectFieldConstructInvocation name_call(later.descriptor,later.target);
    check(!name_call.resume(later.services).completed,"Named pool fixture did not suspend.");
    auto abstract=schema();abstract->flag_1a=1;later.descriptor->object_type=abstract;later.ready=true;
    check(name_call.resume(later.services).completed && later.allocations==0 && later.output()==0,"Descriptor metadata was captured before pool callback returned.");
    Fixture name_binding;name_binding.name("Captured");const auto saved=name_binding.services->values->strings.front();name_binding.services->values->strings.clear();
    IgbObjectFieldConstructInvocation captured_name(name_binding.descriptor,name_binding.target);
    check(!captured_name.resume(name_binding.services).completed && name_binding.trace.empty(),"Missing nonnull name binding was inferred empty.");
    name_binding.descriptor->word_40=0;name_binding.services->values->strings.push_back(saved);
    check(captured_name.resume(name_binding.services).completed && name_binding.trace==std::vector<K>{K::named_pool},"Late name binding reloaded a captured descriptor pointer.");
}
void assignment_order_and_offset_reload(){
    Fixture f;f.old_member();f.ready=false;IgbObjectFieldAssignInvocation assignment(f.descriptor,f.target,f.incoming);
    check(!assignment.resume(f.services).completed && f.output()==address(f.old) && f.incoming.fields->reference_word==2 && f.old.fields->reference_word==0,"Pending old destructor lost retain/release prefix or published early.");
    f.descriptor->offset=0x44;f.ready=true;f.services->factory={};
    check(assignment.resume(f.services).completed && f.destroyed==2 && f.selected==1 && f.incoming.fields->reference_word==2,"Assignment restarted a captured destructor or reference arithmetic.");
    check(f.output()==address(f.old) && f.output(0x44)==address(f.incoming) && f.target.metadata->word_44==address(f.incoming),"Assignment did not reload descriptor offset after old destruction.");
    Fixture unknown;unknown.old_member();unknown.services->objects.clear();IgbObjectFieldAssignInvocation missing(unknown.descriptor,unknown.target,unknown.incoming);
    check(!missing.resume(unknown.services).completed && unknown.incoming.fields->reference_word==2 && unknown.destroyed==0,"Unbound old pointer was treated as null or lost incoming retain.");
    igb_bind_object_field_instance(unknown.services,unknown.old);
    check(missing.resume(unknown.services).completed && unknown.incoming.fields->reference_word==2 && unknown.destroyed==1,"Late old binding repeated retain or skipped destructor.");
    Fixture old_capture;old_capture.old_member();old_capture.descriptor->flag_35.reset();IgbObjectFieldAssignInvocation captured(old_capture.descriptor,old_capture.target,old_capture.incoming);
    check(!captured.resume(old_capture.services).completed && old_capture.incoming.fields->reference_word==1,"Unknown ownership flag was inferred or incremented early.");
    igb_bootstrap_write_word({old_capture.target.storage,old_capture.target.offset},0x28,0);old_capture.descriptor->flag_35=1;
    check(captured.resume(old_capture.services).completed && old_capture.old.fields->reference_word==0 && old_capture.destroyed==1,"Late ownership flag reread the captured old destination pointer.");
}
void aliases_flags_and_live_temporary_release(){
    for(const auto count:{0u,1u,0x12000001u,0xffffffffu}){
        Fixture f;igb_object_set_reference_word(f.incoming,count);igb_bootstrap_write_word({f.target.storage,f.target.offset},0x28,address(f.incoming));
        IgbObjectFieldAssignInvocation alias(f.descriptor,f.target,f.incoming);check(alias.resume(f.services).completed,"Aliased member assignment failed.");
        check(f.incoming.fields->reference_word==count && f.destroyed==((count&0x7fffffu)==0?1u:0u),"Alias skipped full packed retain/decrement or low23 destruction.");
    }
    Fixture raw;raw.old_member();raw.descriptor->flag_35=0;IgbObjectFieldAssignInvocation nonowning(raw.descriptor,raw.target,raw.incoming);
    check(nonowning.resume(raw.services).completed && raw.old.fields->reference_word==1 && raw.incoming.fields->reference_word==1 && raw.destroyed==0,"Nonowning store changed packed references.");
    Fixture strong;strong.descriptor->flag_3c=1;strong.descriptor->flag_35.reset();IgbObjectFieldAssignInvocation conditional(strong.descriptor,strong.target,strong.incoming);
    check(conditional.resume(strong.services).completed && strong.incoming.fields->reference_word==2,"True flag3C incorrectly read unknown flag35.");
    Fixture mutated;mutated.old_member();mutated.action=[&](const auto& c,auto&){if(c.kind==K::destroy_object)mutated.descriptor->flag_35=0;};
    IgbObjectFieldConstructInvocation wrapper(mutated.descriptor,mutated.target);
    check(wrapper.resume(mutated.services).completed && mutated.incoming.fields->reference_word==2 && mutated.destroyed==1,"Wrapper reused assignment ownership flags instead of reloading after old destructor.");
    Fixture zero;zero.old_member();zero.action=[&](const auto& c,auto&){if(c.kind==K::destroy_object && c.object.fields==zero.old.fields)igb_object_set_reference_word(zero.incoming,0x7a000001);};
    IgbObjectFieldConstructInvocation last(zero.descriptor,zero.target);
    check(last.resume(zero.services).completed && zero.destroyed==2 && zero.incoming.fields->reference_word==0x7a000000,"Temporary release skipped low23 destruction with pool bits present.");
}
void faults_and_lifetime(){
    Fixture invalid;invalid.target.storage->bytes->initialized[invalid.target.offset+0x28]=0;IgbObjectFieldAssignInvocation fault(invalid.descriptor,invalid.target,invalid.incoming);
    rejects([&]{fault.resume(invalid.services);},"Unreadable old destination was accepted.");check(fault.faulted() && invalid.incoming.fields->reference_word==1,"Old destination fault occurred after incoming retain.");
    Fixture rebound;rebound.old_member();rebound.ready=false;IgbObjectFieldAssignInvocation binding(rebound.descriptor,rebound.target,rebound.incoming);
    check(!binding.resume(rebound.services).completed,"Prepared destructor did not suspend before binding mutation.");
    rebound.target.storage->bindings.front().metadata=std::make_shared<IgbRuntimeMetaType>();rebound.ready=true;
    rejects([&]{binding.resume(rebound.services);},"Changed physical target identity was accepted.");check(binding.faulted() && rebound.destroyed==1,"Binding fault resumed the captured destructor.");
    Fixture recursive;IgbObjectFieldConstructInvocation recursion(recursive.descriptor,recursive.target);
    recursive.action=[&](const auto&,auto&){try{recursion.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursion.resume(recursive.services);},"Swallowed recursive entry did not poison outer member construction.");
    check(recursion.faulted() && recursive.allocations==0 && recursive.output()==0,"Recursive pool fault ran allocation or publication.");
    Fixture abandoned;abandoned.ready=false;std::weak_ptr<IgbObjectStorage> allocation=abandoned.target.storage;
    {IgbObjectFieldConstructInvocation pending(abandoned.descriptor,abandoned.target);check(!pending.resume(abandoned.services).completed,"Prepared abandoned pool did not pause.");}
    abandoned.target.storage.reset();check(!allocation.expired(),"Abandoned member operation lost target lifetime.");abandoned.services.reset();check(allocation.expired(),"Abandoned member operation retained a permanent services cycle.");
    Fixture completed;std::weak_ptr<IgbObjectStorage> child=completed.incoming.storage;
    {IgbObjectFieldConstructInvocation call(completed.descriptor,completed.target);check(call.resume(completed.services).completed,"Completed allocation fixture failed.");}
    completed.incoming.storage.reset();check(!child.expired(),"Published field-list identity lost its physical allocation lifetime.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_object_field_construct_tests");
    gates_and_nulls();reflected_child_and_pool_order();suspension_and_live_metadata();assignment_order_and_offset_reload();aliases_flags_and_live_temporary_release();faults_and_lifetime();
    std::cout<<"igb_object_field_construct_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Object field construction failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
