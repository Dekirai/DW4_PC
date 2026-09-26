#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_prototype.hpp"
#include "dw4/igb_field_probe.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

// Synthetic checks for the original signed 64-bit descriptor dependency.
namespace {
using namespace dw4;
using E=IgbFieldTypeKind;
using K=IgbMetaFieldKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& operation,const char* why){bool failed{};try{operation();}catch(const std::runtime_error&){failed=true;}check(failed,why);}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){auto storage=std::make_shared<IgbObjectStorage>();
    storage->original_address=address;storage->bytes=std::make_shared<IgbScratchBuffer>();
    storage->bytes->bytes.assign(size,0xcc);storage->bytes->initialized.assign(size,0);return {storage,0};}

void exact_profiles_and_shared_aliases(){
    const auto& factory=igb_meta_field_profile(K::signed_long);
    check(factory.factory==0x8002cea8 && factory.registration==0x8002cfa8 && factory.callback==0x8002cfd0 &&
        factory.metadata_global==0x8056198c && factory.vtable==0x80472100 && factory.bytes==0x34 &&
        !factory.constructor && !factory.reset,"Long descriptor acquired a raw constructor or changed its exact reflected profile.");
    check(igb_meta_field_kind_for_factory(0x8002cea8)==K::signed_long,"Long source factory was not mapped to its distinct kind.");
    const auto& profile=igb_field_type_profile(E::signed_long);
    check(std::string_view(profile.name)=="igLongMetaField" && profile.metadata_global==0x8056198c &&
        profile.wrapper==0x8002cfa8 && profile.body==0x8002cfd0 && profile.size==0x34 && profile.flag==0 &&
        profile.parent_body==0x8002a6d8 && profile.parent_getter==0x80021d70 &&
        profile.field_callback==0x8002d03c && profile.probe==0x8002cf1c && !profile.defaults && !profile.lazy_getter &&
        profile.full_getter==0x8002cee0 && profile.plain_getter==0x80093bec && profile.extras_global==0x8055d36c &&
        profile.extra_callbacks==std::array<std::uint32_t,3>{0x8002d05c,0,0},"Long class profile differs from the sealed original arguments.");
    auto registration=std::make_shared<IgbFieldTypeRegistrationServices>();registration->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
    auto cell=std::make_shared<IgbRegistrationMetaSlot>();registration->meta_fields->metadata_slots[static_cast<std::size_t>(K::signed_long)]=cell;
    check(igb_field_type_metadata_slot(registration,E::signed_long)==cell,"Long registration introduced a duplicate metadata cell.");
    auto prototypes=std::make_shared<IgbFieldPrototypeServices>();prototypes->values=std::make_shared<IgbFieldValuesServices>();
    auto slot=igb_field_prototype_slot(prototypes,E::signed_long);auto value=std::make_shared<IgbDecodeRuntimeField>();
    *slot=value;check(prototypes->values->signed_long_prototype==value,"Long prototype does not alias canonical 80561990.");
    IgbRegistrationCallbacksCall callback;callback.kind=IgbRegistrationCallbacksServiceKind::invoke_function;callback.function=0x8002d05c;
    auto existing=make_native_igb_field_prototype_function_factory(prototypes)(callback);
    check(existing && existing->resume().completed && value->reference_word==1,
        "Exact 2D05C callback did not use its existing shared prototype without another retain.");
    prototypes->values->signed_long_prototype.reset();check(!slot->has_value(),"Prototype alias lost unknown-state propagation.");
}

void always_reflected_with_fresh_metadata(){
    for(const int core_state:{-1,0,1}){
        auto services=std::make_shared<IgbMetaFieldFactoryServices>();services->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();
        if(core_state>=0){auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=static_cast<std::uint8_t>(core_state);
            services->bootstrap->runtime->singleton=core;}
        auto old=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();
        auto cell=std::make_shared<IgbRegistrationMetaSlot>(old);services->metadata_slots[static_cast<std::size_t>(K::signed_long)]=cell;
        auto pool=std::make_shared<ModelSceneData>();auto result=std::make_shared<IgbDecodeRuntimeField>();
        auto storage=raw(0x200000,0x34);storage.storage->bindings.push_back({0,{},{},{},{},result});
        unsigned registrations{},reflections{},raw_calls{},polls{};bool ready{};
        services->bootstrap->factory=[&](const IgbBootstrapCall&){++raw_calls;return std::shared_ptr<IgbBootstrapOperation>{};};
        services->factory=[&](const IgbMetaFieldCall& call){auto operation=std::make_shared<IgbMetaFieldOperation>();
            if(call.kind==IgbMetaFieldServiceKind::register_type){++registrations;
                check(call.function==0x8002cfa8,"Long factory selected another wrapper.");
                operation->resume=[&]{++polls;if(ready)*cell=fresh;return MessengerCallResult{ready,0,"Prepared long registration."};};
            }else{check(call.kind==IgbMetaFieldServiceKind::reflected_factory && call.metadata==fresh && call.pool==pool,
                    "Long reflection lost incoming pool or read metadata before registration.");++reflections;
                operation->resume=[&,out=operation.get()]{out->allocation_result=storage;return MessengerCallResult{true,0,{}};};}
            return operation;
        };
        IgbMetaFieldFactoryInvocation invocation(pool,K::signed_long);
        check(!invocation.resume(services).completed && registrations==1 && !reflections && !raw_calls,
            "Long factory read the core gate or selected a raw allocation.");
        ready=true;check(invocation.resume(services).completed && invocation.result()==result && registrations==1 &&
            reflections==1 && polls==2 && raw_calls==0,"Long factory replayed registration or omitted retained reflection.");
        check(std::all_of(storage.storage->bytes->initialized.begin(),storage.storage->bytes->initialized.end(),
            [](auto value){return value==0;}),"Always-reflected factory executed a raw constructor or post-constructor tail.");
    }
}

void long_body_keeps_its_earlier_prerequisite(){
    auto services=std::make_shared<IgbFieldTypeRegistrationServices>();services->meta_fields=std::make_shared<IgbMetaFieldFactoryServices>();
    auto ready=std::make_shared<IgbRuntimeMetaType>();ready->class_flags=4;
    auto cell=std::make_shared<IgbRegistrationMetaSlot>(ready);
    services->meta_fields->metadata_slots[static_cast<std::size_t>(K::signed_long)]=cell;
    services->callbacks=std::make_shared<IgbRegistrationCallbacksServices>();
    services->types=std::make_shared<IgbTypeRegistrationServices>();services->types->metadata_pool_handle=5;
    unsigned pools{};services->types->factory=[&](const IgbTypeRegistrationCall& call){
        check(call.kind==IgbTypeRegistrationServiceKind::pool_for_handle,"Ready long registrar selected an unrelated lower operation.");
        ++pools;auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;
    };
    IgbFieldTypeRegistrationInvocation body(E::signed_long);
    check(!body.resume(services).completed && pools==0,"Ready long class bypassed its original earlier 21B94 prerequisite.");
    services->callbacks->prerequisites=std::make_shared<IgbRegistrationPrerequisitesState>();
    services->callbacks->prerequisites->initialized=1;services->callbacks->prerequisites->started=1;
    check(body.resume(services).completed && pools==1 && !body.result(),"Long class failed to resume after represented prerequisite state.");
    cell->reset();IgbFieldTypeLookupInvocation plain(E::signed_long,IgbFieldTypeLookup::plain);
    check(!plain.resume(services).completed && pools==1,"Unknown plain long metadata became known-null or registered again.");
    *cell=std::shared_ptr<IgbRuntimeMetaType>{};check(plain.resume(services).completed && !plain.result() && pools==1,
        "Plain long metadata getter allocated or registered a known-null class.");
}

void exact_no_read_leaves_and_unknown_prototype(){
    auto values=std::make_shared<IgbFieldValuesServices>();auto factory=make_native_igb_field_values_factory(values);
    IgbFieldOperationCall call;call.kind=IgbFieldOperationKind::field_virtual;call.vtable_word=0x80472100;call.virtual_slot=0x64;
    auto size=factory(call);check(size && size->resume().completed && size->resume().value==8,"Long size leaf dereferenced its null receiver or truncated to four.");
    IgbMetadataLayoutCall alignment;alignment.kind=IgbMetadataLayoutKind::field_alignment;
    alignment.vtable_word=0x80472100;alignment.virtual_slot=0x6c;
    auto aligned=make_native_igb_field_alignment_factory()(alignment);
    check(aligned && aligned->resume().completed && aligned->resume().value==8,"Long alignment leaf did not preserve its exact constant eight.");
    call.virtual_slot=0x5c;auto prototype=factory(call);
    check(prototype && !prototype->resume().completed,"Unknown long prototype became known-null.");
    values->signed_long_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    check(prototype->resume().completed && !prototype->field_result,"Plain long prototype getter registered or allocated a null field.");
    auto field=std::make_shared<IgbDecodeRuntimeField>();values->signed_long_prototype=field;
    auto fresh=factory(call);check(fresh && fresh->resume().completed && fresh->field_result==field,"Long prototype getter returned another scalar's cell.");
    auto services=std::make_shared<IgbFieldInitServices>();auto init=make_native_igb_field_init_factory(services);
    IgbObjectInitCall object;object.kind=IgbObjectInitServiceKind::object_virtual;object.vtable_word=0x80472100;
    for(const auto slot:{0x0cu,0x24u,0x2cu,0x34u}){object.virtual_slot=slot;auto leaf=init(object);
        check(leaf && leaf->resume().completed,"Verified long descriptor BLR read a null object.");}
    object.kind=IgbObjectInitServiceKind::field_virtual;object.virtual_slot=0x70;
    check(!init(object),"Long profile expansion admitted unrecovered object-member copy.");
}

struct ValueFixture {
    std::shared_ptr<IgbFieldValuesServices> services=std::make_shared<IgbFieldValuesServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer physical=raw(0x100000,0x34);
    std::shared_ptr<IgbScratchBuffer> payload=raw(0x800000,8).storage->bytes;
    unsigned allocations{},polls{};bool ready{};
    ValueFixture(){
        physical.storage->bindings.push_back({0,{},{},{},{},field});field->physical_storage=IgbFieldStorageBinding{physical.storage,0};
        igb_bootstrap_write_word(physical,0,0x80472100);igb_bootstrap_write_word(physical,4,1);
        igb_bootstrap_write_word(physical,0x20,0);field->default_bytes=IgbRuntimeFieldDefaultSource{};field->default_bytes_address=0;
        services->fields=std::make_shared<IgbFieldOperationsServices>();
        services->fields->factory=make_native_igb_field_values_factory(services,[this](const IgbFieldOperationCall& call){
            auto operation=std::make_shared<IgbFieldOperation>();
            if(call.kind==IgbFieldOperationKind::object_pool){operation->resume=[this,out=operation.get()]{out->pool_result=pool;return MessengerCallResult{true,0,{}};};}
            else{check(call.kind==IgbFieldOperationKind::allocate_default && call.bytes==8 && call.alignment==1 && call.pool==pool,
                    "Long null buffer used a 32-bit allocation or changed the original allocator alignment.");++allocations;
                operation->resume=[this,out=operation.get()]{++polls;if(!ready)return MessengerCallResult{false,0,"Prepared eight-byte allocation."};
                    out->buffer_result={payload,0};out->buffer_address=0x800000;return MessengerCallResult{true,0,{}};};}
            return operation;
        });
    }
    auto select(std::optional<IgbRuntimeFieldDefaultSource> source={},std::optional<std::uint32_t> source_address={}){
        IgbFieldOperationCall call;call.kind=IgbFieldOperationKind::field_virtual;call.field=field;call.vtable_word=0x80472100;
        call.virtual_slot=0x8c;call.source=std::move(source);call.source_address=source_address;return services->fields->factory(call);
    }
};

void actual_eight_byte_copy_and_null_source(){
    ValueFixture f;auto source=raw(0x700000,8).storage->bytes;
    source->bytes={0x80,0x01,0x23,0x45,0xfe,0xdc,0xba,0x98};source->initialized.assign(8,1);
    const auto expected=source->bytes;auto operation=f.select(IgbRuntimeFieldDefaultSource{source,0},0x700000);
    check(operation && !operation->resume().completed && f.allocations==1 && f.field->size==8 &&
        igb_bootstrap_read_word(f.physical,0x20)==0 && !f.field->default_bytes->buffer,
        "Long copy did not suspend after the exact size store and before buffer publication.");
    f.ready=true;f.services->fields->factory={};source.reset();
    check(operation->resume().completed && f.allocations==1 && f.polls==2 && f.payload->bytes==expected &&
        std::all_of(f.payload->initialized.begin(),f.payload->initialized.end(),[](auto byte){return byte==1;}) &&
        f.field->default_bytes->buffer==f.payload && igb_bootstrap_read_word(f.physical,0x20)==0x800000,
        "Retained long copy lost either word, its source owner or its pending allocation result.");
    check(operation->resume().completed && f.polls==2,"Completed long copy replayed its allocation.");

    ValueFixture null;null.ready=true;auto no_source=null.select();
    check(no_source && no_source->resume().completed && null.field->size==8 && null.allocations==1 &&
        std::all_of(null.payload->bytes.begin(),null.payload->bytes.end(),[](auto byte){return byte==0xcc;}) &&
        std::none_of(null.payload->initialized.begin(),null.payload->initialized.end(),[](auto byte){return byte!=0;}),
        "Null long source invented zeroes or initialized unknown allocation contents.");

    ValueFixture partial;partial.ready=true;auto short_source=raw(0x900000,8).storage->bytes;
    short_source->bytes={1,2,3,4,5,6,7,8};short_source->initialized={1,1,1,0,1,1,1,1};
    auto failing=partial.select(IgbRuntimeFieldDefaultSource{short_source,0},0x900000);
    rejects([&]{failing->resume();},"Long copy guessed an unknown source byte.");
    check(partial.allocations==1 && partial.payload->bytes[0]==1 && partial.payload->bytes[1]==2 && partial.payload->bytes[2]==3 &&
        partial.payload->initialized==std::vector<std::uint8_t>({1,1,1,0,0,0,0,0}) &&
        igb_bootstrap_read_word(partial.physical,0x20)==0x800000,
        "Long byte-copy fault lost its original directional prefix or published buffer.");
    short_source->initialized[3]=1;rejects([&]{failing->resume();},"Repair revived a permanently faulted long copy.");
}

struct ProbeFixture {
    std::shared_ptr<IgbFieldProbeServices> services=std::make_shared<IgbFieldProbeServices>();
    std::shared_ptr<IgbDecodeRuntimeField> field=std::make_shared<IgbDecodeRuntimeField>();
    std::shared_ptr<IgbRuntimeTypeRegistry> core=std::make_shared<IgbRuntimeTypeRegistry>();
    std::shared_ptr<ModelSceneObject> pool=std::make_shared<ModelSceneData>();
    IgbBootstrapPointer local=raw(0x500000,0x34);
    ProbeFixture(){
        igb_bootstrap_write_word(local,4,0); // The reset reads incoming packed header bits.
        local.storage->bindings.push_back({0,{},{},{},{},field});
        services->fields=std::make_shared<IgbMetaFieldFactoryServices>();services->fields->bootstrap=std::make_shared<IgbBootstrapServices>();
        services->fields->dependencies=std::make_shared<IgbDependenciesServices>();
        services->fields->dependencies->globals=std::make_shared<SystemStageServices>();services->fields->dependencies->globals->string_pool=pool;
        services->runtime=std::make_shared<IgbRuntimeTypesServices>();services->runtime->singleton=core;
    }
    auto name(std::uint32_t count){auto result=std::make_shared<IgbInternedString>();result->container=pool;result->references=count;
        result->value=std::make_shared<const std::string>("long-probe");igb_meta_field_set_name(local,result,0x600000);return result;}
};

void actual_probe_capture_cleanup_and_unknown_padding(){
    ProbeFixture f;IgbFieldProbeInvocation invocation(f.local,K::signed_long);
    check(!invocation.resume(f.services).completed && igb_bootstrap_read_word(f.local,0)==0x80472100 &&
        igb_bootstrap_read_word(f.local,4)==1 && f.field->size==0 && f.local.storage->bytes->initialized[0x2d]==0,
        "Long probe skipped its base reset/core capture or initialized unrelated padding.");
    auto changed=std::make_shared<IgbRuntimeTypeRegistry>();changed->vtable_offset=4;f.services->runtime->singleton=changed;
    f.core->vtable_offset=0;igb_bootstrap_write_word(f.local,0,0x12345678);auto name=f.name(1);
    bool ready{};unsigned selections{},polls{};
    f.services->fields->dependencies->factory=[&](const IgbDependenciesCall& call){
        check(call.kind==IgbDependenciesServiceKind::release_string && call.string==name && call.pool==f.pool,
            "Long probe selected unrelated cleanup or a stale string container.");++selections;
        auto operation=std::make_shared<IgbDependenciesOperation>();operation->resume=[&]{++polls;
            return MessengerCallResult{ready,0,"Prepared long-probe name release."};};return operation;
    };
    check(!invocation.resume(f.services).completed && selections==1 && polls==1 && name->references==0 &&
        igb_bootstrap_read_word(f.local,0)==0x80471914,"Long probe did not restore its base table before fresh name cleanup.");
    igb_bootstrap_write_word(f.local,0,0xaabbccdd);f.services->fields->dependencies->factory={};ready=true;
    check(invocation.resume(f.services).completed && invocation.value()==0x12345678 && polls==2 && selections==1 && name->references==0 &&
        igb_bootstrap_read_word(f.local,0)==0xaabbccdd && f.local.storage->bytes->initialized[0x2d]==0,
        "Long probe replayed restore/decrement, recaptured core, or lost its selected return word.");

    ProbeFixture padding;padding.core->vtable_offset=0x2c;IgbFieldProbeInvocation unknown(padding.local,K::signed_long);
    rejects([&]{unknown.resume(padding.services);},"Long probe fabricated the three unknown bytes after its reset byte+2C.");
    check(unknown.faulted() && igb_bootstrap_read_word(padding.local,0)==0x80472100 &&
        padding.local.storage->bytes->initialized[0x2c]==1 && padding.local.storage->bytes->initialized[0x2d]==0,
        "Failed long probe restored its base table or altered unknown padding.");

    for(const auto references:{0u,0x800001u}){
        ProbeFixture full;IgbFieldProbeInvocation probe(full.local,K::signed_long);check(!probe.resume(full.services).completed,"Probe did not stop at unknown offset.");
        auto header=full.name(references);full.core->vtable_offset=0;
        check(probe.resume(full.services).completed && header->references==references-1u && probe.value()==0x80472100,
            "Long string cleanup used a packed low23 test or failed full-word wrap.");
    }
}
} // namespace
int main(){try{exact_profiles_and_shared_aliases();always_reflected_with_fresh_metadata();exact_no_read_leaves_and_unknown_prototype();
    long_body_keeps_its_earlier_prerequisite();
    actual_eight_byte_copy_and_null_source();actual_probe_capture_cleanup_and_unknown_padding();
    std::cout<<"Signed-long field checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Signed-long field failure after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}}
