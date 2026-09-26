#include "dw4/igb_gamecube_thread_registration.hpp"
#include "dw4/igb_field_values.hpp"

#include <array>
#include <iostream>

// Field constructors and allocator endpoints are
// prepared. This exercises metadata defaults/schema, not thread execution.
namespace {
using namespace dw4;
using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using FK=IgbFieldOperationKind;using SK=IgbFieldSchemaServiceKind;using Lookup=IgbGamecubeThreadLookup;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F action,const char* why){bool caught{};try{action();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(unsigned size,std::uint8_t fill=0xcc){auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,fill);result->initialized.assign(size,1);return result;}
IgbBootstrapPointer allocation(std::uint32_t address,unsigned size){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address-8;storage->bytes=bytes(size+8);return {storage,8};}
auto metadata(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;
    result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);
    result->fields->backing_bytes=bytes(128,0);result->fields->backing_address=0x900000;return result;}
constexpr std::array<std::uint32_t,9> factories={0x8002450c,0x800274ec,0x8002450c,0x800274ec,0x8002879c,0x800385e0,0x800385e0,0x800385e0,0x800385e0};
constexpr std::array<std::uint32_t,9> offsets={0x38,0x350,0x358,0x30,0x380,0x34,0x35,0x36,0x37};
constexpr std::array names={"_thread","_master","_alarm","_result","_threadDoneSema","_isDeferred","_stopRequested","_atStoppingPoint","_isActive"};

struct Fixture {
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> services=std::make_shared<IgbGamecubeThreadRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(),semaphore=metadata();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    IgbBootstrapPointer physical=allocation(0x600008,0x64),semaphore_physical=allocation(0x610008,0x64);
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> field_storage;
    std::vector<std::uint32_t> constructors;
    std::vector<IgbFieldOperationCall> setters;
    std::vector<IgbDependenciesCall> releases;
    std::function<void(unsigned)> after_field;
    std::function<void()> after_setter,after_name;
    unsigned blocked_factory=99,create_polls{},setter_polls{},name_calls{},pool_queries{},release_polls{};
    bool setter_ready=true,release_ready=true;
    Fixture(){
        services->state=std::make_shared<IgbGamecubeThreadRegistrationState>();services->state->metadata=target;
        services->parent=std::make_shared<IgbNamedTypeRegistrationServices>();services->parent->thread_metadata=std::make_shared<IgbRegistrationMetaSlot>();
        services->dependencies=std::make_shared<IgbThreadDependenciesServices>();services->dependencies->semaphore=std::make_shared<IgbRegistrationMetaSlot>(semaphore);
        semaphore_physical.storage->bindings.push_back({8,{},semaphore});services->dependencies->allocations.push_back(semaphore_physical);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();
        services->types->runtime->singleton=core;core->vtable_offset=0;services->types->metadata_pool_handle=5;
        services->types->factory=[this](const IgbTypeRegistrationCall& call){check(call.kind==TK::pool_for_handle && call.handle==5,"Unexpected prepared registration service.");
            auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[this,out=operation.get()]{++pool_queries;out->object_result=pool;return MessengerCallResult{true,0,{}};};return operation;};
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        services->callbacks->factory=make_native_igb_gamecube_thread_registration_function_factory(services);
        services->local_storage=[]{return allocation(0x700008,0x388);};
        physical.storage->bindings.push_back({8,{},target});services->physical_metadata.push_back(physical);
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;schema.types=services->types;
        schema.pools=std::make_shared<IgbFilePoolsServices>();schema.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        schema.pools->memory_pools->selector=0;schema.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*schema.pools->memory_pools->fallback)->original_pool_access_bound=true;(*schema.pools->memory_pools->fallback)->pools[5]=pool;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& call){auto operation=std::make_shared<IgbDependenciesOperation>();
            if(call.kind==IgbDependenciesServiceKind::release_string)releases.push_back(call);
            operation->resume=[this,call,out=operation.get()]{
                if(call.kind==IgbDependenciesServiceKind::intern_name){++name_calls;auto name=std::make_shared<IgbInternedString>();
                    name->value=igb_dependency_name(call.name);name->container=pool;out->string_result=name;if(after_name)after_name();}
                else{check(call.kind==IgbDependenciesServiceKind::release_string,"Unexpected prepared string operation.");++release_polls;
                    if(!release_ready)return MessengerCallResult{false,0x83,"Prepared string release."};}
                return MessengerCallResult{true,0,{}};};return operation;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();schema.fields->pointer_lists->values.push_back({0x600008,target,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& call){auto operation=std::make_shared<IgbPointerListOperation>();
            operation->resume=[this,call,out=operation.get()]{if(call.kind==IgbPointerListServiceKind::allocation_pool)out->pool_result=pool;
                else{check(call.kind==IgbPointerListServiceKind::allocation_size,"Prepared registry capacity unexpectedly required growth.");out->size_result=128;}
                return MessengerCallResult{true,0,{}};};return operation;};
        values->fields=schema.fields;values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->struct_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->pointer_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        const auto field_factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& call){
            auto operation=std::make_shared<IgbFieldOperation>();operation->resume=[this,call,out=operation.get()]{
                if(call.kind==FK::allocation_pool || call.kind==FK::object_pool)out->pool_result=pool;
                else if(call.kind==FK::allocation_size)return MessengerCallResult{true,1024,{}};
                else throw std::runtime_error("Prepared field default capacity unexpectedly required allocation.");return MessengerCallResult{true,0,{}};};return operation;});
        schema.fields->factory=[this,field_factory](const IgbFieldOperationCall& call){const auto selected=field_factory(call);
            if(call.kind!=FK::field_virtual || call.virtual_slot!=0x8c || !call.source)return selected;
            setters.push_back(call);auto operation=std::make_shared<IgbFieldOperation>();operation->resume=[this,selected,done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++setter_polls;if(!setter_ready)return MessengerCallResult{false,0x81,"Prepared boolean setter suspension."};
                const auto status=selected->resume();if(status.completed){done=true;if(after_setter)after_setter();}return status;};return operation;};
        schema.factory=[this](const IgbFieldSchemaCall& call){check(call.kind==SK::create_field,"Unexpected prepared batch destruction.");
            const auto index=static_cast<unsigned>(constructors.size());constructors.push_back(call.function);
            auto operation=std::make_shared<IgbFieldSchemaOperation>();operation->resume=[this,call,index,out=operation.get(),done=false]()mutable{
                if(done)return MessengerCallResult{true,0,{}};++create_polls;if(index==blocked_factory)return MessengerCallResult{false,0x82,"Prepared field constructor."};
                auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=call.function==0x8002450c?0x80470e00u:
                    call.function==0x800274ec?0x80476630u:call.function==0x8002879c?0x804762fcu:0x80473e30u;
                field->size=call.function==0x8002450c || call.function==0x800385e0?1:4;
                field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                field->default_bytes=IgbRuntimeFieldDefaultSource{bytes(1024),0};field->default_bytes_address=0x800000+index*0x1000;
                auto storage=allocation(0x100008+index*0x100,0x44);storage.storage->bindings.push_back({8,{},{},{},{},field});
                field->physical_storage=IgbFieldStorageBinding{storage.storage,8};igb_bootstrap_write_word(storage,0,*field->vtable_word);
                igb_bootstrap_write_word(storage,4,1);igb_bootstrap_write_halfword(storage,0x14,field->size);igb_bootstrap_write_word(storage,0x20,*field->default_bytes_address);
                if(call.function==0x8002879c){field->object_type=std::weak_ptr<IgbRuntimeMetaType>{};field->flag_34=0;field->flag_35=0x6a;field->flag_3c=0x5b;
                    igb_bootstrap_write_word(storage,0x38,0);igb_bootstrap_write_byte(storage,0x34,0);igb_bootstrap_write_byte(storage,0x35,0x6a);igb_bootstrap_write_byte(storage,0x3c,0x5b);}
                created.push_back(field);field_storage.push_back(storage);out->field_result=field;out->field_address=igb_bootstrap_address(storage);
                done=true;if(after_field)after_field(index);return MessengerCallResult{true,0,{}};};return operation;};
    }
};

void profile_and_prerequisite_capture(){
    Fixture f;const auto args=igb_gamecube_thread_registration_arguments(f.services->state);
    check(args.metadata_slot.get()==&f.services->state->metadata && args.metadata_slot==igb_gamecube_thread_metadata_slot(f.services),"Gamecube thread arguments copied the canonical metadata cell.");
    check(*args.name=="igGamecubeThread" && args.flag==0 && args.native_size==0x388 && args.parent_registration==0x80023c68 &&
        args.parent_getter==0x8003b154 && args.field_callback==0x80023e20 && args.layout_probe==0x8003afc0 && args.defaults_callback==0x8003b15c,
        "Gamecube thread registration profile differs from original3B0E0.");
    check(!args.trailing_callback && args.extra_callbacks && *args.extra_callbacks==std::vector<std::uint32_t>({0x80026098,0}),
        "Thread registration omitted or misclassified the terminated semaphore callback table.");
    check(igb_gamecube_thread_metadata_slot(f.services,Lookup::plain_parent)==f.services->parent->thread_metadata,
        "Plain thread parent getter does not alias existing80561504.");
    for(unsigned i=0;i<9;++i)check(f.services->factories->at(i)==factories[i] && **f.services->names->at(i)==names[i] &&
        f.services->globals->at(i)->address==0x80562078+4*i && f.services->offsets->at(i)==offsets[i],"Nine-field thread schema differs from original tables.");
    rejects([]{igb_gamecube_thread_registration_arguments({});},"Absent canonical thread state was fabricated.");
    rejects([]{IgbGamecubeThreadMetadataInvocation bad(static_cast<Lookup>(2));},"Unknown thread lookup mode was accepted.");
    f.services->callbacks->prerequisites->initialized.reset();f.services->state.reset();IgbGamecubeThreadRegistrationInvocation run;
    check(!run.resume(f.services).completed && f.pool_queries==0,"Thread body captured registrar arguments before21B94.");
    auto captured=std::make_shared<IgbGamecubeThreadRegistrationState>();captured->metadata=metadata(4);f.services->state=captured;
    f.services->callbacks->prerequisites->initialized=1;f.services->types->metadata_pool_handle.reset();
    check(!run.resume(f.services).completed && f.pool_queries==0,"Thread registrar skipped pool lookup before the cache gate.");
    f.services->state=std::make_shared<IgbGamecubeThreadRegistrationState>();f.services->types->metadata_pool_handle=5;
    check(run.resume(f.services).completed && f.pool_queries==1 && (*captured->metadata)->class_flags==4 && !f.services->state->metadata,
        "Thread body recaptured its global cell across a pending registrar.");
}

void metadata_getters_and_redirect(){
    Fixture f;f.services->state->metadata.reset();IgbGamecubeThreadMetadataInvocation unknown;
    check(!unknown.resume(f.services).completed,"Unknown full thread metadata became null.");
    *f.services->parent->thread_metadata=std::shared_ptr<IgbRuntimeMetaType>{};IgbGamecubeThreadMetadataInvocation plain(Lookup::plain_parent);
    check(plain.resume(f.services).completed && !plain.result() && f.pool_queries==0,"Plain parent getter registered known-null metadata.");
    for(const auto flags:{0u,2u,4u,8u}){Fixture gate;gate.target->class_flags=flags;bool ready{};unsigned selections{},polls{};
        const auto captured=gate.services->state;const auto replacement=metadata();
        gate.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& call){++selections;
            check(call.kind==CK::invoke_function && call.function==0x8003b0e0,"Full thread getter selected the wrong body.");
            auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();operation->resume=[&]{++polls;if(ready)captured->metadata=replacement;
                return MessengerCallResult{ready,0x84,"Prepared thread registrar."};};return operation;};
        IgbGamecubeThreadMetadataInvocation run;const auto status=run.resume(gate.services);
        if(flags==4){check(status.completed && run.result()==gate.target && selections==0,"Ready thread metadata registered again.");continue;}
        check(!status.completed && selections==1 && polls==1,"Unready thread getter skipped its retained registration.");
        gate.services->state=std::make_shared<IgbGamecubeThreadRegistrationState>();gate.services->state->metadata=metadata(4);ready=true;gate.services->callbacks->factory={};
        check(run.resume(gate.services).completed && run.result()==replacement && selections==1 && polls==2,"Pending thread getter recaptured its cell or skipped registration.");
    }
    f.services->state->metadata=metadata(4);auto adapter=make_native_igb_gamecube_thread_redirect_factory(f.services);
    IgbFileFactoryCall call;call.kind=IgbFileFactoryServiceKind::redirect_type;call.redirect=0x80023e20;const auto operation=adapter(call);
    check(operation && operation->resume().completed && operation->metadata_result==*f.services->state->metadata && !operation->allocation_result && !operation->object_result,
        "Abstract thread redirect did not return concrete metadata separately from runtime construction.");
    for(const auto redirect:{0x80023a10u,0x80026160u}){call.redirect=redirect;check(!adapter(call),"Concrete thread redirect claimed another class.");}
}

void actual_extra_callback_append_and_execution(){
    Fixture f;f.target->class_flags=2;f.target->post_layout_callback=0;f.core->registration_callback_pool=f.pool;
    auto callbacks=std::make_shared<IgbRegistrationCallbackList>();bool ready{};unsigned polls{},selections{},backings{},frees{};
    f.services->callbacks->factory=make_native_igb_gamecube_thread_registration_function_factory(f.services,[&](const IgbRegistrationCallbacksCall& call){
        auto operation=std::make_shared<IgbRegistrationCallbacksOperation>();
        if(call.kind==CK::invoke_function){++selections;check(call.function==0x80026098,"Thread callback list invoked the wrong extra registration wrapper.");}
        operation->resume=[&,call,out=operation.get()]{
            if(call.kind==CK::allocate_backing){++backings;check(call.bytes==16 && call.pool==f.pool,"Thread callback append changed the real initial capacity/pool.");
                out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);}
            else if(call.kind==CK::invoke_function){++polls;return MessengerCallResult{ready,0x85,"Prepared abstract semaphore registration."};}
            else{check(call.kind==CK::free_backing || call.kind==CK::free_list,"Unexpected callback-list endpoint.");++frees;}
            return MessengerCallResult{true,0,{}};};return operation;});
    f.services->types->factory=make_native_igb_registration_list_factory(f.services->callbacks,[&](const IgbTypeRegistrationCall& call){
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[&,call,out=operation.get()]{
            if(call.kind==TK::pool_for_handle)out->object_result=f.pool;
            else if(call.kind==TK::create_callbacks){check(call.bytes==12 && call.pool==f.pool,"Thread registrar changed callback-list allocation.");out->callbacks_result=callbacks;}
            else check(call.kind==TK::compute_layout,"Prepared layout prefix unexpectedly reran thread defaults.");
            return MessengerCallResult{true,0,{}};};return operation;});
    IgbGamecubeThreadMetadataInvocation getter;
    check(!getter.resume(f.services).completed && f.target->class_flags==6 && callbacks->count==1 && callbacks->backing->at(0)==0x80026098 &&
        selections==1 && polls==1 && backings==1 && f.created.empty(),"Full thread getter skipped real callback append/execution after registration became ready.");
    ready=true;check(getter.resume(f.services).completed && getter.result()==f.target && polls==2 && selections==1 && frees==2,
        "Pending thread extra callback lost its continuation or actual list cleanup.");
    check(getter.resume().completed && polls==2 && frees==2,"Completed full thread getter repeated its extra registration callback.");
}

void probe_stores_capture_and_cleanup(){
    for(const auto offset:{0u,4u,8u,0x10u,0x34u,0x380u,0x384u}){Fixture f;auto local=allocation(0x700008,0x388);f.core->vtable_offset=offset;
        IgbGamecubeThreadProbeInvocation run(local);const auto status=run.resume(f.services);
        const auto expected=offset==0?0x80470110u:offset==4?1u:offset==8 || offset==0x380?0u:0xccccccccu;
        check(status.completed && status.value==expected && run.value()==expected && igb_bootstrap_read_word(local,0)==0x8047650c &&
            igb_bootstrap_read_word(local,8)==0 && igb_bootstrap_read_word(local,0x380)==0 && f.releases.empty(),"Thread probe changed its captured word, local stores or cleanup.");
        check(igb_bootstrap_read_word(local,0x10)==0xcccccccc && igb_bootstrap_read_word(local,0x384)==0xcccccccc,"Thread probe initialized derived padding.");
        igb_bootstrap_write_word(local,0,0x11223344);check(run.resume().value==expected && igb_bootstrap_read_word(local,0)==0x11223344,"Completed thread probe replayed cleanup.");
    }
    Fixture f;auto local=allocation(0x700008,0x388);f.core->vtable_offset.reset();IgbGamecubeThreadProbeInvocation run(local);
    check(!run.resume(f.services).completed && igb_bootstrap_read_word(local,0)==0x80470110 && igb_bootstrap_read_word(local,0x380)==0,"Pending thread probe lost preceding stores.");
    auto other=std::make_shared<IgbRuntimeTypeRegistry>();other->vtable_offset=0;f.services->types->runtime->singleton=other;f.core->vtable_offset=8;
    auto name=std::make_shared<IgbInternedString>();name->value=std::make_shared<const std::string>("late name");name->container=f.pool;name->references=1;
    f.services->strings[0x780008]=name;igb_bootstrap_write_word(local,8,0x780008);igb_bootstrap_write_word(local,0x380,0xdeadbeef);f.release_ready=false;
    check(!run.resume(f.services).completed && f.releases.size()==1 && name->references==0 && igb_bootstrap_read_word(local,0)==0x8047650c,
        "Thread probe did not freshly read and release its late name after restoring base vtables.");
    check(f.releases[0].string==name && f.releases[0].pool==f.pool && igb_bootstrap_read_word(local,0x380)==0xdeadbeef,
        "Thread probe cleanup selected the wrong string owner or treated its literal-zero semaphore guard as a live member.");
    igb_bootstrap_write_word(local,8,0);f.services->strings.clear();f.release_ready=true;
    check(run.resume(f.services).completed && run.value()==0x780008 && name->references==0 && f.release_polls==2,
        "Pending probe release reloaded its name/count, lost its selected word or repeated decrement.");
    for(const auto offset:{1u,0x388u,0xffffffffu}){Fixture invalid;local=allocation(0x700008,0x388);invalid.core->vtable_offset=offset;IgbGamecubeThreadProbeInvocation bad(local);
        rejects([&]{bad.resume(invalid.services);},"Thread probe escaped the represented local layout.");
        check(bad.faulted() && igb_bootstrap_read_word(local,0)==0x80470110,"Failed thread probe performed later cleanup.");
        invalid.core->vtable_offset=0;rejects([&]{bad.resume(invalid.services);},"Faulted thread probe restarted after offset repair.");}
}

void nine_fields_defaults_and_fresh_final_owner(){
    Fixture f;f.target->fields->count=2;f.blocked_factory=8;IgbGamecubeThreadDefaultsInvocation run;
    check(!run.resume(f.services).completed && f.created.size()==8 && f.target->fields->count==10 && f.created[0]->size==1 && f.created[2]->size==1 &&
        f.setters.empty() && f.name_calls==0,"Thread defaults modified fields before the entire nine-field batch.");
    f.blocked_factory=99;f.setter_ready=false;
    check(!run.resume(f.services).completed && f.created.size()==9 && f.target->fields->count==11 && f.created[0]->size==0x318 && f.created[2]->size==0x28 &&
        f.setters.size()==1 && f.setters[0].field==f.created[5] && f.name_calls==0,"Thread defaults lost size order, inherited count or selected boolean setter.");
    check(f.created[4]->object_type && f.created[4]->object_type->lock()==f.semaphore && f.created[4]->flag_34==1 &&
        igb_bootstrap_read_word(f.field_storage[4],0x38)==0x610008 && igb_bootstrap_read_byte(f.field_storage[4],0x34)==1 &&
        f.created[4]->flag_35==0x6a && f.created[4]->flag_3c==0x5b,"Thread-done semaphore type/construct flag lost exact physical publication or changed adjacent flags.");
    const auto captured=f.target;const auto replacement=metadata();auto physical=allocation(0x620008,0x64);physical.storage->bindings.push_back({8,{},replacement});
    f.services->physical_metadata.push_back(physical);f.services->state->metadata=replacement;f.setter_ready=true;
    check(run.resume(f.services).completed && f.constructors==std::vector<std::uint32_t>(factories.begin(),factories.end()) && f.name_calls==9 &&
        replacement->word_40==0x80023cd4 && !captured->word_40 && igb_bootstrap_read_word(physical,0x40)==0x80023cd4,
        "Thread defaults recaptured schema metadata or failed its fresh final metadata+40 reload.");
    for(unsigned i=0;i<9;++i)check(f.created[i]->declaring_type->lock()==captured && *f.created[i]->name==names[i] && f.created[i]->offset==offsets[i] &&
        *f.services->globals->at(i)->slot==f.created[i] && igb_bootstrap_read_word(f.field_storage[i],0x18)==0x600008 &&
        igb_bootstrap_read_word(f.field_storage[i],0x1c)==0x80562078+4*i,"Thread schema changed a name, offset, global or captured declaring type.");
    check(f.setters[0].source && igb_buffer_bytes(f.setters[0].source->buffer,f.setters[0].source->offset,1)[0]==1 &&
        f.created[5]->default_bytes->buffer->bytes[0]==1 && f.created[6]->default_bytes->buffer->bytes[0]==0xcc,
        "Thread deferred setter did not use actual one-byte source1 or changed another boolean.");
    const auto polls=f.create_polls;check(run.resume().completed && f.create_polls==polls && f.name_calls==9,"Completed thread defaults repeated construction/schema.");
}

void captured_semaphore_field_and_fresh_boolean_lookup(){
    Fixture f;f.target->fields->count=2;f.services->dependencies->semaphore->reset();IgbGamecubeThreadDefaultsInvocation run;
    check(!run.resume(f.services).completed && f.created.size()==9 && f.created[0]->size==0x318 && f.created[2]->size==0x28 &&
        f.created[4]->flag_34==0 && f.setters.empty(),"Unknown semaphore metadata was fabricated or bypassed.");
    const auto original=f.created[4];auto replacement=std::make_shared<IgbDecodeRuntimeField>();replacement->flag_34=0;
    f.target->fields->backing->at(6)=replacement;f.target->fields->backing->at(7)=f.created[6];f.setter_ready=false;
    *f.services->dependencies->semaphore=f.semaphore;
    check(!run.resume(f.services).completed && original->object_type->lock()==f.semaphore && original->flag_34==1 && replacement->flag_34==0 &&
        f.setters.size()==1 && f.setters[0].field==f.created[6],"Pending dependency changed captured object-field receiver or failed to freshly select the later boolean.");
}

void default_failure_prefixes(){
    for(const unsigned field_index:{0u,2u}){Fixture f;f.after_field=[&](unsigned index){if(index!=8)return;
        auto storage=f.field_storage[field_index];storage.storage->bytes->bytes.resize(storage.offset+0x15);storage.storage->bytes->initialized.resize(storage.offset+0x15);};
        IgbGamecubeThreadDefaultsInvocation run;rejects([&]{run.resume(f.services);},"Truncated thread struct size store was accepted.");
        check(run.faulted() && f.created.size()==9 && f.created[field_index]->size==1 && f.setters.empty() && f.name_calls==0 &&
            (field_index==0 || f.created[0]->size==0x318),"Struct size failure changed its canonical target or erased preceding defaults.");
        rejects([&]{run.resume(f.services);},"Faulted thread defaults repeated the batch.");}
    Fixture setter;setter.after_setter=[]{throw std::runtime_error("Prepared failure after actual boolean copy.");};IgbGamecubeThreadDefaultsInvocation copy;
    rejects([&]{copy.resume(setter.services);},"Boolean setter callback failure was swallowed.");
    check(copy.faulted() && setter.created[0]->size==0x318 && setter.created[2]->size==0x28 && setter.created[4]->flag_34==1 &&
        setter.created[5]->default_bytes->buffer->bytes[0]==1 && setter.name_calls==0 && !setter.target->word_40,"Setter failure lost preceding defaults or ran schema.");
    setter.after_setter={};rejects([&]{copy.resume(setter.services);},"Failed boolean setter revived after callback repair.");
    Fixture late;late.after_name=[&]{if(late.name_calls==9)late.services->state->metadata.reset();};IgbGamecubeThreadDefaultsInvocation final;
    check(!final.resume(late.services).completed && late.name_calls==9 && !late.target->word_40,"Unknown final metadata became a stale canonical store.");
    late.services->state->metadata=std::shared_ptr<IgbRuntimeMetaType>{};rejects([&]{final.resume(late.services);},"Known-null final thread metadata was accepted.");
    check(final.faulted() && late.name_calls==9,"Final metadata failure repeated schema.");
}

void exact_adapters_and_runtime_boundary(){
    Fixture f;f.target->class_flags=4;*f.services->parent->thread_metadata=f.semaphore;unsigned delegated{};
    auto adapter=make_native_igb_gamecube_thread_registration_type_factory(f.services,[&](const auto&){++delegated;return std::shared_ptr<IgbTypeRegistrationOperation>{};});
    IgbTypeRegistrationCall call;call.kind=TK::get_parent;
    for(const auto function:{0x8003af84u,0x8003b154u}){call.function=function;const auto operation=adapter(call);
        check(operation && operation->resume().completed && operation->metadata_result==(function==0x8003af84?f.target:f.semaphore),"Exact thread metadata getter adapter changed canonical identity.");}
    for(const auto function:{0x80077e94u,0x80077efcu,0x80093400u,0x80075460u}){call.function=function;check(!adapter(call),"Metadata adapter claimed thread initialization, master setup or manager insertion.");}
    check(delegated==4,"Unimplemented runtime identities did not delegate.");
    auto functions=make_native_igb_gamecube_thread_registration_function_factory(f.services);
    for(const auto function:{0x8003b0b8u,0x8003b0e0u}){IgbRegistrationCallbacksCall request;request.kind=CK::invoke_function;request.function=function;
        const auto operation=functions(request);check(operation && operation->resume().completed,"Exact thread registration wrapper/body was not composed.");}
}
}
int main(){try{
    profile_and_prerequisite_capture();metadata_getters_and_redirect();actual_extra_callback_append_and_execution();probe_stores_capture_and_cleanup();nine_fields_defaults_and_fresh_final_owner();
    captured_semaphore_field_and_fresh_boolean_lookup();default_failure_prefixes();exact_adapters_and_runtime_boundary();
    std::cout<<"igb_gamecube_thread_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Gamecube thread registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
