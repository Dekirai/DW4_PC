#include "dw4/igb_named_type_defaults.hpp"
#include "dw4/igb_stream.hpp"

#include <bit>
#include <iostream>

namespace {
using namespace dw4;using Kind=IgbNamedTypeKind;using FK=IgbFieldOperationKind;using SK=IgbFieldSchemaServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f,const char* message){++checks;try{f();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
auto bytes(std::size_t count,std::uint8_t fill=0){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(count,fill);b->initialized.assign(count,1);return b;}
auto metadata(){auto m=std::make_shared<IgbRuntimeMetaType>();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);m->fields->backing_bytes=bytes(128);m->fields->backing_address=0x900000;return m;}
IgbBootstrapPointer metadata_storage(const std::shared_ptr<IgbRuntimeMetaType>& m,std::uint32_t address=0x600000){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;storage->bytes=bytes(0x70,0xcc);
    storage->bindings.push_back({8,{},m});return {storage,8};
}
struct Fixture {
    Kind kind;
    std::shared_ptr<IgbNamedTypeDefaultsServices> services=std::make_shared<IgbNamedTypeDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata();
    std::shared_ptr<IgbRegistrationMetaSlot> cell=std::make_shared<IgbRegistrationMetaSlot>(target);
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::uint32_t> constructors;
    std::vector<IgbFieldOperationCall> scalar_calls;
    std::function<void(const IgbFieldOperationCall&)> scalar_before,scalar_after;
    std::function<void()> created_action,destroy_action,name_action;
    bool create_ready=true,scalar_ready=true,name_ready=true,physical=true;
    unsigned create_polls{},scalar_polls{},name_polls{},null_names{},names{},destroys{},allocations_count{};
    explicit Fixture(Kind k=Kind::thread):kind(k){
        services->registration=std::make_shared<IgbNamedTypeRegistrationServices>();
        services->registration->named_metadata=kind==Kind::named_object?cell:std::make_shared<IgbRegistrationMetaSlot>();
        services->registration->thread_metadata=kind==Kind::thread?cell:std::make_shared<IgbRegistrationMetaSlot>();
        services->registration->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        services->registration->dependencies->semaphore=kind==Kind::semaphore?cell:std::make_shared<IgbRegistrationMetaSlot>();
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;
        schema.types=std::make_shared<IgbTypeRegistrationServices>();schema.types->metadata_pool_handle=5;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*schema.pools->memory_pools->fallback)->original_pool_access_bound=true;(*schema.pools->memory_pools->fallback)->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();
        schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{
                if(c.kind==IgbDependenciesServiceKind::intern_name){++name_polls;if(!name_ready)return MessengerCallResult{false,0,"prepared string intern"};
                    const auto name=igb_dependency_name(c.name);if(!name){++null_names;out->string_result.reset();}
                    else{++names;auto header=std::make_shared<IgbInternedString>();header->value=name;header->container=pool;out->string_result=header;}
                    if(name_action)name_action();
                }else check(c.kind==IgbDependenciesServiceKind::release_string,"Unexpected string endpoint.");
                return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        // Matches metadata_storage(target): allocation base600000, object+8.
        schema.fields->pointer_lists->values.push_back({0x600008,target,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else{check(c.kind==IgbPointerListServiceKind::allocation_size,"Unexpected list growth.");out->size_result=128;}
                return MessengerCallResult{true,0,{}};};return op;};
        services->values=std::make_shared<IgbFieldValuesServices>();services->values->fields=schema.fields;
        services->values->fields->unsigned_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->values->enumeration_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->values->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->values->string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        const auto native=make_native_igb_field_values_factory(services->values,[this](const IgbFieldOperationCall& c){
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{std::uint32_t result{};
                if(c.kind==FK::object_pool || c.kind==FK::allocation_pool)out->pool_result=pool;
                else if(c.kind==FK::allocation_size)result=4;
                else{check(c.kind==FK::allocate_default,"Unexpected field endpoint.");++allocations_count;
                    out->buffer_result={bytes(c.bytes),0};out->buffer_address=0x700000+allocations_count*0x100;}
                return MessengerCallResult{true,result,{}};};return op;});
        schema.fields->factory=[this,native](const IgbFieldOperationCall& c){auto selected=native(c);
            if(c.kind!=FK::field_virtual || c.virtual_slot!=0x8c || !c.source)return selected;
            scalar_calls.push_back(c);auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,selected,done=false]()mutable{if(done)return MessengerCallResult{true,0,{}};++scalar_polls;
                if(scalar_before)scalar_before(c);if(!scalar_ready)return MessengerCallResult{false,0,"prepared scalar virtual"};
                const auto status=selected->resume();if(status.completed){if(scalar_after)scalar_after(c);done=true;}return status;};return op;};
        // Factories and pool/string endpoints are prepared. Actual batch,
        // scalar/string defaults, pointer append and schema bodies execute.
        schema.factory=[this](const IgbFieldSchemaCall& c){
            if(c.kind==SK::create_field)constructors.push_back(c.function);
            auto op=std::make_shared<IgbFieldSchemaOperation>();op->resume=[this,c,out=op.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};
                if(c.kind==SK::create_field){++create_polls;if(!create_ready)return MessengerCallResult{false,0,"prepared constructor"};
                    auto f=std::make_shared<IgbDecodeRuntimeField>();
                    f->vtable_word=c.function==0x80031378?0x804758e4:c.function==0x800274ec?0x80476630:
                        c.function==0x80022a90?0x80476e6c:c.function==0x800251ec?0x8047693c:0x80473e30;
                    f->name_string=std::shared_ptr<IgbInternedString>{};f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4),0};
                    f->default_bytes_address=0x700000+static_cast<std::uint32_t>(created.size())*0x100;f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                    f->method_function=0xdeadbeef;const auto address=0x100000+static_cast<std::uint32_t>(created.size())*0x100;
                    created.push_back(f);out->field_result=f;out->field_address=address;
                    if(physical){auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(0x48,0xcc);
                        storage->bindings.push_back({8,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{storage,8};IgbBootstrapPointer p{storage,8};
                        igb_bootstrap_write_word(p,0,*f->vtable_word);igb_bootstrap_write_word(p,4,1);igb_bootstrap_write_word(p,0x20,*f->default_bytes_address);
                        igb_bootstrap_write_word(p,0x34,0xdeadbeef);allocations.push_back(p);}
                    if(created_action)created_action();
                }else{check(c.kind==SK::destroy_field,"Unexpected schema endpoint.");++destroys;if(destroy_action)destroy_action();}
                done=true;return MessengerCallResult{true,0,{}};};return op;};
    }
    IgbNamedTypeDefaultsTables& table(){return services->tables[static_cast<std::size_t>(kind)];}
};
void complete_original_schemas(){
    for(bool physical:{false,true}){
        Fixture f;f.physical=physical;f.target->fields->count=3;const auto header=metadata_storage(f.target);f.services->physical_metadata.push_back(header);
        IgbNamedTypeDefaultsInvocation call(Kind::thread);check(call.resume(f.services).completed,"Thread defaults did not complete.");
        check(f.created.size()==10 && f.target->fields->count==13 && f.scalar_calls.size()==5 && f.names==10 && f.null_names==0,
            "Thread defaults changed batch/setter/schema counts.");
        constexpr std::array<std::uint32_t,4> getters={0x80023a70,0x80023b4c,0x80023b00,0x80023abc};
        for(unsigned i=0;i<10;++i){const auto field=f.created[i];
            check(field->name && *field->name==**f.table().names->at(i) && field->offset==*f.table().offsets->at(i) &&
                field->declaring_type->lock()==f.target && *f.table().globals->at(i)->slot==field,"Thread schema omitted original row configuration.");
            if(physical)check(igb_bootstrap_read_word(f.allocations[i],0x1c)==f.table().globals->at(i)->address &&
                igb_bootstrap_read_word(f.allocations[i],8)==*f.table().offsets->at(i) &&
                igb_bootstrap_read_word(f.allocations[i],0x18)==igb_bootstrap_address(header),
                "Thread schema did not publish its physical global, offset and declaring metadata words.");
            if(i<4){check(field->method_function==getters[i] && igb_buffer_word(field->default_bytes->buffer,0)==(i?0u:0x80u),"Enum setter/getter pair changed.");
                check(f.scalar_calls[i].source_address==0x8055d070u+4*i,"Enum source lost its original static address.");
                if(physical)check(igb_bootstrap_read_word(f.allocations[i],0x34)==getters[i],"Enum getter word was not published physically.");}
        }
        check(igb_buffer_word(f.created[6]->default_bytes->buffer,0)==0x1000 && !f.scalar_calls[4].source_address &&
            f.scalar_calls[4].field==f.created[6],"Stack size setter changed its four-byte argument or selected index.");
        check(f.target->factory_redirect==0x80023e20 && igb_bootstrap_read_word(header,0x3c)==0x80023e20,
            "Thread redirect did not publish both physical and canonical state.");
        check(call.resume().completed && f.create_polls==10 && f.scalar_polls==5 && f.names==10,"Completed defaults replayed work.");
    }
    Fixture named(Kind::named_object);named.target->fields->count=2;IgbNamedTypeDefaultsInvocation call(Kind::named_object);
    check(call.resume(named.services).completed && named.target->fields->count==3 && named.null_names==1 && named.names==1 &&
        named.constructors==std::vector<std::uint32_t>{0x800251ec} && named.scalar_calls.empty(),"Named defaults bypassed its real null-string setter.");
    check(named.created[0]->name && *named.created[0]->name=="_name" && named.created[0]->offset==8 &&
        igb_buffer_word(named.created[0]->default_bytes->buffer,0)==0 && !named.target->factory_redirect,
        "Named defaults installed unrelated thread fields or a redirect.");
    check(igb_bootstrap_read_word(named.allocations[0],0x1c)==0x80561748 &&
        igb_bootstrap_read_word(named.allocations[0],8)==8 && igb_bootstrap_read_word(named.allocations[0],0x18)==0x600008,
        "Named schema left physical descriptor members at their old bytes.");
}
void pending_capture_and_live_rows(){
    Fixture f;f.scalar_ready=false;auto replaced=metadata();IgbNamedTypeDefaultsInvocation call(Kind::thread);
    check(!call.resume(f.services).completed && f.created.size()==10 && f.scalar_calls.size()==1 && f.names==0 &&
        f.created[0]->method_function==0xdeadbeef,"Pending first setter published the getter or schema early.");
    *f.cell=replaced;const auto selected=f.created[0];f.target->fields->backing->at(0)=f.created[4];
    auto source=f.services->enum_default_sources[0]->buffer;source->bytes[3]=0x91;
    f.scalar_ready=true;check(call.resume(f.services).completed,"Pending scalar operation did not resume.");
    check(selected->method_function==0x80023a70 && igb_buffer_word(selected->default_bytes->buffer,0)==0x91 &&
        f.created[4]->method_function==0xdeadbeef && replaced->factory_redirect==0x80023e20 && !f.target->factory_redirect,
        "Pending setter reselected its descriptor/source or redirect failed to reload the canonical metadata.");
    check(f.scalar_calls.size()==5 && f.scalar_polls==6 && f.target->fields->count==10 && replaced->fields->count==0,
        "Pending defaults recaptured its original metadata or replayed the field batch.");

    Fixture rows;auto replacement=metadata()->fields;replacement->count=10;
    rows.scalar_after=[&](const IgbFieldOperationCall&){if(rows.scalar_calls.size()==1){
        *replacement->backing=*rows.target->fields->backing;replacement->backing->at(1)=rows.created[4];rows.target->fields=replacement;}};
    IgbNamedTypeDefaultsInvocation fresh(Kind::thread);check(fresh.resume(rows.services).completed && rows.created[4]->method_function==0x80023b4c &&
        rows.created[1]->method_function==0xdeadbeef,"Later enum selection did not reload the captured metadata's live registry.");
}
void pending_schema_and_late_redirect(){
    Fixture f;f.table().names->at(0).reset();IgbNamedTypeDefaultsInvocation call(Kind::thread);
    check(!call.resume(f.services).completed && f.scalar_calls.size()==5 && f.created[3]->method_function==0x80023abc &&
        !f.target->factory_redirect,"Missing schema table was reached before defaults or after redirect publication.");
    const auto names=f.table().names;f.table().names=std::make_shared<IgbFieldNameTable>();names->at(0)=std::make_shared<const std::string>("_priority");
    f.name_action=[&]{if(f.names==10)f.cell->reset();};
    check(!call.resume(f.services).completed && f.names==10 && !f.target->factory_redirect,"Unknown final canonical cell was treated as null or stale receiver.");
    auto final=metadata();const auto physical=metadata_storage(final,0x610000);f.services->physical_metadata.push_back(physical);*f.cell=final;
    f.services->schema->fields->pointer_lists->values.push_back({igb_bootstrap_address(physical),final,{}});
    check(call.resume(f.services).completed && final->factory_redirect==0x80023e20 && igb_bootstrap_read_word(physical,0x3c)==0x80023e20 &&
        f.names==10 && f.scalar_calls.size()==5,"Late redirect state replayed schema or skipped the fresh physical publication.");

    Fixture named(Kind::named_object);named.name_ready=false;IgbNamedTypeDefaultsInvocation name(Kind::named_object);
    check(!name.resume(named.services).completed && named.created.size()==1 && named.null_names==0,"Named setter did not retain its null interning call.");
    named.name_ready=true;check(name.resume(named.services).completed && named.null_names==1 && named.names==1 && named.create_polls==1,
        "Named setter replayed its field batch across suspension.");
}
void wrapped_indices_and_fault_prefixes(){
    for(const auto first:{0x40000000u,0x80000000u,0xc0000000u}){
        Fixture f;f.target->fields->count=std::bit_cast<std::int32_t>(first);
        f.created_action=[&]{if(f.created.size()==1)f.target->fields->count=0;
            if(f.created.size()==10)f.created.back()->reference_word=0;};
        f.destroy_action=[&]{f.target->fields->count=std::bit_cast<std::int32_t>(first+10u);};
        IgbNamedTypeDefaultsInvocation call(Kind::thread);
        check(call.resume(f.services).completed && f.created[0]->method_function==0x80023a70 && f.created[3]->method_function==0x80023abc &&
            igb_buffer_word(f.created[6]->default_bytes->buffer,0)==0x1000 && f.names==10,"Wrapped local field indices were rejected before original byte-offset wrapping.");
    }
    Fixture bad;bad.target->fields->count=-1;bad.created_action=[&]{if(bad.created.size()==1)bad.target->fields->count=0;};
    IgbNamedTypeDefaultsInvocation invalid(Kind::thread);rejects([&]{invalid.resume(bad.services);},"Unrepresented wrapped field selection was accepted.");
    check(bad.created.size()==10 && bad.scalar_calls.empty() && bad.names==0 && !bad.target->factory_redirect,"Selection fault changed the completed batch or reached later stores.");
    Fixture short_field;short_field.scalar_after=[&](const auto&){if(short_field.scalar_calls.size()==1){
        short_field.allocations[0].storage->bytes->bytes.resize(8+0x35);short_field.allocations[0].storage->bytes->initialized.resize(8+0x35);}};
    IgbNamedTypeDefaultsInvocation failed(Kind::thread);rejects([&]{failed.resume(short_field.services);},"Short physical enum field accepted a getter word store.");
    check(short_field.created[0]->method_function==0xdeadbeef && igb_buffer_word(short_field.created[0]->default_bytes->buffer,0)==0x80 &&
        short_field.scalar_calls.size()==1 && short_field.names==0,"Getter store failure lost its completed default setter or reached later fields.");
    rejects([&]{failed.resume(short_field.services);},"Faulted defaults invocation restarted.");

    Fixture null_final;null_final.name_action=[&]{if(null_final.names==10)*null_final.cell=std::shared_ptr<IgbRuntimeMetaType>{};};
    IgbNamedTypeDefaultsInvocation null(Kind::thread);rejects([&]{null.resume(null_final.services);},"Represented-null redirect metadata was silently accepted.");
    check(null_final.names==10 && null_final.scalar_calls.size()==5 && !null_final.target->factory_redirect,"Null final redirect lost earlier schema work.");
}
void semaphore_adapters_and_recursion(){
    Fixture sema(Kind::semaphore);sema.services->schema.reset();sema.services->values.reset();sema.cell->reset();
    IgbNamedTypeDefaultsInvocation call(Kind::semaphore);check(!call.resume(sema.services).completed,"Unknown semaphore metadata was fabricated.");
    *sema.cell=sema.target;auto physical=metadata_storage(sema.target);sema.services->physical_metadata.push_back(physical);
    check(call.resume(sema.services).completed && sema.target->factory_redirect==0x80026160 && igb_bootstrap_read_word(physical,0x3c)==0x80026160,
        "Semaphore defaults required a field batch or failed its redirect store.");
    Fixture bad(Kind::semaphore);physical=metadata_storage(bad.target);physical.storage->bytes->bytes.resize(8+0x3e);
    physical.storage->bytes->initialized.resize(8+0x3e);bad.services->physical_metadata.push_back(physical);
    IgbNamedTypeDefaultsInvocation invalid(Kind::semaphore);rejects([&]{invalid.resume(bad.services);},"Short semaphore metadata accepted a redirect store.");
    check(!bad.target->factory_redirect,"Failed physical redirect published its canonical word.");
    bool delegated{};auto adapter=make_native_igb_named_type_defaults_factory(sema.services,[&](const auto&){delegated=true;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall request;request.kind=IgbTypeRegistrationServiceKind::defaults;request.function=0x8002614c;request.metadata=metadata();
    check(adapter(request)->resume().completed && !request.metadata->factory_redirect,"Defaults adapter used an unrelated supplied receiver.");
    request.function=0x80026160;check(!adapter(request) && delegated,"Defaults adapter executed a stored redirect.");
    rejects([]{IgbNamedTypeDefaultsInvocation invalid(static_cast<Kind>(3));},"Unknown defaults kind was accepted.");
    Fixture recursive;IgbNamedTypeDefaultsInvocation poisoned(Kind::thread);
    recursive.scalar_after=[&](const auto&){try{poisoned.resume(recursive.services);}catch(const std::runtime_error&){};};
    rejects([&]{poisoned.resume(recursive.services);},"Caught recursive scalar callback did not poison the outer caller.");
    check(poisoned.faulted() && recursive.created.size()==10 && recursive.scalar_calls.size()==1 && recursive.created[0]->method_function==0xdeadbeef &&
        igb_buffer_word(recursive.created[0]->default_bytes->buffer,0)==0x80 && recursive.names==0,"Recursion lost the completed scalar prefix or reached the getter store.");
}
}
int main(){try{complete_original_schemas();pending_capture_and_live_rows();pending_schema_and_late_redirect();wrapped_indices_and_fault_prefixes();
    semaphore_adapters_and_recursion();std::cout<<"igb_named_type_defaults_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Named-type defaults failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
