#include "dw4/igb_object_defaults.hpp"
#include "dw4/igb_string_pool.hpp"

#include <iostream>

namespace {
using namespace dw4;using BK=IgbBootstrapServiceKind;using FK=IgbFieldOperationKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);storage->bytes->initialized.assign(size,1);return {storage,0};}
std::shared_ptr<IgbRuntimeMetaType> metadata(std::uint32_t address){auto out=std::make_shared<IgbRuntimeMetaType>();out->fields=std::make_shared<IgbDecodeFieldRegistry>();
    out->fields->backing_address=address;out->fields->backing_bytes=std::make_shared<IgbScratchBuffer>();out->fields->backing_bytes->bytes.resize(16);
    out->fields->backing_bytes->initialized.assign(16,0);out->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);return out;}
struct Fixture {
    std::shared_ptr<IgbObjectDefaultsServices> services=std::make_shared<IgbObjectDefaultsServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> meta=metadata(0x900000);
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbStringPoolServices> strings=std::make_shared<IgbStringPoolServices>();
    std::shared_ptr<IgbStringPoolState> string_state=std::make_shared<IgbStringPoolState>();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> physical;
    std::vector<IgbFieldOperationCall> field_calls;
    unsigned allocations{},name_allocations{},name_polls{},buffer_allocations{},buffer_polls{},size_queries{};
    bool name_ready=true,buffer_ready=true;
    std::function<void(const IgbPointerListCall&)> pointer_action;
    std::function<void(const IgbFieldOperationCall&,IgbFieldOperation&)> field_action;
    Fixture(){
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(meta);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->dependencies=std::make_shared<IgbDependenciesServices>();services->dependencies->globals=std::make_shared<SystemStageServices>();services->dependencies->globals->string_pool=pool;
        strings->mutex=std::shared_ptr<LoaderDirectoryMutex>{};string_state->table=std::make_shared<IgbStringTable>();string_state->table->capacity=4;
        string_state->table->storage=std::make_shared<IgbStringTable::Storage>(4);string_state->arena=pool;igb_bind_string_pool(strings,pool,string_state);
        strings->factory=[this](const IgbStringPoolCall& c){check(c.kind==IgbStringPoolServiceKind::allocate_string,"Unexpected string-table growth in fixture.");++name_allocations;
            auto op=std::make_shared<IgbStringPoolOperation>();op->resume=[this,c,out=op.get()]{++name_polls;if(!name_ready)return MessengerCallResult{false,0,"prepared name arena"};
                out->entry_result=std::make_shared<IgbStringEntry>();out->entry_result->owner=std::make_shared<IgbInternedString>();
                out->entry_result->bytes.buffer=std::make_shared<IgbScratchBuffer>();out->entry_result->bytes.buffer->bytes.resize(c.bytes+4);
                out->entry_result->bytes.buffer->initialized.assign(c.bytes+4,0);out->container_result=pool;return MessengerCallResult{true,0,{}};};return op;};
        services->dependencies->strings=strings->strings;services->dependencies->factory=make_native_igb_string_factory(strings);
        services->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();services->meta_fields->dependencies=services->dependencies;
        services->meta_fields->bootstrap=std::make_shared<IgbBootstrapServices>();services->meta_fields->bootstrap->runtime=services->types->runtime;
        services->meta_fields->bootstrap->factory=[this](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x34,"Specialized factory allocation changed.");
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,c,out=op.get()]{check(bool(c.pool),"Factory lost selected allocation pool.");++allocations;
                auto field=std::make_shared<IgbDecodeRuntimeField>();created.push_back(field);auto bytes=allocation(0x100000+allocations*0x100,0x34);
                bytes.storage->bindings.push_back({0,{},{},{},{},field});physical.push_back(bytes);out->allocation_result=bytes;return MessengerCallResult{true,0,{}};};return op;};
        services->fields=std::make_shared<IgbFieldOperationsServices>();services->fields->dependencies=services->dependencies;
        services->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool)out->pool_result=pool;
            else if(c.kind==PK::allocation_size){++size_queries;out->size_result=16;}
            else throw std::runtime_error("Prepared registry unexpectedly grew");if(pointer_action)pointer_action(c);return MessengerCallResult{true,0,{}};};return op;};
        services->fields->factory=make_native_igb_unsigned_int_field_factory(services->fields,[this](const IgbFieldOperationCall& c){
            field_calls.push_back(c);if(c.kind==FK::allocate_default)++buffer_allocations;
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==FK::object_pool){check(c.handle==0,"Specialized reset did not leave pool tag zero.");out->pool_result=pool;}
                else if(c.kind==FK::allocate_default){++buffer_polls;check(c.pool==pool && c.bytes==4 && c.alignment==1,"Native default allocation arguments differ.");
                    if(!buffer_ready)return MessengerCallResult{false,0,"prepared default allocator"};
                    auto bytes=std::make_shared<IgbScratchBuffer>();bytes->bytes.assign(4,0);bytes->initialized.assign(4,1);
                    out->buffer_result={bytes,0};out->buffer_address=0x200000+buffer_allocations*0x100;
                }else throw std::runtime_error("Unexpected field virtual or default-buffer operation");
                if(field_action)field_action(c,*out);return MessengerCallResult{true,0,{}};};return op;});
    }
};
void native_defaults(){
    Fixture f;IgbObjectDefaultsInvocation call;check(call.resume(f.services).completed && !call.faulted(),"Native specialized defaults did not complete.");
    check(f.created.size()==2 && f.allocations==2 && f.name_allocations==2 && f.buffer_allocations==2,"Native two-field caller allocation counts differ.");
    check(f.meta->fields->count==2 && *f.services->ref_count==f.created[0] && *f.services->pool_index==f.created[1],"Global publication or actual field append is missing.");
    for(unsigned i=0;i<2;++i){const auto& field=f.created[i];
        check(field->vtable_word==0x80476e6c && field->offset==4 && field->flag_26==0 && field->default_mode==3,"Specialized descriptor defaults were replaced by generic fields.");
        check(field->size==4 && field->type_ordinal==0xffff && field->reference_word==1,"Finalization or creator-reference release differs.");
        check(field->name && *field->name==(i==0?"_refCount":"_memPoolIndex") && field->name_string && (*field->name_string)->references==1,"Name ownership was not retained once after temporary release.");
        check(field->global_slot_address==(i==0?0x805622a8u:0x805622acu) && f.meta->fields->backing->at(i)==field,"Exact global binding or field identity differs.");
        check(igb_buffer_word(f.meta->fields->backing_bytes,i*4)==igb_bootstrap_address(f.physical[i]),"Raw append did not store the actual allocated pointer.");
    }
    check(call.resume().completed && f.allocations==2,"Completed defaults replayed.");
}
void pending_prefixes(){
    Fixture f;f.name_ready=false;IgbObjectDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.created.size()==1,"Pending name allocator did not retain first field.");
    check(f.created[0]->offset==4 && f.created[0]->global_slot_address==0x805622a8 && f.created[0]->default_mode==0 && f.created[0]->flag_26==1,"Name pause committed future flags or omitted earlier pointer stores.");
    check(!*f.services->ref_count && f.meta->fields->count==0,"Name pause published an unfinalized descriptor.");
    f.name_ready=true;f.buffer_ready=false;check(!call.resume(f.services).completed && f.name_allocations==1 && f.name_polls==2,"Retained name allocator replayed.");
    check(f.created[0]->default_mode==3 && f.created[0]->flag_26==0 && f.created[0]->size==4 && !*f.services->ref_count && f.meta->fields->count==0,"Default-buffer pause lost its reached prefix.");
    f.buffer_ready=true;check(call.resume(f.services).completed && f.allocations==2 && f.buffer_allocations==2 && f.buffer_polls==3,"Resuming finalization replayed creator or buffer allocation.");
    Fixture slots;slots.services->ref_count.reset();IgbObjectDefaultsInvocation missing;
    check(!missing.resume(slots.services).completed && slots.created[0]->offset==4 && slots.name_allocations==0,"Missing global cell skipped prior offset or read later name.");
    slots.services->ref_count=std::make_shared<IgbRuntimeFieldSlot>();check(missing.resume(slots.services).completed && slots.allocations==2,"Late canonical slot repeated field construction.");
}
void live_globals_and_failure(){
    Fixture f;auto second=metadata(0x910000);bool changed{};
    f.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size && !changed){changed=true;*f.services->object_metadata=second;
        f.services->types->metadata_pool_handle=7;f.manager->pools[5].reset();f.manager->pools[7]=f.pool;}};
    IgbObjectDefaultsInvocation call;check(call.resume(f.services).completed,"Live global replacement failed.");
    check(f.meta->fields->count==1 && second->fields->count==1 && f.meta->fields->backing->at(0)==f.created[0] && second->fields->backing->at(0)==f.created[1],"Append failed to capture first metadata then reload second global.");
    Fixture null;*null.services->object_metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbObjectDefaultsInvocation bad;
    rejects([&]{bad.resume(null.services);},"Represented null metadata was accepted.");
    check(bad.faulted() && null.allocations==1 && *null.services->ref_count==null.created[0] && null.created[0]->reference_word==1,"Null metadata fault skipped finalization or released the unappended creator.");
    rejects([&]{bad.resume(null.services);},"Faulted defaults replayed.");
}
void temporary_reference_prefix(){
    // Prepared interner headers isolate the caller's full32 temporary release.
    // Specialized factories, name assignment, finalization and append remain native.
    for(const auto reference:{0u,0xffffffffu}){
        Fixture f;unsigned releases{};bool release_ready{};std::shared_ptr<IgbInternedString> first;
        f.services->dependencies->factory=[&](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[&,c,out=op.get()]{if(c.kind==IgbDependenciesServiceKind::intern_name){auto string=std::make_shared<IgbInternedString>();string->value=igb_dependency_name(c.name);string->container=f.pool;string->references=reference;
                    if(!first)first=string;out->string_result=string;}
                else if(c.kind==IgbDependenciesServiceKind::release_string){++releases;if(!release_ready)return MessengerCallResult{false,0,"prepared header release"};}
                else throw std::runtime_error("Unexpected prepared string operation");return MessengerCallResult{true,0,{}};};return op;};
        IgbObjectDefaultsInvocation call;const auto status=call.resume(f.services);
        if(reference==0){check(!status.completed && first->references==0 && releases==1 && f.created[0]->default_mode==0,"Zero temporary count did not pause after committed decrement and before flags.");
            release_ready=true;check(call.resume(f.services).completed && releases==3 && first->references==0,"Retained temporary release replayed decrement or skipped second header.");}
        else check(status.completed && releases==0 && first->references==0xffffffffu,"Full32 string wrapping was replaced with packed object reference logic.");
    }
}
void destruction_and_adapter(){
    Fixture f;bool injected{},ready{};unsigned releases{};std::weak_ptr<IgbDecodeRuntimeField> weak;
    f.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size && !injected){injected=true;f.created[0]->reference_word=0x80000001;}};
    f.services->factory=[&](const IgbObjectDefaultsCall& c){auto op=std::make_shared<IgbObjectDefaultsOperation>();op->resume=[&,c]{++releases;weak=c.field;
        if(!ready)return MessengerCallResult{false,0,"prepared final field destruction"};return MessengerCallResult{true,0,{}};};return op;};
    IgbObjectDefaultsInvocation call;check(!call.resume(f.services).completed && releases==1 && f.created[0]->reference_word==0x80000000 && f.allocations==1,"Packed creator release boundary differs.");
    ready=true;check(call.resume(f.services).completed && releases==2 && f.allocations==2,"Retained field destructor replayed reference decrement.");
    check(!weak.expired(),"Completed field destruction discarded a still-published canonical owner.");
    Fixture abandoned;std::weak_ptr<IgbDecodeRuntimeField> retained;
    abandoned.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size)abandoned.created[0]->reference_word=0x80000001;};
    abandoned.services->factory=[&](const IgbObjectDefaultsCall& c){auto operation=std::make_shared<IgbObjectDefaultsOperation>();operation->resume=[&,c]{
        retained=c.field;*abandoned.services->ref_count=std::shared_ptr<IgbDecodeRuntimeField>{};
        abandoned.meta->fields->backing.reset();abandoned.services->fields->pointer_lists->values.clear();
        abandoned.created.clear();abandoned.physical.clear();abandoned.field_calls.clear();abandoned.services->meta_fields->completed_allocations.clear();
        return MessengerCallResult{false,0,"prepared abandoned destructor"};};return operation;};
    {IgbObjectDefaultsInvocation suspended;check(!suspended.resume(abandoned.services).completed,"Prepared final destructor did not suspend.");}
    check(!retained.expired(),"Abandoning a pending destructor lost its original selected field owner.");
    abandoned.services.reset();check(retained.expired(),"Abandoned field retention created a permanent service cycle.");
    Fixture adapter;auto factory=make_native_igb_object_defaults_factory(adapter.services);IgbTypeRegistrationCall request;
    request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x80066b7c;request.metadata=metadata(0x920000);
    const auto op=factory(request);check(op && op->resume().completed && adapter.meta->fields->count==2 && request.metadata->fields->count==0,"Defaults adapter incorrectly used the generic caller's metadata argument.");
    request.function=0x8002a164;check(!factory(request),"Adapter completed unrelated metadata defaults.");
    auto expired=std::make_shared<IgbObjectDefaultsServices>();auto stale=make_native_igb_object_defaults_factory(expired);expired.reset();request.function=0x80066b7c;
    rejects([&]{stale(request);},"Expired defaults services were silently recreated.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_object_defaults_tests");
    native_defaults();pending_prefixes();live_globals_and_failure();temporary_reference_prefix();destruction_and_adapter();
    std::cout<<"igb_object_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Object defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
