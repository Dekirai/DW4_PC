#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_field_defaults.hpp"
#include "dw4/igb_field_member_copy.hpp"
#include "dw4/igb_memory_member_copy.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;using Kind=IgbRuntimeObjectKind;using K=IgbObjectInitServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(unsigned count,bool initialized=false){auto out=std::make_shared<IgbScratchBuffer>();out->bytes.assign(count,0xcc);out->initialized.assign(count,initialized?1:0);return out;}
IgbObjectInstance runtime(Kind kind,std::uint32_t address=0x100000){
    IgbObjectInstance out;out.storage=std::make_shared<IgbObjectStorage>();out.storage->original_address=address;out.offset=8;out.storage->bytes=bytes(0x48);
    out.runtime_object=std::make_shared<IgbRuntimeObject>(kind);out.storage->bindings.push_back({8,{},{},{},{},{},out.runtime_object});return out;
}
auto schema(){auto out=std::make_shared<IgbRuntimeMetaType>();out->type_word=0x804700a8;out->native_size=0x10;out->allocation_prefix=8;
    out->factory_redirect=0;out->class_flags=4;out->instance_count=0;out->instances=std::shared_ptr<IgbObjectInstanceList>{};
    out->fields=std::make_shared<IgbDecodeFieldRegistry>();out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();return out;}
struct Fixture {
    IgbObjectInstance object=runtime(Kind::thread_manager);
    std::shared_ptr<IgbRuntimeMetaType> type=schema(),base=schema();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbObjectInitServices> services=std::make_shared<IgbObjectInitServices>();
    std::vector<IgbObjectInitCall> calls;unsigned polls{};bool ready{};
    Fixture(){
        core->vtable_offset=0;core->field_initialization_enabled=1;core->enabled=1;
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;services->object_metadata=base;
        // Prepared object virtuals isolate common667F4. Real manager/list tails
        // are separate bodies; a missing+2C remains pending until supplied.
        services->factory=make_native_igb_scalar_field_factory([this](const IgbObjectInitCall& c){calls.push_back(c);
            auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[this,c,out=op.get()]{
                check(c.object.runtime_object==object.runtime_object && !c.object.file && !c.object.field && !c.object.metadata && !c.object.fields && !c.object.types,
                    "Runtime initializer changed its concrete native identity.");
                if(c.kind==K::object_virtual && c.virtual_slot==0x58)out->metadata_result=type;
                if(c.kind==K::object_virtual && c.virtual_slot==0x2c){++polls;return MessengerCallResult{ready,0x55,"prepared runtime tail"};}
                return MessengerCallResult{true,0,{}};
            };return op;});
    }
};
void initializer_and_reflection(){
    for(const auto kind:{Kind::thread_manager,Kind::thread_list,Kind::thread,Kind::semaphore}){
        Fixture f;f.object=runtime(kind);IgbObjectInitInvocation run(f.object.storage,8,f.type);
        const auto status=run.resume(f.services);
        check(!status.completed && status.value==0x55 && f.polls==1 && !f.object.runtime_object->construction_active_.expired() &&
            f.object.runtime_object->physical_storage && f.object.runtime_object->physical_storage->storage.lock()==f.object.storage &&
            f.object.runtime_object->physical_storage->offset==8 && f.object.runtime_object->kind==kind,
            "Runtime construction lost its reached pending body, kind or physical lease.");
        check(igb_bootstrap_runtime_object({f.object.storage,8})==f.object.runtime_object && f.object.runtime_object->vtable_word==0x804700a8 &&
            f.object.runtime_object->reference_word==1 && igb_object_reference_word(f.object)==1 && f.object.storage->bytes->initialized[16]==0 &&
            f.object.storage->bytes->initialized[20]==0,"Common runtime initialization fabricated derived members or lost header synchronization.");
        rejects([&]{run.result_instance();},"Pending runtime initialization exposed a completed owner.");
        const auto selections=f.calls.size();f.ready=true;
        check(run.resume(f.services).completed && f.polls==2 && f.calls.size()==selections+2 && run.result_instance().runtime_object==f.object.runtime_object &&
            f.object.runtime_object->construction_active_.expired() && !f.object.runtime_object->construction_fault_ && f.type->instance_count==1,
            "Runtime continuation repeated a selection, lost its typed result or retained a completed lease.");
        rejects([&]{run.result();},"Runtime owner was returned as a null file.");
        check(run.resume().completed && f.type->instance_count==1,"Completed runtime constructor replayed its instance count.");
    }
    Fixture reflected;auto services=std::make_shared<IgbFileFactoryServices>();services->initializer=reflected.services;
    auto pool=std::make_shared<ModelSceneData>();unsigned allocations{};
    services->factory=[&](const IgbFileFactoryCall& c){check(c.kind==IgbFileFactoryServiceKind::allocate && c.bytes==0x18 && c.metadata==reflected.type && c.pool==pool,
            "Runtime reflected allocation changed its size, prefix or pool.");
        auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{++allocations;out->allocation_result=reflected.object.storage;return MessengerCallResult{true,0,{}};};return op;};
    IgbReflectedFactoryInvocation factory(reflected.type,pool);
    check(!factory.resume(services).completed && allocations==1,"Runtime reflected factory skipped its initializer body.");reflected.ready=true;
    check(factory.resume(services).completed && allocations==1 && factory.result_instance().runtime_object==reflected.object.runtime_object,
        "Runtime reflected result lost the actual allocation identity.");
    rejects([&]{factory.result();},"Runtime reflected result was returned as a null file.");
}
void wrong_bindings_and_faults(){
    rejects([]{IgbRuntimeObject invalid(static_cast<Kind>(99));},"Unknown runtime layout kind was accepted.");
    for(unsigned mode=0;mode<3;++mode){Fixture f;
        if(mode==0)f.object.storage->bindings.front().file=std::make_shared<LoaderRequestFile>();
        else if(mode==1)f.object.storage->bindings.push_back({8,{},{},{},{},{},std::make_shared<IgbRuntimeObject>(Kind::thread_manager)});
        else f.object.storage->bindings.push_back({8,{},{},{},{},std::make_shared<IgbDecodeRuntimeField>()});
        rejects([&]{igb_bootstrap_runtime_object({f.object.storage,8});},"Bootstrap getter accepted conflicting runtime owners.");
        IgbObjectInitInvocation run(f.object.storage,8,f.type);rejects([&]{run.resume(f.services);},"Initializer accepted mixed or duplicate runtime owners.");
        check(!f.object.runtime_object->construction_fault_ && f.object.runtime_object->construction_active_.expired() &&
            std::all_of(f.object.storage->bytes->initialized.begin(),f.object.storage->bytes->initialized.end(),[](auto b){return b==0;}),
            "Rejected binding changed bytes or acquired a runtime construction lease.");
    }
    Fixture duplicate;duplicate.object.storage->bindings.push_back(duplicate.object.storage->bindings.front());duplicate.ready=true;
    IgbObjectInitInvocation consistent(duplicate.object.storage,8,duplicate.type);
    check(consistent.resume(duplicate.services).completed,"Consistent duplicate runtime bindings were rejected.");
    Fixture competing;IgbObjectInitInvocation original(competing.object.storage,8,competing.type);
    check(!original.resume(competing.services).completed,"Competing runtime fixture did not pause.");
    IgbObjectInitInvocation second(competing.object.storage,8,competing.type);
    rejects([&]{second.resume(competing.services);},"Competing runtime constructor acquired an active allocation.");
    check(!competing.object.runtime_object->construction_fault_ && !competing.object.storage->faulted(),"Rejected competing constructor poisoned the original lease.");
    competing.ready=true;check(original.resume(competing.services).completed,"Original runtime lease failed after rejecting its competitor.");
    Fixture abandoned;{IgbObjectInitInvocation unfinished(abandoned.object.storage,8,abandoned.type);check(!unfinished.resume(abandoned.services).completed,"Abandoned fixture did not pause.");}
    check(abandoned.object.runtime_object->construction_fault_ && abandoned.object.runtime_object->construction_active_.expired() && abandoned.object.storage->faulted(),
        "Abandoned runtime construction did not preserve its fault prefix or release the lease.");
    for(unsigned mutation=0;mutation<4;++mutation){Fixture f;IgbObjectInitInvocation run(f.object.storage,8,f.type);
        check(!run.resume(f.services).completed,"Runtime mutation fixture did not pause.");const auto prefix=f.object.storage->bytes->bytes;
        if(mutation==0)f.object.runtime_object->physical_storage.reset();
        if(mutation==1)f.object.runtime_object->physical_storage->offset=12;
        if(mutation==2)f.object.runtime_object->physical_storage->storage=std::make_shared<IgbObjectStorage>();
        if(mutation==3)f.object.storage->bindings.front().runtime_object=std::make_shared<IgbRuntimeObject>(Kind::thread_manager);
        f.ready=true;rejects([&]{run.resume(f.services);},"Changed or expired runtime physical binding resumed initialization.");
        check(f.object.runtime_object->construction_fault_ && f.object.storage->faulted() && f.object.storage->bytes->bytes==prefix && f.polls==1,
            "Changed runtime binding executed a later body or lost its physical fault prefix.");
    }
    Fixture prior;auto other=std::make_shared<IgbObjectStorage>();prior.object.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{other,8};
    IgbObjectInitInvocation wrong(prior.object.storage,8,prior.type);rejects([&]{wrong.resume(prior.services);},"Existing runtime physical identity was redirected by a constructor.");
    check(!prior.object.runtime_object->construction_fault_ && prior.object.runtime_object->physical_storage->storage.lock()==other,
        "Rejected runtime rebinding changed its previous owner.");
}
void allocation_fault_without_runtime_owner_fault(){
    for(const bool abandon:{false,true}){
        auto object=runtime(Kind::thread_manager);
        object.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{object.storage,object.offset};
        const IgbBootstrapPointer alias{object.storage,object.offset};
        igb_bootstrap_write_word(alias,0,0x804700a8);igb_object_set_reference_word(object,0xa5000002);
        // The raw base constructor owns the shared allocation, not the runtime
        // construction lease. Its failure must invalidate every typed alias
        // even when the runtime owner's independent fault flag remains clear.
        {
            IgbBootstrapBaseInvocation base(alias,true);
            if(abandon){
                check(!base.resume().completed && !base.faulted() && !object.storage->faulted(),
                    "Base alias did not pause after its physical header prefix.");
            }else{
                auto services=std::make_shared<IgbBootstrapServices>();
                services->runtime=std::make_shared<IgbRuntimeTypesServices>();
                services->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
                rejects([&]{base.resume(services);},"Known null ArkCore did not fault the base alias constructor.");
                check(base.faulted(),"Failed base alias lost its constructor fault.");
            }
        }
        check(object.storage->faulted() && !object.runtime_object->construction_fault_ &&
            object.runtime_object->construction_active_.expired() && object.runtime_object->physical_storage &&
            object.runtime_object->physical_storage->storage.lock()==object.storage,
            "Base alias failure did not isolate allocation fault from the live runtime owner identity.");
        check(igb_bootstrap_read_word(alias,0)==0x804717f0 && igb_bootstrap_read_word(alias,4)==1 &&
            object.runtime_object->reference_word==1,
            "Fault fixture erased the already executed base header prefix.");
        rejects([&]{igb_object_validate_instance(object);},"Runtime identity accepted independently faulted allocation storage.");
        rejects([&]{igb_object_reference_word(object);},"Runtime reference read ignored a faulted storage alias.");
        const auto physical=object.storage->bytes->bytes;
        rejects([&]{igb_object_set_reference_word(object,0x12345678);},"Runtime reference write ignored a faulted storage alias.");
        check(object.storage->bytes->bytes==physical && object.runtime_object->reference_word==1,
            "Rejected reference access changed the faulted allocation prefix.");
    }
}
void fresh_null_vtable_stops_before_dispatch(){
    Fixture f;f.ready=true;const auto prepared=f.services->factory;unsigned mutations{};
    f.services->factory=[&](const IgbObjectInitCall& call){
        const auto operation=prepared(call);
        if(call.kind==K::object_virtual && call.virtual_slot==0x0c){
            const auto callback=operation->resume;
            operation->resume=[&,callback]{
                const auto result=callback();
                if(result.completed){++mutations;igb_bootstrap_write_word({f.object.storage,f.object.offset},0,0);}
                return result;
            };
        }
        return operation;
    };
    IgbObjectInitInvocation run(f.object.storage,f.object.offset,f.type);
    rejects([&]{run.resume(f.services);},"Fresh null object vtable selected a later prepared endpoint.");
    check(mutations==1 && run.faulted() && !run.completed() && f.object.storage->faulted() &&
        f.object.runtime_object->construction_fault_ && igb_bootstrap_read_word({f.object.storage,f.object.offset},0)==0,
        "Null-vtable continuation did not retain its physical effect and both construction faults.");
    check(f.calls.size()==5 && f.calls.back().kind==K::object_virtual && f.calls.back().virtual_slot==0x0c &&
        std::count_if(f.calls.begin(),f.calls.end(),[](const auto& call){return call.kind==K::object_virtual && call.virtual_slot==0x58;})==3 &&
        std::none_of(f.calls.begin(),f.calls.end(),[](const auto& call){return call.kind==K::object_virtual && call.vtable_word==0;}) &&
        f.polls==0 && f.type->instance_count==0,
        "Null vtable was detected after a later virtual, tail or instance publication.");
    const auto selections=f.calls.size();
    rejects([&]{run.resume(f.services);},"Faulted null-vtable constructor restarted.");
    check(f.calls.size()==selections && mutations==1,"Retry replayed the callback that invalidated the vtable.");
}
void header_and_member_synchronization(){
    auto object=runtime(Kind::thread_manager);object.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{object.storage,8};
    igb_bootstrap_write_word({object.storage,8},0,0x804700a8);igb_bootstrap_write_word({object.storage,8},4,0xa5000001);
    check(object.runtime_object->vtable_word==0x804700a8 && object.runtime_object->reference_word==0xa5000001,"Bootstrap stores left the runtime header stale.");
    igb_bootstrap_write_byte({object.storage,8},5,0x80);check(object.runtime_object->reference_word==0xa5800001,"Partial physical reference store failed to synchronize the runtime header.");
    igb_object_set_reference_word(object,0xb6000002);check(object.runtime_object->reference_word==0xb6000002 && igb_object_reference_word(object)==0xb6000002,
        "Common reference writer lost runtime physical/canonical synchronization.");
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->offset=4;field->size=4;field->vtable_word=0x80476e6c;
    auto defaults=bytes(4,true);defaults->bytes={0x11,0x22,0x33,0x44};field->default_bytes=IgbRuntimeFieldDefaultSource{defaults,0};
    IgbObjectInitCall request;request.kind=K::field_virtual;request.field=field;request.object=object;request.virtual_slot=0x70;
    check(make_native_igb_scalar_field_factory()(request)->resume().completed && object.runtime_object->reference_word==0x11223344,
        "Raw scalar defaults did not synchronize a runtime header alias.");
    auto source=runtime(Kind::thread,0x200000);source.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{source.storage,8};
    igb_bootstrap_write_word({source.storage,8},0,0x55667788);igb_bootstrap_write_word({source.storage,8},4,0x88776655);
    field->offset=0;field->size=8;IgbFieldMemberCopyInvocation copy(field,object,source,0x800642e4);
    check(copy.resume().completed && object.runtime_object->vtable_word==0x55667788 && object.runtime_object->reference_word==0x88776655,
        "Physical runtime member copy left either common header word stale.");
    auto alias=object;alias.runtime_object.reset();igb_bootstrap_write_word({source.storage,8},0,0x12345678);
    field->size=4;IgbFieldMemberCopyInvocation raw_alias(field,alias,source,0x800642e4);
    check(raw_alias.resume().completed && object.runtime_object->vtable_word==0x12345678,"Raw copy through an untyped alias missed its runtime header binding.");
    auto memory=std::make_shared<IgbDecodeRuntimeField>();memory->offset=4;memory->flag_42=0;memory->flag_40=0;
    memory->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};igb_object_set_reference_word(source,0);
    IgbMemoryMemberCopyInvocation memory_alias(memory,object,source);
    check(memory_alias.resume().completed && object.runtime_object->reference_word==0 && igb_object_reference_word(object)==0,
        "Memory pointer publication through a runtime header alias lost canonical synchronization.");
    source.runtime_object->physical_storage.reset();IgbMemoryMemberCopyInvocation lost_binding(memory,object,source);
    rejects([&]{lost_binding.resume();},"Memory member access accepted a runtime owner after losing its physical binding.");
    auto conflict=object;conflict.runtime_object=std::make_shared<IgbRuntimeObject>(Kind::thread_manager);
    auto objects=std::make_shared<IgbObjectFieldConstructServices>();igb_bind_object_field_instance(objects,object);
    rejects([&]{igb_bind_object_field_instance(objects,conflict);},"Object registry accepted another runtime owner at the same physical identity.");
    object.runtime_object->physical_storage.reset();rejects([&]{igb_object_reference_word(object);},"Runtime reference access silently lost its physical identity.");
}
void object_assignment(){
    auto target=runtime(Kind::thread_manager),incoming=runtime(Kind::semaphore,0x200000),old=runtime(Kind::thread_list,0x300000);
    for(const auto& object:{target,incoming,old}){object.runtime_object->physical_storage=IgbRuntimeObjectStorageBinding{object.storage,8};
        igb_bootstrap_write_word({object.storage,8},0,0x804700a8);igb_object_set_reference_word(object,1);}
    igb_bootstrap_write_word({target.storage,8},12,igb_bootstrap_address({old.storage,old.offset}));
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->offset=12;field->flag_3c=1;
    auto services=std::make_shared<IgbObjectFieldConstructServices>();igb_bind_object_field_instance(services,old);
    unsigned selections{},polls{};bool ready{};
    services->factory=[&](const IgbObjectFieldConstructCall& c){++selections;
        check(c.kind==IgbObjectFieldConstructServiceKind::destroy_object && c.object.runtime_object==old.runtime_object,
            "Runtime assignment selected another destructor owner.");
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared old runtime destruction"};};return op;};
    IgbObjectFieldAssignInvocation run(field,target,incoming);
    check(!run.resume(services).completed && incoming.runtime_object->reference_word==2 && old.runtime_object->reference_word==0 && polls==1 &&
        igb_bootstrap_read_word({target.storage,8},12)==igb_bootstrap_address({old.storage,old.offset}),
        "Runtime assignment lost retain/release ordering before its pending old destructor.");
    ready=true;services->factory={};
    check(run.resume(services).completed && selections==1 && polls==2 && incoming.runtime_object->reference_word==2 && old.runtime_object->reference_word==0 &&
        igb_bootstrap_read_word({target.storage,8},12)==igb_bootstrap_address({incoming.storage,incoming.offset}) && services->objects.back().runtime_object==incoming.runtime_object,
        "Runtime assignment repeated ownership work or lost the published object's identity.");
}
}
int main(){try{initializer_and_reflection();wrong_bindings_and_faults();allocation_fault_without_runtime_owner_fault();
    fresh_null_vtable_stops_before_dispatch();header_and_member_synchronization();object_assignment();
    std::cout<<"igb_runtime_object_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Runtime object tests failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
