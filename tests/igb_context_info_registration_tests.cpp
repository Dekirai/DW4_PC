#include "dw4/igb_context_info_registration.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;
using T=IgbTypeRegistrationServiceKind;using C=IgbRegistrationCallbacksServiceKind;
using F=IgbFieldOperationKind;using S=IgbFieldSchemaServiceKind;using P=IgbPointerListServiceKind;
using N=IgbDependenciesServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class Fn>void rejects(Fn fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(std::size_t size,std::uint8_t value=0xcc){auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,value);result->initialized.assign(size,1);return result;}
void word(const std::shared_ptr<IgbScratchBuffer>& buffer,std::uint32_t value){for(unsigned i=0;i<4;++i){buffer->bytes.at(i)=static_cast<std::uint8_t>(value>>(24-8*i));buffer->initialized.at(i)=1;}}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(size+8);return {storage,8};}
auto metadata(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;result->reference_word=2;result->post_layout_callback=0;
    result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);
    result->fields->backing_bytes=bytes(128,0);result->fields->backing_address=0x900000;return result;}
IgbObjectInstance object(std::uint32_t address,std::uint32_t references){const auto allocation=raw(address,0x20);IgbObjectInstance result;
    result.storage=allocation.storage;result.offset=allocation.offset;result.fields=std::make_shared<IgbDecodeFieldRegistry>();
    result.storage->bindings.push_back({result.offset,{},{},result.fields});igb_object_set_reference_word(result,references);return result;}
// Constructors and lower allocator/string/destructor endpoints are prepared.
// The batch, lazy getters, string assignment, probe and schema remain native.
struct Fixture {
    std::shared_ptr<IgbContextInfoServices> services=std::make_shared<IgbContextInfoServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(),related=metadata(),base=metadata(4);
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbDecodeRuntimeField> character=std::make_shared<IgbDecodeRuntimeField>();
    IgbBootstrapPointer meta_storage=raw(0x610008,0x64),character_storage=raw(0x620008,0x34);
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> physical;
    std::vector<std::uint32_t> constructors,functions;
    std::vector<IgbTypeRegistrationCall> type_calls;
    std::vector<N> string_calls;
    std::function<void(unsigned)> after_field;
    std::function<void(const IgbDependenciesCall&)> string_action;
    unsigned blocked_constructor=99,constructor_polls{},metadata_allocations{},metadata_polls{},null_interns{},named_interns{},releases{};
    bool allocation_ready=true,release_ready=true;
    Fixture(){
        services->metadata=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;core->registration_filter_enabled=0;core->registration_callback_pool=pool;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        services->meta_objects=std::make_shared<IgbMetadataRegistrationServices>();auto& related_services=*services->meta_objects;
        related_services.types=services->types;related_services.callbacks=services->callbacks;
        related_services.pools=std::make_shared<IgbFilePoolsServices>();related_services.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        related_services.pools->memory_pools->selector=0;related_services.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*related_services.pools->memory_pools->fallback)->original_pool_access_bound=true;(*related_services.pools->memory_pools->fallback)->pools[5]=pool;
        related_services.metadata=std::make_shared<IgbMetadataFactoryServices>();related_services.metadata->metadata=related;
        related_services.metadata->bootstrap=std::make_shared<IgbBootstrapServices>();related_services.metadata->bootstrap->runtime=services->types->runtime;
        meta_storage.storage->bindings.push_back({8,{},related});
        related_services.metadata->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==IgbMetadataFactoryServiceKind::register_type)op->resume=[]{return MessengerCallResult{true,0,{}};};
            else{check(c.kind==IgbMetadataFactoryServiceKind::reflected_factory && c.pool==pool,"Metadata allocation lost its selected pool.");++metadata_allocations;
                op->resume=[this,out=op.get()]{++metadata_polls;if(!allocation_ready)return MessengerCallResult{false,0,"Prepared metadata allocation."};out->allocation_result=meta_storage;return MessengerCallResult{true,0,{}};};}return op;};
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;schema.types=services->types;schema.pools=related_services.pools;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{string_calls.push_back(c.kind);
                if(c.kind==N::create_string_pool){check(c.bytes==16,"Null string default changed lazy pool size.");out->pool_result=pool;}
                else if(c.kind==N::initialize_string_pool)out->pool_result=pool;
                else if(c.kind==N::intern_name){if(std::holds_alternative<std::monostate>(c.name)){++null_interns;out->string_result.reset();}
                    else{++named_interns;auto name=std::make_shared<IgbInternedString>();name->value=igb_dependency_name(c.name);name->container=pool;out->string_result=name;}}
                else{check(c.kind==N::release_string,"Unexpected prepared string endpoint.");++releases;if(string_action)string_action(c);return MessengerCallResult{release_ready,0,"Prepared old string release."};}
                if(string_action)string_action(c);return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();schema.fields->pointer_lists->values={{0x600008,target,{}},{0x610008,related,{}}};
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==P::allocation_pool)out->pool_result=pool;
                else{check(c.kind==P::allocation_size,"Prepared field registry unexpectedly required growth.");out->size_result=128;}return MessengerCallResult{true,0,{}};};return op;};
        services->values=std::make_shared<IgbFieldValuesServices>();auto& values=*services->values;values.fields=schema.fields;
        values.object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values.string_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values.memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        character->vtable_word=0x80473f24;character->size=1;character->physical_storage=IgbFieldStorageBinding{character_storage.storage,8};
        character_storage.storage->bindings.push_back({8,{},{},{},{},character});character_storage.storage->bytes->initialized.assign(character_storage.storage->bytes->bytes.size(),0);
        values.signed_byte_prototype=character;
        schema.fields->factory=make_native_igb_field_values_factory(services->values,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==F::allocation_pool || c.kind==F::object_pool)out->pool_result=pool;
                else if(c.kind==F::allocation_size)return MessengerCallResult{true,64,{}};
                else throw std::runtime_error("Prepared descriptor unexpectedly required a buffer allocation.");return MessengerCallResult{true,0,{}};};return op;});
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==S::create_field,"Unexpected prepared descriptor destruction.");
            const auto index=static_cast<unsigned>(constructors.size());constructors.push_back(c.function);auto op=std::make_shared<IgbFieldSchemaOperation>();
            op->resume=[this,c,index,out=op.get(),done=false]()mutable{if(done)return MessengerCallResult{true,0,{}};++constructor_polls;
                if(index==blocked_constructor)return MessengerCallResult{false,0,"Prepared descriptor constructor."};
                auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=c.function==0x8002879c?0x804762fcu:c.function==0x800251ec?0x8047693cu:0x80476088u;field->size=4;
                field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                field->default_bytes=IgbRuntimeFieldDefaultSource{bytes(64),0};word(field->default_bytes->buffer,0);field->default_bytes_address=0x800000+index*0x100;
                field->object_type=std::weak_ptr<IgbRuntimeMetaType>{};field->element_field=std::weak_ptr<IgbDecodeRuntimeField>{};field->flag_34=0xa5;field->flag_35=0x6a;
                auto storage=raw(0x100008+index*0x100,0x50);storage.storage->bindings.push_back({8,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{storage.storage,8};
                igb_bootstrap_write_word(storage,0,*field->vtable_word);igb_bootstrap_write_word(storage,4,1);igb_bootstrap_write_halfword(storage,0x14,4);
                igb_bootstrap_write_word(storage,0x20,*field->default_bytes_address);igb_bootstrap_write_byte(storage,0x34,0xa5);igb_bootstrap_write_byte(storage,0x35,0x6a);
                igb_bootstrap_write_word(storage,0x38,0);igb_bootstrap_write_word(storage,0x3c,0);
                created.push_back(field);physical.push_back(storage);out->field_result=field;out->field_address=igb_bootstrap_address(storage);done=true;
                if(after_field)after_field(index);return MessengerCallResult{true,0,{}};};return op;};
        services->local_storage=[]{auto local=raw(0x700008,20);igb_bootstrap_write_word(local,4,1);return local;};
        services->object_cleanup=std::make_shared<IgbObjectFieldConstructServices>();
        services->callbacks->factory=make_native_igb_context_info_registration_function_factory(services,[this](const IgbRegistrationCallbacksCall& c){
            if(c.kind==C::invoke_function){functions.push_back(c.function);return std::shared_ptr<IgbRegistrationCallbacksOperation>{};}
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[c,out=op.get()]{if(c.kind==C::allocate_backing || c.kind==C::reallocate_backing){
                check(c.bytes<=1024,"Prepared callback allocation exceeds bound.");out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(c.bytes/4);}
                else check(c.kind==C::free_backing || c.kind==C::free_list,"Unexpected callback-list service.");return MessengerCallResult{true,0,{}};};return op;});
        services->types->factory=make_native_igb_context_info_registration_type_factory(services,make_native_igb_registration_list_factory(services->callbacks,
            [this](const IgbTypeRegistrationCall& c){type_calls.push_back(c);auto op=std::make_shared<IgbTypeRegistrationOperation>();
                op->resume=[this,c,out=op.get()]{switch(c.kind){case T::pool_for_handle:out->object_result=pool;break;case T::register_parent:break;
                    case T::get_parent:out->metadata_result=base;break;case T::attach_parent:break;case T::set_name:c.metadata->name=c.name;break;
                    case T::publish_type:break;case T::compute_layout:c.metadata->class_flags|=4;break;
                    case T::create_callbacks:out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();break;
                    default:throw std::runtime_error("Unexpected prepared context-info registrar operation.");}return MessengerCallResult{true,0,{}};};return op;}));
    }
    auto old_string(std::uint32_t references=1){auto result=std::make_shared<IgbInternedString>();result->references=references;result->container=pool;
        result->value=std::make_shared<const std::string>("old");igb_bind_field_string(services->values,0x630008,result);
        after_field=[this](unsigned index){if(index==1)word(created[1]->default_bytes->buffer,0x630008);};return result;}
};

void exact_profile_and_registration(){
    Fixture f;const auto a=igb_context_info_type_registration_arguments(f.services);
    check(a.metadata_slot==f.services->metadata && *a.name=="igContextExtInfo" && a.flag==0 && a.native_size==20,"Context-info canonical identity/name/layout changed.");
    check(a.parent_registration==0x80066b08 && a.parent_getter==0x800237d0 && a.field_callback==0x800373cc && a.layout_probe==0x80037268 &&
        a.defaults_callback==0x800373ec && !a.trailing_callback && a.extra_callbacks && *a.extra_callbacks==std::vector<std::uint32_t>({0x8002a0a8,0}),"Context-info registrar arguments changed.");
    constexpr std::array factories{0x8002879cu,0x800251ecu,0x8002ac34u};constexpr std::array names{"_pMeta","_pName","_pVendorName"};
    for(unsigned i=0;i<3;++i)check(f.services->factories->at(i)==factories[i] && **f.services->names->at(i)==names[i] &&
        f.services->globals->at(i)->address==0x80561dec+4*i && f.services->offsets->at(i)==8+4*i,"Context-info original field tables changed.");
    IgbContextInfoRegistrationInvocation body;check(body.resume(f.services).completed && f.target->type_word==0x804741d4 && f.target->class_flags==7 && f.target->fields->count==3,"Actual context-info registration omitted probe/defaults/readiness.");
    check(body.result() && body.result()->count==1 && body.result()->backing->at(0)==0x8002a0a8 && f.functions.empty(),"Registrar executed the returned extra callback during its body.");
    Fixture wrapper;IgbRegistrationCallbacksInvocation call(0x80037358);
    check(!call.resume(wrapper.services->callbacks).completed && wrapper.target->class_flags==7 && wrapper.functions==std::vector<std::uint32_t>({0x8002a0a8}),"Wrapper did not reach the actual returned metadata callback.");
    rejects([]{IgbContextInfoLookupInvocation bad(static_cast<IgbContextInfoLookup>(2));},"Unknown lookup mode was accepted.");
}

void lookup_and_prototype_aliases(){
    for(const auto flags:{0u,4u,8u,0xffffffffu}){Fixture f;f.target->class_flags=flags;if(flags==0xffffffffu)*f.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
        bool ready{};unsigned selections{},polls{};auto result=metadata(4);
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==C::invoke_function && c.function==0x80037358,"Full getter selected another registrar.");++selections;
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(ready)*f.services->metadata=result;return MessengerCallResult{ready,0,"Prepared registration."};};return op;};
        IgbContextInfoLookupInvocation run;const auto status=run.resume(f.services);
        if(flags==4)check(status.completed && run.result()==f.target && selections==0,"Ready full getter registered again.");
        else{check(!status.completed && selections==1,"Unready full getter bypassed its registrar.");ready=true;f.services->callbacks->factory={};
            check(run.resume(f.services).completed && run.result()==result && selections==1 && polls==2,"Full getter lost pending callback or fresh result.");}}
    Fixture f;f.services->metadata->reset();IgbContextInfoLookupInvocation plain(IgbContextInfoLookup::plain);
    check(!plain.resume(f.services).completed,"Unknown plain metadata became known-null.");*f.services->metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(plain.resume(f.services).completed && !plain.result() && f.type_calls.empty(),"Plain null getter registered metadata.");
    f.services->values->signed_byte_prototype.reset();IgbSignedBytePrototypeInvocation unknown;
    check(!unknown.resume(f.services).completed,"Unknown signed-byte prototype became null.");f.services->values->signed_byte_prototype=f.character;
    check(unknown.resume(f.services).completed && unknown.result()==f.character && f.functions.empty(),"Existing prototype registered or returned class metadata.");
    for(const bool fresh_null:{false,true}){Fixture gate;auto canonical=gate.services->values;canonical->signed_byte_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        bool ready{};unsigned selections{},polls{};gate.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
            check(c.kind==C::invoke_function && c.function==0x80037d28,"Prototype getter selected another field class.");++selections;auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[&]{++polls;if(ready)canonical->signed_byte_prototype=fresh_null?std::shared_ptr<IgbDecodeRuntimeField>{}:gate.character;return MessengerCallResult{ready,0,"Prepared char registration."};};return op;};
        IgbSignedBytePrototypeInvocation lookup;check(!lookup.resume(gate.services).completed && selections==1,"Null prototype skipped registration.");
        gate.services->values=std::make_shared<IgbFieldValuesServices>();gate.services->values->signed_byte_prototype=std::make_shared<IgbDecodeRuntimeField>();
        gate.services->callbacks->factory={};ready=true;
        check(lookup.resume(gate.services).completed && lookup.result()==(fresh_null?std::shared_ptr<IgbDecodeRuntimeField>{}:gate.character) && selections==1 && polls==2,
            "Prototype getter changed canonical alias or fabricated the fresh null result.");
        check(lookup.resume().completed && polls==2,"Completed prototype getter registered again.");}
}

void defaults_and_fresh_fields(){
    Fixture f;f.target->fields->count=2;const auto inherited=std::make_shared<IgbDecodeRuntimeField>();f.target->fields->backing->at(0)=inherited;
    IgbContextInfoDefaultsInvocation run;check(run.resume(f.services).completed && f.target->fields->count==5 && f.target->fields->backing->at(0)==inherited,"Defaults lost inherited count/fields.");
    check(f.created[0]->object_type->lock()==f.related && igb_bootstrap_read_word(f.physical[0],0x38)==0x610008 && f.created[0]->flag_34==0xa5,"Defaults omitted type store or rewrote first construct flag.");
    check(f.created[1]->flag_35==1 && f.created[1]->flag_34==0xa5 && igb_bootstrap_read_byte(f.physical[1],0x35)==1 && f.null_interns==1,"Actual string default/flag35 order was omitted.");
    check(f.created[2]->element_field->lock()==f.character && igb_bootstrap_read_word(f.physical[2],0x3c)==0x620008 && igb_bootstrap_read_word(f.physical[2],0x38)==0,
        "Vendor field received class metadata or the wrong prototype member.");
    for(unsigned i=0;i<3;++i)check(f.created[i]->offset==8+4*i && f.created[i]->declaring_type->lock()==f.target && *f.services->globals->at(i)->slot==f.created[i],"Actual schema omitted offset/type/global publication.");
    check(f.related->class_flags==0 && f.named_interns==3 && f.metadata_allocations==0,"Defaults required related readiness or omitted field names.");
    check(run.resume().completed && f.null_interns==1 && f.created.size()==3,"Completed defaults replayed effects.");
    Fixture paused;paused.blocked_constructor=1;IgbContextInfoDefaultsInvocation capture;
    check(!capture.resume(paused.services).completed && paused.target->fields->count==1,"Pending constructor changed append prefix.");
    auto replacement=metadata();*paused.services->metadata=replacement;paused.blocked_constructor=99;
    check(capture.resume(paused.services).completed && paused.target->fields->count==3 && replacement->fields->count==0,"Defaults recaptured metadata after batch suspension.");
    Fixture fresh;fresh.services->meta_objects->metadata->metadata.reset();IgbContextInfoDefaultsInvocation ordered;
    check(!ordered.resume(fresh.services).completed && fresh.created.size()==3 && fresh.null_interns==0,"Unknown first dependency reached later string work.");
    const auto first=fresh.created[0],last=fresh.created[2];auto second=std::make_shared<IgbDecodeRuntimeField>(*fresh.created[1]);second->physical_storage.reset();
    auto vendor=std::make_shared<IgbDecodeRuntimeField>(*last);vendor->physical_storage.reset();
    fresh.target->fields->backing->at(0)=std::make_shared<IgbDecodeRuntimeField>(*first);fresh.target->fields->backing->at(1)=second;
    fresh.target->fields->backing->at(2)=vendor;fresh.services->names->at(0).reset();fresh.services->meta_objects->metadata->metadata=fresh.related;
    check(!ordered.resume(fresh.services).completed && first->object_type->lock()==fresh.related && second->flag_35==1 &&
        fresh.created[1]->flag_35==0x6a && vendor->element_field->lock()==fresh.character && last->element_field->expired(),"Defaults lost captured first descriptor or fresh later selections.");
}

void defaults_pending_and_identity(){
    Fixture allocation;allocation.services->meta_objects->metadata->metadata=std::shared_ptr<IgbRuntimeMetaType>{};allocation.allocation_ready=false;
    IgbContextInfoDefaultsInvocation lazy;check(!lazy.resume(allocation.services).completed && allocation.metadata_allocations==1 && allocation.created.size()==3 &&
        allocation.null_interns==0 && igb_bootstrap_read_word(allocation.physical[0],0x38)==0,"Lazy metadata did not suspend before the captured type store.");
    allocation.allocation_ready=true;allocation.services->meta_objects->metadata->factory={};
    check(lazy.resume(allocation.services).completed && allocation.metadata_polls==2 && allocation.metadata_allocations==1 && allocation.created[0]->object_type->lock()==allocation.related,
        "Lazy metadata continuation replayed allocation or lost its retained result.");
    Fixture unmapped;unmapped.services->schema->fields->pointer_lists->values.resize(1);IgbContextInfoDefaultsInvocation address;
    check(!address.resume(unmapped.services).completed && unmapped.null_interns==0 && unmapped.created[0]->object_type->expired(),"Missing metadata address was fabricated.");
    unmapped.services->schema->fields->pointer_lists->values.push_back({0x610008,unmapped.related,{}});
    check(address.resume(unmapped.services).completed && unmapped.metadata_allocations==0,"Late metadata identity repeated its lazy getter.");
    Fixture prototype;prototype.character->physical_storage.reset();IgbContextInfoDefaultsInvocation numeric;
    check(!numeric.resume(prototype.services).completed && prototype.null_interns==1 && prototype.created[1]->flag_35==1 &&
        igb_bootstrap_read_word(prototype.physical[2],0x3c)==0 && prototype.named_interns==0,"Missing prototype address bypassed or reordered prior string work.");
    prototype.services->values->signed_byte_prototype=std::make_shared<IgbDecodeRuntimeField>();
    prototype.services->schema->fields->pointer_lists->values.push_back({0x620008,{},prototype.character});
    check(numeric.resume(prototype.services).completed && prototype.created[2]->element_field->lock()==prototype.character &&
        igb_bootstrap_read_word(prototype.physical[2],0x3c)==0x620008,"Late address mapping recaptured the prototype global.");
    Fixture captured;captured.services->values->signed_byte_prototype.reset();IgbContextInfoDefaultsInvocation select;
    check(!select.resume(captured.services).completed && captured.created[1]->flag_35==1,"Unknown prototype did not follow string initialization.");
    const auto vendor=captured.created[2];auto replacement=std::make_shared<IgbDecodeRuntimeField>(*vendor);replacement->physical_storage.reset();
    captured.target->fields->backing->at(2)=replacement;captured.services->names->at(0).reset();captured.services->values->signed_byte_prototype=captured.character;
    check(!select.resume(captured.services).completed && vendor->element_field->lock()==captured.character && replacement->element_field->expired(),"Pending prototype getter recaptured its selected vendor field.");
    for(const bool truncate:{false,true}){Fixture bad;bad.services->values->signed_byte_prototype.reset();IgbContextInfoDefaultsInvocation selected;
        check(!selected.resume(bad.services).completed && bad.created[1]->flag_35==1,"Descriptor identity fixture did not reach the prototype boundary.");
        const auto allocation=bad.physical[2];const auto old=allocation.storage->bytes;
        if(truncate){old->bytes.resize(allocation.offset+0x3e);old->initialized.resize(allocation.offset+0x3e);}
        else allocation.storage->bytes=bytes(old->bytes.size(),0xd5);
        const auto prefix=old->bytes;bad.services->values->signed_byte_prototype=bad.character;
        rejects([&]{selected.resume(bad.services);},"Vendor element store accepted changed or truncated descriptor storage.");
        check(selected.faulted() && old->bytes==prefix && bad.created[2]->element_field->expired() && bad.named_interns==0 &&
            igb_bootstrap_read_word(bad.physical[0],0x38)==0x610008 && igb_buffer_word(bad.created[1]->default_bytes->buffer,0)==0 && bad.created[1]->flag_35==1,
            "Failed vendor store changed bytes or discarded the completed metadata/string prefix.");
        rejects([&]{selected.resume(bad.services);},"Faulted vendor store restarted.");}
}

void string_release_and_parent_guard(){
    Fixture f;const auto old=f.old_string();f.release_ready=false;IgbContextInfoDefaultsInvocation run;
    check(!run.resume(f.services).completed && old->references==0 && f.releases==1 && f.null_interns==1 && f.created[1]->flag_35==0x6a &&
        igb_buffer_word(f.created[1]->default_bytes->buffer,0)==0x630008 && !f.created[2]->element_field->lock(),"Pending release did not preserve the exact string/default prefix.");
    const auto captured=f.created[1]->default_bytes->buffer;auto other=bytes(64,0xd5);f.created[1]->default_bytes=IgbRuntimeFieldDefaultSource{other,0};
    const auto saved=f.services->schema->dependencies->factory;f.services->schema->dependencies->factory={};f.release_ready=true;f.services->names->at(0).reset();
    check(!run.resume(f.services).completed && f.releases==2 && f.null_interns==1 && igb_buffer_word(captured,0)==0 && igb_buffer_word(other,0)==0xd5d5d5d5 &&
        f.created[1]->flag_35==1 && f.created[2]->element_field->lock()==f.character,"Release continuation recaptured buffer, replayed decrement or missed following stores.");
    f.services->schema->dependencies->factory=saved;f.services->names->at(0)=std::make_shared<const std::string>("_pMeta");
    check(run.resume(f.services).completed && old->references==0 && f.releases==2,"String release could not resume through actual schema.");
    for(const auto count:{0u,0xffffffffu}){Fixture wrap;const auto header=wrap.old_string(count);IgbContextInfoDefaultsInvocation clear;
        check(clear.resume(wrap.services).completed && header->references==count-1 && wrap.releases==0 && wrap.created[1]->flag_35==1,"String release used packed object counts.");}
    Fixture lazy;lazy.services->schema->dependencies->globals->string_pool.reset();IgbContextInfoDefaultsInvocation pool;
    check(pool.resume(lazy.services).completed && lazy.string_calls.size()>=3 && lazy.string_calls[0]==N::create_string_pool &&
        lazy.string_calls[1]==N::initialize_string_pool && lazy.string_calls[2]==N::intern_name,"Null input bypassed the lazy string manager path.");
    Fixture fault;const auto released=fault.old_string();IgbContextInfoDefaultsInvocation outer;
    fault.string_action=[&](const IgbDependenciesCall& c){if(c.kind==N::release_string)try{outer.resume(fault.services);}catch(const std::runtime_error&){};};
    rejects([&]{outer.resume(fault.services);},"Caught outer recursion escaped71694 old-string release.");
    check(outer.faulted() && released->references==0 && fault.releases==1 && fault.null_interns==1 && fault.named_interns==0 &&
        igb_buffer_word(fault.created[1]->default_bytes->buffer,0)==0x630008 && fault.created[1]->flag_35==0x6a &&
        igb_bootstrap_read_byte(fault.physical[1],0x35)==0x6a && !fault.created[2]->element_field->lock() && !fault.services->globals->at(0)->slot->has_value(),
        "Caught parent fault published null, flag35, prototype or schema after the completed decrement.");
    check(igb_bootstrap_read_word(fault.physical[0],0x38)==0x610008,"String fault lost the earlier metadata pointer store.");
    rejects([&]{outer.resume(fault.services);},"Faulted string-default parent restarted.");
}

void captured_string_manager_branch(){
    for(const bool initial_pool:{false,true}){Fixture f;const auto globals=f.services->schema->dependencies->globals;
        if(!initial_pool)globals->string_pool.reset();std::shared_ptr<IgbScratchBuffer> captured;
        f.after_field=[&](unsigned index){if(index==1){captured=f.created[1]->default_bytes->buffer;f.created[1]->default_bytes.reset();}};
        IgbContextInfoDefaultsInvocation run;
        check(!run.resume(f.services).completed && captured && f.string_calls.empty() && f.created[1]->flag_35==0x6a,
            "Unknown field+20 did not suspend after the original manager-presence read.");
        const auto replacement=std::make_shared<ModelSceneData>();globals->string_pool=initial_pool?std::shared_ptr<ModelSceneObject>{}:replacement;
        f.created[1]->default_bytes=IgbRuntimeFieldDefaultSource{captured,0};f.services->names->at(0).reset();
        bool interned{};f.string_action=[&](const IgbDependenciesCall& c){if(c.kind==N::intern_name){interned=true;
            check(c.pool==(initial_pool?std::shared_ptr<ModelSceneObject>{}:f.pool),"Null string interning did not reload the manager after its captured branch.");}};
        check(!run.resume(f.services).completed && interned && f.null_interns==1 && f.named_interns==0 && f.created[1]->flag_35==1 &&
            igb_buffer_word(captured,0)==0,"Captured manager branch failed to finish the actual string default before schema.");
        const auto expected=initial_pool?std::vector<N>{N::intern_name}:std::vector<N>{N::create_string_pool,N::initialize_string_pool,N::intern_name};
        check(f.string_calls==expected && globals->string_pool==(initial_pool?std::shared_ptr<ModelSceneObject>{}:f.pool),
            "String default reloaded its manager-presence branch across pending field+20.");
    }
}

void probe_capture_and_cleanup(){
    for(const auto offset:{0u,4u,8u,12u,16u}){Fixture f;auto local=f.services->local_storage();f.core->vtable_offset=offset;
        IgbContextInfoProbeInvocation run(local);check(run.resume(f.services).completed && run.value()==(offset==0?0x804741d4u:offset==4?1u:offset==16?0xccccccccu:0u),"Probe selected the wrong original local word.");
        check(igb_bootstrap_read_word(local,16)==0xcccccccc && igb_bootstrap_read_word(local,0)==0x804741d4,"Probe touched the uninitialized vendor member or lost final VT.");}
    Fixture core;auto local=core.services->local_storage();core.services->types->runtime->singleton.reset();IgbContextInfoProbeInvocation capture(local);
    check(!capture.resume(core.services).completed && igb_bootstrap_read_word(local,0)==0x804717f0 && igb_bootstrap_read_word(local,8)==0xcccccccc,"Probe wrote derived members before capturing ArkCore.");
    core.core->vtable_offset.reset();core.services->types->runtime->singleton=core.core;
    check(!capture.resume(core.services).completed && igb_bootstrap_read_word(local,8)==0 && igb_bootstrap_read_word(local,12)==0,"Probe omitted null member stores before offset lookup.");
    auto other=std::make_shared<IgbRuntimeTypeRegistry>();other->vtable_offset=0;core.services->types->runtime->singleton=other;core.core->vtable_offset=16;
    check(capture.resume(core.services).completed && capture.value()==0xcccccccc,"Probe recaptured the global core while suspended.");

    Fixture cleanup;auto storage=cleanup.services->local_storage();cleanup.core->vtable_offset.reset();IgbContextInfoProbeInvocation release(storage);
    check(!release.resume(cleanup.services).completed,"Cleanup fixture did not pause after null member stores.");
    const auto victim=object(0x730008,0xa5800001);igb_bootstrap_write_word(storage,8,0x730008);igb_bootstrap_write_word(storage,12,0xdeadbeef);cleanup.core->vtable_offset=8;
    check(!release.resume(cleanup.services).completed,"Unmapped fresh object pointer was silently treated as null.");
    unsigned selects{},polls{};bool ready{};cleanup.services->object_cleanup->objects.push_back(victim);
    cleanup.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& c){check(c.kind==IgbObjectFieldConstructServiceKind::destroy_object && c.object.fields==victim.fields,"Probe selected wrong object cleanup.");++selects;
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{++polls;igb_bootstrap_write_word(storage,8,0);igb_bootstrap_write_word(storage,0,0x11111111);return MessengerCallResult{ready,0,"Prepared destructor."};};return op;};
    check(!release.resume(cleanup.services).completed && igb_object_reference_word(victim)==0xa5800000 && selects==1 && polls==1,"Cleanup lost full-word release or destruction boundary.");
    cleanup.services->object_cleanup->factory={};ready=true;
    check(release.resume(cleanup.services).completed && release.value()==0x730008 && polls==2 && selects==1 && igb_object_reference_word(victim)==0xa5800000 && cleanup.string_calls.empty(),
        "Probe replayed release, recaptured selected word or inspected literal-null string cleanup.");
    check(release.resume().completed && polls==2,"Completed probe replayed destruction.");
    for(const auto reference:{0u,0xffffffffu,2u}){Fixture wrap;auto local=wrap.services->local_storage();wrap.core->vtable_offset.reset();IgbContextInfoProbeInvocation run(local);
        check(!run.resume(wrap.services).completed,"Reference fixture did not suspend before cleanup.");const auto item=object(0x740008,reference);
        wrap.services->object_cleanup->objects.push_back(item);igb_bootstrap_write_word(local,8,0x740008);wrap.core->vtable_offset=0;
        check(run.resume(wrap.services).completed && igb_object_reference_word(item)==reference-1,"Object cleanup used a partial reference decrement or destroyed nonzero low23.");}
    Fixture fresh;auto observed=fresh.services->local_storage();fresh.core->vtable_offset.reset();IgbContextInfoProbeInvocation reread(observed);
    check(!reread.resume(fresh.services).completed,"Fresh reference fixture did not suspend.");const auto live=object(0x741008,0xa5800001);
    fresh.services->object_cleanup->objects.push_back(live);igb_bootstrap_write_word(observed,8,0x741008);fresh.core->vtable_offset=0;bool changed{};
    check(reread.resume(fresh.services,[&]{if(!changed && igb_object_reference_word(live)==0xa5800000){changed=true;igb_object_set_reference_word(live,0xa5800002);}}).completed &&
        changed && igb_object_reference_word(live)==0xa5800002,"Probe cleanup tested a cached low23 value after the full-word decrement.");
}

void probe_faults_and_wrap(){
    for(const auto offset:{16u,3u,20u}){Fixture f;auto local=f.services->local_storage();f.core->vtable_offset=offset;
        if(offset==16)local.storage->bytes->initialized[local.offset+16]=0;IgbContextInfoProbeInvocation bad(local);
        rejects([&]{bad.resume(f.services);},"Probe fabricated an unknown or unsupported selected word.");
        check(bad.faulted() && igb_bootstrap_read_word(local,0)==0x804741d4 && igb_bootstrap_read_word(local,8)==0 && igb_bootstrap_read_word(local,12)==0,"Probe read failure lost its reached store prefix.");
        rejects([&]{bad.resume(f.services);},"Faulted probe restarted.");}
    Fixture before_restore;auto observed=before_restore.services->local_storage();before_restore.core->vtable_offset.reset();
    observed.storage->bytes->initialized[observed.offset+16]=0;IgbContextInfoProbeInvocation marker(observed);
    check(!marker.resume(before_restore.services).completed,"Selected-word fault fixture did not pause after the first vtable/null stores.");
    igb_bootstrap_write_word(observed,0,0x12345678);before_restore.core->vtable_offset=16;
    rejects([&]{marker.resume(before_restore.services);},"Unknown selected word did not fault before the second vtable store.");
    check(igb_bootstrap_read_word(observed,0)==0x12345678 && igb_bootstrap_read_word(observed,8)==0 && igb_bootstrap_read_word(observed,12)==0,
        "Probe restored its vtable before completing the selected-word read.");
    Fixture short_member;auto short_local=raw(0x750008,14);igb_bootstrap_write_word(short_local,4,1);IgbContextInfoProbeInvocation short_run(short_local);
    rejects([&]{short_run.resume(short_member.services);},"Short string-member store was accepted.");
    check(igb_bootstrap_read_word(short_local,8)==0 && short_local.storage->bytes->bytes[short_local.offset+12]==0xcc,"Failed member word store was partial or lost preceding object-null store.");
    Fixture wrap;auto local=raw(0xfffffff8,20);igb_bootstrap_write_word(local,4,1);wrap.core->vtable_offset.reset();IgbContextInfoProbeInvocation wrapped(local);
    check(!wrapped.resume(wrap.services).completed,"Wrapped local fixture did not reach the unknown offset.");
    local.storage->bytes->initialized[local.offset+8]=0;wrap.core->vtable_offset=0;
    check(wrapped.resume(wrap.services).completed && wrapped.value()==0x804741d4,"Arithmetic null local+8 still read the physical member.");
    for(unsigned mutation=0;mutation<3;++mutation){Fixture identity;auto local=identity.services->local_storage();identity.core->vtable_offset.reset();IgbContextInfoProbeInvocation selected(local);
        check(!selected.resume(identity.services).completed,"Identity fixture did not suspend.");const auto captured=local.storage->bytes;const auto prefix=captured->bytes;
        if(mutation==0)local.storage->bytes=bytes(captured->bytes.size(),0xd5);else if(mutation==1)captured->released=true;else local.storage->original_address+=4;
        identity.core->vtable_offset=0;rejects([&]{selected.resume(identity.services);},"Probe continued with changed captured local storage.");
        check(selected.faulted() && captured->bytes==prefix,"Probe identity failure performed later stores.");}
    Fixture recursive;auto local2=recursive.services->local_storage();recursive.core->vtable_offset.reset();IgbContextInfoProbeInvocation reentry(local2);
    check(!reentry.resume(recursive.services).completed,"Recursive cleanup fixture did not suspend.");auto item=object(0x760008,1);
    recursive.services->object_cleanup->objects.push_back(item);igb_bootstrap_write_word(local2,8,0x760008);recursive.core->vtable_offset=0;
    recursive.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall&){auto op=std::make_shared<IgbObjectFieldConstructOperation>();
        op->resume=[&]{try{reentry.resume(recursive.services);}catch(const std::runtime_error&){};return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{reentry.resume(recursive.services);},"Caught probe recursion completed object destruction.");
    check(reentry.faulted() && igb_object_reference_word(item)==0,"Recursive cleanup lost its reached decrement.");
}

void abandoned_owner_lifetimes(){
    Fixture descriptor;descriptor.services->values->signed_byte_prototype.reset();std::weak_ptr<IgbScratchBuffer> old_descriptor;
    {auto run=std::make_unique<IgbContextInfoDefaultsInvocation>();
        check(!run->resume(descriptor.services).completed,"Descriptor lifetime fixture did not reach its pending prototype.");
        old_descriptor=descriptor.physical[2].storage->bytes;descriptor.physical[2].storage->bytes=bytes(0x58,0xd5);
        descriptor.services->values->signed_byte_prototype=descriptor.character;
        rejects([&]{run->resume(descriptor.services);},"Replaced descriptor bytes did not fault at the reached element store.");
        check(!old_descriptor.expired(),"Pending descriptor lost its captured old byte owner before abandonment.");}
    check(!old_descriptor.expired(),"Abandoned defaults discarded its captured descriptor byte owner.");
    descriptor.services->retained.clear();check(old_descriptor.expired(),"Retained descriptor byte owner formed a cycle.");

    Fixture probe;auto local=probe.services->local_storage();probe.core->vtable_offset.reset();std::weak_ptr<IgbScratchBuffer> old_object;unsigned destroys{};
    const auto victim=object(0x770008,1);probe.services->object_cleanup->objects.push_back(victim);
    probe.services->object_cleanup->factory=[&](const IgbObjectFieldConstructCall& c){check(c.kind==IgbObjectFieldConstructServiceKind::destroy_object,"Lifetime probe selected unrelated cleanup.");
        auto op=std::make_shared<IgbObjectFieldConstructOperation>();op->resume=[&]{++destroys;return MessengerCallResult{false,0,"Prepared pending destructor."};};return op;};
    {IgbContextInfoProbeInvocation run(local);check(!run.resume(probe.services).completed,"Object lifetime fixture did not pause before cleanup.");
        igb_bootstrap_write_word(local,8,0x770008);probe.core->vtable_offset=0;
        check(!run.resume(probe.services).completed && destroys==1 && igb_object_reference_word(victim)==0,"Pending destruction lost its completed reference decrement.");
        old_object=victim.storage->bytes;victim.storage->bytes=bytes(0x28,0xd5);}
    check(!old_object.expired() && igb_buffer_word(old_object.lock(),victim.offset+4)==0 && destroys==1,"Abandoned probe discarded the byte owner containing its completed decrement.");
    probe.services->retained.clear();check(old_object.expired(),"Retained cleanup byte owner formed a cycle.");

    Fixture allocation;allocation.services->schema->dependencies->globals->string_pool.reset();const auto old=allocation.old_string();
    std::weak_ptr<ModelSceneObject> returned_pool;unsigned creates{};auto outer=std::make_unique<IgbContextInfoDefaultsInvocation>();
    allocation.services->schema->dependencies->factory=[&](const IgbDependenciesCall& c){check(c.kind==N::create_string_pool && c.bytes==16,"Pool lifetime fixture passed its caught callback fault.");
        auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&,out=op.get()]{++creates;out->pool_result=std::make_shared<ModelSceneData>();returned_pool=out->pool_result;
            try{outer->resume(allocation.services);}catch(const std::runtime_error&){};return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{outer->resume(allocation.services);},"Caught outer recursion escaped lazy string-pool allocation.");
    check(outer->faulted() && creates==1 && old->references==1 && !allocation.services->schema->dependencies->globals->string_pool &&
        igb_buffer_word(allocation.created[1]->default_bytes->buffer,0)==0x630008 && allocation.created[1]->flag_35==0x6a &&
        allocation.created[2]->element_field->expired(),"Pool callback fault published a manager or continued string/default work.");
    outer.reset();check(!returned_pool.expired(),"Abandoned71694 discarded its unconsumed pool result.");
    allocation.services->values->unfinished.clear();check(returned_pool.expired(),"Retained unconsumed pool result formed a cycle.");

    Fixture interning;auto field=std::make_shared<IgbDecodeRuntimeField>();field->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0),0};
    std::weak_ptr<IgbInternedString> returned_string;unsigned interns{};
    auto setter=std::make_unique<IgbFieldStringDefaultInvocation>(field,SystemStageString{std::make_shared<const std::string>("new")});
    interning.services->schema->dependencies->factory=[&](const IgbDependenciesCall& c){check(c.kind==N::intern_name && *igb_dependency_name(c.name)=="new","String lifetime fixture changed its nonnull input.");
        auto op=std::make_shared<IgbDependenciesOperation>();op->resume=[&,out=op.get()]{++interns;out->string_result=std::make_shared<IgbInternedString>();
            out->string_result->container=interning.pool;returned_string=out->string_result;
            try{setter->resume(interning.services->values);}catch(const std::runtime_error&){};return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{setter->resume(interning.services->values);},"Caught recursion escaped nonnull string interning.");
    check(setter->faulted() && interns==1 && igb_buffer_word(field->default_bytes->buffer,0)==0 && !returned_string.expired() && returned_string.lock()->references==1,
        "Intern callback fault published its unconsumed string or changed its reference count.");
    setter.reset();check(!returned_string.expired(),"Abandoned71694 discarded its unconsumed string result.");
    interning.services->values->unfinished.clear();check(returned_string.expired(),"Retained unconsumed string result formed a cycle.");
}
}
int main(){try{exact_profile_and_registration();lookup_and_prototype_aliases();defaults_and_fresh_fields();defaults_pending_and_identity();string_release_and_parent_guard();captured_string_manager_branch();probe_capture_and_cleanup();probe_faults_and_wrap();abandoned_owner_lifetimes();
    std::cout<<"IGB context-info registration: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB context-info registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
