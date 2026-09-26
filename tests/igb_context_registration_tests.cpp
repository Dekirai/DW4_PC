#include "dw4/igb_context_registration.hpp"
#include "dw4/igb_field_values.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;using E=IgbContextKind;using D=IgbDataListKind;using L=IgbDataListLookup;
using T=IgbTypeRegistrationServiceKind;using C=IgbRegistrationCallbacksServiceKind;
using F=IgbFieldOperationKind;using S=IgbFieldSchemaServiceKind;using P=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class Fn>void rejects(Fn fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto bytes(std::size_t size,std::uint8_t value=0xcc){auto result=std::make_shared<IgbScratchBuffer>();result->bytes.assign(size,value);result->initialized.assign(size,1);return result;}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address-8;storage->bytes=bytes(size+8);return {storage,8};}
auto metadata(std::uint32_t flags=0){auto result=std::make_shared<IgbRuntimeMetaType>();result->class_flags=flags;result->reference_word=2;result->post_layout_callback=0;
    result->fields=std::make_shared<IgbDecodeFieldRegistry>();result->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(32);
    result->fields->backing_bytes=bytes(128,0);result->fields->backing_address=0x900000;return result;}
// Descriptor constructors and lower allocation/name endpoints are prepared.
// Batch append, lazy metadata getters, configuration and schema are native.
struct Fixture {
    std::shared_ptr<IgbContextRegistrationServices> services=std::make_shared<IgbContextRegistrationServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata(),extension=metadata(),info=metadata(),instances=metadata(),base=metadata(4);
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    IgbBootstrapPointer info_storage=raw(0x610008,0x64),instances_storage=raw(0x620008,0x64);
    std::vector<std::shared_ptr<IgbDecodeRuntimeField>> created;
    std::vector<IgbBootstrapPointer> physical;
    std::vector<std::uint32_t> constructors,functions;
    std::vector<IgbTypeRegistrationCall> type_calls;
    std::function<void(unsigned)> after_field;
    std::function<void()> during_allocation,after_name;
    unsigned blocked_constructor=99,constructor_polls{},metadata_allocations{},metadata_polls{},name_calls{};
    bool allocation_ready=true;
    Fixture(){
        services->context_metadata=std::make_shared<IgbRegistrationMetaSlot>(target);services->extension_metadata=std::make_shared<IgbRegistrationMetaSlot>(extension);
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        core->enabled=1;core->field_initialization_enabled=1;core->vtable_offset=0;core->registration_filter_enabled=0;core->registration_callback_pool=pool;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
        services->lists=std::make_shared<IgbDataListRegistrationServices>();auto& lists=*services->lists;lists.types=services->types;lists.callbacks=services->callbacks;
        lists.context_info_metadata=std::make_shared<IgbRegistrationMetaSlot>(info);lists.context_extension_metadata=std::make_shared<IgbRegistrationMetaSlot>(instances);
        lists.ref_object_metadata=std::make_shared<IgbRegistrationMetaSlot>(base);
        lists.pools=std::make_shared<IgbFilePoolsServices>();lists.pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        lists.pools->memory_pools->selector=0;lists.pools->memory_pools->fallback=make_original_igb_memory_pool_registry();
        (*lists.pools->memory_pools->fallback)->original_pool_access_bound=true;(*lists.pools->memory_pools->fallback)->pools[5]=pool;
        info_storage.storage->bindings.push_back({8,{},info});instances_storage.storage->bindings.push_back({8,{},instances});
        lists.allocations={info_storage,instances_storage};lists.local_storage=[](D){return raw(0x700008,20);};
        lists.metadata=std::make_shared<IgbMetadataFactoryServices>();lists.metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        lists.metadata->bootstrap->runtime=services->types->runtime;lists.metadata->metadata=metadata(4);
        lists.metadata->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==IgbMetadataFactoryServiceKind::register_type)op->resume=[]{return MessengerCallResult{true,0,{}};};
            else{check(c.kind==IgbMetadataFactoryServiceKind::reflected_factory && c.pool==pool,"Lazy context-list allocator lost captured pool.");++metadata_allocations;
                op->resume=[this,out=op.get()]{++metadata_polls;if(during_allocation)during_allocation();if(!allocation_ready)return MessengerCallResult{false,0,"Prepared lazy metadata."};
                    out->allocation_result=info_storage;return MessengerCallResult{true,0,{}};};}return op;};
        services->schema=std::make_shared<IgbFieldSchemaServices>();auto& schema=*services->schema;schema.types=services->types;schema.pools=lists.pools;
        schema.dependencies=std::make_shared<IgbDependenciesServices>();schema.dependencies->globals=std::make_shared<SystemStageServices>();schema.dependencies->globals->string_pool=pool;
        schema.dependencies->factory=[this](const IgbDependenciesCall& c){auto op=std::make_shared<IgbDependenciesOperation>();
            op->resume=[this,c,out=op.get()]{check(c.kind==IgbDependenciesServiceKind::intern_name,"Prepared context schema reached an unexpected string release.");++name_calls;
                auto name=std::make_shared<IgbInternedString>();name->value=igb_dependency_name(c.name);name->container=pool;out->string_result=name;if(after_name)after_name();return MessengerCallResult{true,0,{}};};return op;};
        schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->dependencies=schema.dependencies;
        schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();schema.fields->pointer_lists->values.push_back({0x600008,target,{}});
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==P::allocation_pool)out->pool_result=pool;
                else{check(c.kind==P::allocation_size,"Prepared context field registry unexpectedly needed growth.");out->size_result=128;}return MessengerCallResult{true,0,{}};};return op;};
        values->fields=schema.fields;values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        schema.fields->factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();
            op->resume=[this,c,out=op.get()]{if(c.kind==F::allocation_pool || c.kind==F::object_pool)out->pool_result=pool;
                else if(c.kind==F::allocation_size)return MessengerCallResult{true,64,{}};
                else throw std::runtime_error("Prepared context descriptor unexpectedly required a buffer allocation.");return MessengerCallResult{true,0,{}};};return op;});
        schema.factory=[this](const IgbFieldSchemaCall& c){check(c.kind==S::create_field,"Prepared field constructor reached unexpected creator destruction.");
            const auto index=static_cast<unsigned>(constructors.size());constructors.push_back(c.function);auto op=std::make_shared<IgbFieldSchemaOperation>();
            op->resume=[this,c,index,out=op.get(),done=false]()mutable{if(done)return MessengerCallResult{true,0,{}};++constructor_polls;
                if(index==blocked_constructor)return MessengerCallResult{false,0,"Prepared context descriptor."};
                auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=c.function==0x8002879c?0x804762fcu:0x80475b60u;field->size=4;
                field->name_string=std::shared_ptr<IgbInternedString>{};field->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
                field->default_bytes=IgbRuntimeFieldDefaultSource{bytes(64),0};field->default_bytes_address=0x800000+index*0x100;
                auto storage=raw(0x100008+index*0x100,0x44);storage.storage->bindings.push_back({8,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{storage.storage,8};
                igb_bootstrap_write_word(storage,0,*field->vtable_word);igb_bootstrap_write_word(storage,4,1);igb_bootstrap_write_halfword(storage,0x14,4);igb_bootstrap_write_word(storage,0x20,*field->default_bytes_address);
                if(c.function==0x8002879c){field->object_type=std::weak_ptr<IgbRuntimeMetaType>{};field->flag_34=0xa5;field->flag_35=0x6a;
                    igb_bootstrap_write_word(storage,0x38,0);igb_bootstrap_write_byte(storage,0x34,0xa5);igb_bootstrap_write_byte(storage,0x35,0x6a);}
                created.push_back(field);physical.push_back(storage);out->field_result=field;out->field_address=igb_bootstrap_address(storage);done=true;
                if(after_field)after_field(index);return MessengerCallResult{true,0,{}};};return op;};
        services->callbacks->factory=make_native_igb_context_registration_function_factory(services,make_native_igb_data_list_registration_function_factory(services->lists,
            [this](const IgbRegistrationCallbacksCall& c){functions.push_back(c.function);if(c.kind==C::invoke_function)return std::shared_ptr<IgbRegistrationCallbacksOperation>{};
                auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[c,out=op.get()]{if(c.kind==C::allocate_backing || c.kind==C::reallocate_backing){
                    check(c.bytes<=1024,"Prepared callback-list allocation exceeds its bound.");out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(c.bytes/4);}
                    else check(c.kind==C::free_backing || c.kind==C::free_list,"Unexpected callback-list service.");return MessengerCallResult{true,0,{}};};return op;}));
        services->types->factory=make_native_igb_context_registration_type_factory(services,make_native_igb_data_list_registration_type_factory(services->lists,
            make_native_igb_registration_list_factory(services->callbacks,[this](const IgbTypeRegistrationCall& c){type_calls.push_back(c);auto op=std::make_shared<IgbTypeRegistrationOperation>();
                op->resume=[this,c,out=op.get()]{switch(c.kind){
                    case T::pool_for_handle:out->object_result=pool;break;case T::register_parent:break;
                    case T::get_parent:out->metadata_result=base;break;case T::attach_parent:break;
                    case T::set_name:c.metadata->name=c.name;break;case T::publish_type:break;
                    case T::compute_layout:c.metadata->class_flags|=4;break;
                    case T::create_callbacks:out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();break;
                    default:throw std::runtime_error("Unexpected prepared context registrar service.");}return MessengerCallResult{true,0,{}};};return op;})));
    }
};
void exact_profiles(){
    Fixture f;constexpr std::array kinds{E::context,E::extension};constexpr std::array<std::uint32_t,2> globals{0x80561e00,0x80561df8},bodies{0x80037798,0x80037510};
    for(unsigned i=0;i<2;++i){const auto& p=igb_context_profile(kinds[i]);const auto a=igb_context_type_registration_arguments(f.services,kinds[i]);
        check(p.global==globals[i] && p.body==bodies[i] && a.metadata_slot==igb_context_metadata_slot(f.services,kinds[i]),"Context profile lost exact class/canonical identity.");
        check(a.flag==1 && a.native_size==20 && a.layout_probe==0 && a.trailing_callback==0 && *a.name==p.name,"Abstract context profile invented probe/runtime metadata.");
        check(a.parent_registration==(i?0x80037798u:0x80066b08u) && a.parent_getter==(i?0x80037594u:0x800237d0u),"Context parent/getter relationship changed.");
        if(i)check(!a.extra_callbacks && !a.defaults_callback && !p.plain_getter,"Extension invented defaults or plain getter.");
        else check(a.defaults_callback==0x8003782c && a.extra_callbacks && *a.extra_callbacks==std::vector<std::uint32_t>({0x80037140,0x80036f6c,0}),"Context returned callback order changed.");}
    constexpr std::array factory{0x8002879cu,0x8002e1acu,0x8002879cu};constexpr std::array names{"_pExtList","_iNumExts","_pExtInstances"};
    for(unsigned i=0;i<3;++i)check(f.services->factories->at(i)==factory[i] && **f.services->names->at(i)==names[i] && f.services->globals->at(i)->address==0x80561e04+4*i && f.services->offsets->at(i)==8+4*i,"Context schema tables differ from original3782C.");
    rejects([]{igb_context_profile(static_cast<E>(2));},"Unknown context kind was accepted.");
    rejects([]{IgbContextLookupInvocation bad(E::extension,IgbContextLookup::plain);},"Unrecovered extension plain getter was invented.");
}
void defaults_and_capture(){
    Fixture f;f.target->fields->count=2;IgbContextDefaultsInvocation run;
    check(run.resume(f.services).completed && f.target->fields->count==5 && f.created.size()==3,"Context defaults lost inherited count or actual batch append.");
    check(f.created[0]->object_type->lock()==f.info && f.created[2]->object_type->lock()==f.instances,"Object fields received wrong list dependencies.");
    check(igb_bootstrap_read_word(f.physical[0],0x38)==0x610008 && igb_bootstrap_read_word(f.physical[2],0x38)==0x620008,"Physical object type pointers were not published.");
    check(f.created[0]->flag_34==0xa5 && igb_bootstrap_read_byte(f.physical[0],0x34)==0xa5 && f.created[2]->flag_34==1 && igb_bootstrap_read_byte(f.physical[2],0x34)==1,"Defaults touched first construct flag or missed only second flag34 store.");
    for(unsigned i=0;i<3;++i)check(f.created[i]->offset==8+4*i && f.created[i]->declaring_type->lock()==f.target && *f.services->globals->at(i)->slot==f.created[i],"Actual schema omitted declaring type/offset/global publication.");
    check(f.info->class_flags==0 && f.instances->class_flags==0 && f.metadata_allocations==0,"Lazy dependency lookup registered ready classes.");
    check(run.resume().completed && f.created.size()==3,"Completed defaults replayed the batch.");
    Fixture paused;paused.blocked_constructor=1;const auto receiver=paused.target;IgbContextDefaultsInvocation captured;
    check(!captured.resume(paused.services).completed && receiver->fields->count==1,"Second constructor pause changed appended prefix.");
    *paused.services->context_metadata=metadata();paused.blocked_constructor=99;
    check(captured.resume(paused.services).completed && receiver->fields->count==3 && (**paused.services->context_metadata)->fields->count==0,"Defaults recaptured metadata after suspended batch.");
    Fixture first;first.services->lists->context_info_metadata->reset();IgbContextDefaultsInvocation live;
    check(!live.resume(first.services).completed && first.created.size()==3 && first.name_calls==0,"Unknown info dependency bypassed captured first field.");
    auto old_first=first.created[0],old_last=first.created[2];auto alternate=std::make_shared<IgbDecodeRuntimeField>(*old_last);
    alternate->physical_storage.reset();alternate->object_type=std::weak_ptr<IgbRuntimeMetaType>{};alternate->flag_34=0x31;
    first.target->fields->backing->at(0)=std::make_shared<IgbDecodeRuntimeField>(*old_first);first.target->fields->backing->at(0)->physical_storage.reset();
    first.target->fields->backing->at(2)=alternate;
    // Hold schema at its first unknown name before it selects registry fields.
    first.services->names->at(0).reset();*first.services->lists->context_info_metadata=first.info;
    check(!live.resume(first.services).completed && old_first->object_type->lock()==first.info && alternate->object_type->lock()==first.instances && alternate->flag_34==1 && old_last->flag_34==0xa5,"Defaults did not retain selected first field and freshly select third field.");
    Fixture unknown;unknown.after_field=[&](unsigned index){if(index==0){unknown.created[0]->flag_34.reset();unknown.physical[0].storage->bytes->initialized[unknown.physical[0].offset+0x34]=0;}};
    IgbContextDefaultsInvocation preserve;check(preserve.resume(unknown.services).completed && !unknown.created[0]->flag_34 && unknown.physical[0].storage->bytes->initialized[unknown.physical[0].offset+0x34]==0,
        "Context defaults fabricated the untouched first object's construct flag.");
}
void lazy_pending_and_identity(){
    Fixture f;*f.services->lists->context_info_metadata=std::shared_ptr<IgbRuntimeMetaType>{};f.allocation_ready=false;IgbContextDefaultsInvocation run;
    check(!run.resume(f.services).completed && f.metadata_allocations==1 && f.created.size()==3 && igb_bootstrap_read_word(f.physical[0],0x38)==0,"Lazy metadata allocator did not suspend before type store.");
    f.services->lists->metadata.reset();f.allocation_ready=true;
    check(run.resume(f.services).completed && f.metadata_polls==2 && f.metadata_allocations==1 && f.created[0]->object_type->lock()==f.info,"Lazy pending getter restarted allocation or lost captured result.");
    Fixture unmapped;unmapped.services->lists->allocations.clear();IgbContextDefaultsInvocation awaiting;
    check(!awaiting.resume(unmapped.services).completed && unmapped.name_calls==0 && igb_bootstrap_read_word(unmapped.physical[0],0x38)==0,"Missing metadata address was fabricated.");
    unmapped.services->schema->fields->pointer_lists->values.push_back({0x710008,unmapped.info,{}});
    check(!awaiting.resume(unmapped.services).completed && igb_bootstrap_read_word(unmapped.physical[0],0x38)==0x710008,"Late first metadata identity did not publish before missing second identity.");
    unmapped.services->schema->fields->pointer_lists->values.push_back({0x720008,unmapped.instances,{}});
    check(awaiting.resume(unmapped.services).completed,"Late dependency identities could not finish schema.");
    for(unsigned mutation=0;mutation<4;++mutation){Fixture bad;bad.services->lists->context_info_metadata->reset();IgbContextDefaultsInvocation selected;
        check(!selected.resume(bad.services).completed && bad.created.size()==3,"Identity fixture did not capture first field.");
        const auto original=bad.physical[0];if(mutation==0)bad.created[0]->physical_storage.reset();
        else if(mutation==1)original.storage->bytes=bytes(0x4c,0xd3);
        else if(mutation==2)original.storage->original_address+=0x1000;
        else original.storage->bindings.clear();
        *bad.services->lists->context_info_metadata=bad.info;
        rejects([&]{selected.resume(bad.services);},"Captured descriptor identity mutation was ignored.");
        check(selected.faulted() && bad.name_calls==0 && bad.created[2]->flag_34==0xa5,"Identity failure proceeded to later defaults/schema.");
        rejects([&]{selected.resume(bad.services);},"Descriptor identity fault was not permanent.");}
}
void context_registration_and_lookups(){
    Fixture f;IgbContextRegistrationInvocation body(E::context);
    check(body.resume(f.services).completed && f.target->flag_1a==1 && !f.target->type_word,"Abstract registrar invented concrete type word.");
    const auto callbacks=body.result();check(callbacks && callbacks->count==2 && callbacks->backing->at(0)==0x80037140 && callbacks->backing->at(1)==0x80036f6c,"Context registrar lost ordered list callbacks.");
    for(const auto& c:f.type_calls)check(c.kind!=T::layout_probe,"Abstract context executed a probe.");
    IgbContextRegistrationInvocation child(E::extension);check(child.resume(f.services).completed && f.extension->flag_1a==1 && !f.extension->type_word,"Abstract extension incorrectly constructed a runtime type.");
    auto abstract_services=std::make_shared<IgbFileFactoryServices>();IgbReflectedFactoryInvocation no_object(f.target,f.pool);f.target->factory_redirect=0;
    check(no_object.resume(abstract_services).completed && !no_object.result_instance().storage,"Abstract registered context allocated a concrete object.");
    for(const auto kind:{E::context,E::extension})for(const auto flags:{0u,4u,8u}){Fixture gate;auto cell=igb_context_metadata_slot(gate.services,kind);(**cell)->class_flags=flags;
        bool ready{};unsigned selections{},polls{};auto replacement=metadata(4);
        gate.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==C::invoke_function && c.function==igb_context_profile(kind).body,"Context full getter selected another registration body.");++selections;
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;if(ready)*cell=replacement;return MessengerCallResult{ready,0,"Prepared registration."};};return op;};
        IgbContextLookupInvocation lookup(kind);const auto status=lookup.resume(gate.services);
        if(flags==4)check(status.completed && selections==0,"Ready context getter registered again.");
        else{check(!status.completed && selections==1,"Unready context getter skipped wrapper.");ready=true;gate.services->callbacks->factory={};
            check(lookup.resume(gate.services).completed && lookup.result()==replacement && polls==2 && selections==1,"Context full getter lost retained callback or fresh result.");}}
    Fixture unknown;unknown.services->context_metadata->reset();IgbContextLookupInvocation plain(E::context,IgbContextLookup::plain);
    check(!plain.resume(unknown.services).completed,"Unknown context cell became known-null.");*unknown.services->context_metadata=std::shared_ptr<IgbRuntimeMetaType>{};
    check(plain.resume(unknown.services).completed && !plain.result() && unknown.type_calls.empty(),"Plain context getter registered null metadata.");
}
void context_list_profiles_and_probes(){
    for(const auto kind:{D::context_info,D::context_extension}){Fixture f;const bool info=kind==D::context_info;const auto& p=igb_data_list_profile(kind);
        const auto a=igb_data_list_type_registration_arguments(f.services->lists,kind);
        check(p.global==(info?0x80561de4u:0x80561de0u) && p.wrapper==(info?0x80037140u:0x80036f6cu) && p.body==(info?0x80037168u:0x80036f94u),"Context list profile used another canonical identity.");
        check(a.flag==0 && a.native_size==20 && a.parent_registration==0x8002907c && a.parent_getter==0x80024180 && !a.defaults_callback && !a.trailing_callback,"Context list inheritance/layout profile changed.");
        check(a.extra_callbacks && *a.extra_callbacks==std::vector<std::uint32_t>({info?0x80037330u:0x800374e8u,0}),"Context list extra callback changed.");
        for(const auto offset:{0u,4u,8u,12u,16u}){auto local=raw(0x700008,20);igb_bootstrap_write_word(local,4,1);f.core->vtable_offset=offset;
            const auto preserved=offset>=8?igb_bootstrap_read_word(local,offset):0u;IgbDataListLayoutProbeInvocation probe(local,kind);
            check(probe.resume(f.services->lists).completed && probe.value()==(offset==0?(info?0x80474230u:0x804742f8u):offset==4?1u:preserved),"List probe changed original final VT or untouched physical word.");}
        auto local=raw(0x700008,20);f.services->types->runtime->singleton.reset();IgbDataListLayoutProbeInvocation capture(local,kind);
        check(!capture.resume(f.services->lists).completed && igb_bootstrap_read_word(local,0)==0x80476c7c,"List probe captured ArkCore before its third VT.");
        f.core->vtable_offset.reset();f.services->types->runtime->singleton=f.core;
        check(!capture.resume(f.services->lists).completed && igb_bootstrap_read_word(local,0)==(info?0x80474230u:0x804742f8u),"List probe missed two class VTs after core capture.");
        auto fresh=std::make_shared<IgbRuntimeTypeRegistry>();fresh->vtable_offset=0;f.services->types->runtime->singleton=fresh;f.core->vtable_offset=16;igb_bootstrap_write_word(local,16,0x12345678);
        check(capture.resume(f.services->lists).completed && capture.value()==0x12345678,"List probe recaptured ArkCore or reset local payload.");
        auto unknown=raw(0x700108,20);unknown.storage->bytes->initialized[unknown.offset+16]=0;fresh->vtable_offset=16;IgbDataListLayoutProbeInvocation bad(unknown,kind);
        rejects([&]{bad.resume(f.services->lists);},"Unknown context-list local payload was fabricated.");
        auto slot=igb_data_list_metadata_slot(f.services->lists,kind);(**slot)->class_flags=0;IgbDataListLookupInvocation lazy(kind,L::allocate_if_null);
        check(lazy.resume(f.services->lists).completed && lazy.result()==**slot && f.metadata_allocations==0,"Existing lazy list getter required ready flags.");
        Fixture registrar;IgbDataListTypeRegistrationInvocation body(kind);check(body.resume(registrar.services->lists).completed,"Actual context-list registrar did not complete.");
        const auto callbacks=body.result();check(callbacks && callbacks->count==1 && callbacks->backing->at(0)==(info?0x80037330u:0x800374e8u) &&
            (**igb_data_list_metadata_slot(registrar.services->lists,kind))->class_flags==7,"Actual list registrar changed callback or readiness publication.");
    }
    Fixture boundary;IgbRegistrationCallbacksInvocation wrapper(0x80037168);
    check(!wrapper.resume(boundary.services->callbacks).completed && boundary.info->class_flags==7 &&
        std::count(boundary.functions.begin(),boundary.functions.end(),0x80037330)==1,"Info-list wrapper did not stop at the actual unrecovered37330 callback.");
}
void nested_parent_fault_prefixes(){
    Fixture constructor;IgbContextDefaultsInvocation outer;
    constructor.after_field=[&](unsigned index){check(index==0,"Faulted first constructor selected another descriptor.");
        try{outer.resume(constructor.services);}catch(const std::runtime_error&){};};
    rejects([&]{outer.resume(constructor.services);},"Caught outer recursion escaped the field constructor.");
    check(outer.faulted() && constructor.created.size()==1 && constructor.created[0]->reference_word==1 && constructor.target->fields->count==0 &&
        !constructor.target->fields->backing->at(0) && constructor.name_calls==0,"Constructor fault still appended or released its returned descriptor.");
    check(constructor.services->schema->fields->pointer_lists->values.size()==1,"Constructor fault published a new field address binding.");
    rejects([&]{outer.resume(constructor.services);},"Constructor parent fault resumed.");

    for(const bool finalize:{false,true}){Fixture buffer;IgbContextDefaultsInvocation configure;std::shared_ptr<IgbScratchBuffer> incoming;unsigned allocations{};
        const auto clear_default=[&]{buffer.created[0]->default_bytes=IgbRuntimeFieldDefaultSource{};buffer.created[0]->default_bytes_address=0;igb_bootstrap_write_word(buffer.physical[0],0x20,0);};
        if(!finalize)buffer.after_field=[&](unsigned index){if(index==0)clear_default();};
        const auto native=buffer.services->schema->fields->factory;
        buffer.services->schema->fields->factory=[&,native](const IgbFieldOperationCall& c){
            if(finalize && c.kind==F::field_virtual && c.virtual_slot==0x8c && c.field==buffer.created[0])clear_default();
            if(c.kind!=F::allocate_default)return native(c);
            ++allocations;check(c.bytes==4 && c.field==buffer.created[0],"Schema parent guard fixture allocated another descriptor payload.");
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[&,out=op.get()]{incoming=bytes(4,0xd5);incoming->initialized.assign(4,0);
                out->buffer_result={incoming,0};out->buffer_address=0x750000;
                try{configure.resume(buffer.services);}catch(const std::runtime_error&){};return MessengerCallResult{true,0,{}};};return op;};
        rejects([&]{configure.resume(buffer.services);},"Caught context recursion escaped nested schema buffer preparation.");
        check(configure.faulted() && allocations==1 && incoming && incoming->bytes==std::vector<std::uint8_t>(4,0xd5) && incoming->initialized==std::vector<std::uint8_t>(4,0),"Nested buffer fault wrote incoming default bytes.");
        check(buffer.created.size()==3 && buffer.target->fields->count==3 && buffer.created[0]->name_string && *buffer.created[0]->name_string &&
            buffer.created[0]->declaring_type->lock()==buffer.target && buffer.created[0]->offset==8 && buffer.created[0]->global_slot->lock()==buffer.services->globals->at(0)->slot &&
            !buffer.services->globals->at(0)->slot->has_value(),"Nested buffer fault lost reached name/schema stores or published finalized global.");
        check(!buffer.created[0]->default_bytes->buffer && buffer.created[0]->default_bytes_address==0 && igb_bootstrap_read_word(buffer.physical[0],0x20)==0 &&
            igb_bootstrap_read_word(buffer.physical[0],0x18)==0x600008 && igb_bootstrap_read_word(buffer.physical[0],0x1c)==0x80561e04 &&
            buffer.created[2]->flag_34==1,"Nested buffer fault published returned allocation or discarded earlier dependency stores.");
        rejects([&]{configure.resume(buffer.services);},"Nested schema parent fault resumed after callback return.");
    }

    Fixture reserve;IgbContextDefaultsInvocation append;const auto fields=reserve.target->fields;const auto old_backing=fields->backing;
    const auto old_bytes=fields->backing_bytes;const auto old_address=fields->backing_address;const auto original=old_bytes->bytes;
    IgbPointerListBacking incoming;unsigned reallocations{};
    reserve.services->schema->fields->pointer_lists->factory=[&](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();
        op->resume=[&,c,out=op.get()]{if(c.kind==P::allocation_pool || c.kind==P::object_pool)out->pool_result=reserve.pool;
            else if(c.kind==P::allocation_size)out->size_result=0;
            else{check(c.kind==P::reallocate && c.fields==fields && c.bytes==16,"First field append did not use original empty-capacity growth.");++reallocations;
                incoming.address=0x980000;incoming.bytes=bytes(16,0xd5);incoming.bytes->initialized.assign(16,0);incoming.fields=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);out->backing_result=incoming;
                try{append.resume(reserve.services);}catch(const std::runtime_error&){};}
            return MessengerCallResult{true,0,{}};};return op;};
    rejects([&]{append.resume(reserve.services);},"Caught context recursion escaped nested pointer-list growth.");
    check(append.faulted() && reallocations==1 && reserve.created.size()==1 && reserve.created[0]->reference_word==2 && fields->count==0 && fields->backing==old_backing &&
        fields->backing_bytes==old_bytes && fields->backing_address==old_address && old_bytes->bytes==original && !old_backing->at(0),"Reserve parent fault published backing/count/element or released the reached retain.");
    check(incoming.bytes->bytes==std::vector<std::uint8_t>(16,0xd5) && incoming.bytes->initialized==std::vector<std::uint8_t>(16,0),"Reserve parent fault zeroed an unpublished incoming allocation.");
    rejects([&]{append.resume(reserve.services);},"Reserve parent fault resumed after callback return.");
}
void checked_constructor_continuation(){
    Fixture f;bool ready{};unsigned checked_polls{},legacy_polls{};std::shared_ptr<IgbFieldSchemaOperation> selected;
    const auto original=f.services->schema->factory;
    f.services->schema->factory=[&,original](const IgbFieldSchemaCall& c){auto op=original(c);if(!selected){selected=op;const auto body=op->resume;
        op->resume_checked=[&,body](const std::function<void()>& guard){++checked_polls;guard();if(!ready)return MessengerCallResult{false,0,"Prepared checked constructor."};return body();};
        op->resume=[&]{++legacy_polls;return MessengerCallResult{false,0,"Unexpected legacy constructor."};};}return op;};
    IgbContextDefaultsInvocation run;check(!run.resume(f.services).completed && checked_polls==1 && legacy_polls==0 && f.created.empty(),"Batch did not prefer checked constructor continuation.");
    selected->resume_checked={};ready=true;
    check(run.resume(f.services).completed && checked_polls==2 && legacy_polls==0 && f.created.size()==3,"Pending batch reselected its constructor callback mode.");
}
void configure_byte_owner_identity(){
    for(unsigned mutation=0;mutation<5;++mutation){Fixture f;
        auto run=std::make_unique<IgbContextDefaultsInvocation>();
        std::shared_ptr<IgbScratchBuffer> captured,replacement;
        std::shared_ptr<IgbDependenciesOperation> intern;
        std::vector<std::uint8_t> prefix,prefix_mask,incoming,incoming_mask;
        const auto factory=f.services->schema->dependencies->factory;
        f.services->schema->dependencies->factory=[&,factory](const IgbDependenciesCall& c){intern=factory(c);return intern;};
        f.after_name=[&]{captured=f.physical[0].storage->bytes;prefix=captured->bytes;prefix_mask=captured->initialized;
            if(mutation!=3){replacement=bytes(captured->bytes.size(),0xd5);
                if(mutation==1)replacement->initialized.assign(replacement->bytes.size(),0);
                if(mutation==2)replacement->released=true;
                incoming=replacement->bytes;incoming_mask=replacement->initialized;}
            if(mutation==4)captured->released=true;
            f.physical[0].storage->bytes=replacement;};
        rejects([&]{run->resume(f.services);},"Intern callback redirected a captured descriptor byte owner.");
        check(run->faulted() && f.name_calls==1 && intern && intern->string_result && intern->string_result->references==1 &&
            intern->string_result->container==f.pool && *intern->string_result->value=="_pExtList",
            "Descriptor identity fault discarded or replayed the completed intern operation.");
        check(f.target->fields->count==3 && f.created[0]->object_type->lock()==f.info && f.created[2]->flag_34==1 &&
            f.created[0]->name_string && !*f.created[0]->name_string && f.created[0]->offset==0 && !f.created[0]->declaring_type &&
            !f.created[0]->global_slot_address && !f.services->globals->at(0)->slot->has_value(),
            "Descriptor identity fault lost earlier defaults or reached later name/schema publication.");
        check(captured->bytes==prefix && captured->initialized==prefix_mask && (!replacement ||
            (replacement->bytes==incoming && replacement->initialized==incoming_mask)),
            "Descriptor identity fault changed the captured prefix or replacement bytes.");
        captured->released=false;f.physical[0].storage->bytes=captured;
        rejects([&]{run->resume(f.services);},"Restoring the descriptor byte owner restarted faulted configuration.");
        check(f.name_calls==1 && captured->bytes==prefix && captured->initialized==prefix_mask,"Faulted configuration replayed interning or physical stores.");
        // Keep the reached old allocation alive even after the external binding
        // has been replaced and the failed continuation has been abandoned.
        std::weak_ptr<IgbScratchBuffer> retained=captured;f.physical[0].storage->bytes=replacement;captured.reset();run.reset();
        check(!retained.expired(),"Abandoned configuration lost its captured descriptor bytes.");
        f.services->schema->unfinished.clear();check(retained.expired(),"Captured descriptor bytes formed an ownership cycle.");
    }
    Fixture unknown;unknown.after_field=[&](unsigned index){if(index)return;const auto p=unknown.physical[0];
        for(const auto offset:{8u,0x0cu,0x18u,0x1cu})for(unsigned byte=0;byte<4;++byte)p.storage->bytes->initialized[p.offset+offset+byte]=0;};
    IgbContextDefaultsInvocation establish;check(establish.resume(unknown.services).completed,"Captured descriptor owner required unrelated known bytes.");
    for(const auto offset:{8u,0x18u,0x1cu})for(unsigned byte=0;byte<4;++byte)
        check(unknown.physical[0].storage->bytes->initialized[unknown.physical[0].offset+offset+byte]==1,"Reached schema store did not establish its physical bytes.");
    check(unknown.physical[0].storage->bytes->initialized[unknown.physical[0].offset+0x0c]==0,"Descriptor identity check fabricated untouched name bytes.");
    Fixture released;released.after_name=[&]{released.physical[0].storage->bytes->released=true;};
    IgbContextDefaultsInvocation reached;rejects([&]{reached.resume(released.services);},"Released captured allocation accepted a schema store.");
    check(released.name_calls==1 && released.created[0]->name_string && *released.created[0]->name_string &&
        (*released.created[0]->name_string)->references==1 && !released.created[0]->global_slot_address && released.created[0]->offset==0,
        "Byte-owner identity validation moved the existing released-buffer failure before completed name effects.");
}
}
int main(){try{exact_profiles();defaults_and_capture();lazy_pending_and_identity();context_registration_and_lookups();context_list_profiles_and_probes();nested_parent_fault_prefixes();checked_constructor_continuation();configure_byte_owner_identity();
    std::cout<<"IGB context registration: "<<checks<<" checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<"IGB context registration failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
