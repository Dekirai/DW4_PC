#include "dw4/igb_thread_defaults.hpp"

#include <algorithm>
#include <iostream>

namespace {
using namespace dw4;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
auto buffer(unsigned size,std::uint8_t value=0){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,value);b->initialized.assign(size,1);return b;}
auto metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(16);m->fields->backing_bytes=buffer(64);m->fields->backing_address=0x900000;return m;}
IgbBootstrapPointer bind_metadata(const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::uint32_t address){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=buffer(0x70,0xcc);storage->bindings.push_back({8,{},metadata});return {storage,8};
}
auto field(){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=0x804762fc;f->flag_34=0;f->flag_35=0x32;f->flag_3c=0x54;
    f->name_string=std::shared_ptr<IgbInternedString>{};f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->global_slot_address=0;
    f->default_bytes=IgbRuntimeFieldDefaultSource{buffer(4),0};f->default_bytes_address=0x700000;return f;}
IgbBootstrapPointer bind_field(const std::shared_ptr<IgbDecodeRuntimeField>& field,std::uint32_t address,unsigned size=0x44){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=buffer(size+8,0xcc);storage->bindings.push_back({8,{},{},{},{},field});
    field->physical_storage=IgbFieldStorageBinding{storage,8};IgbBootstrapPointer pointer{storage,8};
    igb_bootstrap_write_word(pointer,0,*field->vtable_word);igb_bootstrap_write_word(pointer,4,field->reference_word);
    igb_bootstrap_write_word(pointer,0x20,*field->default_bytes_address);igb_bootstrap_write_byte(pointer,0x34,*field->flag_34);
    igb_bootstrap_write_byte(pointer,0x35,*field->flag_35);if(size>0x3c)igb_bootstrap_write_byte(pointer,0x3c,*field->flag_3c);return pointer;
}
struct Fixture {
    std::shared_ptr<IgbThreadDefaultsServices> services=std::make_shared<IgbThreadDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> thread=metadata(),list=metadata(),semaphore=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRegistrationCallbackList> callbacks=std::make_shared<IgbRegistrationCallbackList>();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;std::vector<IgbBootstrapPointer> field_allocations;
    IgbBootstrapPointer thread_allocation=bind_metadata(thread,0x200000),list_allocation=bind_metadata(list,0x210000),semaphore_allocation=bind_metadata(semaphore,0x220000);
    std::function<void(unsigned,const std::shared_ptr<IgbDecodeRuntimeField>&)> field_action;
    std::function<void(const IgbDependenciesCall&)> name_action;
    std::function<void(const IgbRegistrationCallbacksCall&)> append_action;
    bool factory_ready=true,append_ready=true,physical=true;unsigned factory_selections{},factory_polls{},names{},append_polls{};
    Fixture(){
        thread->fields->count=2;thread->fields->backing->at(0)=field();thread->fields->backing->at(1)=field();
        services->thread=std::make_shared<IgbThreadRegistrationState>();services->thread->metadata=thread;
        services->physical_metadata={thread_allocation};services->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        services->dependencies->thread_list=std::make_shared<IgbRegistrationMetaSlot>(list);
        services->dependencies->semaphore=std::make_shared<IgbRegistrationMetaSlot>(semaphore);
        services->dependencies->allocations={list_allocation,semaphore_allocation};
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;services->dependencies->types=schema.types;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        auto manager=make_original_igb_memory_pool_registry();manager->original_pool_access_bound=true;manager->pools[5]=pool;
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=manager;services->dependencies->pools=schema.pools;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbDependenciesServiceKind::intern_name){++names;auto header=std::make_shared<IgbInternedString>();header->value=igb_dependency_name(c.name);header->container=pool;out->string_result=header;}
            else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected string endpoint.");
            if(name_action)name_action(c);return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        schema.fields->pointer_lists->values.push_back({igb_bootstrap_address(thread_allocation),thread,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
            else if(c.kind==IgbPointerListServiceKind::allocation_size)out->size_result=64;
            else throw std::runtime_error("Unexpected pointer-list growth.");return MessengerCallResult{true,0,{}};};return op;};
        schema.fields->factory=[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==IgbFieldOperationKind::allocation_pool)out->pool_result=pool;
            else if(c.kind==IgbFieldOperationKind::allocation_size)return MessengerCallResult{true,4,{}};
            else if(c.kind==IgbFieldOperationKind::field_virtual){
                if(c.virtual_slot==0x64)return MessengerCallResult{true,4,{}};
                check(c.virtual_slot==0x5c || c.virtual_slot==0x8c,"Unexpected field virtual.");
            }else throw std::runtime_error("Unexpected field endpoint.");return MessengerCallResult{true,0,{}};};return op;};
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==IgbFieldSchemaServiceKind::create_field && c.function==0x8002879c,"Original two-field factory table changed.");
            ++factory_selections;const auto index=static_cast<unsigned>(created.size());auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,index,out=op.get()]{
                ++factory_polls;if(!factory_ready)return MessengerCallResult{false,0,"prepared field constructor"};auto f=field();created.push_back(f);
                const auto address=0x300000u+index*0x100;if(physical)field_allocations.push_back(bind_field(f,address));
                out->field_result=f;out->field_address=address;if(field_action)field_action(index,f);return MessengerCallResult{true,0,{}};};return op;};
        callbacks->count=1;callbacks->capacity=4;callbacks->backing=std::make_shared<IgbRegistrationCallbackList::Storage>(4);callbacks->backing->at(0)=0x1234;
        core->bootstrap_callbacks=callbacks;core->registration_callback_pool=pool;core->registration_callbacks=std::make_shared<IgbRegistrationCallbackList>();
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=std::make_shared<IgbRuntimeTypesServices>();services->callbacks->runtime->singleton=core;
        services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[this,c,out=op.get()]{
            ++append_polls;if(!append_ready)return MessengerCallResult{false,0,"prepared callback allocation"};
            check(c.kind==IgbRegistrationCallbacksServiceKind::allocate_backing,"Defaults executed a queued callback or selected wrong append endpoint.");
            check(c.pool==pool && c.bytes==16,"Bootstrap append lost its original pool/capacity.");out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);
            if(append_action)append_action(c);return MessengerCallResult{true,0,{}};};return op;};
    }
};
void tables_and_complete(){
    Fixture f;const auto before=f.thread_allocation.storage->bytes->bytes;IgbThreadDefaultsInvocation call;
    check(call.resume(f.services).completed,"Thread defaults did not complete with represented dependencies.");
    check(f.thread->flag_18==1 && f.thread->factory_redirect==0x80023a10 && f.created.size()==2 && f.thread->fields->count==4,"Defaults metadata state or batch count changed.");
    check(igb_bootstrap_read_byte(f.thread_allocation,0x18)==1 && igb_bootstrap_read_word(f.thread_allocation,0x3c)==0x80023a10,"Metadata stores were not physically published.");
    for(unsigned i=0;i<2;++i){const auto& field=f.created[i];const auto target=i?f.semaphore:f.list;
        check(field->object_type && field->object_type->lock()==target && field->flag_34==1,"Local object type/construct flag differs.");
        check(field->flag_35==0x32 && field->flag_3c==0x54,"Defaults manufactured ownership flag changes.");
        check(igb_bootstrap_read_word(f.field_allocations[i],0x38)==(i?0x220000u:0x210000u) && igb_bootstrap_read_byte(f.field_allocations[i],0x34)==1,"Descriptor stores were not physically published.");
        check(igb_bootstrap_read_byte(f.field_allocations[i],0x35)==0x32 && igb_bootstrap_read_byte(f.field_allocations[i],0x3c)==0x54,"Descriptor store changed adjacent bytes.");
        check(*field->name==(i?"_sema":"_threadList") && field->offset==(i?12u:8u) && field->declaring_type->lock()==f.thread,"Actual schema did not preserve names, offsets and captured owner.");
        check(*f.services->globals->at(i)->slot==field && field->global_slot_address==(i?0x805614f0u:0x805614ecu),"Original descriptor globals were not finalized.");
        check(igb_bootstrap_read_word(f.field_allocations[i],0x18)==igb_bootstrap_address(f.thread_allocation) &&
            igb_bootstrap_read_word(f.field_allocations[i],8)==field->offset &&
            igb_bootstrap_read_word(f.field_allocations[i],0x1c)==*field->global_slot_address,"Thread schema did not publish its physical declaring-type, offset and global words.");
    }
    check(f.callbacks->count==2 && f.callbacks->backing->at(0)==0x1234 && f.callbacks->backing->at(1)==0x80023a50 && (*f.core->registration_callbacks)->count==0,"Defaults appended to the wrong callback list or executed its callback.");
    for(std::size_t i=0;i<before.size();++i)if(i!=8+0x18 && (i<8+0x3c || i>=8+0x40))check(before[i]==f.thread_allocation.storage->bytes->bytes[i],"Metadata store changed unrelated bytes.");
    check(call.resume().completed && f.factory_selections==2 && f.names==2 && f.callbacks->count==2,"Completed defaults replayed work.");
    Fixture canonical;canonical.physical=false;canonical.services->physical_metadata.clear();canonical.services->dependencies->allocations.clear();
    IgbThreadDefaultsInvocation host;check(host.resume(canonical.services).completed,"Explicit canonical-only fixtures incorrectly required invented physical addresses.");
}
void captured_receiver_and_fresh_redirect(){
    Fixture f;auto replacement=metadata();auto physical=bind_metadata(replacement,0x400000);f.services->physical_metadata.push_back(physical);
    f.factory_ready=false;IgbThreadDefaultsInvocation call;
    check(!call.resume(f.services).completed && f.thread->flag_18==1 && f.factory_selections==1 && f.created.empty(),"Suspended constructor lost volatile prefix.");
    f.services->thread->metadata=replacement;f.factory_ready=true;
    check(call.resume(f.services).completed && f.thread->fields->count==4 && replacement->fields->count==0,"Resumption used reloaded global as captured batch receiver.");
    check(!f.thread->factory_redirect && replacement->factory_redirect==0x80023a10 && !replacement->flag_18,"Redirect used captured receiver instead of fresh global.");
    check(igb_bootstrap_read_word(physical,0x3c)==0x80023a10 && igb_bootstrap_read_word(f.thread_allocation,0x3c)==0xcccccccc,"Physical redirect ignored canonical replacement.");
    check(f.factory_selections==2 && f.factory_polls==3 && f.created[0]->declaring_type->lock()==f.thread,"Pending batch reselected constructor or lost owner.");
    Fixture first;first.thread->fields->count=3;first.thread->fields->backing->at(2)=field();IgbThreadDefaultsInvocation dynamic;
    check(dynamic.resume(first.services).completed && first.thread->fields->count==5 && first.thread->fields->backing->at(3)==first.created[0],"Defaults assumed two inherited fields instead of reading count.");
}
void unknown_and_lazy_dependency_boundaries(){
    Fixture absent;absent.services->dependencies->thread_list->reset();IgbThreadDefaultsInvocation call;
    check(!call.resume(absent.services).completed && absent.created.size()==2 && !absent.created[0]->object_type && absent.created[0]->flag_34==0 && absent.names==0,"Unknown lazy metadata bypassed the reached getter or published flags early.");
    *absent.services->dependencies->thread_list=absent.list;absent.services->dependencies->semaphore->reset();
    check(!call.resume(absent.services).completed && absent.created[0]->object_type->lock()==absent.list && absent.created[0]->flag_34==1 && absent.created[1]->flag_34==0,"Second dependency suspension lost first descriptor stores.");
    *absent.services->dependencies->semaphore=absent.semaphore;
    check(call.resume(absent.services).completed && absent.factory_selections==2 && absent.names==2,"Late dependency globals replayed batch construction.");
    Fixture null;*null.services->dependencies->thread_list=std::shared_ptr<IgbRuntimeMetaType>{};IgbThreadDefaultsInvocation cold;
    const auto status=cold.resume(null.services);
    check(!status.completed && status.diagnostic.find("29E64")!=std::string::npos && null.created.size()==2 && !null.created[0]->object_type,"Known null dependency failed to reach actual metadata factory.");
    Fixture address;address.services->dependencies->allocations.clear();IgbThreadDefaultsInvocation delayed;
    check(!delayed.resume(address.services).completed && address.created.size()==2 && address.created[0]->flag_34==0,"Missing type address fabricated a physical pointer or published construct flag.");
    auto changed=metadata();*address.services->dependencies->thread_list=changed;
    address.services->schema->fields->pointer_lists->values.push_back({0x210000,address.list,{}});
    address.services->schema->fields->pointer_lists->values.push_back({0x220000,address.semaphore,{}});
    check(delayed.resume(address.services).completed && address.created[0]->object_type->lock()==address.list && address.factory_selections==2,"Late arithmetic binding reran the getter or replaced its captured result.");
}
void schema_and_append_suspension(){
    Fixture schema;schema.services->names->at(0).reset();IgbThreadDefaultsInvocation naming;
    check(!naming.resume(schema.services).completed && schema.created[0]->flag_34==1 && schema.created[1]->flag_34==1 && schema.names==0 && !schema.thread->factory_redirect,"Missing schema input skipped original type/flag prefix or published future redirect.");
    schema.services->names->at(0)=std::make_shared<const std::string>("_threadList");check(naming.resume(schema.services).completed && schema.names==2,"Late schema name did not resume.");
    Fixture append;append.callbacks->count=append.callbacks->capacity=0;append.callbacks->backing.reset();append.append_ready=false;IgbThreadDefaultsInvocation call;
    check(!call.resume(append.services).completed && append.thread->factory_redirect==0x80023a10 && append.callbacks->capacity==4 && append.callbacks->count==0 && append.append_polls==1,"Callback allocation pause lost redirect/capacity prefix.");
    auto alternate=std::make_shared<IgbRuntimeTypeRegistry>();alternate->bootstrap_callbacks=std::make_shared<IgbRegistrationCallbackList>();append.services->callbacks->runtime->singleton=alternate;
    append.append_ready=true;append.services->callbacks->factory={};
    check(call.resume(append.services).completed && append.callbacks->count==1 && append.callbacks->backing->at(0)==0x80023a50 && (*alternate->bootstrap_callbacks)->count==0 && append.append_polls==2,"Pending append replaced its captured list or operation.");
}
void faults_and_adapters(){
    Fixture null;null.services->thread->metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbThreadDefaultsInvocation bad;
    rejects([&]{bad.resume(null.services);},"Known null ThreadManager global was treated as absent.");check(bad.faulted() && null.created.empty(),"Null volatile store fault constructed fields.");
    Fixture metadata_fault;metadata_fault.thread_allocation.storage->bytes->bytes.resize(8+0x18);IgbThreadDefaultsInvocation short_metadata;
    rejects([&]{short_metadata.resume(metadata_fault.services);},"Truncated metadata byte store was accepted.");check(!metadata_fault.thread->flag_18 && metadata_fault.created.empty(),"Faulting physical metadata store published canonical state.");
    Fixture descriptor_fault;descriptor_fault.field_action=[&](unsigned i,const auto& f){if(i==0){const auto pointer=descriptor_fault.field_allocations[0];pointer.storage->bytes->bytes.resize(pointer.offset+0x38);}};
    IgbThreadDefaultsInvocation short_field;rejects([&]{short_field.resume(descriptor_fault.services);},"Truncated descriptor type store was accepted.");
    check(short_field.faulted() && descriptor_fault.created.size()==2 && descriptor_fault.created[0]->flag_34==0 && !descriptor_fault.created[0]->object_type,"Faulting type store published construct flag or canonical type.");
    Fixture recursive;IgbThreadDefaultsInvocation recursion;recursive.field_action=[&](unsigned,const auto&){try{recursion.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{recursion.resume(recursive.services);},"Swallowed recursive defaults were accepted.");check(recursion.faulted() && recursive.callbacks->count==1,"Recursive failure appended teardown callback.");
    Fixture delegated;unsigned calls{};const auto factory=make_native_igb_thread_defaults_factory(delegated.services,[&](const auto&)->std::shared_ptr<IgbTypeRegistrationOperation>{++calls;return {};});
    IgbTypeRegistrationCall call;call.kind=IgbTypeRegistrationServiceKind::defaults;call.function=0x80023940;call.metadata=metadata();
    const auto operation=factory(call);check(operation && operation->resume().completed && delegated.thread->flag_18==1 && !call.metadata->flag_18,"Defaults adapter used request metadata instead of canonical805614E8.");
    call.function=0x80023a30;check(!factory(call) && calls==1,"Defaults adapter consumed trailing ThreadManager startup callback.");
    auto services=std::make_shared<IgbThreadDefaultsServices>();const auto expired=make_native_igb_thread_defaults_factory(services);services.reset();call.function=0x80023940;
    rejects([&]{expired(call);},"Expired defaults services were replaced by synthetic state.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_thread_defaults_tests");
    tables_and_complete();captured_receiver_and_fresh_redirect();unknown_and_lazy_dependency_boundaries();schema_and_append_suspension();faults_and_adapters();
    std::cout<<"igb_thread_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Thread defaults failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
