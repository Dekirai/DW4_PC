#include "dw4/igb_field_prototype.hpp"

#include <iostream>

namespace {
using namespace dw4;using E=IgbFieldTypeKind;using K=IgbFieldPrototypeServiceKind;using PK=IgbPointerListServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F fn,const char* why){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
auto metadata(bool abstract=false){auto m=std::make_shared<IgbRuntimeMetaType>();m->factory_redirect=0;m->flag_1a=abstract?1:0;m->allocation_prefix=0;
    m->native_size=0x44;m->type_word=0x804762fc;return m;}
auto field(){auto out=std::make_shared<IgbDecodeRuntimeField>();out->reference_word=1;out->vtable_word=0x804762fc;
    out->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};return out;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size){auto s=std::make_shared<IgbObjectStorage>();s->original_address=address;
    s->bytes=std::make_shared<IgbScratchBuffer>();s->bytes->bytes.assign(size,0xcc);s->bytes->initialized.assign(size,1);return {s,0};}
struct Fixture {
    std::shared_ptr<IgbFieldPrototypeServices> services=std::make_shared<IgbFieldPrototypeServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbDecodeRuntimeField> created=field();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    IgbBootstrapPointer backing=allocation(0x300000,16);
    std::function<void(const IgbPointerListCall&)> pointer_action;
    IgbFieldPrototypeFactory remaining;
    unsigned reflected_calls{},finalized{},pointer_calls{};
    Fixture(){
        services->registration=std::make_shared<IgbFieldTypeRegistrationServices>();auto r=services->registration;
        r->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();r->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(metadata(true));
        r->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=std::make_shared<IgbRegistrationMetaSlot>(metadata());
        r->types=std::make_shared<IgbTypeRegistrationServices>();r->types->metadata_pool_handle=5;r->types->runtime=std::make_shared<IgbRuntimeTypesServices>();r->types->runtime->singleton=core;
        r->pools=std::make_shared<IgbFilePoolsServices>();r->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        r->pools->memory_pools->selector=0;r->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->values=std::make_shared<IgbFieldValuesServices>();services->values->object_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        services->reference_prototype=std::make_shared<IgbRuntimeFieldSlot>(std::shared_ptr<IgbDecodeRuntimeField>{});
        services->values->signed_int_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        auto int_meta=metadata();int_meta->native_size=0x34;int_meta->type_word=0x80475b60;
        r->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_int)]=std::make_shared<IgbRegistrationMetaSlot>(int_meta);
        services->values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        auto memory_meta=metadata();memory_meta->native_size=0x50;memory_meta->type_word=0x80476088;
        r->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=std::make_shared<IgbRegistrationMetaSlot>(memory_meta);
        services->values->boolean_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        auto boolean_meta=metadata();boolean_meta->native_size=0x34;boolean_meta->type_word=0x80473e30;
        r->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::boolean)]=std::make_shared<IgbRegistrationMetaSlot>(boolean_meta);
        fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(4);fields->backing_bytes=backing.storage->bytes;fields->backing_address=0x300000;
        services->registry=std::make_shared<std::optional<std::shared_ptr<IgbDecodeFieldRegistry>>>(fields);
        services->schema=std::make_shared<IgbFieldSchemaServices>();services->schema->fields=std::make_shared<IgbFieldOperationsServices>();
        services->schema->fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        services->schema->fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{++pointer_calls;
            if(pointer_action)pointer_action(c);if(c.kind==PK::allocation_pool || c.kind==PK::object_pool)out->pool_result=pool;
            else if(c.kind==PK::allocation_size)out->size_result=16;else throw std::runtime_error("Unprepared prototype backing growth");return MessengerCallResult{true,0,{}};};return op;};
        // Prepared descriptor virtuals isolate the actual63F14 caller.
        services->schema->fields->factory=[this](const IgbFieldOperationCall& c){check(c.kind==IgbFieldOperationKind::field_virtual,"Unexpected finalization operation.");
            auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c]{++finalized;return MessengerCallResult{true,c.virtual_slot==0x64?4u:0u,{}};};return op;};
        services->factory=[this](const IgbFieldPrototypeCall& c){if(remaining)return remaining(c);check(c.kind==K::reflected_field && c.function==0x8006546c && c.pool==pool,"Prototype changed its prepared reflected call.");
            auto op=std::make_shared<IgbFieldPrototypeOperation>();op->resume=[this,out=op.get()]{++reflected_calls;out->field_result=created;out->field_address=0x200000;return MessengerCallResult{true,0,{}};};return op;};
    }
    auto slot(E kind){return igb_field_prototype_slot(services,kind);}
};
void gates_and_native_reflection(){
    for(const auto kind:{E::reference,E::object,E::signed_int,E::memory_reference,E::boolean}){Fixture f;*f.slot(kind)=f.created;f.services->registration.reset();f.services->factory={};IgbFieldPrototypeInvocation gate(kind);
        check(gate.resume(f.services).completed && f.created->reference_word==1 && f.fields->count==0,"Existing prototype failed null-only gate.");}
    Fixture unknown;unknown.slot(E::object)->reset();IgbFieldPrototypeInvocation missing(E::object);
    check(!missing.resume(unknown.services).completed && unknown.reflected_calls==0,"Unknown prototype was treated as null.");
    *unknown.slot(E::object)=std::shared_ptr<IgbDecodeRuntimeField>{};unknown.services->registration->types->metadata_pool_handle.reset();
    check(!missing.resume(unknown.services).completed && unknown.reflected_calls==0,"Prototype skipped current metadata handle.");
    Fixture abstract;abstract.services->reflected=std::make_shared<IgbFileFactoryServices>();abstract.services->factory={};IgbFieldPrototypeInvocation null(E::reference);
    check(null.resume(abstract.services).completed && abstract.slot(E::reference)->has_value() && !abstract.slot(E::reference)->value() && abstract.fields->count==0,
        "Actual abstract6546C did not publish null and skip prototype ownership/finalization.");
    Fixture concrete;concrete.services->reflected=std::make_shared<IgbFileFactoryServices>();auto bytes=allocation(0x500000,0x44);bytes.storage->bindings.push_back({0,{},{},{},{},concrete.created});unsigned allocations{};
    concrete.services->reflected->factory=[&](const IgbFileFactoryCall& c){check(c.kind==IgbFileFactoryServiceKind::allocate && c.pool==concrete.pool && c.bytes==0x44,"Actual reflected allocation changed metadata size or pool.");
        auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{++allocations;out->allocation_result=bytes.storage;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldPrototypeInvocation actual(E::object);const auto status=actual.resume(concrete.services);
    check(!status.completed && status.diagnostic.find("ArkCore")!=std::string::npos && allocations==1 && !concrete.slot(E::object)->value(),"Concrete reflection bypassed its actual initializer's missing ArkCore boundary.");
    check(!actual.resume(concrete.services).completed && allocations==1 && concrete.fields->count==0 && concrete.reflected_calls==0,"Pending initialization replayed allocation or used a direct constructor.");
    rejects([]{IgbFieldPrototypeInvocation invalid(E::base);},"Nonexistent base prototype callback was invented.");
}
void reflected_instance_publication(){
    Fixture f;f.services->reflected=std::make_shared<IgbFileFactoryServices>();
    auto initializer=std::make_shared<IgbObjectInitServices>();f.services->reflected->initializer=initializer;
    initializer->runtime=f.services->registration->types->runtime;f.core->vtable_offset=0;f.core->field_initialization_enabled=1;
    const auto meta=igb_field_type_metadata_slot(f.services->registration,E::object)->value();
    meta->allocation_prefix=8;meta->class_flags=4;meta->instance_count=0;meta->instances=std::shared_ptr<IgbObjectInstanceList>{};
    meta->fields=std::make_shared<IgbDecodeFieldRegistry>();meta->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    auto base=metadata();base->fields=std::make_shared<IgbDecodeFieldRegistry>();initializer->object_metadata=base;
    auto bytes=allocation(0x500000,0x4c);bytes.storage->bindings.push_back({8,{},{},{},{},f.created});const auto storage=std::weak_ptr(bytes.storage);
    unsigned allocations{},polls{};bool ready{};
    f.services->reflected->factory=[&](const IgbFileFactoryCall& c){check(c.kind==IgbFileFactoryServiceKind::allocate && c.bytes==0x4c,"Reflected prototype lost its allocation prefix.");
        auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{++allocations;out->allocation_result=bytes.storage;return MessengerCallResult{true,0,{}};};return op;};
    // Prepared empty field metadata and object virtuals isolate actual6546C,
    // 667F4 and the prototype callback. Real descriptor defaults are separate.
    initializer->factory=[&](const IgbObjectInitCall& c){
        check(c.object.field==f.created && !c.object.file && c.object.offset==8,"Prototype initializer changed its canonical typed allocation.");
        auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[&,c,out=op.get()]{
            if(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x58)out->metadata_result=meta;
            if(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x34){++polls;if(!ready)return MessengerCallResult{false,0,"Prepared field post-defaults."};}
            return MessengerCallResult{true,0,{}};};return op;};
    {
        IgbFieldPrototypeInvocation actual(E::object);
        check(!actual.resume(f.services).completed && allocations==1 && polls==1 && !f.slot(E::object)->value() && f.fields->count==0,
            "Prototype was published before retained reflected initialization completed.");
        check(f.created->physical_storage && f.created->physical_storage->offset==8 && !f.created->construction_active_.expired(),
            "Pending reflected prototype lost its physical owner or construction token.");
        ready=true;f.services->reflected->factory={};
        check(actual.resume(f.services).completed && allocations==1 && polls==2 && f.reflected_calls==0,
            "Native reflected field result used the prepared shortcut or replayed allocation.");
        check(f.slot(E::object)->value()==f.created && f.fields->count==1 && f.fields->backing->at(0)==f.created &&
            igb_buffer_word(f.backing.storage->bytes,0)==0x500008,"Actual prototype append lost its adjusted physical identity.");
        check(f.created->reference_word==1 && f.created->ordinal==0 && f.created->size==4 && f.finalized==3 && meta->instance_count==1,
            "Reflected prototype skipped initializer accounting or downstream ownership/finalization.");
        const auto ordinal_bytes=igb_buffer_bytes(bytes.storage->bytes,8+0x12,2);
        check(igb_buffer_word(bytes.storage->bytes,12)==1 && ordinal_bytes[0]==0 && ordinal_bytes[1]==0,
            "Prototype reference/ordinal publication left stale physical constructor storage.");
        bytes.storage.reset();
    }
    check(!storage.expired() && f.created->physical_storage->storage.lock()==storage.lock(),
        "Completed prototype lost physical storage when its reflected invocation was destroyed.");
    // Raw callback publication must still reject a completed file result.
    Fixture wrong;wrong.services->reflected=std::make_shared<IgbFileFactoryServices>();auto file=std::make_shared<LoaderRequestFile>();
    auto wrong_storage=allocation(0x620000,0x44);wrong_storage.storage->bindings.push_back({0,file});
    const auto wrong_meta=igb_field_type_metadata_slot(wrong.services->registration,E::object)->value();
    wrong_meta->class_flags=4;wrong_meta->instance_count=0;wrong_meta->instances=std::shared_ptr<IgbObjectInstanceList>{};
    wrong_meta->fields=std::make_shared<IgbDecodeFieldRegistry>();wrong_meta->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    auto init=std::make_shared<IgbObjectInitServices>();wrong.services->reflected->initializer=init;init->runtime=wrong.services->registration->types->runtime;
    wrong.core->vtable_offset=0;init->object_metadata=wrong_meta;
    init->factory=[&](const IgbObjectInitCall& c){auto op=std::make_shared<IgbObjectInitOperation>();op->resume=[c,wrong_meta,out=op.get()]{
        if(c.kind==IgbObjectInitServiceKind::object_virtual && c.virtual_slot==0x58)out->metadata_result=wrong_meta;return MessengerCallResult{true,0,{}};};return op;};
    wrong.services->reflected->factory=[&](const IgbFileFactoryCall&){auto op=std::make_shared<IgbFileFactoryOperation>();op->resume=[&,out=op.get()]{out->allocation_result=wrong_storage.storage;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldPrototypeInvocation invalid(E::object);rejects([&]{invalid.resume(wrong.services);},"Prototype accepted a completed file as its descriptor.");
    check(invalid.faulted() && !wrong.slot(E::object)->value() && wrong.fields->count==0,"Wrong typed reflected result changed prototype publication.");
}
void native_registry(){
    Fixture f;*f.services->registry=std::shared_ptr<IgbDecodeFieldRegistry>{};f.services->registries=std::make_shared<IgbFieldRegistryServices>();
    f.services->registries->bootstrap=std::make_shared<IgbBootstrapServices>();f.services->registries->bootstrap->runtime=f.services->registration->types->runtime;
    auto fields=std::make_shared<IgbDecodeFieldRegistry>();auto object=allocation(0x600000,16);object.storage->bindings.push_back({0,{},{},fields});auto backing=allocation(0x610000,16);
    unsigned allocations{},backings{};bool ready{};
    f.services->registries->bootstrap->factory=[&](const IgbBootstrapCall& c){check(c.kind==IgbBootstrapServiceKind::pool_allocate && c.pool==f.pool && c.bytes==16,"Registry getter changed actual2DB4C allocation.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,out=op.get()]{++allocations;out->allocation_result=object;return MessengerCallResult{true,0,{}};};return op;};
    f.services->registries->factory=[&](const IgbFieldRegistryCall& c){auto op=std::make_shared<IgbFieldRegistryOperation>();op->resume=[&,c,out=op.get()]{
        if(c.kind==IgbFieldRegistryServiceKind::object_pool)out->pool_result=f.pool;
        else{check(c.kind==IgbFieldRegistryServiceKind::allocate_backing && c.pool==f.pool && c.bytes==16,"Registry constructor selected another backing operation.");++backings;
            if(!ready)return MessengerCallResult{false,0,"prepared backing"};out->backing_result={backing,std::make_shared<IgbDecodeFieldRegistry::Storage>(4)};}
        return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldPrototypeRegistryInvocation get;check(!get.resume(f.services).completed && allocations==1 && !f.services->registry->value(),"Registry published before actual backing construction.");
    auto displaced=std::make_shared<IgbDecodeFieldRegistry>();std::weak_ptr<IgbDecodeFieldRegistry> weak=displaced;*f.services->registry=displaced;displaced.reset();ready=true;
    f.services->registration->types->metadata_pool_handle=9;f.services->registries->factory={};
    check(get.resume(f.services).completed && get.result()==fields && f.services->registry->value()==fields && allocations==1 && backings==2,"Registry getter replayed construction or skipped captured-result raw publication.");
    check(!weak.expired() && fields->count==0 && igb_bootstrap_read_word(object,0)==0x80475cb0 && igb_bootstrap_read_word(object,8)==0x610000,"Native registry reset/backing effects or displaced ownership differ.");
    *f.services->registry=f.fields;check(get.resume().completed && get.result()==fields,"Completed registry getter reloaded replacement.");
}
void append_release_finalize(){
    for(const auto kind:{E::reference,E::object,E::signed_int,E::memory_reference,E::boolean}){Fixture f;
        // The nonnull reference case represents a changed live abstract flag;
        // the canonical cold reference profile returns null in the test above.
        if(kind==E::reference)f.services->registration->reference_metadata->value()->flag_1a=0;
        IgbFieldPrototypeInvocation call(kind);
        check(call.resume(f.services).completed && f.slot(kind)->value()==f.created && f.reflected_calls==1,"Prepared nonnull prototype did not publish its returned descriptor.");
        check(f.fields->count==1 && f.fields->backing->at(0)==f.created && igb_buffer_word(f.backing.storage->bytes,0)==0x200000,"Prototype skipped actual raw field registry append.");
        check(f.created->reference_word==1 && f.created->ordinal==0 && f.finalized==3,"Prototype retain/release, ordinal or finalization order differs.");
        check(call.resume().completed && f.reflected_calls==1 && f.fields->count==1,"Completed prototype replayed append or reflection.");}
    Fixture live;auto old=field(),replacement=field();replacement->reference_word=7;auto registry=std::make_shared<IgbDecodeFieldRegistry>();registry->count=65538;bool changed{},release_ready{};unsigned destroys{};
    live.pointer_action=[&](const IgbPointerListCall& c){if(!changed && c.kind==PK::allocation_size){changed=true;*live.slot(E::object)=old;}};
    live.remaining=[&](const IgbFieldPrototypeCall& c){auto op=std::make_shared<IgbFieldPrototypeOperation>();
        if(c.kind==K::reflected_field)op->resume=[&,out=op.get()]{out->field_result=live.created;out->field_address=0x200000;return MessengerCallResult{true,0,{}};};
        else{check(c.kind==K::destroy_field && c.function==0x80066e1c && c.field==old,"Prototype release destroyed captured factory result instead of live global.");
            op->resume=[&]{++destroys;if(!release_ready)return MessengerCallResult{false,0,"prepared destroy"};*live.services->registry=registry;*live.slot(E::object)=replacement;return MessengerCallResult{true,0,{}};};}return op;};
    IgbFieldPrototypeInvocation call(E::object);check(!call.resume(live.services).completed && old->reference_word==0 && live.created->reference_word==2 && live.fields->count==1,
        "Pending live-global destruction lost completed append or repeated reference changes.");
    release_ready=true;live.services->factory={};check(call.resume(live.services).completed && destroys==2 && old->reference_word==0 && replacement->reference_word==7 && replacement->ordinal==1,
        "Prototype failed fresh registry/global reload or wrapped ordinal after retained destruction.");
    check(live.fields->backing->at(0)==live.created && registry->count==65538 && live.finalized==3,"Prototype appended replacement or changed later registry count.");
    Fixture null;null.pointer_action=[&](const IgbPointerListCall& c){if(c.kind==PK::allocation_size)*null.slot(E::object)=std::shared_ptr<IgbDecodeRuntimeField>{};};
    IgbFieldPrototypeInvocation invalid(E::object);rejects([&]{invalid.resume(null.services);},"Null reloaded prototype release did not fault.");
    check(null.fields->count==1 && null.created->reference_word==2 && invalid.faulted(),"Null release fault lost original append/retain prefix.");
}
void callbacks_and_missing(){
    Fixture f;auto factory=make_native_igb_field_prototype_function_factory(f.services);*f.slot(E::reference)=f.created;*f.slot(E::object)=f.created;*f.slot(E::signed_int)=f.created;*f.slot(E::memory_reference)=f.created;*f.slot(E::boolean)=f.created;
    for(const auto address:{0x80027454u,0x80028b64u,0x8002e440u,0x8002b178u,0x800388b0u}){IgbRegistrationCallbacksCall c;c.kind=IgbRegistrationCallbacksServiceKind::invoke_function;c.function=address;
        auto op=factory(c);check(op && op->resume().completed && !op->callbacks_result,"Prototype callback fabricated a returned deferred list.");}
    IgbRegistrationCallbacksCall other;other.kind=IgbRegistrationCallbacksServiceKind::invoke_function;other.function=0x800289f8;check(!factory(other),"Prototype adapter swallowed registration wrapper.");
    Fixture retained;retained.services->registration->types->metadata_pool_handle.reset();IgbRegistrationCallbacksCall c;c.kind=IgbRegistrationCallbacksServiceKind::invoke_function;c.function=0x80028b64;
    auto op=make_native_igb_field_prototype_function_factory(retained.services)(c);check(!op->resume().completed,"Missing prototype handle did not suspend.");
    std::weak_ptr<IgbFieldPrototypeServices> weak=retained.services;retained.services.reset();check(!weak.expired(),"Selected prototype lost services.");op.reset();check(weak.expired(),"Prototype adapter retained a permanent ownership cycle.");
    Fixture mismatch;mismatch.remaining=[&](const auto&){auto op=std::make_shared<IgbFieldPrototypeOperation>();op->resume=[out=op.get()]{out->field_address=0x200000;return MessengerCallResult{true,0,{}};};return op;};
    IgbFieldPrototypeInvocation bad(E::object);rejects([&]{bad.resume(mismatch.services);},"Nonnull physical result was treated as a null prototype.");
    check(!mismatch.slot(E::object)->value() && mismatch.fields->count==0,"Malformed reflected result changed canonical publication.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_prototype_tests");
    gates_and_native_reflection();reflected_instance_publication();native_registry();append_release_finalize();callbacks_and_missing();
    std::cout<<"igb_field_prototype_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Field prototype failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
