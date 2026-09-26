#include "dw4/igb_data_list_defaults.hpp"
#include "dw4/igb_meta_enum.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace dw4;using E=IgbDataListKind;using L=IgbDataListLookup;
using TK=IgbTypeRegistrationServiceKind;using CK=IgbRegistrationCallbacksServiceKind;using MK=IgbMetadataFactoryServiceKind;
using FK=IgbFieldOperationKind;using PK=IgbPointerListServiceKind;using DK=IgbListDefaultsServiceKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& fn,const char* why){++checks;try{fn();}catch(const std::runtime_error&){return;}throw std::runtime_error(why);}
auto bytes(std::size_t size,std::uint8_t fill=0xcc){auto b=std::make_shared<IgbScratchBuffer>();b->bytes.assign(size,fill);b->initialized.assign(size,1);return b;}
auto meta(std::uint32_t flags=0){auto m=std::make_shared<IgbRuntimeMetaType>();m->class_flags=flags;return m;}
IgbBootstrapPointer allocation(std::uint32_t address,std::size_t size,std::uint32_t prefix=0){auto s=std::make_shared<IgbObjectStorage>();
    s->original_address=address-prefix;s->bytes=bytes(size+prefix+8);return {s,prefix};}
void raw_word(const IgbBootstrapPointer& p,std::uint32_t at,std::uint32_t value){for(unsigned i=0;i<4;++i){
    p.storage->bytes->bytes.at(p.offset+at+i)=std::uint8_t(value>>(24-8*i));p.storage->bytes->initialized.at(p.offset+at+i)=1;}}
auto descriptor(){auto f=std::make_shared<IgbDecodeRuntimeField>();f->vtable_word=0x80476088;f->size=4;f->reference_word=1;
    f->name=std::make_shared<const std::string>("_data");f->name_string.reset();f->offset=16;
    f->default_bytes=IgbRuntimeFieldDefaultSource{bytes(4,0),0};f->default_bytes_address=0x700000;
    f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};f->flag_35=1;f->flag_40=1;f->flag_41=1;f->flag_42=0xa5;f->word_38=0x12345678;return f;}
IgbBootstrapPointer field_storage(const std::shared_ptr<IgbDecodeRuntimeField>& f,std::uint32_t address,std::uint32_t reference){
    auto p=allocation(address,0x50,8);p.storage->bindings.push_back({8,{},{},{},{},f});f->physical_storage=IgbFieldStorageBinding{p.storage,8};
    igb_bootstrap_write_word(p,0,*f->vtable_word);igb_bootstrap_write_word(p,4,reference);
    igb_bootstrap_write_word(p,0x1c,0);f->global_slot_address=0;f->global_slot=std::weak_ptr<IgbRuntimeFieldSlot>{};
    igb_bootstrap_write_word(p,0x20,*f->default_bytes_address);
    igb_bootstrap_write_word(p,0x38,*f->word_38);for(const auto at:{0x35u,0x40u,0x41u})igb_bootstrap_write_byte(p,at,1);
    igb_bootstrap_write_byte(p,0x42,0xa5);return p;}
auto metadata(std::uint32_t address=0x900000){auto m=meta();m->fields=std::make_shared<IgbDecodeFieldRegistry>();
    m->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>(8);m->fields->backing_bytes=bytes(32,0);m->fields->backing_address=address;return m;}
struct Expected{E kind;const char* name;std::uint32_t global,wrapper,body,field,probe,defaults,full,lazy,plain,data,prototype_body,final_vtable;};
constexpr std::array<Expected,2> profiles{{
    {E::string_ref,"igStringRefList",0x8056158c,0x80024c64,0x80024c8c,0x80024cfc,0x80024c0c,0x80024d24,0x80024bd0,0x80024b94,0x80093abc,0x80561590,0x80025404,0x80470f5c},
    {E::signed_int,"igIntList",0x80561dc4,0x80036684,0x800366ac,0x8003671c,0x8003662c,0x8003673c,0x800365f0,0x800365b4,0x80093f28,0x80561dc8,0x8002e3b8,0x80474600}}};
struct RegistrationFixture {
    std::shared_ptr<IgbDataListRegistrationServices> services=std::make_shared<IgbDataListRegistrationServices>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbMemoryPoolRegistry> manager=make_original_igb_memory_pool_registry();
    std::shared_ptr<IgbRuntimeMetaType> constructed=meta();IgbBootstrapPointer physical=allocation(0x100000,0x64,8);
    IgbTypeRegistrationFactory remaining;IgbRegistrationCallbacksFactory callback_remaining;
    std::vector<IgbTypeRegistrationCall> calls;std::vector<std::uint32_t> functions;
    std::function<void()> during_allocation;unsigned allocations{},allocation_polls{},locals{};bool allocation_ready=true;
    RegistrationFixture(){
        services->string_ref_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->signed_int_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->data_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->ref_object_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->ref_thread_metadata=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->dependencies=std::make_shared<IgbThreadDependenciesServices>();
        services->dependencies->thread_list=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
        services->types->runtime=std::make_shared<IgbRuntimeTypesServices>();services->types->runtime->singleton=core;core->enabled=1;
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->runtime=services->types->runtime;
        services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
        services->callbacks->prerequisites->initialized=std::uint8_t{1};services->callbacks->prerequisites->started=std::uint8_t{1};
        services->pools=std::make_shared<IgbFilePoolsServices>();services->pools->memory_pools=std::make_shared<IgbMemoryPoolsServices>();
        services->pools->memory_pools->selector=0;services->pools->memory_pools->fallback=manager;manager->original_pool_access_bound=true;manager->pools[5]=pool;
        services->metadata=std::make_shared<IgbMetadataFactoryServices>();services->metadata->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->metadata->bootstrap->runtime=services->types->runtime;services->metadata->metadata=meta(4);
        physical.storage->bindings.push_back({physical.offset,{},constructed});
        // Explicit reflected allocation isolates the getter/registrar. Actual
        // nested field-list construction belongs to cold-bootstrap integration.
        services->metadata->factory=[this](const IgbMetadataFactoryCall& c){auto op=std::make_shared<IgbMetadataFactoryOperation>();
            if(c.kind==MK::register_type)op->resume=[] {return MessengerCallResult{true,0,{}};};
            else{check(c.kind==MK::reflected_factory && c.pool==pool,"Lazy metadata changed its actual factory or pool.");++allocations;
                op->resume=[this,out=op.get()]{++allocation_polls;if(during_allocation)during_allocation();
                    if(!allocation_ready)return MessengerCallResult{false,0,"prepared reflected allocation"};out->allocation_result=physical;return MessengerCallResult{true,0,{}};};}
            return op;};
        services->types->factory=make_native_igb_registration_pool_factory(services->pools,make_native_igb_metadata_factory(services->metadata,
            [this](const IgbTypeRegistrationCall& c){calls.push_back(c);return remaining?remaining(c):std::shared_ptr<IgbTypeRegistrationOperation>{};}));
        services->callbacks->factory=make_native_igb_data_list_registration_function_factory(services,[this](const IgbRegistrationCallbacksCall& c){
            functions.push_back(c.function);return callback_remaining?callback_remaining(c):std::shared_ptr<IgbRegistrationCallbacksOperation>{};});
        services->dependencies->types=services->types;services->dependencies->pools=services->pools;services->dependencies->metadata=services->metadata;
        services->local_storage=[this](E){++locals;return allocation(0x200000+locals*0x100,20,8);};
    }
    auto slot(E kind){return igb_data_list_metadata_slot(services,kind);}
};

void profiles_and_lookup_contracts(){
    for(const auto& e:profiles){RegistrationFixture f;const auto& p=igb_data_list_profile(e.kind);const auto a=igb_data_list_type_registration_arguments(f.services,e.kind);
        check(std::string(p.name)==e.name && p.global==e.global && p.wrapper==e.wrapper && p.body==e.body && p.field_callback==e.field &&
            p.probe==e.probe && p.defaults==e.defaults && p.full_getter==e.full && p.lazy_getter==e.lazy && p.plain_getter==e.plain,
            "Enum list profile differs from the original class identity.");
        check(a.metadata_slot==f.slot(e.kind) && *a.name==e.name && a.flag==0 && a.native_size==20 && a.parent_registration==0x80033a14 &&
            a.parent_getter==0x80024d1c && a.field_callback==e.field && a.layout_probe==e.probe && a.defaults_callback==e.defaults &&
            !a.extra_callbacks && a.trailing_callback==0,"Enum list registrar changed ancestry, layout or callbacks.");
        check(f.slot(e.kind)!=f.services->data_metadata && f.services->string_ref_metadata!=f.services->signed_int_metadata,
            "Distinct list metadata globals were aliased.");
        f.slot(e.kind)->reset();IgbDataListLookupInvocation plain(e.kind,L::plain);
        check(!plain.resume(f.services).completed && f.allocations==0,"Unknown plain getter became known null.");
        *f.slot(e.kind)=std::shared_ptr<IgbRuntimeMetaType>{};check(plain.resume(f.services).completed && !plain.result() && f.allocations==0,"Plain null getter allocated.");
        for(const auto flags:{0u,4u,8u}){RegistrationFixture live;auto original=meta(flags);*live.slot(e.kind)=original;
            IgbDataListLookupInvocation lazy(e.kind,L::allocate_if_null);check(lazy.resume(live.services).completed && lazy.result()==original && live.allocations==0,
                "Allocation-only getter tested registration flags or allocated a nonnull class.");
            unsigned selections{},polls{};bool ready{};live.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){
                ++selections;check(c.kind==CK::invoke_function && c.function==e.body,"Full getter selected another registration body.");
                auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[&]{++polls;return MessengerCallResult{ready,0,"prepared class registration"};};return op;};
            IgbDataListLookupInvocation full(e.kind);const auto status=full.resume(live.services);
            if(flags&4)check(status.completed && full.result()==original && selections==0,"Ready full getter re-registered.");
            else{check(!status.completed && selections==1 && polls==1,"Unready full getter skipped registration.");auto replacement=meta();*live.slot(e.kind)=replacement;
                live.services->callbacks->factory={};ready=true;check(full.resume(live.services).completed && full.result()==replacement && polls==2 && selections==1,
                    "Full getter failed to retain its callback and freshly reload the canonical result.");}
        }
        RegistrationFixture pending;pending.allocation_ready=false;const auto captured=pending.slot(e.kind);IgbDataListLookupInvocation lazy(e.kind,L::allocate_if_null);
        check(!lazy.resume(pending.services).completed && pending.allocations==1 && !**captured,"Lazy getter published before allocation completion.");
        auto displaced=meta();displaced->reference_word=73;*captured=displaced;auto alternate=std::make_shared<IgbRegistrationMetaSlot>(std::shared_ptr<IgbRuntimeMetaType>{});
        if(e.kind==E::string_ref)pending.services->string_ref_metadata=alternate;else pending.services->signed_int_metadata=alternate;
        pending.allocation_ready=true;pending.services->metadata.reset();
        check(lazy.resume(pending.services).completed && lazy.result()==pending.constructed && **captured==pending.constructed && !**alternate &&
            pending.allocations==1 && pending.allocation_polls==2 && displaced->reference_word==73 && pending.constructed->class_flags==0,
            "Lazy allocation reselected its cell, released a displaced value or fabricated ready metadata.");
        RegistrationFixture cold;IgbDataListTypeRegistrationInvocation body(e.kind);
        check(!body.resume(cold.services).completed && cold.constructed->class_flags==8 && cold.calls.size()==1 && cold.calls[0].kind==TK::register_parent &&
            cold.calls[0].function==0x80033a14 && cold.allocations==1,"Registrar skipped its actual lazy/busy/parent prefix.");
        check(!body.resume(cold.services).completed && cold.allocations==1,"Pending registrar repeated class construction.");
        RegistrationFixture adapters;auto value=meta(4);*adapters.slot(e.kind)=value;auto factory=make_native_igb_data_list_registration_type_factory(adapters.services);
        for(const auto fn:{e.full,e.lazy,e.plain,e.field}){IgbTypeRegistrationCall c;c.kind=TK::get_parent;c.function=fn;auto op=factory(c);
            check(op && op->resume().completed && op->metadata_result==value,"Exact getter adapter selected another list global.");}
    }
}

void complete_registrar_order(){
    // Prepared lower publication owns a second reference; its full registry
    // implementation is exercised by the original cold-bootstrap fixture.
    for(const auto& e:profiles){RegistrationFixture f;auto value=meta();value->reference_word=2;value->post_layout_callback=0;*f.slot(e.kind)=value;
        auto parent=meta(4);f.core->registration_filter_enabled=0;f.core->registration_callback_pool=f.pool;std::vector<TK> reached;
        f.remaining=[&](const IgbTypeRegistrationCall& c){auto op=std::make_shared<IgbTypeRegistrationOperation>();op->resume=[&,c,out=op.get()]{reached.push_back(c.kind);
            if(c.kind==TK::register_parent)check(c.function==0x80033a14,"New list registered the wrong parent.");
            else if(c.kind==TK::get_parent){check(c.function==0x80024d1c,"New list used a non-DataList parent getter.");out->metadata_result=parent;}
            else if(c.kind==TK::set_name)c.metadata->name=c.name;
            else if(c.kind==TK::layout_probe){check(c.function==e.probe,"New list selected another probe.");return MessengerCallResult{true,e.final_vtable,{}};}
            else if(c.kind==TK::defaults)check(c.function==e.defaults,"New list selected another defaults callback.");
            else if(c.kind==TK::compute_layout)c.metadata->class_flags|=4;
            else if(c.kind==TK::create_callbacks)out->callbacks_result=std::make_shared<IgbRegistrationCallbackList>();
            else check(c.kind==TK::attach_parent || c.kind==TK::publish_type,"Unexpected registrar endpoint.");
            return MessengerCallResult{true,0,{}};};return op;};
        IgbDataListTypeRegistrationInvocation call(e.kind);check(call.resume(f.services).completed && (value->class_flags&4) && value->native_size==20 &&
            value->field_callback==e.field && value->name && *value->name==e.name,"Prepared lower endpoints did not complete the actual registrar.");
        const auto position=[&](TK kind){return std::find(reached.begin(),reached.end(),kind);};
        check(position(TK::register_parent)<position(TK::get_parent) && position(TK::get_parent)<position(TK::defaults) &&
            position(TK::defaults)<position(TK::compute_layout),"Registrar reordered parent, defaults or layout.");
        check(!call.result() || call.result()->count==0,"New list manufactured deferred extra callbacks.");
    }
}

void probe_capture_and_unknown_words(){
    for(const auto& e:profiles){RegistrationFixture f;auto p=f.services->local_storage(e.kind);
        for(unsigned n=8;n<20;++n)p.storage->bytes->initialized[p.offset+n]=0;
        const auto before=p.storage->bytes->bytes,known=p.storage->bytes->initialized;f.services->types->runtime->singleton.reset();
        IgbDataListLayoutProbeInvocation call(p,e.kind);check(!call.resume(f.services).completed && igb_bootstrap_read_word(p,0)==0x80472fa0 &&
            igb_bootstrap_read_word(p,4)==1,"List probe captured ArkCore before/after the wrong vtable boundary.");
        f.services->types->runtime->singleton=f.core;check(!call.resume(f.services).completed && igb_bootstrap_read_word(p,0)==e.final_vtable,
            "Probe did not complete the remaining vtable stores before reading unknown core offset.");
        for(unsigned n=8;n<20;++n)check(p.storage->bytes->bytes[p.offset+n]==before[p.offset+n] && p.storage->bytes->initialized[p.offset+n]==known[p.offset+n],
            "Probe initialized an unused list member.");
        auto replacement=std::make_shared<IgbRuntimeTypeRegistry>();replacement->vtable_offset=0;f.services->types->runtime->singleton=replacement;
        f.core->vtable_offset=16;igb_bootstrap_write_word(p,16,0x12345678);
        check(call.resume(f.services).completed && call.value()==0x12345678 && f.allocations==0,"Probe reloaded core or repeated local construction.");
        check(call.resume().value==0x12345678,"Completed probe did not retain its selected word.");
        for(const auto offset:{0u,4u,8u,12u,16u}){RegistrationFixture all;auto raw=all.services->local_storage(e.kind);all.core->vtable_offset=offset;
            const auto expected=offset==0?e.final_vtable:offset==4?1u:igb_bootstrap_read_word(raw,offset);IgbDataListLayoutProbeInvocation read(raw,e.kind);
            check(read.resume(all.services).completed && read.value()==expected,"Probe returned a guessed list layout word.");}
        for(const auto offset:{1u,20u,0xfffffffcu}){RegistrationFixture invalid;auto raw=invalid.services->local_storage(e.kind);invalid.core->vtable_offset=offset;
            IgbDataListLayoutProbeInvocation bad(raw,e.kind);rejects([&]{bad.resume(invalid.services);},"Probe escaped its represented aligned 20-byte local.");}
        RegistrationFixture unknown;auto raw=unknown.services->local_storage(e.kind);raw.storage->bytes->initialized[raw.offset+8]=0;unknown.core->vtable_offset=8;
        IgbDataListLayoutProbeInvocation bad(raw,e.kind);rejects([&]{bad.resume(unknown.services);},"Unknown selected local word was fabricated.");
        igb_bootstrap_write_word(raw,8,7);rejects([&]{bad.resume(unknown.services);},"Probe fault revived after external repair.");
    }
}

struct DefaultsFixture {
    E kind;std::shared_ptr<IgbDataListDefaultsServices> services=std::make_shared<IgbDataListDefaultsServices>();
    std::shared_ptr<IgbRuntimeMetaType> target=metadata();std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    std::shared_ptr<IgbDecodeRuntimeField> source=descriptor(),clone,prototype=descriptor();
    IgbBootstrapPointer source_bytes=field_storage(source,0x100000,2),clone_bytes;
    std::shared_ptr<IgbFieldValuesServices> values=std::make_shared<IgbFieldValuesServices>();
    std::vector<IgbListDefaultsCall> calls;std::vector<std::uint32_t> functions;
    unsigned clone_selections{},clone_polls{},destroy_polls{},prototype_polls{},size_polls{};
    bool clone_ready=true,destroy_ready=true,prototype_ready=true,size_ready=true;
    std::function<void()> after_clone,during_size;std::function<void(const IgbListDefaultsCall&)> during_destroy;
    explicit DefaultsFixture(E k):kind(k){
        services->registration=std::make_shared<IgbDataListRegistrationServices>();
        services->registration->string_ref_metadata=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->registration->signed_int_metadata=std::make_shared<IgbRegistrationMetaSlot>(target);
        services->lists=std::make_shared<IgbListDefaultsServices>();services->lists->schema=std::make_shared<IgbFieldSchemaServices>();
        auto& schema=*services->lists->schema;schema.fields=std::make_shared<IgbFieldOperationsServices>();schema.fields->pointer_lists=std::make_shared<IgbPointerListServices>();
        auto name=descriptor();name->offset=12;services->lists->name_descriptor=name;
        target->fields->count=2;target->fields->backing->at(0)=descriptor();target->fields->backing->at(0)->name=std::make_shared<const std::string>("_count");
        target->fields->backing->at(1)=source;target->fields->backing_bytes->bytes[5]=0x10;
        schema.fields->pointer_lists->values={{0x100000,{},source},{0x300000,{},prototype}};
        schema.fields->pointer_lists->factory=[this](const IgbPointerListCall& c){auto op=std::make_shared<IgbPointerListOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==PK::allocation_pool)out->pool_result=pool;else{check(c.kind==PK::allocation_size,"Unexpected prepared replacement reserve.");++size_polls;
                if(!size_ready)return MessengerCallResult{false,0,"prepared registry size"};out->size_result=32;if(during_size)during_size();}
            return MessengerCallResult{true,0,{}};};return op;};
        values->fields=schema.fields;values->memory_reference_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
        values->string_prototype=prototype;values->signed_int_prototype=prototype;services->lists->values=values;
        schema.fields->factory=make_native_igb_field_values_factory(values,[this](const IgbFieldOperationCall& c){auto op=std::make_shared<IgbFieldOperation>();op->resume=[this,c,out=op.get()]{
            if(c.kind==FK::allocation_pool)out->pool_result=pool;else{check(c.kind==FK::allocation_size,"Unexpected default buffer operation.");return MessengerCallResult{true,4,{}};}
            return MessengerCallResult{true,0,{}};};return op;});
        services->lists->factory=[this](const IgbListDefaultsCall& c){calls.push_back(c);if(c.kind==DK::clone_field)++clone_selections;
            auto op=std::make_shared<IgbListDefaultsOperation>();op->resume=[this,c,out=op.get()]{
                if(c.kind==DK::clone_field){++clone_polls;check(c.field==source && c.virtual_slot==0x54 && c.argument==1,"Inherited clone selected another field or arguments.");
                    if(!clone_ready)return MessengerCallResult{false,0,"prepared inherited clone"};clone=std::make_shared<IgbDecodeRuntimeField>(*source);clone->physical_storage.reset();
                    clone_bytes=field_storage(clone,0x180000,1);out->field_result=clone;out->field_address=0x180000;if(after_clone)after_clone();}
                else{check(c.kind==DK::destroy_field && c.function==0x80066e1c,"Unexpected list-default service.");++destroy_polls;
                    if(during_destroy)during_destroy(c);if(!destroy_ready)return MessengerCallResult{false,0,"prepared descriptor destruction"};}
                return MessengerCallResult{true,0,{}};};return op;};
        services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();services->callbacks->factory=[this](const IgbRegistrationCallbacksCall& c){functions.push_back(c.function);
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[this]{++prototype_polls;if(!prototype_ready)return MessengerCallResult{false,0,"prepared element registration"};
                prototype_slot()=prototype;return MessengerCallResult{true,0,{}};};return op;};
    }
    IgbRuntimeFieldSlot& prototype_slot(){return kind==E::string_ref?values->string_prototype:values->signed_int_prototype;}
    auto data_slot(){return kind==E::string_ref?services->string_ref_data:services->signed_int_data;}
};

void shared_prototype_getters(){
    for(const auto& e:profiles){DefaultsFixture f(e.kind);const auto kind=e.kind==E::string_ref?IgbDataListPrototypeKind::string:IgbDataListPrototypeKind::signed_int;
        f.prototype_slot().reset();IgbDataListPrototypeInvocation call(kind);
        check(!call.resume(f.services).completed && f.functions.empty(),"Unknown shared element prototype became known null.");
        f.prototype_slot()=std::shared_ptr<IgbDecodeRuntimeField>{};f.prototype_ready=false;
        check(!call.resume(f.services).completed && f.functions==std::vector<std::uint32_t>{e.prototype_body} && f.prototype_polls==1,
            "Null shared prototype did not select its exact existing field registration body.");
        const auto captured=f.values;auto unrelated=std::make_shared<IgbFieldValuesServices>();f.services->lists->values=unrelated;
        f.services->callbacks->factory={};f.prototype_ready=true;
        check(call.resume(f.services).completed && call.result()==f.prototype && f.prototype_polls==2 &&
            !(e.kind==E::string_ref?unrelated->string_prototype:unrelated->signed_int_prototype),
            "Prototype getter copied the cell or reselected its values owner/callback while pending.");
        check((e.kind==E::string_ref?captured->string_prototype:captured->signed_int_prototype)==f.prototype,"Existing shared prototype cell was not updated.");
        DefaultsFixture null(e.kind);null.prototype_slot()=std::shared_ptr<IgbDecodeRuntimeField>{};
        null.services->callbacks->factory=[&](const IgbRegistrationCallbacksCall& c){check(c.function==e.prototype_body,"Null-return registration selected another body.");
            auto op=std::make_shared<IgbRegistrationCallbacksOperation>();op->resume=[]{return MessengerCallResult{true,0,{}};};return op;};
        IgbDataListPrototypeInvocation empty(kind);check(empty.resume(null.services).completed && !empty.result(),"Prototype getter invented a nonnull post-registration result.");
    }
}

void inherited_clone_defaults(){
    for(const auto& e:profiles)for(const bool unknown_flag:{false,true}){DefaultsFixture f(e.kind);f.source->reference_word=91;
        f.after_clone=[&]{f.clone->reference_word=77;if(unknown_flag){f.clone->flag_42.reset();f.clone_bytes.storage->bytes->initialized[f.clone_bytes.offset+0x42]=0;}};
        IgbDataListDefaultsInvocation call(e.kind);check(call.resume(f.services).completed && f.clone_selections==1 && f.target->fields->count==2 &&
            f.target->fields->backing->at(1)==f.clone && f.source!=f.clone,"List specialization did not replace the inherited descriptor at captured index.");
        check(f.clone->element_field->lock()==f.prototype && igb_bootstrap_read_word(f.clone_bytes,0x3c)==0x300000 && f.clone->word_38==0 &&
            igb_bootstrap_read_word(f.clone_bytes,0x38)==0 && igb_bootstrap_read_word(f.clone_bytes,0x1c)==e.data && *f.data_slot()==f.clone,
            "List specialization changed element/size/global store identities.");
        check(f.clone->flag_35==1 && f.clone->flag_40==1 && f.clone->flag_41==1 && f.clone_bytes.storage->bytes->bytes[f.clone_bytes.offset+0x42]==0xa5 &&
            f.clone_bytes.storage->bytes->initialized[f.clone_bytes.offset+0x42]==(unknown_flag?0:1) && (unknown_flag?!f.clone->flag_42:f.clone->flag_42==0xa5),
            "Reference specialization cleared cloned flags or made unknown flag42 known.");
        check(igb_bootstrap_read_word(f.source_bytes,4)==1 && f.source->reference_word==1 && igb_bootstrap_read_word(f.clone_bytes,4)==1 && f.clone->reference_word==1,
            "Replacement or creator release computed from stale canonical references.");
        check(igb_buffer_word(f.target->fields->backing_bytes,4)==0x180000 && f.functions.empty() && f.calls.size()==1 &&
            !f.services->nonref_data->has_value() && !f.services->ref_data->has_value(),"Defaults registered an existing prototype or wrote unrelated descriptor globals.");
        check(call.resume().completed && f.clone_selections==1,"Completed specialization repeated its inherited clone.");
    }
    for(const auto& e:profiles){DefaultsFixture f(e.kind);f.after_clone=[&]{f.clone->physical_storage.reset();f.clone_bytes={};};
        IgbDataListDefaultsInvocation call(e.kind);check(call.resume(f.services).completed && !f.clone->physical_storage &&
            f.clone->reference_word==1 && f.clone->element_field->lock()==f.prototype && *f.data_slot()==f.clone &&
            f.clone->global_slot_address==e.data,"Canonical-only list specialization acquired a physical descriptor profile.");
    }
}

void captured_clone_index_and_prototype_before_store(){
    for(const auto& e:profiles){DefaultsFixture f(e.kind);f.clone_ready=false;IgbDataListDefaultsInvocation call(e.kind);
        check(!call.resume(f.services).completed && f.clone_selections==1 && !f.clone,"Pending clone did not retain the source call.");
        auto replacement=metadata(0x910000);replacement->fields->count=2;replacement->fields->backing->at(0)=f.source;
        auto old=descriptor();auto old_bytes=field_storage(old,0x110000,7);replacement->fields->backing->at(1)=old;
        f.target->fields=replacement->fields;auto unrelated=metadata(0x920000);
        *igb_data_list_metadata_slot(f.services->registration,e.kind)=unrelated;
        f.clone_ready=true;f.prototype_slot().reset();check(!call.resume(f.services).completed && f.clone_polls==2 && f.functions.empty() &&
            igb_bootstrap_read_word(f.clone_bytes,0x3c)==0xcccccccc && f.target->fields->backing->at(1)==old,
            "Unknown element prototype skipped the captured clone/index prefix.");
        f.prototype_slot()=f.prototype;check(call.resume(f.services).completed && f.target->fields->backing->at(0)==f.source &&
            f.target->fields->backing->at(1)==f.clone && unrelated->fields->count==0 && igb_bootstrap_read_word(old_bytes,4)==6 &&
            igb_bootstrap_read_word(f.source_bytes,4)==2,"Defaults recaptured metadata/index or decremented the original search source instead of the fresh old slot.");
        for(unsigned damage=0;damage<3;++damage){DefaultsFixture bad(e.kind);bad.prototype_slot()=std::shared_ptr<IgbDecodeRuntimeField>{};bad.prototype_ready=false;
            IgbDataListDefaultsInvocation pending(e.kind);check(!pending.resume(bad.services).completed && bad.functions.size()==1 && bad.clone,
                "Destination identity fixture did not reach pending prototype registration.");
            const auto old_owner=bad.clone_bytes.storage->bytes;const auto before=old_owner->bytes;const auto address=bad.clone_bytes.storage->original_address;
            if(damage==0)bad.clone_bytes.storage->bytes=bytes(old_owner->bytes.size());
            else if(damage==1)++bad.clone_bytes.storage->original_address;
            else bad.clone->physical_storage->storage.reset();
            check(!pending.resume(bad.services).completed && bad.prototype_polls==2,"Pending prototype dereferenced its future destination.");bad.prototype_ready=true;
            rejects([&]{pending.resume(bad.services);},"Completed prototype redirected later stores to a changed clone allocation.");
            check(old_owner->bytes==before && bad.target->fields->backing->at(1)==bad.source && !bad.data_slot()->has_value() && bad.prototype_slot()==bad.prototype,
                "Identity fault lost prototype effects or wrote later clone/registry state.");
            bad.clone_bytes.storage->bytes=old_owner;bad.clone_bytes.storage->original_address=address;bad.clone->physical_storage=IgbFieldStorageBinding{bad.clone_bytes.storage,8};
            rejects([&]{pending.resume(bad.services);},"Restoring clone identity revived a faulted defaults operation.");
        }
    }
}

void physical_clone_dispatch_and_pending_capture(){
    for(const auto& e:profiles){DefaultsFixture f(e.kind);f.clone_ready=false;raw_word(f.source_bytes,0,0x80475b60);
        IgbDataListDefaultsInvocation call(e.kind);
        check(!call.resume(f.services).completed && f.clone_selections==1 && f.clone_polls==1 && f.calls.front().vtable_word==0x80475b60 &&
            f.source->vtable_word==0x80476088 && f.functions.empty(),"Clone dispatch ignored the original physical +00 load or refreshed its canonical cache.");
        for(unsigned i=0;i<4;++i)f.source_bytes.storage->bytes->initialized[f.source_bytes.offset+i]=0;
        f.services->lists->factory={};
        check(!call.resume(f.services).completed && f.clone_selections==1 && f.clone_polls==2,
            "Pending clone reread its physical VT or reselected its factory.");
        f.clone_ready=true;f.prototype_slot().reset();
        check(!call.resume(f.services).completed && f.clone && f.clone_selections==1 && f.clone_polls==3 &&
            f.calls.front().vtable_word==0x80475b60 && f.source->vtable_word==0x80476088 && f.functions.empty() &&
            igb_bootstrap_read_word(f.clone_bytes,0x3c)==0xcccccccc && f.target->fields->backing->at(1)==f.source,
            "Completed retained clone reread source VT or skipped the later unknown prototype boundary.");

        for(const bool unknown_cache:{false,true}){DefaultsFixture physical(e.kind);physical.clone_ready=false;
            if(unknown_cache)physical.source->vtable_word.reset();else physical.source->vtable_word=0;
            IgbDataListDefaultsInvocation selected(e.kind);
            check(!selected.resume(physical.services).completed && physical.clone_selections==1 &&
                physical.calls.front().vtable_word==0x80476088 &&
                (unknown_cache?!physical.source->vtable_word:physical.source->vtable_word==0),
                "A valid physical source VT depended on its unknown/null canonical cache.");
        }
        for(unsigned damage=0;damage<8;++damage){DefaultsFixture bad(e.kind);const auto original_binding=*bad.source->physical_storage;
            auto owner=bad.source_bytes.storage->bytes;const auto before=owner->bytes,known=owner->initialized;
            if(damage<4)owner->initialized[bad.source_bytes.offset+damage]=0;
            else if(damage==4)owner->released=true;
            else if(damage==5)bad.source->physical_storage->storage.reset();
            else if(damage==6)bad.source_bytes.storage->bindings.front().field=descriptor();
            else{owner->bytes.resize(bad.source_bytes.offset+3);owner->initialized.resize(bad.source_bytes.offset+3);}
            const auto reached_bytes=owner->bytes,reached_known=owner->initialized;
            IgbDataListDefaultsInvocation invalid(e.kind);rejects([&]{invalid.resume(bad.services);},"Unavailable physical clone VT used the canonical cache.");
            check(invalid.faulted() && bad.calls.empty() && bad.functions.empty() && !bad.clone && owner->bytes==reached_bytes &&
                owner->initialized==reached_known && bad.target->fields->backing->at(1)==bad.source && !bad.data_slot()->has_value(),
                "Failed source VT read selected a callback or changed a later defaults prefix.");
            owner->released=false;owner->bytes=before;owner->initialized=known;bad.source->physical_storage=original_binding;
            bad.source_bytes.storage->bindings.front().field=bad.source;
            rejects([&]{invalid.resume(bad.services);},"Repairing the physical source VT revived faulted defaults.");
        }
        DefaultsFixture null(e.kind);raw_word(null.source_bytes,0,0);IgbDataListDefaultsInvocation invalid(e.kind);
        rejects([&]{invalid.resume(null.services);},"Known physical null VT fell back to the canonical clone virtual.");
        check(null.calls.empty() && null.functions.empty(),"Null physical clone VT invoked a later dependency.");
    }
    DefaultsFixture retained(E::string_ref);retained.clone_ready=false;auto call=std::make_unique<IgbDataListDefaultsInvocation>(E::string_ref);
    check(!call->resume(retained.services).completed && retained.clone_selections==1,"Source-owner fixture did not reach a pending clone.");
    std::weak_ptr<IgbScratchBuffer> bytes_owner=retained.source_bytes.storage->bytes;
    retained.source_bytes.storage->bytes=bytes(retained.source_bytes.storage->bytes->bytes.size());call.reset();
    check(!bytes_owner.expired(),"Abandoned clone dropped the selected physical source byte owner.");
    retained.services->retained.clear();check(bytes_owner.expired(),"Selected clone source ownership formed a retained callback cycle.");
}

void physical_replace_and_creator_continuations(){
    for(const auto& e:profiles){DefaultsFixture f(e.kind);raw_word(f.source_bytes,4,0x80000001);f.source->reference_word=90;f.destroy_ready=false;
        IgbDataListDefaultsInvocation call(e.kind);check(!call.resume(f.services).completed && f.calls.size()==2 && f.calls[1].field==f.source &&
            igb_bootstrap_read_word(f.source_bytes,4)==0x80000000 && igb_bootstrap_read_word(f.clone_bytes,4)==2 && *f.data_slot()==f.clone &&
            f.target->fields->backing->at(1)==f.source,"Old-descriptor destruction did not follow physical retain/release and finalization.");
        raw_word(f.source_bytes,4,0xf0000020);f.source->reference_word=19;f.destroy_ready=true;
        f.during_destroy=[&](const IgbListDefaultsCall& c){if(c.field==f.source)f.source_bytes.storage->bytes->released=true;};
        check(call.resume(f.services).completed && f.destroy_polls==2 && f.source->reference_word==19 && f.target->fields->backing->at(1)==f.clone &&
            igb_bootstrap_read_word(f.clone_bytes,4)==1,"Pending old destructor replayed release or was followed by an invalid old-header read.");
        DefaultsFixture creator(e.kind);bool changed{};creator.during_size=[&]{if(!changed){changed=true;raw_word(creator.clone_bytes,4,0x95800001);creator.clone->reference_word=11;}};
        creator.destroy_ready=false;IgbDataListDefaultsInvocation release(e.kind);
        check(!release.resume(creator.services).completed && creator.target->fields->backing->at(1)==creator.clone && creator.calls.back().field==creator.clone &&
            igb_bootstrap_read_word(creator.clone_bytes,4)==0x95800000 && creator.clone->reference_word==0x95800000,
            "Defaults creator release ignored the fresh physical value after replacement publication.");
        creator.destroy_ready=true;creator.during_destroy=[&](const auto&){creator.clone_bytes.storage->bytes->released=true;};
        check(release.resume(creator.services).completed && creator.destroy_polls==2,"Completed creator destructor was followed by a forbidden clone access.");
    }
    DefaultsFixture fresh(E::signed_int);auto incoming=descriptor();auto incoming_bytes=field_storage(incoming,0x190000,0xffffffff);incoming->reference_word=55;
    fresh.services->lists->schema->fields->pointer_lists->values.push_back({0x190000,{},incoming});raw_word(fresh.source_bytes,4,0x80000001);fresh.source->reference_word=9;
    bool mutated{};IgbFieldReplaceInvocation call(fresh.target,1,incoming);
    const auto after_decrement=[&]{if(!mutated && igb_bootstrap_read_word(fresh.source_bytes,4)==0x80000000){mutated=true;raw_word(fresh.source_bytes,4,0x80000002);}};
    check(call.resume(fresh.services->lists,after_decrement).completed && mutated && fresh.destroy_polls==0 &&
        igb_bootstrap_read_word(incoming_bytes,4)==0 && incoming->reference_word==0 && fresh.source->reference_word==0x80000000 &&
        igb_bootstrap_read_word(fresh.source_bytes,4)==0x80000002 && fresh.target->fields->count==2,
        "FieldReplace masked incoming full32 wrap, reused its old decrement result, refreshed the read cache or changed list count.");
}

void replacement_failure_prefix_and_owner_lifetime(){
    for(const bool short_header:{false,true}){DefaultsFixture f(E::string_ref);auto incoming=descriptor();auto raw=field_storage(incoming,0x190000,1);
        f.services->lists->schema->fields->pointer_lists->values.push_back({0x190000,{},incoming});
        const auto original_bytes=f.source_bytes.storage->bytes->bytes,original_mask=f.source_bytes.storage->bytes->initialized;
        if(short_header){f.source_bytes.storage->bytes->bytes.resize(f.source_bytes.offset+7);f.source_bytes.storage->bytes->initialized.resize(f.source_bytes.offset+7);}
        else f.source_bytes.storage->bytes->initialized[f.source_bytes.offset+4]=0;
        const auto old_bytes=f.source_bytes.storage->bytes->bytes,old_mask=f.source_bytes.storage->bytes->initialized;
        IgbFieldReplaceInvocation call(f.target,1,incoming);rejects([&]{call.resume(f.services->lists);},"FieldReplace accepted an unavailable old reference word.");
        check(igb_bootstrap_read_word(raw,4)==2 && incoming->reference_word==2 && f.source_bytes.storage->bytes->bytes==old_bytes &&
            f.source_bytes.storage->bytes->initialized==old_mask && f.target->fields->backing->at(1)==f.source && f.target->fields->count==2 &&
            f.size_polls==0 && f.destroy_polls==0,"Failed old reference read changed the incoming-retain/old-release/publication prefix.");
        f.source_bytes.storage->bytes->bytes=original_bytes;f.source_bytes.storage->bytes->initialized=original_mask;
        rejects([&]{call.resume(f.services->lists);},"Repairing old physical bytes replayed the incoming retain.");
        check(igb_bootstrap_read_word(raw,4)==2,"Permanent replacement fault retained the incoming descriptor again.");
    }
    DefaultsFixture null_old(E::signed_int);auto incoming=descriptor();auto raw=field_storage(incoming,0x190000,3);
    null_old.services->lists->schema->fields->pointer_lists->values.push_back({0x190000,{},incoming});null_old.target->fields->backing->at(1).reset();
    std::fill_n(null_old.target->fields->backing_bytes->bytes.begin()+4,4,0);null_old.source_bytes.storage->bytes->released=true;
    IgbFieldReplaceInvocation replace(null_old.target,1,incoming);check(replace.resume(null_old.services->lists).completed &&
        igb_bootstrap_read_word(raw,4)==4 && null_old.target->fields->backing->at(1)==incoming && null_old.target->fields->count==2 &&
        null_old.destroy_polls==0,"Null old slot read a displaced descriptor or changed list count.");

    DefaultsFixture pending(E::string_ref);raw_word(pending.source_bytes,4,1);pending.destroy_ready=false;
    auto selected=descriptor();auto selected_bytes=field_storage(selected,0x190000,1);
    pending.services->lists->schema->fields->pointer_lists->values.push_back({0x190000,{},selected});
    auto call=std::make_unique<IgbFieldReplaceInvocation>(pending.target,1,selected);
    check(!call->resume(pending.services->lists).completed && pending.destroy_polls==1 && igb_bootstrap_read_word(pending.source_bytes,4)==0,
        "Owner-lifetime fixture did not reach retained old destruction.");
    std::weak_ptr<IgbScratchBuffer> original=pending.source_bytes.storage->bytes;
    pending.source_bytes.storage->bytes=bytes(pending.source_bytes.storage->bytes->bytes.size());call.reset();
    check(!original.expired(),"Abandoned replacement dropped the exact old reference byte owner.");
    pending.services->lists->unfinished.clear();check(original.expired(),"Replacement owner retention formed a callback/service cycle.");
}

void defaults_unfinished_owner_retention(){
    DefaultsFixture captured(E::string_ref);captured.prototype_slot().reset();
    auto call=std::make_unique<IgbDataListDefaultsInvocation>(E::string_ref);
    check(!call->resume(captured.services).completed && captured.clone,"Captured-owner fixture did not stop before its element store.");
    std::weak_ptr<IgbScratchBuffer> original=captured.clone_bytes.storage->bytes;
    captured.clone_bytes.storage->bytes=bytes(captured.clone_bytes.storage->bytes->bytes.size());captured.prototype_slot()=captured.prototype;
    rejects([&]{call->resume(captured.services);},"Pending defaults redirected to a replacement physical byte owner.");call.reset();
    check(!original.expired() && captured.target->fields->backing->at(1)==captured.source && !captured.data_slot()->has_value(),
        "Faulted defaults lost its original byte owner or published after the identity failure.");
    captured.services->retained.clear();check(original.expired(),"Defaults retained a parent/service callback cycle.");

    DefaultsFixture returned(E::signed_int);auto outer=std::make_unique<IgbDataListDefaultsInvocation>(E::signed_int);
    returned.after_clone=[&]{rejects([&]{outer->resume(returned.services);},"Recursive defaults unexpectedly completed inside clone.");};
    rejects([&]{outer->resume(returned.services);},"Caught clone recursion did not permanently invalidate defaults.");
    check(returned.clone && returned.clone_selections==1 && igb_bootstrap_read_word(returned.clone_bytes,0x3c)==0xcccccccc &&
        igb_bootstrap_read_word(returned.clone_bytes,4)==1 && returned.target->fields->backing->at(1)==returned.source &&
        !returned.data_slot()->has_value(),"Faulted clone callback performed later element/reference/global/registry publication.");
    std::weak_ptr<IgbDecodeRuntimeField> result=returned.clone;std::weak_ptr<IgbScratchBuffer> result_bytes=returned.clone_bytes.storage->bytes;
    outer.reset();returned.clone.reset();returned.clone_bytes={};
    check(!result.expired() && !result_bytes.expired(),"Unconsumed clone result lost its field or physical owner during abandonment.");
    returned.services->retained.clear();check(result.expired() && result_bytes.expired(),"Unconsumed clone retention captured its callback or service owner.");
}

void meta_enum_allocation_only_adapter(){
    for(const auto& e:profiles){RegistrationFixture f;auto pointers=std::make_shared<IgbPointerListServices>();unsigned delegated{};
        auto factory=make_native_igb_meta_enum_list_factory(f.services,pointers,[&](const auto&){++delegated;return std::shared_ptr<IgbMetaEnumOperation>{};});
        auto invalid=descriptor();invalid->construction_fault_=true;IgbMetaEnumCall request;request.function=e.lazy;request.field=invalid;
        f.allocation_ready=false;auto op=factory(request);check(op && !op->resume().completed && f.allocations==1 && f.functions.empty(),
            "Meta-enum getter accessed its caller descriptor or forced full list registration.");
        pointers->values.push_back({0x100000,f.constructed,{}});f.allocation_ready=true;
        check(op->resume().completed && op->metadata_result==f.constructed && op->metadata_address==0x100000 && f.constructed->class_flags==0 &&
            f.calls.empty() && f.functions.empty(),"Allocation-only adapter fabricated flags or lost canonical metadata/address.");
        pointers->values.clear();check(op->resume().completed && op->metadata_address==0x100000 && f.allocation_polls==2,"Completed getter replayed address resolution/allocation.");
        request.function=e.full;check(!factory(request) && delegated==1,"Adapter admitted a full getter as allocation-only.");
        RegistrationFixture unbound;auto absent=std::make_shared<IgbPointerListServices>();*unbound.slot(e.kind)=unbound.constructed;
        auto unmapped=make_native_igb_meta_enum_list_factory(unbound.services,absent);request.function=e.lazy;auto unknown=unmapped(request);
        check(unknown->resume().completed && unknown->metadata_result==unbound.constructed && !unknown->metadata_address && unbound.allocations==0,
            "Unmapped metadata invented a guest address or repeated allocation.");
        RegistrationFixture null;null.physical={};auto null_map=std::make_shared<IgbPointerListServices>();
        auto null_getter=make_native_igb_meta_enum_list_factory(null.services,null_map)(request);
        check(null_getter->resume().completed && !null_getter->metadata_result && null_getter->metadata_address==0 && null.allocations==1 &&
            null.slot(e.kind)->has_value() && !**null.slot(e.kind) && null.functions.empty(),
            "Allocation-only getter retried a completed null factory or invented a nonnull result.");
        for(unsigned conflict=0;conflict<3;++conflict){RegistrationFixture bad;*bad.slot(e.kind)=bad.constructed;auto map=std::make_shared<IgbPointerListServices>();
            map->values.push_back({0x100000,bad.constructed,{}});if(conflict==0)map->values.push_back({0x100004,bad.constructed,{}});
            else if(conflict==1)map->values.push_back({0x100000,meta(),{}});else map->values[0].field=descriptor();
            auto checked=make_native_igb_meta_enum_list_factory(bad.services,map)(request);rejects([&]{checked->resume();},"Conflicting metadata address mapping was accepted.");
            map->values={{0x100000,bad.constructed,{}}};rejects([&]{checked->resume();},"Address conflict revived after external repair.");
        }
    }
}
}
int main(int argc,char**){try{if(argc!=1)throw std::runtime_error("Usage: igb_enum_list_dependency_tests");
    profiles_and_lookup_contracts();complete_registrar_order();probe_capture_and_unknown_words();shared_prototype_getters();inherited_clone_defaults();
    captured_clone_index_and_prototype_before_store();physical_clone_dispatch_and_pending_capture();physical_replace_and_creator_continuations();replacement_failure_prefix_and_owner_lifetime();
    defaults_unfinished_owner_retention();meta_enum_allocation_only_adapter();
    std::cout<<"igb_enum_list_dependency_tests: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"Enum list dependencies failed after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}


