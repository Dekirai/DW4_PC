#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_thread_registration.hpp"

#include <iostream>

namespace {
using namespace dw4;using E=IgbFieldTypeKind;using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;
using MK=IgbMetadataFactoryServiceKind;using BK=IgbBootstrapServiceKind;
unsigned checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){bool caught{};try{fn();}catch(const std::runtime_error&){caught=true;}check(caught,message);}
auto meta(std::uint32_t flags=0){auto out=std::make_shared<IgbRuntimeMetaType>();out->class_flags=flags;return out;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t bytes){auto out=std::make_shared<IgbObjectStorage>();out->original_address=address;
    out->bytes=std::make_shared<IgbScratchBuffer>();out->bytes->bytes.assign(bytes,0xcc);out->bytes->initialized.assign(bytes,1);return {out,0};}
struct Fixture {
    std::shared_ptr<IgbFieldTypeRegistrationServices> services=std::make_shared<IgbFieldTypeRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    IgbBootstrapPointer object=allocation(0x100000,0x64),field_list=allocation(0x110000,16);
    std::vector<IgbTypeRegistrationCall> calls;
    IgbTypeRegistrationFactory remaining;IgbMetadataFactory metadata_remaining;
    unsigned allocations{};
    Fixture(){
        services->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
        services->meta_fields->metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->base_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->reference_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)]=
            std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::signed_int)]=
            std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::memory_reference)]=
            std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::boolean)]=
            std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        for(const auto kind:{IgbMetaFieldKind::string,IgbMetaFieldKind::signed_byte,IgbMetaFieldKind::enumeration,IgbMetaFieldKind::pointer,IgbMetaFieldKind::unsigned_byte,IgbMetaFieldKind::structure,IgbMetaFieldKind::unsigned_short})
            services->meta_fields->metadata_slots[static_cast<std::size_t>(kind)]=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->types=std::make_shared<IgbTypeRegistrationServices>();
        services->types->metadata_pool_handle=5;services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->bootstrap->runtime=services->types->runtime;
        services->metadata->dependencies=std::make_shared<IgbDependenciesServices>();services->metadata->dependencies->globals=std::make_shared<SystemStageServices>();
        services->metadata->dependencies->globals->string_pool=pool;
        object.storage->bindings.push_back({0,{},constructed,{}});field_list.storage->bindings.push_back({0,{},{},fields});
        services->metadata->bootstrap->factory=[this](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x64 && c.pool==pool,"Metadata factory changed its original pool/size.");
            auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[this,out=op.get()]{++allocations;out->allocation_result=object;return MessengerCallResult{true,0,{}};};return op;};
        // Prepared internal-list allocation isolates the actual metadata factory.
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){if(metadata_remaining)return metadata_remaining(c);
            check(c.kind==MK::create_fields,"Unexpected prepared metadata operation.");auto op=std::make_shared<IgbMetadataFactoryOperation>();
            op->resume=[this,out=op.get()]{out->allocation_result=field_list;out->fields_result=fields;return MessengerCallResult{true,0,{}};};return op;};
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,make_native_igb_metadata_factory(services->metadata,
            [this](const IgbTypeRegistrationCall& c){calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_field_type_registration_function_factory(services);

    }
    auto slot(E entry){return igb_field_type_metadata_slot(services,entry);}
};
void exact_profiles(){
    struct Expected {E entry;const char* name;std::uint32_t global,wrapper,body,size,flag,parent,getter,field,probe,defaults,extras;std::vector<std::uint32_t> callbacks;};
    const std::array expected{
        Expected{E::base,"igMetaField",0x805617bc,0x8002a6b0,0x8002a6d8,0x34,1,0x80066b08,0x800237d0,0x8002a744,0,0x8002a764,0x8055d290,{0x8002a0a8,0}},
        Expected{E::reference,"igRefMetaField",0x80561684,0x80027338,0x80027360,0x38,1,0x8002a6d8,0x80021d70,0x800273cc,0,0x800273ec,0x8055d158,{0x80027454,0}},
        Expected{E::object,"igObjectRefMetaField",0x805616e8,0x800289f8,0x80028a20,0x44,0,0x80027360,0x80025494,0x80028a94,0x80028934,0x80028ab4,0x804640c4,{0x8002a0a8,0x80028b64,0}},
        Expected{E::signed_int,"igIntMetaField",0x805619e0,0x8002e390,0x8002e3b8,0x34,0,0x8002a6d8,0x80021d70,0x8002e420,0x8002e308,0,0x8055d3c4,{0x8002e440,0}},
        Expected{E::memory_reference,"igMemoryRefMetaField",0x80561818,0x8002ae84,0x8002aeac,0x50,0,0x80027360,0x80025494,0x8002af20,0x8002ad90,0x8002af40,0x804649f0,{0x8002a6b0,0x8002b178,0}},
        Expected{E::boolean,"igBoolMetaField",0x80561e48,0x80038800,0x80038828,0x34,0,0x8002a6d8,0x80021d70,0x80038890,0x80038778,0,0x8055d690,{0x800388b0,0}},
        Expected{E::string,"igStringMetaField",0x805615b4,0x800253dc,0x80025404,0x3c,0,0x80027360,0x80025494,0x80025474,0x80025348,0x8002549c,0x8055d0e8,{0x80025628,0}},
        Expected{E::signed_byte,"igCharMetaField",0x80561e1c,0x80037d00,0x80037d28,0x34,0,0x8002a6d8,0x80021d70,0x80037d90,0x80037c78,0,0x8055d670,{0x80037db0,0}},
        Expected{E::enumeration,"igEnumMetaField",0x80561c30,0x8003155c,0x80031584,0x38,0,0x8002a6d8,0x80021d70,0x800315f4,0x800314d4,0x80031614,0x8055d510,{0x80031690,0}},
        Expected{E::pointer,"igRawRefMetaField",0x80561694,0x800276dc,0x80027704,0x38,0,0x80027360,0x80025494,0x8002776c,0x80027648,0,0x8055d180,{0x8002778c,0}},
        Expected{E::unsigned_byte,"igUnsignedCharMetaField",0x805614d0,0x8002320c,0x80023234,0x34,0,0x8002a6d8,0x80021d70,0x800232a0,0x80023180,0,0x8055cf70,{0x800232c0,0}},
        Expected{E::unsigned_int,"igUnsignedIntMetaField",0x805614bc,0x80022cb0,0x80022cd8,0x34,0,0x8002a6d8,0x80021d70,0x80022d40,0x80022c28,0,0x8055cf50,{0x80022d60,0}},
        Expected{E::structure,"igStructMetaField",0x80561560,0x8002460c,0x80024634,0x40,0,0x8002a6d8,0x80021d70,0x800246a8,0x80024580,0x800246c8,0x8055d090,{0x800247e4,0}},
        Expected{E::unsigned_short,"igUnsignedShortMetaField",0x80561494,0x80021ff4,0x8002201c,0x34,0,0x8002a6d8,0x80021d70,0x80022084,0x80021f6c,0,0x8055cf08,{0x800220a4,0}}
    };
    for(const auto& e:expected){Fixture f;const auto& p=igb_field_type_profile(e.entry);const auto args=igb_field_type_registration_arguments(f.services,e.entry);
        check(p.metadata_global==e.global && p.wrapper==e.wrapper && p.body==e.body && p.extras_global==e.extras,"Field-type profile changed canonical numeric identities.");
        check(args.metadata_slot==f.slot(e.entry) && *args.name==e.name && args.native_size==e.size && args.flag==e.flag,"Field-type arguments changed name, cell, size or abstract flag.");
        check(args.parent_registration==e.parent && args.parent_getter==e.getter && args.field_callback==e.field,"Field-type parent/body/getter identities differ.");
        check(args.layout_probe==e.probe && args.defaults_callback==e.defaults && args.trailing_callback==0 && *args.extra_callbacks==e.callbacks,"Field-type probe/defaults/extras arguments differ.");
        const auto value=meta();*f.slot(e.entry)=value;check(*args.metadata_slot==value,"Field-type arguments copied the original global.");
        if(e.entry==E::object)check(args.metadata_slot==f.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::object)],"Object field metadata acquired a separate cache.");
        if(e.entry==E::structure)check(args.metadata_slot==f.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::structure)] &&
            p.lazy_getter==0 && p.full_getter==0x80024544 && p.plain_getter==0x80093aa4,
            "Structure registration invented an independent metadata cell or getter.");
        if(e.entry==E::unsigned_short)check(args.metadata_slot==f.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_short)] &&
            p.lazy_getter==0 && p.full_getter==0x80021f30 && p.plain_getter==0x80094158,
            "Unsigned-short registration created a second metadata cell or an unsupported getter.");
        const auto owner=args.metadata_slot;f.services.reset();check(owner->value()==value,"Captured metadata cell lost its canonical owner.");
    }
    check(static_cast<unsigned>(E::unsigned_int)==11 && static_cast<unsigned>(E::structure)==12 &&
        static_cast<unsigned>(E::unsigned_short)==13 && static_cast<unsigned>(IgbMetaFieldKind::unsigned_short)==6 &&
        static_cast<unsigned>(IgbMetaFieldKind::structure)==12,"Unsigned-short admission shifted an existing field-type or raw profile.");
    rejects([]{igb_field_type_profile(static_cast<E>(16));},"Unknown field-type profile was accepted.");
    rejects([]{igb_field_type_registration_arguments({},E::base);},"Arguments accepted absent canonical storage.");
    rejects([]{IgbFieldTypeLookupInvocation invalid(E::reference,IgbFieldTypeLookup::allocate_if_null);},"An unsupported reference-field lazy getter was invented.");
    rejects([]{IgbFieldTypeLookupInvocation invalid(E::object,IgbFieldTypeLookup::plain);},"An unsupported object-field plain getter was invented.");
    for(const auto entry:{E::enumeration,E::pointer,E::unsigned_byte,E::structure,E::unsigned_short})rejects([&]{IgbFieldTypeLookupInvocation invalid(entry,IgbFieldTypeLookup::allocate_if_null);},
        "Enum or pointer lookup invented an allocate-only getter.");
}
void registration_gates(){
    for(const auto entry:{E::base,E::reference,E::object,E::signed_int,E::memory_reference,E::boolean,E::string,E::signed_byte,E::enumeration,E::pointer,E::unsigned_int,E::unsigned_short}){
        Fixture f;IgbFieldTypeRegistrationInvocation run(entry);check(!run.resume(f.services).completed,"Unknown parent registration completed.");
        check(f.allocations==1 && *f.slot(entry)==f.constructed && f.constructed->class_flags==8 && f.constructed->fields==f.fields,"Registration skipped actual metadata construction or busy publication.");
        check(!f.services->callbacks->prerequisites && f.calls.size()==1 && f.calls.back().kind==TK::register_parent && f.calls.back().function==igb_field_type_profile(entry).parent_body,
            "Field-type body introduced prerequisite guard or changed parent.");
        check(!run.resume(f.services).completed && f.allocations==1,"Pending parent replayed metadata construction.");
        for(const auto flags:{4u,8u,12u,0xffffffffu}){Fixture ready;const auto value=meta(flags);*ready.slot(entry)=value;IgbFieldTypeRegistrationInvocation gate(entry);
            check(gate.resume(ready.services).completed && !gate.result() && value->class_flags==flags && ready.allocations==0 && ready.calls.empty(),"Ready/busy registration changed original gates.");}
        Fixture earlier;*earlier.slot(entry)=meta(4);earlier.manager->pools[5].reset();IgbFieldTypeRegistrationInvocation pool_first(entry);
        check(!pool_first.resume(earlier.services).completed,"Ready metadata skipped the earlier actual pool lookup.");
        Fixture defaults;auto value=meta(1);*defaults.slot(entry)=value;
        defaults.remaining=[](const IgbTypeRegistrationCall& c){if(c.kind!=TK::set_name)return std::shared_ptr<IgbTypeRegistrationOperation>{};
            auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[c]{c.metadata->name=c.name;return MessengerCallResult{true,0,{}};};return op;};
        IgbFieldTypeRegistrationInvocation current(entry);check(!current.resume(defaults.services).completed,"Field-type registration skipped its reached defaults/probe.");
        const auto& reached=defaults.calls.back();const bool object=igb_field_type_profile(entry).probe!=0;
        check(reached.kind==(object?TK::layout_probe:TK::defaults) && reached.function==(object?igb_field_type_profile(entry).probe:igb_field_type_profile(entry).defaults),
            "Abstract parents invoked a fake probe or object probe was skipped.");
        check(value->class_flags==9 && value->flag_1a==(object?0:1) && value->native_size==igb_field_type_profile(entry).size && !value->type_word,
            "Pending field-type operation published guessed type identity or ready state.");
    }
}
void unsigned_short_ignores_prerequisite_state(){
    Fixture f;auto guards=std::make_shared<IgbRegistrationPrerequisitesState>();f.services->callbacks->prerequisites=guards;
    const auto cell=f.slot(E::unsigned_short);IgbFieldTypeRegistrationInvocation run(E::unsigned_short);
    check(!run.resume(f.services).completed && f.allocations==1 && cell->value()==f.constructed &&
        f.constructed->class_flags==8 && f.calls.size()==1 && f.calls.front().function==0x8002a6d8,
        "Unsigned-short body read unrelated prerequisite guards before its real parent boundary.");
    check(!guards->initialized && !guards->started,"Unsigned-short registration modified unrelated21B94 guards.");
    auto replacement=std::make_shared<IgbRegistrationMetaSlot>(meta(4));
    f.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_short)]=replacement;
    check(!run.resume(f.services).completed && f.allocations==1 && cell->value()==f.constructed &&
        replacement->value()->class_flags==4 && f.constructed->class_flags==8,
        "Pending unsigned-short registration redirected its captured metadata cell or reran allocation.");
}

void actual_parent_chain(){
    Fixture f;std::vector<std::shared_ptr<IgbRuntimeMetaType>> constructed;std::vector<IgbBootstrapPointer> storage;unsigned list_allocations{};
    f.services->metadata->bootstrap->factory=[&](const IgbBootstrapCall& c){check(c.kind==BK::pool_allocate && c.bytes==0x64 && c.pool==f.pool,"Parent-chain allocation changed native metadata size/pool.");
        auto op=std::make_shared<IgbBootstrapOperation>();op->resume=[&,out=op.get()]{auto value=meta();auto physical=allocation(0x300000+static_cast<std::uint32_t>(constructed.size())*0x100,0x64);
            physical.storage->bindings.push_back({0,{},value,{}});constructed.push_back(value);storage.push_back(physical);out->allocation_result=physical;return MessengerCallResult{true,0,{}};};return op;};
    f.services->metadata->factory=[&](const IgbMetadataFactoryCall& c){check(c.kind==MK::create_fields,"Parent chain selected another prepared allocation.");auto op=std::make_shared<IgbMetadataFactoryOperation>();
        op->resume=[&,out=op.get()]{auto fields=std::make_shared<IgbDecodeFieldRegistry>();auto local=allocation(0x400000+(++list_allocations)*0x100,16);local.storage->bindings.push_back({0,{},{},fields});
            out->allocation_result=local;out->fields_result=fields;return MessengerCallResult{true,0,{}};};return op;};
    f.services->types->factory=make_native_igb_field_type_registration_type_factory(f.services,f.services->types->factory);
    IgbFieldTypeRegistrationInvocation chain(E::object);check(!chain.resume(f.services).completed && constructed.size()==3,"Actual object/reference/base registration chain did not construct three metadata owners.");
    check(*f.slot(E::object)==constructed[0] && *f.slot(E::reference)==constructed[1] && *f.slot(E::base)==constructed[2],"Parent chain changed original canonical publication order.");
    check(f.calls.size()==1 && f.calls.back().kind==TK::register_parent && f.calls.back().function==0x80066b08,"Three-class chain did not reach actual igObject parent body.");
    for(const auto& value:constructed)check(value->class_flags==8 && value->fields && value->parent.has_value() && !*value->parent && value->type_word==0,
        "Unbound igObject parent was replaced by prepared ready metadata.");
    check(!chain.resume(f.services).completed && constructed.size()==3,"Pending igObject parent replayed the actual cold metadata chain.");
}
void deferred_tables(){
    for(const auto entry:{E::base,E::reference,E::object,E::signed_int,E::memory_reference,E::boolean,E::string,E::signed_byte,E::enumeration,E::pointer,E::unsigned_byte,E::unsigned_int,E::structure,E::unsigned_short}){Fixture f;auto value=meta(3);value->post_layout_callback=0;*f.slot(entry)=value;
        if(entry==E::unsigned_byte || entry==E::structure){f.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
            f.services->callbacks->prerequisites->initialized=std::uint8_t{1};f.services->callbacks->prerequisites->started=std::uint8_t{1};}
        auto list=std::make_shared<IgbRegistrationCallbackList>();unsigned list_allocations{},backing_allocations{};
        f.services->callbacks->runtime=f.services->types->runtime;f.core->registration_callback_pool=std::shared_ptr<ModelSceneObject>{};
        f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.kind==CK::allocate_backing && c.bytes==16,"Deferred table changed its first backing request.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&,out=op.get()]{++backing_allocations;out->backing_result=std::make_shared<IgbRegistrationCallbackList::Storage>(4);return MessengerCallResult{true,0,{}};};return op;};
        f.remaining=[&](const IgbTypeRegistrationCall& c){auto op=std::make_shared<IgbTypeRegistrationOperation>();
            if(c.kind==TK::compute_layout)op->resume=[c]{c.metadata->class_flags|=4;return MessengerCallResult{true,0,{}};};
            else{check(c.kind==TK::create_callbacks && c.bytes==12 && c.pool==f.pool,"Deferred table requested another prepared endpoint.");
                op->resume=[&,out=op.get()]{++list_allocations;out->callbacks_result=list;return MessengerCallResult{true,0,{}};};}return op;};
        f.services->types->factory=make_native_igb_registration_list_factory(f.services->callbacks,f.services->types->factory);
        IgbFieldTypeRegistrationInvocation run(entry);check(run.resume(f.services).completed && run.result()==list && value->class_flags==7,"Original deferred callbacks failed body completion.");
        const auto expected=igb_field_type_registration_arguments(f.services,entry).extra_callbacks;
        check(list->count==static_cast<std::int32_t>(expected->size()-1) && list_allocations==1 && backing_allocations==1 && list->capacity==4,"Deferred callbacks changed count, native append or allocation.");
        for(std::size_t i=0;i<expected->size()-1;++i)check(list->backing->at(i)==expected->at(i),"Original extra callback order differs.");
    }
}
void getters_and_lifetimes(){
    for(const auto entry:{E::base,E::reference,E::object,E::signed_int,E::memory_reference,E::boolean,E::string,E::signed_byte,E::enumeration,E::pointer,E::unsigned_byte,E::unsigned_int,E::structure,E::unsigned_short}){
        Fixture f;auto ready=meta(4);*f.slot(entry)=ready;f.services->types.reset();f.services->pools.reset();IgbFieldTypeLookupInvocation run(entry);
        check(run.resume(f.services).completed && run.result()==ready,"Ready full getter required registration or unrelated dependencies.");
        *f.slot(entry)=meta();check(run.resume().completed && run.result()==ready,"Completed getter reloaded metadata.");
        Fixture pending;*pending.slot(entry)=meta(8);auto replacement=meta(0);unsigned selections{},polls{};
        pending.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){++selections;check(c.kind==CK::invoke_function && c.function==igb_field_type_profile(entry).body,"Getter selected another wrapper body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{if(++polls==1)return MessengerCallResult{false,0,"prepared body"};*pending.slot(entry)=replacement;return MessengerCallResult{true,0,{}};};return op;};
        IgbFieldTypeLookupInvocation full(entry);check(!full.resume(pending.services).completed,"Full getter ignored busy-but-unready registration.");
        *pending.slot(entry)=meta(4);pending.services->callbacks->factory={};
        check(full.resume(pending.services).completed && full.result()==replacement && selections==1 && polls==2,"Getter replayed retained registration or rechecked ready after return.");
        Fixture late;*late.slot(entry)=meta();unsigned calls{};late.services->callbacks->factory=[&](const auto&){auto op=std::make_shared<IgbRegistrationCallbacksOperation>();
            op->resume=[&]{++calls;late.slot(entry)->reset();return MessengerCallResult{true,0,{}};};return op;};
        IgbFieldTypeLookupInvocation reload(entry);check(!reload.resume(late.services).completed && calls==1,"Missing reloaded state was treated as null.");
        *late.slot(entry)=std::shared_ptr<IgbRuntimeMetaType>{};check(reload.resume(late.services).completed && !reload.result() && calls==1,"Known-null reload replayed registration.");
    }
    for(const auto entry:{E::base,E::reference,E::enumeration,E::pointer,E::unsigned_byte,E::unsigned_int,E::structure,E::unsigned_short}){Fixture f;f.services->types.reset();f.services->callbacks.reset();IgbFieldTypeLookupInvocation run(entry,IgbFieldTypeLookup::plain);
        check(run.resume(f.services).completed && !run.result(),"Plain parent getter constructed known-null metadata.");}
    for(const auto entry:{E::base,E::object,E::boolean,E::unsigned_int}){
        Fixture f;f.services->types->metadata_pool_handle.reset();IgbFieldTypeLookupInvocation run(entry,IgbFieldTypeLookup::allocate_if_null);
        check(!run.resume(f.services).completed && f.allocations==0,"Lazy field-type getter skipped original handle read.");f.services->types->metadata_pool_handle=5;
        check(run.resume(f.services).completed && run.result()==f.constructed && *f.slot(entry)==f.constructed && f.allocations==1 && f.constructed->class_flags==0,"Lazy getter skipped actual factory or added registration.");
        Fixture overwritten;std::weak_ptr<IgbRuntimeMetaType> displaced;overwritten.metadata_remaining=[&](const IgbMetadataFactoryCall& c){check(c.kind==MK::create_fields,"Unexpected lazy factory endpoint.");
            auto op=std::make_shared<IgbMetadataFactoryOperation>();op->resume=[&,out=op.get()]{auto value=meta(9);value->reference_word=19;displaced=value;*overwritten.slot(entry)=value;
                out->allocation_result=overwritten.field_list;out->fields_result=overwritten.fields;return MessengerCallResult{true,0,{}};};return op;};
        {IgbFieldTypeLookupInvocation publish(entry,IgbFieldTypeLookup::allocate_if_null);check(publish.resume(overwritten.services).completed && publish.result()==overwritten.constructed,"Lazy factory result did not overwrite callback replacement.");}
        check(!displaced.expired() && displaced.lock()->reference_word==19,"Raw lazy publication released displaced guest ownership.");
    }
}
void prerequisite_profiles(E entry,IgbMetaFieldKind kind){
    Fixture f;auto fields=f.services->meta_fields;const auto old_cell=f.slot(entry);
    auto fresh_cell=std::make_shared<IgbRegistrationMetaSlot>(meta(4));f.services->meta_fields.reset();
    auto state=std::make_shared<IgbRegistrationPrerequisitesState>();state->initialized=std::uint8_t{0};state->started=std::uint8_t{0};
    f.services->callbacks->prerequisites=state;std::vector<std::uint32_t> selections;unsigned polls{};bool ready{};
    f.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
        check(c.kind==CK::invoke_function,"Prerequisite profile prerequisites selected a callback-list allocation.");selections.push_back(c.function);
        auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&,c]{
            check(state->started==1 && state->initialized==1 && f.allocations==0 && f.calls.empty(),
                "Prerequisite profile registration captured registrar state before committing prerequisite guards.");
            if(c.function==0x80023880){++polls;if(!ready)return MessengerCallResult{false,0,"Prepared first prerequisite."};}
            if(c.function==0x800374e8){fields->metadata_slots[static_cast<std::size_t>(kind)]=fresh_cell;
                f.services->meta_fields=fields;}
            return MessengerCallResult{true,0,{}};
        };return op;
    };
    IgbFieldTypeRegistrationInvocation call(entry);
    check(!call.resume(f.services).completed && selections==std::vector<std::uint32_t>{0x80023880} && polls==1,
        "Absent class services prevented the earlier original prerequisite callback.");
    check(old_cell->has_value() && !old_cell->value() && !call.faulted(),"Pending prerequisites published metadata or faulted the retained caller.");
    ready=true;
    check(call.resume(f.services).completed && !call.result() && polls==2 && f.allocations==0 && f.calls.empty() &&
        selections==std::vector<std::uint32_t>({0x80023880,0x800240cc,0x80023c40,0x80031250,0x800374e8}) &&
        f.slot(entry)==fresh_cell && old_cell->has_value() && !old_cell->value(),
        "Prerequisite profile body replayed guards, changed prerequisite order, or captured the old registrar cell.");
    f.services->callbacks.reset();f.services->meta_fields.reset();
    check(call.resume().completed && polls==2,"Completed prerequisite profile body reentered prerequisite work.");

    Fixture unknown;*unknown.slot(entry)=meta(4);IgbFieldTypeRegistrationInvocation wait(entry);
    check(!wait.resume(unknown.services).completed && unknown.allocations==0 && unknown.calls.empty(),
        "Ready class skipped its earlier unknown prerequisite state.");
    unknown.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    unknown.services->callbacks->prerequisites->initialized=std::uint8_t{1};unknown.services->callbacks->prerequisites->started=std::uint8_t{1};
    check(wait.resume(unknown.services).completed && unknown.allocations==0,"Late prerequisite guards failed to resume the ready class.");

    Fixture cold;cold.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    cold.services->callbacks->prerequisites->initialized=std::uint8_t{1};cold.services->callbacks->prerequisites->started=std::uint8_t{1};
    IgbFieldTypeRegistrationInvocation construct(entry);
    check(!construct.resume(cold.services).completed && cold.allocations==1 && *cold.slot(entry)==cold.constructed &&
        cold.constructed->class_flags==8 && cold.calls.back().kind==TK::register_parent && cold.calls.back().function==0x8002a6d8,
        "Completed prerequisites bypassed actual prerequisite profile metadata construction or parent registration.");
    check(!construct.resume(cold.services).completed && cold.allocations==1,
        "Pending prerequisite profile parent replayed construction.");
}
void unsigned_int_canonical_cell(){
    Fixture f;const auto cell=f.services->meta_fields->metadata;
    f.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_int)]=
        std::make_shared<IgbRegistrationMetaSlot>(meta(4));
    check(f.slot(E::unsigned_int)==cell,"Unsigned integer registration used a second metadata cell.");
    f.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    f.services->types->metadata_pool_handle.reset();IgbFieldTypeRegistrationInvocation run(E::unsigned_int);
    check(!run.resume(f.services).completed && !cell->value() && f.allocations==0,
        "Unsigned integer skipped its canonical null or the earlier missing pool handle.");
    const auto replacement=std::make_shared<IgbRegistrationMetaSlot>(meta(4));
    f.services->meta_fields->metadata=replacement;f.services->types->metadata_pool_handle=5;
    check(!run.resume(f.services).completed && cell->value()==f.constructed && f.constructed->class_flags==8 &&
        replacement->value()->class_flags==4 && f.allocations==1 && f.calls.size()==1 &&
        f.calls.back().kind==TK::register_parent && f.calls.back().function==0x8002a6d8,
        "Unsigned integer lost its captured cell or introduced prerequisite work.");
    check(!f.services->callbacks->prerequisites->initialized && !f.services->callbacks->prerequisites->started,
        "Unsigned integer body initialized the unsigned-byte prerequisite guards.");
    check(!run.resume(f.services).completed && f.allocations==1,"Pending unsigned integer registration repeated allocation.");

    Fixture absent;absent.services->meta_fields->metadata.reset();
    absent.services->meta_fields->metadata_slots[static_cast<std::size_t>(IgbMetaFieldKind::unsigned_int)]=replacement;
    IgbFieldTypeLookupInvocation missing(E::unsigned_int,IgbFieldTypeLookup::plain);
    check(!missing.resume(absent.services).completed,"Unsigned integer plain getter substituted the indexed metadata cache.");
    absent.services->meta_fields->metadata=cell;
    check(missing.resume(absent.services).completed && missing.result()==f.constructed,"Late canonical unsigned integer cell was not retained.");
}
void adapters(){
    Fixture f;std::array<std::shared_ptr<IgbRuntimeMetaType>,14> owners;
    for(auto& owner:owners)owner=meta(4);
    for(std::size_t i=0;i<owners.size();++i)*f.slot(static_cast<E>(i))=owners[i];
    auto factory=make_native_igb_field_type_registration_type_factory(f.services);
    const std::array<std::pair<std::uint32_t,E>,40> getters{{{0x8002a638,E::base},{0x8002a674,E::base},{0x8002a744,E::base},{0x80021d70,E::base},
        {0x800272fc,E::reference},{0x800273cc,E::reference},{0x80025494,E::reference},{0x800288bc,E::object},{0x800288f8,E::object},{0x80028a94,E::object},
        {0x8002e2cc,E::signed_int},{0x8002e420,E::signed_int},{0x8002ad54,E::memory_reference},{0x8002af20,E::memory_reference},
        {0x80038700,E::boolean},{0x8003873c,E::boolean},{0x80038890,E::boolean},{0x8002530c,E::string},{0x80025474,E::string},{0x80037c3c,E::signed_byte},{0x80037d90,E::signed_byte},
        {0x80031498,E::enumeration},{0x800315f4,E::enumeration},{0x80094000,E::enumeration},
        {0x8002760c,E::pointer},{0x8002776c,E::pointer},{0x800940c8,E::pointer},
        {0x80023144,E::unsigned_byte},{0x800232a0,E::unsigned_byte},{0x80093a20,E::unsigned_byte},
        {0x80022bb0,E::unsigned_int},{0x80022bec,E::unsigned_int},{0x80022d40,E::unsigned_int},{0x80094138,E::unsigned_int},
        {0x80024544,E::structure},{0x800246a8,E::structure},{0x80093aa4,E::structure},
        {0x80021f30,E::unsigned_short},{0x80022084,E::unsigned_short},{0x80094158,E::unsigned_short}}};
    for(const auto& [address,entry]:getters){IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=address;auto op=factory(c);
        check(op && op->resume().completed && op->metadata_result==owners[static_cast<std::size_t>(entry)],"Concrete getter adapter selected another canonical cell.");}
    f.services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    f.services->callbacks->prerequisites->initialized=std::uint8_t{1};f.services->callbacks->prerequisites->started=std::uint8_t{1};
    for(const auto entry:{E::base,E::reference,E::object,E::signed_int,E::memory_reference,E::boolean,E::string,E::signed_byte,E::enumeration,E::pointer,E::unsigned_byte,E::unsigned_int,E::structure,E::unsigned_short})for(const auto address:{igb_field_type_profile(entry).body,igb_field_type_profile(entry).wrapper}){
        IgbRegistrationCallbacksCall c;c.kind=CK::invoke_function;c.function=address;auto op=f.services->callbacks->factory(c);
        check(op && op->resume().completed,"Exact wrapper/body adapter failed its original ready gate.");}
    for(const auto entry:{E::object,E::signed_int,E::memory_reference,E::boolean,E::string,E::signed_byte,E::enumeration,E::pointer,E::unsigned_byte,E::unsigned_int,E::structure,E::unsigned_short}){
        IgbMetaFieldCall c;c.kind=IgbMetaFieldServiceKind::register_type;c.function=igb_field_type_profile(entry).wrapper;
        auto reflected=make_native_igb_field_type_registration_factory(f.services)(c);
        check(reflected && reflected->resume().completed,"Enabled field factory missed original registration.");}
    for(const auto address:{0x80028934u,0x8002a764u,0x800273ecu,0x80028ab4u,0x80028bfcu}){IgbTypeRegistrationCall unknown;unknown.kind=address==0x80028934?TK::layout_probe:TK::defaults;unknown.function=address;
        check(!factory(unknown),"Registration adapter claimed unimplemented probe/default/prototype work.");}
    for(const auto address:{0x80093aacu,0x80024580u,0x800246c8u,0x8002213cu,0x80094160u,0x80021f6cu,0x800220a4u}){
        IgbTypeRegistrationCall unknown;unknown.kind=TK::get_parent;unknown.function=address;
        check(!factory(unknown),"Registration adapter claimed a prototype, probe or defaults as a metadata getter.");}
    Fixture retained;retained.services->types->metadata_pool_handle.reset();*retained.slot(E::object)=meta(4);
    IgbRegistrationCallbacksCall body;body.kind=CK::invoke_function;body.function=0x80028a20;auto selected=retained.services->callbacks->factory(body);
    check(!selected->resume().completed,"Selected body did not suspend at missing handle.");std::weak_ptr<IgbFieldTypeRegistrationServices> weak=retained.services;
    retained.services.reset();check(!weak.expired(),"Pending adapter lost canonical services.");selected.reset();check(weak.expired(),"Weak adapter created a permanent service cycle.");
    auto owner=std::make_shared<IgbFieldTypeRegistrationServices>();auto expired=make_native_igb_field_type_registration_function_factory(owner);owner.reset();
    rejects([&]{expired(body);},"Expired adapter silently replaced services.");
    Fixture recursive;*recursive.slot(E::base)=meta(1);IgbFieldTypeRegistrationInvocation run(E::base);
    recursive.remaining=[&](const auto&){rejects([&]{run.resume(recursive.services);},"Recursive registration was accepted.");return std::shared_ptr<IgbTypeRegistrationOperation>{};};
    rejects([&]{run.resume(recursive.services);},"Recursive failure did not poison outer body.");check(run.faulted(),"Recursive field-type registration lost fault state.");
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_field_type_registration_tests");
    exact_profiles();registration_gates();unsigned_short_ignores_prerequisite_state();actual_parent_chain();deferred_tables();getters_and_lifetimes();
    prerequisite_profiles(E::unsigned_byte,IgbMetaFieldKind::unsigned_byte);prerequisite_profiles(E::structure,IgbMetaFieldKind::structure);
    unsigned_int_canonical_cell();adapters();
    std::cout<<"igb_field_type_registration_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"Field-type registration failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
