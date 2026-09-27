#include "dw4/igb_field_init.hpp"
#include "dw4/igb_field_probe.hpp"
#include "dw4/igb_field_prototype.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

// Synthetic profile and continuation checks derived from the independently
// recovered original unsigned-long descriptor contract.
namespace {
using namespace dw4;
using K=IgbMetaFieldKind;
using E=IgbFieldTypeKind;
unsigned checks{};
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>void rejects(F&& body,const char* why){bool caught{};try{body();}catch(const std::runtime_error&){caught=true;}check(caught,why);}
IgbBootstrapPointer raw(std::uint32_t address,std::size_t size){
    auto storage=std::make_shared<IgbObjectStorage>();storage->original_address=address;
    storage->bytes=std::make_shared<IgbScratchBuffer>();storage->bytes->bytes.assign(size,0xcc);
    storage->bytes->initialized.assign(size,0);return {storage,0};
}
auto meta_services(){auto services=std::make_shared<IgbMetaFieldFactoryServices>();
    services->bootstrap=std::make_shared<IgbBootstrapServices>();
    services->bootstrap->runtime=std::make_shared<IgbRuntimeTypesServices>();return services;}

void exact_profile_and_distinct_cells(){
    check(static_cast<unsigned>(K::signed_long)==13 && static_cast<unsigned>(K::unsigned_long)==14 &&
        static_cast<unsigned>(E::signed_long)==14 && static_cast<unsigned>(E::unsigned_long)==15,
        "Appending unsigned-long changed an existing kind index.");
    const auto& p=igb_meta_field_profile(K::unsigned_long);
    check(p.factory==0x80022450 && p.constructor==0x80075c38 && p.reset==0x80075c84 &&
        p.vtable==0x80476f60 && p.bytes==0x34 && p.registration==0x80022634 &&
        p.callback==0x8002265c && p.metadata_global==0x805614a8,"Unsigned-long factory profile differs from original22450.");
    check(igb_meta_field_kind_for_factory(0x80022450)==K::unsigned_long &&
        !igb_meta_field_kind_for_factory(0x800224f4),"Creation profile admitted the unrecovered clone entry.");
    const auto& c=igb_field_type_profile(E::unsigned_long);
    check(std::string_view(c.name)=="igUnsignedLongMetaField" && c.metadata_global==0x805614a8 &&
        c.wrapper==0x80022634 && c.body==0x8002265c && c.size==0x34 && c.flag==0 &&
        c.parent_body==0x8002a6d8 && c.parent_getter==0x80021d70 && c.field_callback==0x800226c4 &&
        c.probe==0x800225ac && !c.defaults && !c.lazy_getter && c.full_getter==0x80022570 &&
        c.plain_getter==0x80094148 && c.extras_global==0x8055cf30 &&
        c.extra_callbacks==std::array<std::uint32_t,3>{0x800226e4,0,0},"Unsigned-long class ABI differs from2265C.");
    auto registration=std::make_shared<IgbFieldTypeRegistrationServices>();registration->meta_fields=meta_services();
    auto cell=std::make_shared<IgbRegistrationMetaSlot>();
    registration->meta_fields->metadata_slots[static_cast<std::size_t>(K::unsigned_long)]=cell;
    check(igb_field_type_metadata_slot(registration,E::unsigned_long)==cell,
        "Unsigned-long metadata uses a duplicate or signed-long canonical cell.");
    auto services=std::make_shared<IgbFieldPrototypeServices>();services->values=std::make_shared<IgbFieldValuesServices>();
    auto signed_field=std::make_shared<IgbDecodeRuntimeField>();services->values->signed_long_prototype=signed_field;
    auto alias=igb_field_prototype_slot(services,E::unsigned_long);
    check(alias && !alias->has_value(),"Unrepresented unsigned prototype became known-null.");
    auto unsigned_field=std::make_shared<IgbDecodeRuntimeField>();*alias=unsigned_field;
    check(services->values->unsigned_long_prototype==unsigned_field && services->values->signed_long_prototype==signed_field,
        "Unsigned prototype publication changed the signed prototype cell.");
    IgbFieldPrototypeLookupInvocation getter(E::unsigned_long);
    check(getter.resume(services).completed && getter.result()==unsigned_field,"2277C did not return its ready independent prototype.");
    IgbRegistrationCallbacksCall call;call.kind=IgbRegistrationCallbacksServiceKind::invoke_function;call.function=0x800226e4;
    auto callback=make_native_igb_field_prototype_function_factory(services)(call);
    check(callback && callback->resume().completed && unsigned_field->reference_word==1,
        "Ready226E4 repeated prototype construction or acquired another guest reference.");
}

void factory_gate_and_fresh_reflection(){
    auto services=meta_services();auto pool=std::make_shared<ModelSceneData>();
    unsigned lower_calls{};
    services->bootstrap->factory=[&](const IgbBootstrapCall&){++lower_calls;return std::shared_ptr<IgbBootstrapOperation>{};};
    services->factory=[&](const IgbMetaFieldCall&){++lower_calls;return std::shared_ptr<IgbMetaFieldOperation>{};};
    IgbMetaFieldFactoryInvocation unknown(pool,K::unsigned_long);
    check(!unknown.resume(services).completed && lower_calls==0,"22450 bypassed the unknown core gate.");
    services->bootstrap->runtime->singleton=std::shared_ptr<IgbRuntimeTypeRegistry>{};
    rejects([&]{unknown.resume(services);},"22450 treated a known-null core as disabled.");
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();core->enabled=0;services->bootstrap->runtime->singleton=core;
    rejects([&]{unknown.resume(services);},"Repair revived a faulted unsigned-long gate.");
    unsigned allocations{};
    services->bootstrap->factory=[&](const IgbBootstrapCall& call){
        check(call.kind==IgbBootstrapServiceKind::pool_allocate && call.pool==pool && call.bytes==0x34,
            "Disabled22450 changed its raw allocation size, prefix or captured pool.");++allocations;
        auto operation=std::make_shared<IgbBootstrapOperation>();operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;
    };
    IgbMetaFieldFactoryInvocation disabled(pool,K::unsigned_long);
    rejects([&]{disabled.resume(services);},"A null raw allocation skipped22450's following physical VT read.");
    check(allocations==1 && lower_calls==0,"Disabled22450 selected registration instead of raw allocation.");

    core->enabled=1;auto old=std::make_shared<IgbRuntimeMetaType>(),fresh=std::make_shared<IgbRuntimeMetaType>();
    auto cell=std::make_shared<IgbRegistrationMetaSlot>(old);
    services->metadata_slots[static_cast<std::size_t>(K::unsigned_long)]=cell;
    auto field=std::make_shared<IgbDecodeRuntimeField>();auto physical=raw(0x200000,0x34);
    physical.storage->bindings.push_back({0,{},{},{},{},field});
    unsigned selections{},polls{},reflections{};bool ready{};
    services->factory=[&](const IgbMetaFieldCall& call){auto operation=std::make_shared<IgbMetaFieldOperation>();
        if(call.kind==IgbMetaFieldServiceKind::register_type){++selections;check(call.function==0x80022634,"22450 selected another wrapper.");
            operation->resume=[&]{++polls;if(ready)*cell=fresh;return MessengerCallResult{ready,0,"Prepared22634."};};}
        else{check(call.kind==IgbMetaFieldServiceKind::reflected_factory && call.pool==pool && call.metadata==fresh,
                "22450 did not read fresh metadata after registration or lost its captured pool.");++reflections;
            operation->resume=[&,out=operation.get()]{out->allocation_result=physical;return MessengerCallResult{true,0,{}};};}
        return operation;
    };
    IgbMetaFieldFactoryInvocation enabled(pool,K::unsigned_long);
    check(!enabled.resume(services).completed && selections==1 && polls==1 && !reflections,"Enabled22450 did not retain its registration call.");
    core->enabled=0;ready=true;
    check(enabled.resume(services).completed && enabled.result()==field && selections==1 && polls==2 && reflections==1 && allocations==1,
        "Enabled22450 reread the gate, replayed registration or selected raw allocation after suspension.");
    check(std::none_of(physical.storage->bytes->initialized.begin(),physical.storage->bytes->initialized.end(),[](auto b){return b!=0;}),
        "Enabled22450 executed a direct constructor or tail on its reflected result.");
}

void no_prerequisite_body_and_exact_leaves(){
    auto registration=std::make_shared<IgbFieldTypeRegistrationServices>();registration->meta_fields=meta_services();
    auto metadata=std::make_shared<IgbRuntimeMetaType>();metadata->class_flags=4;
    auto cell=std::make_shared<IgbRegistrationMetaSlot>(metadata);
    registration->meta_fields->metadata_slots[static_cast<std::size_t>(K::unsigned_long)]=cell;
    registration->types=std::make_shared<IgbTypeRegistrationServices>();registration->types->metadata_pool_handle=5;
    unsigned pool_calls{};registration->types->factory=[&](const IgbTypeRegistrationCall& call){
        check(call.kind==IgbTypeRegistrationServiceKind::pool_for_handle,"Ready2265C selected an unrelated service.");++pool_calls;
        auto operation=std::make_shared<IgbTypeRegistrationOperation>();operation->resume=[]{return MessengerCallResult{true,0,{}};};return operation;};
    IgbFieldTypeRegistrationInvocation body(E::unsigned_long);
    check(body.resume(registration).completed && pool_calls==1 && !body.result(),"2265C invented a21B94 prerequisite before ready66204.");
    auto values=std::make_shared<IgbFieldValuesServices>();auto value_factory=make_native_igb_field_values_factory(values);
    IgbFieldOperationCall call;call.kind=IgbFieldOperationKind::field_virtual;call.vtable_word=0x80476f60;call.virtual_slot=0x64;
    auto size=value_factory(call);check(size && size->resume().completed && size->resume().value==8,"Unsigned-long size leaf changed or read its receiver.");
    IgbMetadataLayoutCall align;align.kind=IgbMetadataLayoutKind::field_alignment;align.vtable_word=call.vtable_word;align.virtual_slot=0x6c;
    auto alignment=make_native_igb_field_alignment_factory()(align);
    check(alignment && alignment->resume().completed && alignment->resume().value==8,"Unsigned-long alignment leaf changed or read its receiver.");
    call.virtual_slot=0x5c;auto prototype=value_factory(call);check(prototype && !prototype->resume().completed,"Unknown94150 returned fabricated null.");
    values->unsigned_long_prototype=std::shared_ptr<IgbDecodeRuntimeField>{};
    check(prototype->resume().completed && !prototype->field_result,"Plain94150 registered a known-null prototype.");
    auto init_services=std::make_shared<IgbFieldInitServices>();init_services->registration=registration;
    auto init=make_native_igb_field_init_factory(init_services);IgbObjectInitCall object;
    object.kind=IgbObjectInitServiceKind::object_virtual;object.vtable_word=0x80476f60;
    for(auto slot:{0x0cu,0x24u,0x2cu,0x34u}){object.virtual_slot=slot;auto operation=init(object);
        check(operation && operation->resume().completed,"Unsigned-long literal BLR read a null receiver.");}
    object.virtual_slot=0x58;cell->reset();auto plain=init(object);
    check(plain && !plain->resume().completed,"Unknown94148 became known-null.");
    *cell=std::shared_ptr<IgbRuntimeMetaType>{};check(plain->resume().completed && !plain->metadata_result && pool_calls==1,
        "Plain94148 forced registration of known-null metadata.");
    object.virtual_slot=0x54;check(!init(object),"Unsigned-long profile silently implemented its clone.");
}

void specialized_probe_precedes_core_capture(){
    auto services=std::make_shared<IgbFieldProbeServices>();services->fields=meta_services();
    services->runtime=std::make_shared<IgbRuntimeTypesServices>();
    services->fields->dependencies=std::make_shared<IgbDependenciesServices>();
    services->fields->dependencies->globals=std::make_shared<SystemStageServices>();
    services->fields->dependencies->globals->string_pool=std::make_shared<ModelSceneData>();
    auto field=std::make_shared<IgbDecodeRuntimeField>();auto local=raw(0x500000,0x34);
    local.storage->bindings.push_back({0,{},{},{},{},field});igb_bootstrap_write_word(local,4,0);
    IgbFieldProbeInvocation probe(local,K::unsigned_long);
    check(!probe.resume(services).completed && igb_bootstrap_read_word(local,0)==0x80476f60 &&
        igb_bootstrap_read_word(local,4)==1 && local.storage->bytes->initialized[0x2d]==0,
        "225AC did not finish specialized75C84 before requesting ArkCore.");
    auto core=std::make_shared<IgbRuntimeTypeRegistry>();services->runtime->singleton=core;
    check(!probe.resume(services).completed,"225AC invented the captured core's offset.");
    auto other=std::make_shared<IgbRuntimeTypeRegistry>();other->vtable_offset=4;services->runtime->singleton=other;
    core->vtable_offset=0;igb_bootstrap_write_word(local,0,0x12345678);
    check(probe.resume(services).completed && probe.value()==0x12345678 && igb_bootstrap_read_word(local,0)==0x80471914 &&
        local.storage->bytes->initialized[0x2d]==0,"225AC recaptured core, lost its selected word or initialized padding.");
}

void actual_eight_byte_assignment_retains_source(){
    auto values=std::make_shared<IgbFieldValuesServices>();values->fields=std::make_shared<IgbFieldOperationsServices>();
    auto field=std::make_shared<IgbDecodeRuntimeField>();field->vtable_word=0x80476f60;
    auto destination=raw(0x800000,14).storage->bytes;auto source=raw(0x700000,8).storage->bytes;
    source->bytes={0x80,0x01,0x23,0x45,0xfe,0xdc,0xba,0x98};source->initialized.assign(8,1);
    const auto expected=source->bytes;field->default_bytes=IgbRuntimeFieldDefaultSource{destination,3};field->default_bytes_address=0x800003;
    auto pool=std::make_shared<ModelSceneData>();bool ready{};unsigned selections{},polls{};
    values->fields->factory=make_native_igb_field_values_factory(values,[&](const IgbFieldOperationCall& call){
        auto operation=std::make_shared<IgbFieldOperation>();
        if(call.kind==IgbFieldOperationKind::allocation_pool){
            check(call.address==0x800003 && call.buffer.buffer==destination && call.buffer.offset==3,
                "63C74 changed the captured unsigned-long buffer identity.");
            operation->resume=[&,out=operation.get()]{out->pool_result=pool;return MessengerCallResult{true,0,{}};};
        }else{check(call.kind==IgbFieldOperationKind::allocation_size && call.pool==pool,
                "An existing eight-byte unsigned-long buffer was unnecessarily freed or allocated.");++selections;
            operation->resume=[&]{++polls;return MessengerCallResult{ready,8,"Prepared buffer capacity."};};}
        return operation;
    });
    IgbFieldOperationCall call;call.kind=IgbFieldOperationKind::field_virtual;call.field=field;
    call.vtable_word=0x80476f60;call.virtual_slot=0x8c;call.source=IgbRuntimeFieldDefaultSource{source,0};call.source_address=0x700000;
    auto operation=values->fields->factory(call);call.source.reset();source.reset();
    check(operation && !operation->resume().completed && field->size==8 && selections==1 && polls==1 &&
        std::none_of(destination->initialized.begin(),destination->initialized.end(),[](auto b){return b!=0;}),
        "Unsigned-long assignment copied before its pending capacity call or truncated its size.");
    ready=true;values->fields->factory={};
    check(operation->resume().completed && selections==1 && polls==2 &&
        std::equal(expected.begin(),expected.end(),destination->bytes.begin()+3),
        "Unsigned-long assignment lost its retained source or either original payload word.");
    for(std::size_t i=0;i<destination->bytes.size();++i){const bool copied=i>=3 && i<11;
        check(destination->initialized[i]==(copied?1:0) && (copied || destination->bytes[i]==0xcc),
            "Unsigned-long assignment modified bytes outside its exact eight-byte destination.");}
    check(operation->resume().completed && polls==2,"Completed unsigned-long assignment replayed a lower call.");
}
} // namespace
int main(){try{exact_profile_and_distinct_cells();factory_gate_and_fresh_reflection();no_prerequisite_body_and_exact_leaves();
    specialized_probe_precedes_core_capture();actual_eight_byte_assignment_retains_source();
    std::cout<<"Unsigned-long focused checks: "<<checks<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Unsigned-long focused failure after "<<checks<<": "<<error.what()<<'\n';return 1;}}
